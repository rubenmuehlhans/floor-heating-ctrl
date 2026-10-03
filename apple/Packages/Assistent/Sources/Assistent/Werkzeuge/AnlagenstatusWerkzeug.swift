import Foundation
import FoundationModels
import Anlage
import Geraeteschnittstelle

/// Lesendes Werkzeug: der Zustand der Anlage aus der laufenden Abfrage der App.
///
/// Je Gerät die Kurzfassung des Zustands, dazu Erreichbarkeit, Alter der Werte und die
/// Zuordnung der Heizkreise zu den Verteilern. Mit `rohdaten` die vollständige letzte Antwort
/// eines Geräts. Zugangsdaten verlassen die App nie.
public struct AnlagenstatusWerkzeug: Tool {
    public let name = "anlage_status"
    public let description = """
        Liest den aktuellen Zustand der Heizungsanlage aus der laufenden Abfrage der App: je \
        Verteiler die Räume mit Soll- und Istwert, Thermometer und Ventilstellungen; je \
        Heizungsgerät Fühler, Brenner, Ladung des Pufferspeichers, Heizkreise, Pumpen, Relais \
        und die Befunde der Firmware; dazu Erreichbarkeit und Alter der Werte sowie die \
        Zuordnung der Heizkreise zu den Verteilern. Mit rohdaten und geraet die vollständige \
        letzte Antwort eines Geräts. Liefert JSON.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Welche Geräte gelesen werden", .anyOf(["alle", "verteiler", "heizung"]))
        public var bereich: String
        @Guide(description: "Nur dieses Gerät: Kennung wie fbh_… oder heiz_…, oder sein Ort wie Erdgeschoss, Kessel, Pufferspeicher")
        public var geraet: String?
        @Guide(description: "true: die vollständige letzte Antwort des Geräts statt der Kurzfassung; nur mit geraet")
        public var rohdaten: Bool?
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let staende = await zugriff.staende()
        let bild = await zugriff.bild()
        return Self.ausgabe(arguments, staende: staende, bild: bild, jetzt: .now).ohneZugangsdaten().kompakt
    }

    static func ausgabe(_ a: Argumente, staende: [Geraetestand], bild: Anlagenbild, jetzt: Date) -> JSONWert {
        var auswahl = Aufloesung.geraete(a.geraet, in: staende, bild: bild)
        if a.geraet != nil, auswahl.isEmpty {
            return ["fehler": .text("Kein Gerät „\(a.geraet ?? "")“. Bekannt sind: \(staende.map { "\($0.ort) (\($0.geraet.id))" }.joined(separator: ", ")).")]
        }
        switch a.bereich {
        case "verteiler": auswahl = auswahl.filter { $0.geraet.art == .verteiler }
        case "heizung": auswahl = auswahl.filter { $0.geraet.art == .heizung }
        default: break
        }
        auswahl.sort { ($0.geraet.art.rawValue, $0.ort, $0.geraet.id) < ($1.geraet.art.rawValue, $1.ort, $1.geraet.id) }

        if a.rohdaten == true, a.geraet != nil, let s = auswahl.first {
            return ["geraet": .text(s.geraet.id), "ort": .text(s.ort), "erreichbar": .bool(s.erreichbar),
                    "rohdaten": s.rohzustand ?? .null]
        }

        var ergebnis: [String: JSONWert] = ["stand": .text(Zeitangabe.text(jetzt))]
        let verteiler = auswahl.filter { $0.geraet.art == .verteiler }
        let heizung = auswahl.filter { $0.geraet.art == .heizung }
        if !verteiler.isEmpty { ergebnis["verteiler"] = .liste(verteiler.map { geraet($0, jetzt) }) }
        if !heizung.isEmpty {
            ergebnis["heizungsgeraete"] = .liste(heizung.map { s in
                var e = geraet(s, jetzt)
                if let rolle = bild.geraete.first(where: { $0.id == s.geraet.id })?.art, rolle == .kessel || rolle == .speicher {
                    e["rolle"] = .text(rolle == .kessel ? "Kessel" : "Pufferspeicher")
                }
                return e
            })
        }
        let kreise = bild.heizkreise.map { k -> JSONWert in
            let orte = k.versorgteVerteiler.map { id in staende.first { $0.geraet.id == id }?.ort ?? id }
            return ["nr": .zahl(Double(k.nummer)), "name": .text(k.name), "versorgt": .liste(orte.map(JSONWert.text))]
        }
        if !kreise.isEmpty, a.bereich != "verteiler" { ergebnis["heizkreise_versorgen"] = .liste(kreise) }
        if let aussen = bild.aussen, a.bereich != "heizung" {
            ergebnis["aussen"] = ["c": .zahl((aussen.temperatur * 10).rounded() / 10), "quelle": .text(aussen.quelle),
                                  "alter_s": .zahl(Double(aussen.alter))]
        }
        return .objekt(ergebnis)
    }

    static func geraet(_ s: Geraetestand, _ jetzt: Date) -> JSONWert {
        var e: JSONWert = s.verteiler?.kurzfassung ?? s.heizgeraet?.kurzfassung ?? [:]
        e["geraet"] = .text(s.geraet.id)
        e["ort"] = .text(s.ort)
        e["erreichbar"] = .bool(s.erreichbar)
        if let kontakt = s.letzterKontakt {
            e["werte_alter_s"] = .zahl(max(0, jetzt.timeIntervalSince(kontakt)).rounded())
        }
        if !s.erreichbar {
            e["fehler"] = s.fehler.map(JSONWert.text) ?? "noch nicht abgefragt"
        }
        return e
    }
}

/// Zeitangaben für die KI: Ortszeit mit Abstand zu UTC, auf Sekunden
enum Zeitangabe {
    static func text(_ d: Date) -> String {
        d.formatted(Date.ISO8601FormatStyle(timeZone: .current))
    }

    static func tag(_ d: Date) -> String {
        String(text(d).prefix(10))
    }
}
