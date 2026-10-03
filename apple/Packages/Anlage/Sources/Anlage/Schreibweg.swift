import Foundation
import Geraeteschnittstelle

/// Eine geänderte Einstellung, bei Listen samt Eintrag (`id` oder `rom`).
public struct Wertaenderung: Sendable, Hashable {
    public var parameter: Parameter
    public var eintrag: JSONWert?
    public var wert: JSONWert

    public init(_ parameter: Parameter, eintrag: JSONWert? = nil, wert: JSONWert) {
        self.parameter = parameter
        self.eintrag = eintrag
        self.wert = wert
    }
}

/// Baut die Teilangabe für `PUT /api/config` nach den Regeln der Firmware und prüft sie vorher
/// wie `config_store.c`.
///
/// - Gruppen wie `buffer` ändern nur, was genannt ist.
/// - `rooms` ersetzt die ganze Liste, und jeder Raum wird aus den Vorgaben neu aufgebaut; ein
///   fehlendes `sensor_mac` löst das Thermometer. Die App sendet deshalb stets alle Räume
///   vollständig, aus der frisch gelesenen Konfiguration.
/// - `channels` wird je `id` zusammengeführt; es genügt der geänderte Eintrag.
/// - `circuits` und `probes` ersetzen die Liste, jeder Eintrag verbindet sich aber mit dem
///   bisherigen gleicher Kennung. Die App sendet alle Kennungen mit, damit kein Eintrag entfällt,
///   und nur die geänderten Felder.
public enum Schreibweg {
    public static func teil(_ aenderungen: [Wertaenderung], konfiguration k: JSONWert) -> JSONWert {
        var teil: JSONWert = [:]
        for a in aenderungen where a.parameter.ort == .geraet {
            setzen(&teil, a.parameter.pfad, a.wert, vorlage: k)
        }
        // Räume: vollständig
        let raeume = aenderungen.filter { $0.parameter.ort == .raum }
        if !raeume.isEmpty {
            var liste = k["rooms"]?.alsListe ?? []
            for a in raeume {
                guard let i = liste.firstIndex(where: { $0["id"] == a.eintrag }) else { continue }
                setzen(&liste[i], a.parameter.pfad, a.wert, vorlage: liste[i])
            }
            teil["rooms"] = .liste(liste)
        }
        // Kanäle: nur die geänderten Einträge
        let kanaele = aenderungen.filter { $0.parameter.ort == .kanal }
        if !kanaele.isEmpty {
            var eintraege: [JSONWert] = []
            for a in kanaele {
                guard let id = a.eintrag else { continue }
                if let i = eintraege.firstIndex(where: { $0["id"] == id }) {
                    setzen(&eintraege[i], a.parameter.pfad, a.wert, vorlage: .null)
                } else {
                    var e: JSONWert = ["id": id]
                    setzen(&e, a.parameter.pfad, a.wert, vorlage: .null)
                    eintraege.append(e)
                }
            }
            teil["channels"] = .liste(eintraege)
        }
        // Heizkreise und Fühler: alle Kennungen, geänderte Felder
        for ort in [Parameter.Ort.heizkreis, .fuehler] {
            let betroffen = aenderungen.filter { $0.parameter.ort == ort }
            guard !betroffen.isEmpty, let name = ort.liste else { continue }
            let schluessel = ort.schluessel
            var liste: [JSONWert] = (k[name]?.alsListe ?? []).compactMap { e in
                e[schluessel].map { [schluessel: $0] }
            }
            for a in betroffen {
                guard let id = a.eintrag else { continue }
                if let i = liste.firstIndex(where: { $0[schluessel] == id }) {
                    setzen(&liste[i], a.parameter.pfad, a.wert, vorlage: .null)
                } else {
                    var e: JSONWert = [schluessel: id]
                    setzen(&e, a.parameter.pfad, a.wert, vorlage: .null)
                    liste.append(e)
                }
            }
            teil[name] = .liste(liste)
        }
        return teil
    }

    /// Setzt einen Wert an einem Pfad. Zahlen im Pfad sind Listenplätze; die übrigen Plätze
    /// kommen aus der Vorlage, weil die Firmware Listen wie `onewire_pin` platzweise übernimmt.
    static func setzen(_ ziel: inout JSONWert, _ pfad: [String], _ wert: JSONWert, vorlage: JSONWert?) {
        guard let kopf = pfad.first else {
            ziel = wert
            return
        }
        let rest = Array(pfad.dropFirst())
        if let n = rest.first.flatMap(Int.init), rest.count == 1 {
            var liste = ziel[kopf]?.alsListe ?? vorlage?[kopf]?.alsListe ?? []
            while liste.count <= n { liste.append(.null) }
            liste[n] = wert
            ziel[kopf] = .liste(liste)
            return
        }
        if case .objekt = ziel {} else { ziel = [:] }
        var unter = ziel[kopf] ?? [:]
        setzen(&unter, rest, wert, vorlage: vorlage?[kopf])
        ziel[kopf] = unter
    }

    // MARK: - Zusammenführen wie die Firmware

    /// Die Konfiguration, wie sie nach dem Schreiben der Teilangabe aussähe.
    public static func zusammengefuehrt(_ k: JSONWert, _ teil: JSONWert) -> JSONWert {
        var neu = k
        for (schluessel, wert) in teil.alsObjekt ?? [:] {
            switch schluessel {
            case "rooms":
                neu[schluessel] = wert
            case "channels":
                var liste = k[schluessel]?.alsListe ?? []
                for e in wert.alsListe ?? [] {
                    if let i = liste.firstIndex(where: { $0["id"] == e["id"] }) {
                        liste[i] = objektZusammen(liste[i], e)
                    }
                }
                neu[schluessel] = .liste(liste)
            case "circuits", "probes":
                let alt = k[schluessel]?.alsListe ?? []
                let key = schluessel == "probes" ? "rom" : "id"
                neu[schluessel] = .liste((wert.alsListe ?? []).map { e in
                    alt.first { $0[key] == e[key] }.map { objektZusammen($0, e) } ?? e
                })
            case "onewire_pin":
                var liste = k[schluessel]?.alsListe ?? []
                for (i, e) in (wert.alsListe ?? []).enumerated() where e.alsZahl != nil {
                    while liste.count <= i { liste.append(.null) }
                    liste[i] = e
                }
                neu[schluessel] = .liste(liste)
            default:
                if case .objekt = wert, let alt = k[schluessel] {
                    neu[schluessel] = objektZusammen(alt, wert)
                } else if wert != .null {
                    neu[schluessel] = wert
                }
            }
        }
        return neu
    }

    private static func objektZusammen(_ alt: JSONWert, _ neu: JSONWert) -> JSONWert {
        guard case .objekt(var felder) = alt, case .objekt(let dazu) = neu else { return neu }
        for (k, v) in dazu {
            if case .objekt = v, let a = felder[k] {
                felder[k] = objektZusammen(a, v)
            } else if case .liste = v, k == "thresholds", case .liste(var bisher)? = felder[k] {
                for (i, e) in (v.alsListe ?? []).enumerated() where e != .null {
                    while bisher.count <= i { bisher.append(.null) }
                    bisher[i] = e
                }
                felder[k] = .liste(bisher)
            } else {
                felder[k] = v
            }
        }
        return .objekt(felder)
    }

    // MARK: - Prüfung

    /// Meldungen wie die Firmware, nur deutsch mit Umlauten; leer, wenn alles passt.
    public static func pruefen(_ k: JSONWert, _ art: Geraeteart) -> [String] {
        var fehler: [String] = []
        for p in Parameterkatalog.alle where p.geraet == art {
            for (wert, wo) in werte(p, in: k) {
                if let f = bereichsfehler(p, wert) { fehler.append(wo.map { "\($0): \(f)" } ?? f) }
            }
        }
        let z = { (pfad: String) in pfad.split(separator: ".").reduce(Optional(k)) { $0?[String($1)] }?.alsZahl }
        if let ap = k["wifi"]?["ap_pass"]?.alsText, !ap.isEmpty, ap.count < 8 {
            fehler.append("Das Kennwort des Zugangspunkts braucht mindestens acht Zeichen.")
        }
        if k["mqtt"]?["enabled"]?.alsBool == true, (k["mqtt"]?["uri"]?.alsText ?? "").isEmpty {
            fehler.append("Ohne Adresse des Brokers lässt sich MQTT nicht einschalten.")
        }
        switch art {
        case .verteiler:
            let raeume = k["rooms"]?.alsListe ?? []
            if raeume.count > 11 { fehler.append("Höchstens 11 Räume sind möglich.") }
            var belegt: [Int: String] = [:]
            var ids: Set<Int> = []
            for r in raeume {
                let name = r["name"]?.alsText ?? ""
                if name.trimmingCharacters(in: .whitespaces).isEmpty { fehler.append("Jeder Raum braucht einen Namen.") }
                if let id = r["id"]?.alsGanzzahl, !ids.insert(id).inserted { fehler.append("Raumkennung \(id) ist doppelt vergeben.") }
                for c in r["channels"]?.alsListe?.compactMap(\.alsGanzzahl) ?? [] {
                    if let anderer = belegt[c] { fehler.append("Kanal \(c) gehört schon zu „\(anderer)“.") }
                    belegt[c] = name
                }
            }
            for c in k["channels"]?.alsListe ?? [] {
                let n = c["id"]?.alsGanzzahl ?? 0
                let laengste = max(c["open_ms"]?.alsZahl ?? 0, c["close_ms"]?.alsZahl ?? 0)
                if let m = c["max_ms"]?.alsZahl, m < laengste {
                    fehler.append("Kanal \(n): Die Maximallaufzeit ist kürzer als die Fahrzeit.")
                }
                if let h = c["bemf_hyst_mv"]?.alsZahl, let s = c["bemf_mv"]?.alsZahl, h > s {
                    fehler.append("Kanal \(n): Die Hysterese ist größer als die Auslöseschwelle.")
                }
            }
        case .heizung:
            if let an = z("burner.delta_on_k"), let aus = z("burner.delta_off_k"), an <= aus {
                fehler.append("Die Einschaltschwelle des Brenners muss über der Ausschaltschwelle liegen.")
            }
            if let voll = z("buffer.voll_c"), let leer = z("buffer.leer_c"), voll <= leer {
                fehler.append("Der Wert für „voll“ muss über dem für „leer“ liegen.")
            }
            let rollen = (k["probes"]?.alsListe ?? []).compactMap { $0["role"]?.alsText }.filter { !$0.isEmpty }
            if (k["probes"]?.alsListe ?? []).count > 12 { fehler.append("Höchstens 12 Fühler sind möglich.") }
            for r in Set(rollen) where rollen.filter({ $0 == r }).count > 1 {
                fehler.append("Die Rolle „\(Parameterkatalog.rollenname(r))“ ist zweimal vergeben.")
            }
            let kreise = k["circuits"]?.alsListe ?? []
            if kreise.count > 4 { fehler.append("Höchstens 4 Heizkreise sind möglich.") }
            if !kreise.isEmpty && !rollen.contains("puffer") {
                fehler.append("Heizkreise brauchen den Pufferfühler an diesem Gerät.")
            }
            for c in kreise where (c["peers"]?.alsListe ?? []).count > 4 {
                fehler.append("\(c["name"]?.alsText ?? "Ein Heizkreis") versorgt mehr als 4 Verteiler.")
            }
            if k["boiler_pump"]?["enabled"]?.alsBool == true {
                if !(rollen.contains("kessel_vl") && rollen.contains("kessel_rl")) {
                    fehler.append("Die Kesselkreispumpe braucht Kesselvor- und -rücklauf an diesem Gerät.")
                }
                if let an = z("boiler_pump.on_k"), let aus = z("boiler_pump.off_k"), aus >= an {
                    fehler.append("Die Ausschaltschwelle der Kesselkreispumpe muss unter der Einschaltschwelle liegen.")
                }
            }
            let pins = (k["onewire_pin"]?.alsListe ?? []).compactMap(\.alsGanzzahl)
            if !pins.contains(where: { $0 >= 0 }) { fehler.append("Mindestens ein 1-Wire-Bus muss angegeben sein.") }
            if pins.count == 2, pins[0] >= 0, pins[0] == pins[1] { fehler.append("Beide Busse können nicht am selben GPIO liegen.") }
            for pin in pins where pin >= 0 {
                if let f = gpiofehler(pin) { fehler.append("GPIO \(pin) \(f).") }
            }
        }
        return Array(NSOrderedSet(array: fehler)) as? [String] ?? fehler
    }

    /// Wie `pin_problem` in `config_store.c`
    static func gpiofehler(_ pin: Int) -> String? {
        switch pin {
        case 34...39: "ist nur als Eingang ausgelegt und kann den Bus nicht treiben"
        case 6...11: "gehört zum Flash-Speicher"
        case 1, 3: "ist die serielle Schnittstelle"
        case 12: "entscheidet beim Start über die Flash-Spannung"
        case 34...: "gibt es nicht"
        default: nil
        }
    }

    /// Alle Werte eines Parameters in der Konfiguration, bei Listen je Eintrag
    static func werte(_ p: Parameter, in k: JSONWert) -> [(JSONWert, String?)] {
        let lesen = { (e: JSONWert) in p.pfad.reduce(Optional(e)) { acc, t in Int(t).map { acc?[$0] } ?? acc?[t] } }
        guard let name = p.ort.liste else {
            return lesen(k).map { [($0, nil)] } ?? []
        }
        return (k[name]?.alsListe ?? []).compactMap { e in
            guard let w = lesen(e) else { return nil }
            let wo = e["name"]?.alsText ?? e[p.ort.schluessel]?.kompakt
            return (w, wo)
        }
    }

    static func bereichsfehler(_ p: Parameter, _ w: JSONWert) -> String? {
        let deutsch = Locale(identifier: "de_DE")
        let f = { (x: Double) in x.formatted(.number.precision(.fractionLength(0...2)).locale(deutsch)) }
        let einheit = p.einheit.isEmpty ? "" : "\u{00A0}\(p.einheit)"
        switch p.art {
        case .zahl(let bereich, _, _):
            guard let x = w.alsZahl else { return nil }
            return bereich.contains(x) ? nil : "\(p.bezeichnung) muss zwischen \(f(bereich.lowerBound)) und \(f(bereich.upperBound))\(einheit) liegen"
        case .ganzzahl(let bereich, _):
            guard let x = w.alsZahl else { return nil }
            return (Double(bereich.lowerBound)...Double(bereich.upperBound)).contains(x) ? nil
                : "\(p.bezeichnung) muss zwischen \(bereich.lowerBound) und \(bereich.upperBound)\(einheit) liegen"
        case .millisekunden(let bereich, _):
            guard let x = w.alsZahl.map({ $0 / 1000 }) else { return nil }
            return bereich.contains(x) ? nil : "\(p.bezeichnung) muss zwischen \(f(bereich.lowerBound)) und \(f(bereich.upperBound))\(einheit) liegen"
        case .text(let laenge):
            guard let t = w.alsText else { return nil }
            return t.utf8.count > laenge ? "\(p.bezeichnung) ist länger als \(laenge) Zeichen" : nil
        case .kennwort(let laenge, _):
            guard let t = w.alsText else { return nil }
            return t.utf8.count > laenge ? "\(p.bezeichnung) ist länger als \(laenge) Zeichen" : nil
        case .schalter, .auswahl:
            return nil
        }
    }
}
