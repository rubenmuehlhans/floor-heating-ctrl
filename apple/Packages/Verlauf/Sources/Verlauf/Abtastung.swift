import Foundation
import Geraeteschnittstelle

/// Die Messgrößen des Verlaufs und ihre Schlüssel. Ein Schlüssel gilt innerhalb eines Geräts:
///
/// | Schlüssel | Bedeutung | Einheit |
/// |---|---|---|
/// | `raum.<n>.ist`, `.soll`, `.stellung`, `.feuchte` | Raum n des Verteilers | °C, °C, Anteil, % |
/// | `kanal.<n>.stellung` | Kanal n des Verteilers | Anteil |
/// | `vorlauf.<i>` | 1-Wire-Fühler i auf der Verteilerplatine | °C |
/// | `aussen` | Außenfühler am Verteiler | °C |
/// | `fuehler.<rolle>` | eigener Fühler des Heizungsgeräts | °C |
/// | `brenner` | Brenner läuft, als Anteil des Platzes | Anteil |
/// | `fuellstand` | geschätzte Ladung des Speichers | Anteil |
/// | `pumpe.<id>` | Pumpe des Heizkreises läuft | Anteil |
/// | `kkp` | Kesselkreispumpe läuft | Anteil |
public enum Messgroesse {
    public enum Einheit: String, Sendable, Hashable {
        case grad, anteil, prozent
    }

    public static func einheit(_ schluessel: String) -> Einheit {
        if schluessel.hasSuffix(".feuchte") { return .prozent }
        if schluessel.hasSuffix(".stellung") || schluessel.hasPrefix("pumpe.")
            || ["brenner", "fuellstand", "kkp"].contains(schluessel) {
            return .anteil
        }
        return .grad
    }

    public static func raum(_ nummer: Int, _ groesse: String) -> String { "raum.\(nummer).\(groesse)" }
    public static func fuehler(_ rolle: String) -> String { "fuehler.\(rolle)" }
}

/// Was eine Abfrage eines Geräts für den Verlauf hergibt: Messwerte nach Schlüssel und die
/// Bezeichnungen, unter denen sie in Diagrammen erscheinen.
public struct Abtastung: Sendable, Equatable {
    public var geraet: String
    public var ort: String
    public var zeit: Date
    public var werte: [String: Double]
    public var bezeichnungen: [String: String]

    public init(geraet: String, ort: String, zeit: Date, werte: [String: Double], bezeichnungen: [String: String]) {
        self.geraet = geraet
        self.ort = ort
        self.zeit = zeit
        self.werte = werte
        self.bezeichnungen = bezeichnungen
    }

    public var leer: Bool { werte.isEmpty }

    mutating func setzen(_ schluessel: String, _ wert: Double?, _ bezeichnung: String) {
        guard let wert, wert.isFinite else { return }
        werte[schluessel] = wert
        bezeichnungen[schluessel] = bezeichnung
    }
}

extension Abtastung {
    /// Räume mit gültigem Messwert, Sollwert nur im Heizbetrieb, Stellungen, Fühler der Platine
    /// und ein zugeordneter Außenfühler
    public static func verteiler(_ z: Verteilerzustand, geraet: String, zeit: Date) -> Abtastung {
        var a = Abtastung(geraet: geraet, ort: z.geraet?.ort ?? "", zeit: zeit, werte: [:], bezeichnungen: [:])
        for r in z.raeume ?? [] {
            guard let n = r.id else { continue }
            let name = r.name.flatMap { $0.isEmpty ? nil : $0 } ?? "Raum \(n)"
            if r.messwertGueltig == true {
                a.setzen(Messgroesse.raum(n, "ist"), r.temperaturC, name)
                a.setzen(Messgroesse.raum(n, "feuchte"), r.feuchte, "\(name), Luftfeuchte")
            }
            // Ein ausgeschalteter Raum hat keinen wirksamen Sollwert; die Lücke zeigt das.
            if r.betriebsart != "off" {
                a.setzen(Messgroesse.raum(n, "soll"), r.sollC, "\(name), Soll")
            }
            a.setzen(Messgroesse.raum(n, "stellung"), r.zielstellung, "\(name), Ventile")
        }
        for k in z.kanaele ?? [] where k.stellungBekannt != false {
            guard let n = k.id else { continue }
            a.setzen("kanal.\(n).stellung", k.stellung, "Kanal \(n)")
        }
        for (i, f) in (z.bordfuehler?.einwire ?? []).enumerated() where f.gueltig == true {
            a.setzen("vorlauf.\(i)", f.temperaturC, "Vorlauffühler \(i + 1)")
        }
        if z.aussen?.gueltig == true {
            a.setzen("aussen", z.aussen?.temperaturC, "Außen")
        }
        return a
    }

    /// Nur was dieses Gerät selbst misst: eigene Fühler, den Brenner nur mit eigenem Abgasfühler,
    /// den Füllstand nur mit eigenem Pufferfühler. Werte des Nachbargeräts zeichnet dessen
    /// Abtastung auf.
    public static func heizgeraet(_ z: Heizgeraetezustand, geraet: String, zeit: Date) -> Abtastung {
        var a = Abtastung(geraet: geraet, ort: z.geraet?.ort ?? "", zeit: zeit, werte: [:], bezeichnungen: [:])
        let rollen = eigeneRollen(z)
        for (rolle, name) in rollen {
            let f = z.fuehler?.first { $0.rolle == rolle && $0.zugeordnet != false }
            a.setzen(Messgroesse.fuehler(rolle), f?.temperaturC, name)
        }
        // Maßgeblich ist, wer den Fühler hat: Das Gerät ohne Abgasfühler meldet den Brenner des
        // Nachbarn, das ohne Pufferfühler dessen Füllstand.
        if rollen["abgas"] != nil, let b = z.brenner, b.erkannt == true, b.fremdgemessen != true, let laeuft = b.laeuft {
            a.setzen("brenner", laeuft ? 1 : 0, "Brenner")
        }
        if rollen["puffer"] != nil, let l = z.ladung, l.pufferFremd != true {
            a.setzen("fuellstand", l.fuellstand, "Füllstand Speicher")
        }
        for k in z.heizkreise ?? [] {
            guard let id = k.id, let ein = k.pumpeEin else { continue }
            a.setzen("pumpe.\(id)", ein ? 1 : 0, "\(k.name.flatMap { $0.isEmpty ? nil : $0 } ?? "Heizkreis \(id)"), Pumpe")
        }
        if let p = z.kesselkreispumpe, p.aktiv == true, let ein = p.ein {
            a.setzen("kkp", ein ? 1 : 0, "Kesselkreispumpe")
        }
        return a
    }

    /// Rollen der zugeordneten eigenen Fühler mit ihrer Bezeichnung
    public static func eigeneRollen(_ z: Heizgeraetezustand) -> [String: String] {
        var rollen: [String: String] = [:]
        for f in z.fuehler ?? [] where f.zugeordnet != false {
            guard let rolle = f.rolle, !rolle.isEmpty, rolle != "none" else { continue }
            rollen[rolle] = f.rollenname.flatMap { $0.isEmpty ? nil : $0 } ?? rolle
        }
        return rollen
    }
}
