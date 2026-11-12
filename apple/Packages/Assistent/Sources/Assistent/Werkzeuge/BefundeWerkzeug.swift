import Foundation
import FoundationModels
import Anlage
import Diagnose
import Geraeteschnittstelle

/// Lesendes Werkzeug: die Befunde der Firmware und der Prüfungen der App mit Beginn und Beleg.
public struct BefundeWerkzeug: Tool {
    public let name = "befunde"
    public let description = """
        Liest die offenen Befunde: Meldungen der Firmware (etwa Rückströmung, vertauschter \
        Vorlauf, Taktung) und der Prüfungen der App (etwa Gerät nicht erreichbar, Raum ohne \
        Thermometer, Heizkreis ohne Verteiler, fehlende Sicherung), je mit Schwere, Text als \
        Beleg, Ort, Quelle und seit wann er ansteht. Unter erklaert die Befunde, die der \
        Betreiber als bekannt und gewollt erklärt hat, mit seiner Erklärung; sie sind kein \
        Problem. Auf Wunsch die in den letzten 30 Tagen erledigten.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "true: auch die in den letzten 30 Tagen erledigten Befunde")
        public var erledigte: Bool
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        Self.ausgabe(await zugriff.befundlage(), erledigte: arguments.erledigte).kompakt
    }

    static func schwere(_ s: Befund.Schwere) -> String {
        switch s {
        case .stoerung: "stoerung"
        case .warnung: "warnung"
        case .hinweis: "hinweis"
        }
    }

    static func ausgabe(_ lage: Befundlage, erledigte: Bool) -> JSONWert {
        let offen = lage.offen.sorted { $0.befund.schwere > $1.befund.schwere }.map { s -> JSONWert in
            var e: JSONWert = [
                "id": .text(s.befund.id), "schwere": .text(schwere(s.befund.schwere)), "titel": .text(s.befund.titel),
                "text": .text(s.befund.text), "ort": .text(s.befund.ort), "quelle": .text(s.befund.quelle),
            ]
            if let seit = s.seit { e["seit"] = .text(Zeitangabe.text(seit)) }
            if s.stumm { e["mitteilung_stumm"] = true }
            return e
        }
        var ergebnis: JSONWert = ["offen": .liste(offen)]
        if offen.isEmpty { ergebnis["hinweis"] = "Keine offenen Befunde." }
        if !lage.erklaert.isEmpty {
            ergebnis["erklaert"] = .liste(lage.erklaert.map { x in
                var e: JSONWert = [
                    "id": .text(x.stand.befund.id), "schwere": .text(schwere(x.stand.befund.schwere)),
                    "titel": .text(x.stand.befund.titel), "ort": .text(x.stand.befund.ort),
                    "erklaerung": .text(x.erklaerung.text), "erklaert_am": .text(Zeitangabe.text(x.erklaerung.gemerkt ?? x.erklaerung.erstellt)),
                    "geltung": .text(BefundErklaerenWerkzeug.geltung(x.erklaerung)),
                ]
                if let seit = x.stand.seit { e["seit"] = .text(Zeitangabe.text(seit)) }
                return e
            })
        }
        if erledigte {
            ergebnis["erledigt"] = .liste(lage.erledigt.map { e in
                ["id": .text(e.id), "schwere": .text(schwere(e.befundschwere)), "titel": .text(e.titel), "ort": .text(e.ort),
                 "von": .text(Zeitangabe.text(e.erstmals)), "bis": e.erledigt.map { .text(Zeitangabe.text($0)) } ?? .null]
            })
        }
        return ergebnis
    }
}
