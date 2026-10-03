import Foundation
import FoundationModels
import Anlage

/// Lesendes Werkzeug: Ladungs- und Tagesprotokoll des Heizungsgeräts und das
/// Änderungsprotokoll der App.
public struct ProtokolleWerkzeug: Tool {
    public let name = "protokolle"
    public let description = """
        Liest Protokolle. ladungen: je Speicherladung Beginn, Dauer, Brennerlaufzeit, Starts, \
        Speicher vorher und nachher, höchster Kesselvorlauf, höchstes Abgas, mittlere \
        Außentemperatur und geschätzter Ölverbrauch. tage: je Kalendertag Brennerlaufzeit, \
        Starts, Öl, Heizgradtage, Außentemperatur tiefst und höchst. aenderungen: Änderungen an \
        Einstellungen über die App, auch übernommene und zurückgenommene Vorschläge. Jüngste zuerst.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Welches Protokoll", .anyOf(["ladungen", "tage", "aenderungen"]))
        public var art: String
        @Guide(description: "Höchstens so viele Einträge, jüngste zuerst", .range(1...120))
        public var anzahl: Int
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let anzahl = min(120, max(1, arguments.anzahl))
        switch arguments.art {
        case "aenderungen":
            return Self.aenderungen(Array(await zugriff.aenderungen().prefix(anzahl)))
        case "tage":
            return Self.tage(Array(await zugriff.bild().tage.sorted { $0.datum > $1.datum }.prefix(anzahl)))
        default:
            return Self.ladungen(Array(await zugriff.bild().ladungen.sorted { $0.beginn > $1.beginn }.prefix(anzahl)))
        }
    }

    static func ladungen(_ l: [Ladungssatz]) -> String {
        guard !l.isEmpty else { return "Das Ladungsprotokoll ist leer oder kein Heizungsgerät mit Protokoll eingebunden." }
        var t = "beginn;dauer_min;brenner_min;starts;speicher_vorher_c;speicher_nachher_c;kessel_vl_max_c;abgas_max_c;aussen_mittel_c;oel_l\n"
        for e in l {
            t += [Zeitangabe.text(e.beginn), String(e.dauer / 60), String(e.brenner / 60), String(e.starts),
                  f(e.speicherVorher), f(e.speicherNachher), f(e.kesselVorlaufMax), f(e.abgasMax),
                  e.aussenMittel.map { f($0) } ?? "", String(format: "%.2f", e.liter)].joined(separator: ";") + "\n"
        }
        return t
    }

    static func tage(_ tage: [Tagessatz]) -> String {
        guard !tage.isEmpty else { return "Das Tagesprotokoll ist leer oder kein Heizungsgerät mit Protokoll eingebunden." }
        var t = "datum;brenner_h;starts;oel_l;heizgradtage;aussen_min_c;aussen_max_c\n"
        for e in tage {
            t += [Zeitangabe.tag(e.datum), String(format: "%.2f", Double(e.laufzeit) / 3600), String(e.starts),
                  String(format: "%.2f", e.liter), f(e.heizgradtage), e.aussenMin.map { f($0) } ?? "",
                  e.aussenMax.map { f($0) } ?? ""].joined(separator: ";") + "\n"
        }
        return t
    }

    static func aenderungen(_ a: [Aenderung]) -> String {
        guard !a.isEmpty else { return "Über die App wurde noch nichts geändert." }
        var t = "zeit;geraet;einstellung;bisher;neu;ausloeser\n"
        for e in a {
            t += [Zeitangabe.text(e.zeit), e.geraet, e.parameter, e.bisher, e.neu, e.ausloeser].joined(separator: ";") + "\n"
        }
        return t
    }

    private static func f(_ x: Double) -> String { String(format: "%.1f", x) }
}
