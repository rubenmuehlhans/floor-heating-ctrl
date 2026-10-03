import Foundation
import FoundationModels

/// Lesendes Werkzeug für die Apple-Modelle: Abschnitte aus Handbuch und Konzepten. Claude hat
/// beides vollständig in den Anweisungen und bekommt das Werkzeug nicht.
public struct WissenWerkzeug: Tool {
    public let name = "wissen_suchen"
    public let description = """
        Sucht im Handbuch der Anlage und in den Konzepten zu Wärmeerzeuger und Auswertung. \
        Liefert die drei passendsten Abschnitte mit Überschrift. Für Fragen, wie etwas \
        funktioniert, was eine Einstellung bewirkt oder was ein Befund bedeutet.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Suchbegriffe, etwa Kesselkreispumpe Schaltpunkte oder Rückströmung Schwerkraftbremse")
        public var anfrage: String
    }

    let wissen: Wissen
    let laenge: Int

    public init(wissen: Wissen, laenge: Int = 1500) {
        self.wissen = wissen
        self.laenge = laenge
    }

    public func call(arguments: Argumente) async throws -> String {
        let treffer = wissen.suchen(arguments.anfrage)
        guard !treffer.isEmpty else { return "Kein passender Abschnitt. Versuchen Sie andere Begriffe." }
        return treffer.map { a in
            let text = a.text.count > laenge ? String(a.text.prefix(laenge)) + " …" : a.text
            return "## \(a.titel) (\(a.dokument))\n\(text)"
        }.joined(separator: "\n\n")
    }
}
