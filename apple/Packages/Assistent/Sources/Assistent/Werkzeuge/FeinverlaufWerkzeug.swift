import Foundation
import FoundationModels
import Anlage
import Geraeteschnittstelle

/// Lesendes Werkzeug: Messwerte im Takt der Abfrage, die der Leitstand auf seiner Karte hält.
/// Für kurze Zeiträume, in denen das Fünfminutenmittel des Verlaufs zu grob ist, etwa die
/// Minuten vor einem Neustart. Nur im Heimnetz, weil der Leitstand die Werte selbst liefert.
public struct FeinverlaufWerkzeug: Tool {
    public let name = "feinverlauf"
    public let description = """
        Liest Messwerte eines Geräts im Takt der Abfrage (30 s) vom Leitstand, für höchstens \
        24 Stunden und höchstens acht Reihen. Neben allen Reihen des Verlaufs gibt es hier \
        geraet.heap (freier Arbeitsspeicher, Byte), geraet.rssi (WLAN-Empfang, dBm) und \
        geraet.laufzeit (s seit dem Neustart), beim Leitstand auch funk.<adresse>.temp|feuchte|rssi. \
        Nur im Heimnetz und nur mit Leitstand; für längere Zeiträume verlauf nutzen.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Gerät: Kennung wie heiz_2370ec oder Ort wie Kessel; leitstand für den Leitstand selbst")
        public var geraet: String
        @Guide(description: "Schlüssel wie geraet.heap, geraet.rssi, fuehler.kessel_vl, brenner; höchstens acht")
        public var reihen: [String]
        @Guide(description: "Dauer in Stunden", .range(1...24))
        public var stunden: Int
        @Guide(description: "Beginn in Ortszeit wie 2026-09-21T14:00; leer heißt: die letzten Stunden bis jetzt")
        public var beginn: String?
    }

    let zugriff: any Anlagenzugriff
    let zeilen: Int

    public init(zugriff: any Anlagenzugriff, zeilen: Int = 120) {
        self.zugriff = zugriff
        self.zeilen = zeilen
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let stunden = min(24, max(1, arguments.stunden))
        let dauer = Double(stunden) * 3600
        let jetzt = Date.now
        let von = arguments.beginn.flatMap(Self.ortszeit).map { min($0, jetzt.addingTimeInterval(-60)) } ?? jetzt.addingTimeInterval(-dauer)
        let bis = min(jetzt, von.addingTimeInterval(dauer))
        let reihen = Array(arguments.reihen.map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }.prefix(8))
        guard !reihen.isEmpty else {
            return "Keine Reihe angegeben. Üblich sind geraet.heap, geraet.rssi, geraet.laufzeit und die Schlüssel aus verlauf, etwa fuehler.kessel_vl oder raum.3.ist."
        }
        guard let geraet = await Self.kennung(arguments.geraet, zugriff: zugriff) else {
            return "Gerät \(arguments.geraet) unbekannt. Kennung wie heiz_2370ec oder einen Ort angeben."
        }
        let raster = Self.raster(sekunden: bis.timeIntervalSince(von), zeilen: zeilen)
        do {
            let r = try await zugriff.feinverlauf(geraet: geraet, schluessel: reihen, von: von, bis: bis, raster: raster)
            return Self.ausgabe(r, von: von, bis: bis)
        } catch {
            return "Den Feinverlauf liefert der Leitstand, und er ist nicht erreichbar (\(error.localizedDescription)). Das geht nur im Heimnetz. Für Fünfminutenmittel verlauf nutzen."
        }
    }

    /// Ein Vielfaches von 30 s, sodass höchstens `zeilen` Zeilen entstehen; ab 300 s ein
    /// Vielfaches von 300 s, dann rechnet der Leitstand aus den Fünfminutenmitteln.
    static func raster(sekunden: TimeInterval, zeilen: Int) -> Int {
        let roh = Int((sekunden / Double(max(1, zeilen))).rounded(.up))
        if roh <= 30 { return 30 }
        if roh < 300 { return (roh + 29) / 30 * 30 }
        return (roh + 299) / 300 * 300
    }

    /// Kennung aus Kennung, Ort oder „leitstand“
    static func kennung(_ angabe: String, zugriff: any Anlagenzugriff) async -> String? {
        let a = angabe.trimmingCharacters(in: .whitespaces)
        if a.lowercased() == "leitstand" { return await zugriff.leitstandKennung() }
        if a.hasPrefix("heiz_") || a.hasPrefix("fbh_") || a.hasPrefix("lst_") { return a }
        return Aufloesung.geraete(a, in: await zugriff.staende(), bild: await zugriff.bild()).first?.geraet.id
    }

    /// `2026-09-21T14:00` oder mit Sekunden, in Ortszeit; mit Zone auch als ISO 8601
    static func ortszeit(_ text: String) -> Date? {
        if let d = try? Date(text, strategy: .iso8601) { return d }
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = .current
        for muster in ["yyyy-MM-dd'T'HH:mm:ss", "yyyy-MM-dd'T'HH:mm", "yyyy-MM-dd HH:mm"] {
            f.dateFormat = muster
            if let d = f.date(from: text) { return d }
        }
        return nil
    }

    static func ausgabe(_ r: Leitstand.Protokollreihe, von: Date, bis: Date) -> String {
        var text = "Gerät \(r.geraet), \(Zeitangabe.text(von)) bis \(Zeitangabe.text(bis)), Mittel je \(r.raster) s.\n"
        guard !r.zeilen.isEmpty else {
            return text + "Keine Werte. Entweder fehlen diese Reihen am Gerät, oder der Leitstand hat im Zeitraum nicht aufgezeichnet."
        }
        text += "Kennwerte (reihe | tiefst | hoechst | mittel | letzter | werte):\n"
        for (i, name) in r.spalten.enumerated() {
            let w = r.zeilen.compactMap { i < $0.werte.count ? $0.werte[i] : nil }
            guard let lo = w.min(), let hi = w.max(), let letzter = w.last else {
                text += "\(name) | keine Werte\n"
                continue
            }
            text += "\(name) | \(zahl(lo)) | \(zahl(hi)) | \(zahl(w.reduce(0, +) / Double(w.count))) | \(zahl(letzter)) | \(w.count)\n"
        }
        text += "\nzeit;" + r.spalten.joined(separator: ";") + "\n"
        for z in r.zeilen {
            text += Zeitangabe.text(z.zeit) + ";" + z.werte.map { $0.map(zahl) ?? "" }.joined(separator: ";") + "\n"
        }
        return text
    }

    static func zahl(_ x: Double) -> String {
        x.rounded() == x ? String(Int(x)) : String(format: "%.2f", x)
    }
}
