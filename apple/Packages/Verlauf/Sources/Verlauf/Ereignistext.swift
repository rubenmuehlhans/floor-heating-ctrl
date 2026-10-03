import Foundation
import Geraeteschnittstelle

/// Ereignisse in Worten, für Listen, Markierungen und den Assistenten
public enum Ereignistext {
    /// Arten, die im Verlauf der Heizung als Markierung erscheinen. Brenner und Pumpen stehen
    /// dort schon als Reihe, Sollwerte als Linie.
    public static let markiert: Set<String> = ["neustart", "version", "nicht_erreichbar", "erreichbar", "befund"]

    /// Neustartgrund aus `reset_reason`
    public static func grund(_ g: String) -> String {
        switch g {
        case "power_on": "Einschalten oder Stromausfall"
        case "software": "Neustart durch die Software, etwa nach einem Update"
        case "panic": "Absturz"
        case "int_wdt", "task_wdt", "wdt": "Wächter ausgelöst"
        case "brownout", "pwr_glitch": "Unterspannung"
        case "deepsleep": "Aufwachen aus dem Tiefschlaf"
        case "ext": "Reset-Taste oder externer Reset"
        case "usb", "jtag": "Neustart über USB"
        case "cpu_lockup": "Prozessor blockiert"
        default: "Grund unbekannt"
        }
    }

    /// Kurze Überschrift, etwa „Neustart“
    public static func titel(_ e: Ereignis) -> String {
        switch e.art {
        case "neustart": "Neustart"
        case "nicht_erreichbar": "Nicht erreichbar"
        case "erreichbar": "Wieder erreichbar"
        case "version": "Neue Firmware"
        case "brenner": e.felder["ein"]?.alsBool == true ? "Brenner an" : "Brenner aus"
        case "pumpe": e.felder["ein"]?.alsBool == true ? "Pumpe an" : "Pumpe aus"
        case "sollwert": "Sollwert geändert"
        case "betriebsart": e.felder["neu"]?.alsText == "aus" ? "Raum ausgeschaltet" : "Raum eingeschaltet"
        case "befund": e.felder["stand"]?.alsText == "erledigt" ? "Befund erledigt" : "Neuer Befund"
        case "funk": "Funkthermometer"
        case "leitstand": "Leitstand"
        default: e.art
        }
    }

    /// Einzelheiten ohne Gerät und Zeit; leer, wenn das Ereignis keine hat. `befundtitel`
    /// übersetzt die Kennung eines Befunds, etwa `Zusammenfuehrung.geraetebefundTitel`.
    public static func einzelheiten(_ e: Ereignis, befundtitel: (String) -> String? = { _ in nil }) -> String {
        let f = e.felder
        switch e.art {
        case "neustart":
            var teile: [String] = []
            if let g = f["grund"]?.alsText { teile.append(grund(g)) }
            if let s = f["laufzeit_vorher_s"]?.alsZahl { teile.append("lief vorher \(dauer(s))") }
            return teile.joined(separator: ", ")
        case "erreichbar":
            return f["dauer_s"]?.alsZahl.map { "nach \(dauer($0))" } ?? ""
        case "version":
            return [f["alt"]?.alsText, f["neu"]?.alsText].compactMap { $0 }.joined(separator: " → ")
        case "brenner", "pumpe":
            var teile: [String] = []
            if e.art == "pumpe", let p = f["pumpe"]?.alsText { teile.append(p == "kkp" ? "Kesselkreispumpe" : "Kreis \(p)") }
            if let s = f["dauer_vorher_s"]?.alsZahl { teile.append("vorher \(dauer(s))") }
            return teile.joined(separator: ", ")
        case "sollwert":
            let raum = f["raum"]?.alsGanzzahl.map { "Raum \($0): " } ?? ""
            return raum + [f["alt"]?.alsZahl, f["neu"]?.alsZahl].compactMap { $0.map(grad) }.joined(separator: " → ")
        case "betriebsart":
            return f["raum"]?.alsGanzzahl.map { "Raum \($0)" } ?? ""
        case "befund":
            return f["code"]?.alsText.map { befundtitel($0) ?? $0 } ?? ""
        case "funk":
            let name = f["name"]?.alsText ?? f["adresse"]?.alsText ?? ""
            switch f["stand"]?.alsText {
            case "verloren": return "\(name) seit einer Viertelstunde stumm"
            case "wieder": return "\(name) wieder empfangen"
            case "schluessel_falsch": return "Schlüssel für \(name) passt nicht"
            default: return name
            }
        case "leitstand":
            let akku = f["akku_prozent"]?.alsGanzzahl.map { ", Akku \($0)\u{00A0}%" } ?? ""
            switch f["was"]?.alsText {
            case "start": return "gestartet" + (f["grund"]?.alsText.map { ", \(grund($0))" } ?? "")
            case "versorgung_aus": return "Stromversorgung ausgefallen, läuft auf Akku" + akku
            case "versorgung_wieder": return "Stromversorgung wieder da" + akku
            case "karte": return "Karte eingehängt"
            case "karte_verloren": return "Karte entfernt oder gestört"
            case "karte_formatiert": return "Karte formatiert"
            case let was?: return was
            case nil: return ""
            }
        default:
            return ""
        }
    }

    /// „3 h 12 min“, „45 s“
    public static func dauer(_ s: Double) -> String {
        let s = Int(s.rounded())
        // Geschützte Leerzeichen, damit Zahl und Einheit nicht getrennt umbrechen
        let f = "\u{00A0}"
        if s < 60 { return "\(s)\(f)s" }
        if s < 3600 { return "\(s / 60)\(f)min" }
        if s < 86_400 { return s % 3600 / 60 == 0 ? "\(s / 3600)\(f)h" : "\(s / 3600)\(f)h \(s % 3600 / 60)\(f)min" }
        return "\(s / 86_400)\(f)d \(s % 86_400 / 3600)\(f)h"
    }

    private static func grad(_ w: Double) -> String {
        w.formatted(.number.precision(.fractionLength(1)).locale(Locale(identifier: "de_DE"))) + "\u{00A0}°C"
    }

    /// Zeiträume, in denen ein Gerät nicht erreichbar war: aus `erreichbar` mit `dauer_s`, und
    /// bis `bis`, wenn auf `nicht_erreichbar` noch keine Antwort folgte.
    public static func ausfaelle(_ liste: [Ereignis], bis: Date) -> [(geraet: String, von: Date, bis: Date)] {
        var offen: [String: Date] = [:]
        var ergebnis: [(geraet: String, von: Date, bis: Date)] = []
        for e in liste.sorted(by: { $0.zeit < $1.zeit }) {
            switch e.art {
            case "nicht_erreichbar":
                offen[e.geraet] = offen[e.geraet] ?? e.zeit
            case "erreichbar":
                let beginn = e.felder["dauer_s"]?.alsZahl.map { e.zeit.addingTimeInterval(-$0) } ?? offen[e.geraet] ?? e.zeit
                ergebnis.append((e.geraet, beginn, e.zeit))
                offen[e.geraet] = nil
            default:
                break
            }
        }
        for (g, beginn) in offen { ergebnis.append((g, beginn, bis)) }
        return ergebnis.sorted { $0.von < $1.von }
    }
}
