import Foundation
import FoundationModels
import Anlage
import Verlauf

/// Lesendes Werkzeug: Ereignisse aus dem Protokoll des Leitstands, das die App übernimmt.
/// Zuerst eine Zusammenfassung je Art und Gerät, damit Fragen wie „wie oft ist der Brenner
/// gestern angesprungen“ ohne Zählen beantwortet werden; danach die Ereignisse selbst.
public struct EreignisseWerkzeug: Tool {
    public let name = "ereignisse"
    public let description = """
        Liest Ereignisse aus dem Protokoll des Leitstands: Neustarts mit Grund und vorheriger \
        Laufzeit (neustart), Ausfälle (nicht_erreichbar, erreichbar mit Dauer), Firmwarewechsel \
        (version), Brenner und Pumpen ein und aus mit Dauer des vorigen Zustands (brenner, pumpe), \
        Sollwert- und Betriebsartänderungen je Raum (sollwert, betriebsart), Befunde der \
        Heizungsgeräte (befund), Funkthermometer (funk) und Starts des Leitstands (leitstand). \
        Liefert Anzahl, Starts und Laufzeiten je Art und Gerät und die Ereignisse, jüngste zuerst.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Zeitraum in Stunden bis jetzt", .range(1...2160))
        public var stunden: Int
        @Guide(description: "Nur Ereignisse dieses Geräts: Kennung oder Ort wie Kessel oder Obergeschoss")
        public var geraet: String?
        @Guide(description: "Nur diese Arten, etwa neustart oder brenner. Leer: alle")
        public var arten: [String]
    }

    let zugriff: any Anlagenzugriff
    let hoechstens: Int

    public init(zugriff: any Anlagenzugriff, hoechstens: Int = 80) {
        self.zugriff = zugriff
        self.hoechstens = hoechstens
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let bis = Date.now
        let von = bis.addingTimeInterval(-Double(min(2160, max(1, arguments.stunden))) * 3600)
        let liste = await zugriff.ereignisse(von: von, bis: bis)
        return Self.ausgabe(arguments, liste: liste, von: von, bis: bis, staende: await zugriff.staende(),
                            bild: await zugriff.bild(), hoechstens: hoechstens)
    }

    static func ausgabe(_ a: Argumente, liste alle: [Ereignis], von: Date, bis: Date, staende: [Geraetestand],
                        bild: Anlagenbild, hoechstens: Int) -> String {
        var liste = alle
        if let g = a.geraet?.trimmingCharacters(in: .whitespaces), !g.isEmpty {
            // Der Leitstand und nicht eingebundene Geräte stehen nur mit ihrer Kennung im Protokoll.
            if g.lowercased() == "leitstand" {
                liste = liste.filter { $0.geraet.hasPrefix("lst_") }
            } else {
                let ids = Set(Aufloesung.geraete(g, in: staende, bild: bild).map(\.geraet.id))
                liste = liste.filter { (ids.isEmpty ? [g] : ids).contains($0.geraet) }
            }
        }
        let arten = Set(a.arten.map { $0.trimmingCharacters(in: .whitespaces).lowercased() }.filter { !$0.isEmpty })
        if !arten.isEmpty { liste = liste.filter { arten.contains($0.art) } }

        let zeitraum = "Zeitraum \(Zeitangabe.text(von)) bis \(Zeitangabe.text(bis))."
        guard !liste.isEmpty else {
            return alle.isEmpty
                ? "\(zeitraum) Keine Ereignisse. Ereignisse gibt es nur mit einem eingerichteten Leitstand, und erst ab dem Tag, an dem sein Protokoll begann."
                : "\(zeitraum) Keine Ereignisse, die zu Gerät und Art passen. Im Zeitraum gibt es \(alle.count) andere."
        }
        let orte = Dictionary(staende.map { ($0.geraet.id, $0.ort) }, uniquingKeysWith: { a, _ in a })
        func geraet(_ id: String) -> String { orte[id].map { "\($0) (\(id))" } ?? id }

        var text = "\(zeitraum) \(liste.count) Ereignisse. Zeiten in Ortszeit, Dauern in Sekunden umgerechnet.\n"
        text += "Zusammenfassung (art | gerät | anzahl | bemerkung):\n"
        // Pumpen je Pumpe: Ein Heizungsgerät schaltet Heizkreise und die Kesselkreispumpe.
        let gruppen = Dictionary(grouping: liste) { "\($0.art)|\($0.geraet)|\($0.felder["pumpe"]?.alsText ?? "")" }
        for schluessel in gruppen.keys.sorted() {
            let g = gruppen[schluessel]!
            text += "\(g[0].art) | \(geraet(g[0].geraet)) | \(g.count) | \(bemerkung(g))\n"
        }
        text += "\nEreignisse, jüngste zuerst (zeit | gerät | art | beschreibung):\n"
        for e in liste.reversed().prefix(hoechstens) {
            let einzelheiten = Ereignistext.einzelheiten(e, befundtitel: Zusammenfuehrung.geraetebefundTitel)
            let beschreibung = [Ereignistext.titel(e), einzelheiten].filter { !$0.isEmpty }.joined(separator: ": ")
            text += "\(Zeitangabe.text(e.zeit)) | \(geraet(e.geraet)) | \(e.art) | \(beschreibung.replacingOccurrences(of: "\u{00A0}", with: " "))\n"
        }
        if liste.count > hoechstens {
            text += "… \(liste.count - hoechstens) ältere nicht aufgeführt; für sie den Zeitraum verkürzen oder nach Gerät und Art filtern.\n"
        }
        return text
    }

    /// Starts und Laufzeiten für Brenner und Pumpen, Gründe für Neustarts, Dauer für Ausfälle
    static func bemerkung(_ g: [Ereignis]) -> String {
        switch g[0].art {
        case "brenner", "pumpe":
            let ein = g.filter { $0.felder["ein"]?.alsBool == true }
            // Die Dauer eines Laufs steht im Aus-Ereignis als Dauer des vorigen Zustands.
            let laeufe = g.filter { $0.felder["ein"]?.alsBool == false }.compactMap { $0.felder["dauer_vorher_s"]?.alsZahl }
            var teile = ["\(ein.count) Starts"]
            if !laeufe.isEmpty {
                let summe = laeufe.reduce(0, +)
                teile.append("Laufzeit \(dauer(summe)) in \(laeufe.count) beendeten Läufen, Mittel \(dauer(summe / Double(laeufe.count)))")
            }
            if g[0].art == "pumpe" {
                let pumpen = Set(g.compactMap { $0.felder["pumpe"]?.alsText })
                if !pumpen.isEmpty { teile.append("Pumpe " + pumpen.sorted().joined(separator: ", ")) }
            }
            return teile.joined(separator: "; ")
        case "neustart", "leitstand":
            let gruende = Dictionary(grouping: g.compactMap { $0.felder["grund"]?.alsText }, by: { $0 })
                .map { "\($0.value.count)× \(Ereignistext.grund($0.key)) (\($0.key))" }.sorted()
            return gruende.isEmpty ? "" : gruende.joined(separator: ", ")
        case "erreichbar":
            let d = g.compactMap { $0.felder["dauer_s"]?.alsZahl }
            return d.isEmpty ? "" : "zusammen \(dauer(d.reduce(0, +))) nicht erreichbar, längster Ausfall \(dauer(d.max()!))"
        default:
            return ""
        }
    }

    static func dauer(_ s: Double) -> String {
        Ereignistext.dauer(s).replacingOccurrences(of: "\u{00A0}", with: " ")
    }
}
