import Foundation
import FoundationModels
import Anlage
import Diagnose
import Geraeteschnittstelle

/// Merkt sich die Erklärung des Betreibers zu einem Befund, etwa dass die Relais hinter der
/// Kesselregelung nachts stromlos sind. Wirkt nicht sofort: Der Betreiber bestätigt die Erklärung
/// auf einer Karte. Danach erscheint der Befund nicht mehr im Lagebericht und meldet sich nicht,
/// solange er an denselben Orten und mit derselben Schwere ansteht.
public struct BefundErklaerenWerkzeug: Tool {
    public let name = "befund_erklaeren"
    public let description = """
        Merkt sich, dass der Betreiber einen anstehenden Befund als bekannt oder gewollt erklärt \
        hat. Nur, wenn er das selbst sagt, nie aus eigener Einschätzung. Wirkt erst, wenn er die \
        Karte bestätigt. Danach gilt der Befund als erklärt, solange er an denselben Orten und \
        nicht schwerer ansteht; kommt ein Ort hinzu, steigt die Schwere, läuft die Geltung ab oder \
        steht er länger als die Höchstdauer an, meldet die App ihn wieder.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Kennung des Befunds aus befunde, etwa relais oder kreis-gesperrt:1")
        public var befund: String
        @Guide(description: "Die Erklärung des Betreibers in seinen Worten, ein bis drei Sätze")
        public var erklaerung: String
        @Guide(description: "Geltung in Tagen; 0 bis auf Widerruf", .range(0...3650))
        public var gueltigTage: Int
        @Guide(description: "Höchstens so viele Stunden am Stück gilt der Befund als erklärt, etwa die Dauer einer Nachtabsenkung mit Reserve; 0 ohne Grenze", .range(0...8760))
        public var hoechstdauerStunden: Int
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        switch Self.bauen(arguments, lage: await zugriff.befundlage(), jetzt: .now) {
        case .failure(let fehler):
            return JSONWert.objekt(["angelegt": false, "fehler": .liste(fehler.meldungen.map(JSONWert.text))]).kompakt
        case .success(let e):
            await zugriff.erklaerungAnlegen(e)
            return JSONWert.objekt([
                "angelegt": true, "erklaerung": .text(e.id), "befund": .text(e.befund), "titel": .text(e.titel),
                "orte": .liste(e.orte.map(JSONWert.text)), "geltung": .text(Self.geltung(e)),
                "hinweis": "Der Nutzer sieht die Erklärung als Karte mit Merken und Verwerfen; erst danach wirkt sie. Verweisen Sie auf die Karte.",
            ]).kompakt
        }
    }

    struct Fehler: Error {
        var meldungen: [String]
    }

    static func bauen(_ a: Argumente, lage: Befundlage, jetzt: Date) -> Result<Erklaerung, Fehler> {
        let kennung = a.befund.trimmingCharacters(in: .whitespacesAndNewlines)
        let text = a.erklaerung.trimmingCharacters(in: .whitespacesAndNewlines)
        let staende = lage.offen + lage.erklaert.map(\.stand)
        var fehler: [String] = []
        let stand = staende.first { $0.befund.id == kennung }
        if stand == nil {
            let bekannt = staende.map(\.befund.id).joined(separator: ", ")
            fehler.append("Der Befund „\(kennung)“ steht gerade nicht an. Erklären lassen sich nur anstehende Befunde\(bekannt.isEmpty ? "; zurzeit steht keiner an." : ": \(bekannt).")")
        }
        if text.isEmpty { fehler.append("Die Erklärung ist leer.") }
        if !(0...3650).contains(a.gueltigTage) { fehler.append("gueltigTage liegt zwischen 0 und 3650.") }
        if !(0...8760).contains(a.hoechstdauerStunden) { fehler.append("hoechstdauerStunden liegt zwischen 0 und 8760.") }
        guard fehler.isEmpty, let stand else { return .failure(Fehler(meldungen: fehler)) }
        return .success(Erklaerung(
            befund: stand.befund, text: text, erstellt: jetzt,
            bis: a.gueltigTage > 0 ? jetzt.addingTimeInterval(Double(a.gueltigTage) * 86_400) : nil,
            hoechstdauer: a.hoechstdauerStunden > 0 ? Double(a.hoechstdauerStunden) * 3600 : nil))
    }

    /// Geltung in Worten, etwa „bis auf Widerruf, höchstens 14 Stunden am Stück“
    static func geltung(_ e: Erklaerung) -> String {
        var teile = [e.bis.map { "bis \(Zeitangabe.tag($0))" } ?? "bis auf Widerruf"]
        if let h = e.hoechstdauer {
            let stunden = Int((h / 3600).rounded())
            teile.append("höchstens \(stunden == 1 ? "eine Stunde" : "\(stunden) Stunden") am Stück")
        }
        return teile.joined(separator: ", ")
    }
}
