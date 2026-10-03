import Foundation

/// Fehler, für die FoundationModels keinen eigenen Fall hat. Ablehnung, Ratenbegrenzung und
/// überschrittener Kontext kommen als `LanguageModelError`.
public enum ClaudeFehler: Error, Sendable, Equatable {
    /// 401: Schlüssel fehlt, ist falsch oder widerrufen.
    case schluesselUngueltig
    /// 403: Der Schlüssel darf dieses Modell oder diese Funktion nicht verwenden.
    case keineBerechtigung(String)
    /// 400 und andere Fehler der Anfrage; die Meldung der API bleibt erhalten.
    case anfrage(status: Int, art: String, meldung: String)
    /// 529 oder 5xx, als HTTP-Status oder als Fehlerereignis im Strom, auch nach Wiederholung
    case ueberlastet(status: Int)
    /// Fehlerereignis im Datenstrom
    case imStrom(art: String, meldung: String)
    /// Die Antwort endete an `max_tokens`, während ein Werkzeugaufruf noch geschrieben wurde. Ein
    /// halber Aufruf wird nicht ausgeführt.
    case werkzeugaufrufAbgeschnitten
    /// Der Datenstrom brach ab, bevor die Nachricht vollständig war.
    case unvollstaendig
}

extension ClaudeFehler: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .schluesselUngueltig:
            "Der API-Schlüssel von Anthropic wurde abgelehnt. Bitte prüfen Sie ihn in den Einstellungen."
        case .keineBerechtigung(let meldung):
            "Der API-Schlüssel hat keine Berechtigung dafür: \(meldung)"
        case .anfrage(let status, _, let meldung):
            "Die Anfrage an Claude wurde abgewiesen (\(status)): \(meldung)"
        case .ueberlastet:
            "Claude ist gerade überlastet. Bitte versuchen Sie es in einigen Minuten erneut."
        case .imStrom(_, let meldung):
            "Die Antwort von Claude brach ab: \(meldung)"
        case .werkzeugaufrufAbgeschnitten:
            "Die Antwort wurde abgeschnitten, während Claude ein Werkzeug aufrufen wollte."
        case .unvollstaendig:
            "Die Verbindung zu Claude brach vor dem Ende der Antwort ab."
        }
    }
}

extension ClaudeFehler {
    /// Überlastung und Serverfehler vergehen meist von selbst; eine Wiederholung lohnt.
    var voruebergehend: Bool {
        switch self {
        case .ueberlastet: true
        case .imStrom(let art, _): art == "overloaded_error" || art == "api_error"
        default: false
        }
    }
}
