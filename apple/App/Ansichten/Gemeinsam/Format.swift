import Foundation

/// Zahlen und Zeiten in deutscher Schreibweise, unabhängig von der Spracheinstellung des Geräts.
enum Format {
    static let deutsch = Locale(identifier: "de_DE")

    /// Geschütztes Leerzeichen zwischen Zahl und Einheit, damit „72,3 °C“ nicht umbricht. In den
    /// festen Texten der App steht es als Zeichen U+00A0.
    static let fest = "\u{00A0}"

    /// „72,3 °C“ mit geschütztem Leerzeichen.
    static func mitEinheit(_ zahl: String, _ einheit: String) -> String {
        einheit.isEmpty ? zahl : zahl + fest + einheit
    }

    /// Schützt in Fließtext, der von außen kommt – Befunde der Geräte, Antworten der KI –, das
    /// Leerzeichen zwischen Zahl und Einheit.
    static func einheitenFest(_ text: String) -> String {
        text.replacing(#/([0-9)]) (?=(?:°C|K|%|l\/h|l|min|h|s|ms|mV|V|dBm|kΩ)(?![\p{L}\p{N}]))/#) { treffer in
            treffer.output.1 + fest
        }
    }

    static func zahl(_ wert: Double, stellen: Int = 1) -> String {
        wert.formatted(.number.precision(.fractionLength(stellen)).locale(deutsch))
    }

    static func temperatur(_ wert: Double?, stellen: Int = 1) -> String {
        guard let wert else { return "–" }
        return mitEinheit(zahl(wert, stellen: stellen), "°C")
    }

    static func kelvin(_ wert: Double?, stellen: Int = 1, vorzeichen: Bool = false) -> String {
        guard let wert else { return "–" }
        let text = zahl(wert, stellen: stellen)
        return mitEinheit((vorzeichen && wert > 0 ? "+" : "") + text, "K")
    }

    static func prozent(_ anteil: Double?) -> String {
        guard let anteil else { return "–" }
        return mitEinheit(prozentzahl(anteil), "%")
    }

    /// Nur die Zahl, für Kennzahlen mit eigener Einheit
    static func prozentzahl(_ anteil: Double?) -> String {
        guard let anteil else { return "–" }
        return "\(Int((anteil * 100).rounded()))"
    }

    static func liter(_ wert: Double, stellen: Int = 1) -> String {
        mitEinheit(zahl(wert, stellen: stellen), "l")
    }

    /// „57 min“, „1 h 12 min“, „28 h“
    /// „1 Start“, „3 Starts“
    static func starts(_ anzahl: Int) -> String {
        anzahl == 1 ? "1 Start" : "\(anzahl) Starts"
    }

    /// „3 h“, „2 Tagen“; für „seit …“
    static func seit(_ datum: Date, jetzt: Date = .now) -> String {
        let s = max(0, Int(jetzt.timeIntervalSince(datum)))
        if s < 60 { return "weniger als einer Minute" }
        if s < 3600 { return "\(s / 60)\u{00A0}min" }
        if s < 48 * 3600 { return "\(s / 3600)\u{00A0}h" }
        return "\(s / 86_400) Tagen"
    }

    static func dauer(_ sekunden: Int) -> String {
        if sekunden < 60 { return mitEinheit("\(sekunden)", "s") }
        let minuten = sekunden / 60
        if minuten < 60 { return mitEinheit("\(minuten)", "min") }
        let stunden = minuten / 60
        let rest = minuten % 60
        if stunden >= 24 || rest == 0 { return mitEinheit("\(stunden)", "h") }
        return mitEinheit("\(stunden)", "h") + " " + mitEinheit("\(rest)", "min")
    }

    /// „vor 12 min“, gemessen am Stand der Daten und nicht an der Uhr – im Klickmodell liegt der
    /// Stand in der Vergangenheit.
    static func alter(_ sekunden: Int?) -> String {
        guard let sekunden else { return "–" }
        return "vor " + dauer(sekunden)
    }

    static func relativ(_ datum: Date, bezug: Date) -> String {
        alter(max(0, Int(bezug.timeIntervalSince(datum))))
    }

    static func uhrzeit(_ datum: Date) -> String {
        datum.formatted(Date.FormatStyle(date: .omitted, time: .shortened).locale(deutsch))
    }

    static func tag(_ datum: Date) -> String {
        datum.formatted(Date.FormatStyle().day(.twoDigits).month(.twoDigits).year().locale(deutsch))
    }

    static func tagMitZeit(_ datum: Date) -> String {
        "\(tag(datum)) \(uhrzeit(datum))"
    }

    static func wochentag(_ nummer: Int?) -> String {
        guard let nummer, (0...6).contains(nummer) else { return "kein Termin" }
        return ["Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"][nummer]
    }

    static func signal(_ dbm: Int?) -> String {
        guard let dbm else { return "–" }
        let guete = switch dbm {
        case (-55)...: "sehr gut"
        case (-67)..<(-55): "gut"
        case (-75)..<(-67): "ausreichend"
        default: "schwach"
        }
        return mitEinheit("\(dbm)", "dBm") + " · " + guete
    }
}
