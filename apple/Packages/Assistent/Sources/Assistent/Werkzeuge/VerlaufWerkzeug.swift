import Foundation
import FoundationModels
import Anlage
import Verlauf

/// Lesendes Werkzeug: Zeitreihen aus dem gespeicherten Verlauf, verdichtet zu einer kurzen
/// Tabelle mit Kennwerten je Reihe.
///
/// Der Verlauf liegt im Fünf-Minuten-Raster in der App, gespeist aus den eigenen Abfragen, dem
/// Protokoll des Leitstands und dem 24-Stunden-Verlauf der Heizungsgeräte. Die Tabelle hat
/// höchstens `zeilen` Zeilen; ein längerer Zeitraum wird in gröbere Mittel zusammengefasst, über
/// 30 Tage mindestens in Tagesmittel.
public struct VerlaufWerkzeug: Tool {
    public let name = "verlauf"
    public let description = """
        Liest Zeitreihen aus dem gespeicherten Verlauf der App: Temperaturen der Fühler am \
        Kessel, Speicher und an den Heizkreisen (fuehler.<rolle>), Brenner und Füllstand \
        (Anteil 0–1), Pumpen (pumpe.<nr>, kkp), Außentemperatur (aussen), je Raum Ist, Soll, \
        Ventilstellung und Feuchte (raum.<nr>.ist|soll|stellung|feuchte) und den Vorlauf an \
        den Verteilern (vorlauf.<nr>). Liefert je Reihe Tiefst-, Höchst-, Mittel- und letzten \
        Wert und eine Tabelle mit Mittelwerten, bis zu einem Jahr; über 30 Tage in Tagesmitteln \
        oder gröber. Ohne reihen die Liste der vorhandenen Reihen.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Reihen als Schlüssel wie fuehler.puffer, brenner, raum.3.ist, oder Gruppen: waermeerzeugung, heizkreise, aussen, raeume, pumpen. Leer: Liste der Reihen")
        public var reihen: [String]
        @Guide(description: "Zeitraum in Stunden bis jetzt, bis 8760 (ein Jahr)", .range(1...8760))
        public var stunden: Int
        @Guide(description: "Nur Reihen dieses Geräts: Kennung oder Ort wie Erdgeschoss oder Kessel")
        public var geraet: String?
    }

    let zugriff: any Anlagenzugriff
    let zeilen: Int
    let hoechstensReihen: Int

    /// `zeilen` begrenzt die Tabelle; die Apple-Modelle bekommen weniger.
    public init(zugriff: any Anlagenzugriff, zeilen: Int = 48, hoechstensReihen: Int = 10) {
        self.zugriff = zugriff
        self.zeilen = zeilen
        self.hoechstensReihen = hoechstensReihen
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let bis = Date.now
        let stunden = min(8760, max(1, arguments.stunden))
        let von = bis.addingTimeInterval(-Double(stunden) * 3600)
        let schritt = Self.schritt(stunden: stunden, zeilen: zeilen)
        let staende = await zugriff.staende()
        let bild = await zugriff.bild()
        guard let auszug = await zugriff.verlauf(von: von, bis: bis, schritt: schritt, schluessel: nil) else {
            return "Kein gespeicherter Verlauf vorhanden."
        }
        return Self.ausgabe(arguments, auszug: auszug, staende: staende, bild: bild, hoechstensReihen: hoechstensReihen)
    }

    /// Ein Vielfaches von fünf Minuten, sodass höchstens `zeilen` Zeilen entstehen; über 30 Tage
    /// mindestens ein Tag, damit die Tabelle Tage zeigt und nicht zufällige Tageszeiten
    static func schritt(stunden: Int, zeilen: Int) -> TimeInterval {
        let roh = Double(stunden) * 3600 / Double(max(1, zeilen))
        let stufen: [TimeInterval] = [300, 600, 900, 1800, 3600, 7200, 10_800, 21_600, 43_200, 86_400, 2 * 86_400, 7 * 86_400]
        let s = stufen.first { $0 >= roh } ?? 7 * 86_400
        return stunden > 720 ? max(s, 86_400) : s
    }

    static func ausgabe(_ a: Argumente, auszug: Verlaufsauszug, staende: [Geraetestand], bild: Anlagenbild, hoechstensReihen: Int) -> String {
        var reihen = auszug.reihen
        if let g = a.geraet {
            let ids = Set(Aufloesung.geraete(g, in: staende, bild: bild).map(\.geraet.id))
            reihen = reihen.filter { ids.contains($0.geraet) }
        }
        let gewuenscht = a.reihen.map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        if gewuenscht.isEmpty || gewuenscht == ["liste"] {
            return liste(reihen, auszug: auszug)
        }
        var gewaehlt: [Verlaufsauszug.Reihe] = []
        for w in gewuenscht {
            let passend = reihen.filter { r in
                switch w {
                case "waermeerzeugung": Verlaufsgliederung.gruppe(r) == .waermeerzeugung || r.schluessel == "brenner" || r.schluessel == "fuellstand"
                case "heizkreise": Verlaufsgliederung.gruppe(r) == .heizkreise
                case "aussen": Verlaufsgliederung.gruppe(r) == .aussen
                case "raeume": Verlaufsgliederung.gruppe(r) == .raeume
                case "pumpen": r.schluessel.hasPrefix("pumpe.") || r.schluessel == "kkp"
                case "vorlauf": Verlaufsgliederung.gruppe(r) == .verteilervorlauf
                default: r.schluessel == w || r.id == w
                }
            }
            for r in passend where !gewaehlt.contains(where: { $0.id == r.id }) { gewaehlt.append(r) }
        }
        guard !gewaehlt.isEmpty else {
            return "Keine passende Reihe. " + liste(reihen, auszug: auszug)
        }
        let orte = Dictionary(staende.map { ($0.geraet.id, $0.ort) }, uniquingKeysWith: { a, _ in a })
        let mittel = auszug.schritt >= 86_400
            ? "\(Int(auszug.schritt / 86_400)) Tag(e), gerechnet von Mitternacht UTC, also ab 1 Uhr (Winter) oder 2 Uhr (Sommer) Ortszeit"
            : "\(Int(auszug.schritt / 60)) min"
        var text = "Zeitraum \(Zeitangabe.text(auszug.beginn)) bis \(Zeitangabe.text(auszug.ende)), Mittel je \(mittel). Anteile 0–1 als Prozent.\n"
        text += "Kennwerte (reihe | bezeichnung | ort | einheit | tiefst | hoechst | mittel | letzter):\n"
        for r in gewaehlt {
            let werte = r.werte.compactMap { $0 }
            let letzter = r.werte.last { $0 != nil } ?? nil
            let einheit = einheit(r)
            text += "\(r.id) | \(r.bezeichnung) | \(orte[r.geraet] ?? r.ort) | \(einheit) | "
            if werte.isEmpty {
                text += "keine Werte\n"
                continue
            }
            let mittel = werte.reduce(0, +) / Double(werte.count)
            text += [werte.min(), werte.max(), mittel, letzter].map { zahl($0, r) }.joined(separator: " | ") + "\n"
        }
        let tabelle = Array(gewaehlt.prefix(hoechstensReihen))
        if gewaehlt.count > hoechstensReihen {
            text += "Tabelle nur für die ersten \(hoechstensReihen) Reihen; die übrigen einzeln abfragen.\n"
        }
        text += "\nzeit;" + tabelle.map(\.id).joined(separator: ";") + "\n"
        for i in 0..<auszug.anzahl {
            let werte = tabelle.map { i < $0.werte.count ? $0.werte[i] : nil }
            guard werte.contains(where: { $0 != nil }) else { continue }
            let zeit = auszug.beginn.addingTimeInterval(Double(i) * auszug.schritt)
            text += kurzzeit(zeit) + ";" + zip(werte, tabelle).map { zahl($0, $1) }.joined(separator: ";") + "\n"
        }
        return text
    }

    static func liste(_ reihen: [Verlaufsauszug.Reihe], auszug: Verlaufsauszug) -> String {
        guard !reihen.isEmpty else { return "Im Zeitraum liegen keine Werte vor." }
        let zeilen = reihen.sorted { ($0.geraet, $0.schluessel) < ($1.geraet, $1.schluessel) }.map { r in
            "\(r.id) | \(r.bezeichnung) | \(r.ort) | \(einheit(r)) | \(r.werte.compactMap { $0 }.count) Werte"
        }
        return "Vorhandene Reihen (gerät/schlüssel | bezeichnung | ort | einheit):\n" + zeilen.joined(separator: "\n")
    }

    static func einheit(_ r: Verlaufsauszug.Reihe) -> String {
        switch r.einheit {
        case .grad: "°C"
        case .anteil: "%"
        case .prozent: "% rF"
        }
    }

    static func zahl(_ x: Double?, _ r: Verlaufsauszug.Reihe) -> String {
        guard let x, x.isFinite else { return "" }
        if r.einheit == .anteil { return String(Int((x * 100).rounded())) }
        return String(format: "%.1f", x)
    }

    static func kurzzeit(_ d: Date) -> String {
        let t = Zeitangabe.text(d)
        // 2026-09-22T14:05:00+02:00 → 09-22 14:05
        let tag = t.dropFirst(5).prefix(5)
        let uhr = t.dropFirst(11).prefix(5)
        return "\(tag) \(uhr)"
    }
}
