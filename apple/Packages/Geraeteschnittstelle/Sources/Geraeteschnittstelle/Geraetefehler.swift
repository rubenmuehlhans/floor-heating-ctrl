import Foundation

/// Fehler im Umgang mit einem Gerät. Die Meldungen der Firmware sind deutsch und werden
/// unverändert angezeigt.
public enum Geraetefehler: Error, Sendable, Equatable {
    /// `{"ok":false,"error":"…"}` – das Gerät hat die Anfrage mit einer Begründung abgelehnt.
    case meldung(String, status: Int)
    /// HTTP-Fehler ohne lesbare Begründung
    case status(Int)
    /// Das Gerät leitet um. So antwortet es im Zugangspunktbetrieb auf unbekannte Pfade; einer
    /// Weiterleitung folgt die App nie.
    case weiterleitung(ziel: String?)
    /// Die Antwort ist kein JSON oder passt nicht zum erwarteten Aufbau.
    case unerwarteteAntwort(String)
    case nichtErreichbar(String)
    case zeitueberschreitung
    /// Die Firmwaredatei gehört zur anderen Geräteart.
    case falscheFirmware(erwartet: String, gefunden: String)
    /// Der Befehl ist so nicht zulässig, etwa `position` an alle Kreise.
    case unzulaessig(String)
}

extension Geraetefehler: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .meldung(_, status: 404), .status(404):
            "Das Gerät kennt diese Funktion nicht; vermutlich ist seine Firmware älter als die App."
        case .meldung(let text, _):
            text
        case .status(let code):
            "Das Gerät hat mit Status \(code) geantwortet."
        case .weiterleitung:
            "Das Gerät leitet die Anfrage um. Vermutlich ist es im Einrichtungsbetrieb."
        case .unerwarteteAntwort(let grund):
            "Die Antwort des Geräts ist nicht lesbar: \(grund)"
        case .nichtErreichbar(let grund):
            "Das Gerät ist nicht erreichbar: \(grund)"
        case .zeitueberschreitung:
            "Das Gerät hat nicht rechtzeitig geantwortet."
        case .falscheFirmware(let erwartet, let gefunden):
            "Die Datei enthält „\(gefunden)“, dieses Gerät braucht „\(erwartet)“."
        case .unzulaessig(let grund):
            grund
        }
    }
}
