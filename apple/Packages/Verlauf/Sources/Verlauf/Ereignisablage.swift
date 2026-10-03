import Foundation
import SwiftData
import Geraeteschnittstelle

/// Die Ereignisse eines UTC-Tages aus einer Quelle, so wie sie in `ereignisse.jsonl` des
/// Leitstands stehen: vollständige Zeilen, in der Reihenfolge der Datei. Die Länge der Zeilen ist
/// zugleich die Stelle, ab der der nächste Abgleich die Datei holt; ein erneuter Abgleich kann
/// deshalb nichts doppelt ablegen.
@Model
final class Ereignisblock {
    /// „<Quelle>|<Tag>“
    @Attribute(.unique) var kennung: String
    var quelle: String
    /// Tage seit 1970 in UTC, wie `Messblock.tag`
    var tag: Int
    var zeilen: Data

    init(quelle: String, tag: Int) {
        kennung = Self.kennung(quelle, tag)
        self.quelle = quelle
        self.tag = tag
        zeilen = Data()
    }

    static func kennung(_ quelle: String, _ tag: Int) -> String { "\(quelle)|\(tag)" }
}

/// Ein Ereignis aus dem Protokoll, Felder nach `docs/katalog-messgroessen.md`
public struct Ereignis: Sendable, Equatable, Identifiable {
    public var zeit: Date
    public var geraet: String
    public var art: String
    /// Alle Felder außer `zeit`, `geraet` und `art`
    public var felder: [String: JSONWert]
    /// Stelle in der Ablage; eindeutig innerhalb eines Auszugs
    public var id: String

    public init(zeit: Date, geraet: String, art: String, felder: [String: JSONWert] = [:], id: String? = nil) {
        self.zeit = zeit
        self.geraet = geraet
        self.art = art
        self.felder = felder
        self.id = id ?? "\(geraet)|\(art)|\(zeit.timeIntervalSince1970)"
    }

    /// Eine Zeile aus `ereignisse.jsonl`; nil, wenn Zeit, Gerät oder Art fehlen.
    public static func lesen(_ zeile: some StringProtocol, id: String? = nil) -> Ereignis? {
        guard let wert = try? JSONWert.lesen(Data(zeile.utf8)), var o = wert.alsObjekt,
              let zeit = o["zeit"]?.alsText.flatMap(Protokollabgleich.iso),
              let geraet = o["geraet"]?.alsText, let art = o["art"]?.alsText else { return nil }
        o["zeit"] = nil
        o["geraet"] = nil
        o["art"] = nil
        return Ereignis(zeit: zeit, geraet: geraet, art: art, felder: o, id: id)
    }
}

extension Verlaufsspeicher {
    /// Byte der abgelegten Zeilen eines Tages; ab hier holt der Abgleich weiter.
    public func ereignisstelle(quelle: String, tag: String) throws -> Int {
        guard let t = Self.tageszahl(tag) else { return 0 }
        return try ereignisblock(quelle, t, anlegen: false)?.zeilen.count ?? 0
    }

    /// Hängt `daten` an die Ereignisse eines Tages an, sofern sie an der abgelegten Stelle
    /// beginnen. Übernommen werden nur vollständige Zeilen. Liefert die Zahl neuer Ereignisse.
    @discardableResult
    public func ereignisseAnhaengen(quelle: String, tag: String, daten: Data, ab: Int) throws -> Int {
        guard let t = Self.tageszahl(tag),
              let letztes = daten.lastIndex(of: UInt8(ascii: "\n")) else { return 0 }
        let block = try ereignisblock(quelle, t, anlegen: true)!
        // Eine Antwort für eine andere Stelle, etwa nach einem parallelen Abgleich, wird verworfen.
        guard block.zeilen.count == ab else { return 0 }
        let neu = daten[daten.startIndex...letztes]
        block.zeilen.append(neu)
        try modelContext.save()
        return neu.reduce(0) { $1 == UInt8(ascii: "\n") ? $0 + 1 : $0 }
    }

    /// Verwirft die Ereignisse eines Tages, etwa wenn die Datei auf dem Leitstand kürzer ist als
    /// das Abgelegte: Dann wurde die Karte getauscht oder formatiert, und der Tag beginnt neu.
    public func ereignisseVerwerfen(quelle: String, tag: String) throws {
        guard let t = Self.tageszahl(tag), let block = try ereignisblock(quelle, t, anlegen: false) else { return }
        modelContext.delete(block)
        try modelContext.save()
    }

    /// Ereignisse eines Zeitraums, zeitlich geordnet
    public func ereignisse(von: Date, bis: Date, geraet: String? = nil, arten: Set<String>? = nil) throws -> [Ereignis] {
        let erster = Raster.tag(Raster.index(von))
        let letzter = Raster.tag(Raster.index(bis))
        let beschreibung = FetchDescriptor<Ereignisblock>(predicate: #Predicate { $0.tag >= erster && $0.tag <= letzter })
        var liste: [Ereignis] = []
        for block in try modelContext.fetch(beschreibung) {
            let text = String(decoding: block.zeilen, as: UTF8.self)
            for (i, zeile) in text.split(separator: "\n").enumerated() {
                guard let e = Ereignis.lesen(zeile, id: "\(block.kennung)|\(i)"),
                      e.zeit >= von, e.zeit <= bis,
                      geraet == nil || e.geraet == geraet,
                      arten?.contains(e.art) ?? true else { continue }
                liste.append(e)
            }
        }
        return liste.sorted { $0.zeit < $1.zeit }
    }

    /// Zahl der abgelegten Ereignisse, für die Übersicht in den Einstellungen
    public func ereignisumfang() throws -> (tage: Int, ereignisse: Int, groesse: Int) {
        let bloecke = try modelContext.fetch(FetchDescriptor<Ereignisblock>())
        let anzahl = bloecke.reduce(0) { s, b in s + b.zeilen.reduce(0) { $1 == UInt8(ascii: "\n") ? $0 + 1 : $0 } }
        return (Set(bloecke.map(\.tag)).count, anzahl, bloecke.reduce(0) { $0 + $1.zeilen.count })
    }

    private func ereignisblock(_ quelle: String, _ tag: Int, anlegen: Bool) throws -> Ereignisblock? {
        let kennung = Ereignisblock.kennung(quelle, tag)
        var beschreibung = FetchDescriptor<Ereignisblock>(predicate: #Predicate { $0.kennung == kennung })
        beschreibung.fetchLimit = 1
        if let block = try modelContext.fetch(beschreibung).first { return block }
        guard anlegen else { return nil }
        let block = Ereignisblock(quelle: quelle, tag: tag)
        modelContext.insert(block)
        return block
    }

    static func tageszahl(_ tag: String) -> Int? {
        Protokollabgleich.tagesbeginn(tag).map { Raster.tag(Raster.index($0)) }
    }
}
