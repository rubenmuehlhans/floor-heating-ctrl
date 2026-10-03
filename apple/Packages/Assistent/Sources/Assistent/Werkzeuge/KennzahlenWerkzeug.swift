import Foundation
import FoundationModels
import Anlage
import Diagnose

/// Lesendes Werkzeug: Kennzahlen über einen Zeitraum, berechnet in der App.
public struct KennzahlenWerkzeug: Tool {
    public let name = "kennzahlen"
    public let description = """
        Berechnet Kennzahlen über die letzten Tage: Brennerstarts und Laufzeit je Tag, \
        Laufzeit je Start, Öl je Tag, Heizgradtage, Stunden Brennerlauf je Heizgradtag und \
        Grundlast aus der Verbrauchslinie des Heizungsgeräts, Zahl, Dauer und Endtemperatur der \
        Speicherladungen, Abgas-Vorlauf-Abstand und Taktung des Kessels, je Raum mittlere \
        Abweichung vom Sollwert, Anteil der Zeit zu kalt (mehr als 0,5 K unter Soll) und zu \
        warm (mehr als 1 K über Soll) und die mittlere Ventilstellung.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Zeitraum in Tagen bis jetzt", .range(1...90))
        public var tage: Int
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let bis = Date.now
        let von = bis.addingTimeInterval(-Double(min(90, max(1, arguments.tage))) * 86_400)
        let k = await zugriff.kennzahlen(von: von, bis: bis)
        return Self.ausgabe(k)
    }

    static func ausgabe(_ k: Kennzahlen) -> String {
        let e = JSONEncoder()
        e.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        e.dateEncodingStrategy = .custom { datum, encoder in
            var c = encoder.singleValueContainer()
            try c.encode(Zeitangabe.text(datum))
        }
        e.nonConformingFloatEncodingStrategy = .convertToString(positiveInfinity: "inf", negativeInfinity: "-inf", nan: "nan")
        guard let daten = try? e.encode(k) else { return "{}" }
        var text = String(decoding: daten, as: UTF8.self)
        if k.tage == 0 { text += "\nHinweis: Für den Zeitraum liegt kein Tagesprotokoll vor." }
        if k.raeume.allSatisfy({ $0.messpunkte == 0 }) { text += "\nHinweis: Für die Räume liegt im Zeitraum kein gespeicherter Verlauf vor." }
        return text
    }
}
