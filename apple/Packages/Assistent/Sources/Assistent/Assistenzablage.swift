import Foundation
import FoundationModels
import Observation

/// Verbrauch eines Modells in einem Monat
public struct Verbrauch: Codable, Sendable, Hashable, Identifiable {
    public var id: String { "\(monat)|\(modell)" }
    /// `2026-09`
    public var monat: String
    public var modell: String
    public var anfragen: Int = 0
    public var eingabe: Int = 0
    /// Aus dem Zwischenspeicher gelesene Eingabe; sie kostet einen Bruchteil.
    public var zwischengespeichert: Int = 0
    public var ausgabe: Int = 0
    /// Davon Denkschritte
    public var denken: Int = 0

    public var trefferquote: Double? { eingabe > 0 ? Double(zwischengespeichert) / Double(eingabe) : nil }
}

/// Die Ablage des Assistenten: Vorschläge, Änderungsprotokoll, Lageberichte, Verbrauch und
/// Gespräche samt Transkript, als Dateien im Ordner der App. Gespräche lassen sich so nach einem
/// Neustart fortsetzen; das Transkript enthält Werkzeugergebnisse, aber nie Zugangsdaten.
@MainActor
@Observable
public final class Assistenzablage {
    public private(set) var vorschlaege: [Vorschlag] = []
    public private(set) var aenderungen: [Aenderung] = []
    public private(set) var lageberichte: [Lagebericht] = []
    public private(set) var verbrauch: [Verbrauch] = []
    public private(set) var gespraeche: [Gespraechskopf] = []

    public struct Gespraechskopf: Codable, Sendable, Hashable, Identifiable {
        public var id: String
        public var titel: String
        public var zuletzt: Date
        public var beitraege: Int
    }

    struct GespeichertesGespraech: Codable {
        var gespraech: Gespraech
        var transkript: Transcript?
    }

    @ObservationIgnored private let ordner: URL?

    public static let hoechstensGespraeche = 30
    public static let hoechstensAenderungen = 500

    /// `ordner` nil hält alles nur im Speicher, etwa für Tests und die Beispielanlage.
    public init(ordner: URL?) {
        self.ordner = ordner
        guard let ordner else { return }
        try? FileManager.default.createDirectory(at: ordner.appending(path: "gespraeche"), withIntermediateDirectories: true)
        vorschlaege = lesen("vorschlaege.json") ?? []
        aenderungen = lesen("aenderungen.json") ?? []
        lageberichte = lesen("lageberichte.json") ?? []
        verbrauch = lesen("verbrauch.json") ?? []
        gespraeche = lesen("gespraeche.json") ?? []
    }

    public static func standard() -> Assistenzablage {
        Assistenzablage(ordner: URL.applicationSupportDirectory.appending(path: "Heizung/Assistent", directoryHint: .isDirectory))
    }

    // MARK: Vorschläge

    public func vorschlag(_ id: String) -> Vorschlag? {
        vorschlaege.first { $0.id == id }
    }

    public func vorschlagSichern(_ v: Vorschlag) {
        if let i = vorschlaege.firstIndex(where: { $0.id == v.id }) {
            vorschlaege[i] = v
        } else {
            vorschlaege.insert(v, at: 0)
        }
        // Erledigtes älter als ein Jahr entfällt; offene und übernommene bleiben.
        let grenze = Date.now.addingTimeInterval(-365 * 86_400)
        vorschlaege.removeAll { $0.erstellt < grenze && [.verworfen, .zurueckgenommen].contains($0.status) }
        schreiben(vorschlaege, "vorschlaege.json")
    }

    // MARK: Änderungsprotokoll

    public func protokollieren(_ neu: [Aenderung]) {
        guard !neu.isEmpty else { return }
        aenderungen.insert(contentsOf: neu, at: 0)
        if aenderungen.count > Self.hoechstensAenderungen { aenderungen.removeLast(aenderungen.count - Self.hoechstensAenderungen) }
        schreiben(aenderungen, "aenderungen.json")
    }

    // MARK: Lageberichte

    public var letzterLagebericht: Lagebericht? { lageberichte.first }

    public func lageberichtSichern(_ l: Lagebericht) {
        lageberichte.insert(l, at: 0)
        if lageberichte.count > 30 { lageberichte.removeLast(lageberichte.count - 30) }
        schreiben(lageberichte, "lageberichte.json")
    }

    // MARK: Verbrauch

    public func verbrauchErfassen(modell: String, _ usage: LanguageModelSession.Usage, jetzt: Date = .now) {
        let monat = String(Self.monatsformat.string(from: jetzt))
        var v = verbrauch.first { $0.monat == monat && $0.modell == modell } ?? Verbrauch(monat: monat, modell: modell)
        v.anfragen += 1
        v.eingabe += usage.input.totalTokenCount
        v.zwischengespeichert += usage.input.cachedTokenCount
        v.ausgabe += usage.output.totalTokenCount
        v.denken += usage.output.reasoningTokenCount
        verbrauch.removeAll { $0.id == v.id }
        verbrauch.insert(v, at: 0)
        verbrauch.sort { ($0.monat, $0.modell) > ($1.monat, $1.modell) }
        schreiben(verbrauch, "verbrauch.json")
    }

    public func verbrauch(monat: Date = .now) -> [Verbrauch] {
        let m = Self.monatsformat.string(from: monat)
        return verbrauch.filter { $0.monat == m }
    }

    static var monatsformat: DateFormatter {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM"
        return f
    }

    // MARK: Gespräche

    public func gespraechSichern(_ g: Gespraech, transkript: Transcript?) {
        guard !g.beitraege.isEmpty else { return }
        let kopf = Gespraechskopf(id: g.id, titel: g.titel, zuletzt: g.zuletzt, beitraege: g.beitraege.count)
        gespraeche.removeAll { $0.id == g.id }
        gespraeche.insert(kopf, at: 0)
        for alt in gespraeche.dropFirst(Self.hoechstensGespraeche) {
            if let ordner { try? FileManager.default.removeItem(at: ordner.appending(path: "gespraeche/\(alt.id).json")) }
        }
        gespraeche = Array(gespraeche.prefix(Self.hoechstensGespraeche))
        schreiben(gespraeche, "gespraeche.json")
        schreiben(GespeichertesGespraech(gespraech: g, transkript: transkript), "gespraeche/\(g.id).json")
    }

    public func gespraechLaden(_ id: String) -> (gespraech: Gespraech, transkript: Transcript?)? {
        guard let g: GespeichertesGespraech = lesen("gespraeche/\(id).json") else { return nil }
        return (g.gespraech, g.transkript)
    }

    public func gespraechLoeschen(_ id: String) {
        gespraeche.removeAll { $0.id == id }
        schreiben(gespraeche, "gespraeche.json")
        if let ordner { try? FileManager.default.removeItem(at: ordner.appending(path: "gespraeche/\(id).json")) }
    }

    // MARK: Dateien

    private func lesen<T: Decodable>(_ name: String) -> T? {
        guard let ordner, let daten = try? Data(contentsOf: ordner.appending(path: name)) else { return nil }
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return try? d.decode(T.self, from: daten)
    }

    private func schreiben<T: Encodable>(_ wert: T, _ name: String) {
        guard let ordner else { return }
        let e = JSONEncoder()
        e.dateEncodingStrategy = .iso8601
        e.outputFormatting = [.sortedKeys]
        guard let daten = try? e.encode(wert) else { return }
        try? daten.write(to: ordner.appending(path: name), options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }
}
