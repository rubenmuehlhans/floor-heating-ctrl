import Foundation
import Testing
import Geraeteschnittstelle
import Verlauf
@testable import Anlage

@Suite("Analysepaket")
struct AnalysepaketTests {
    private let beginn = Date(timeIntervalSince1970: 1_790_121_600) // 2026-09-23T00:00:00Z

    private func auszug() -> Verlaufsauszug {
        Verlaufsauszug(beginn: beginn, schritt: 900, anzahl: 4, reihen: [
            .init(geraet: "heiz_1", ort: "Kessel", schluessel: "fuehler.kessel_vl", bezeichnung: "", werte: [61.25, nil, 62, nil]),
            .init(geraet: "heiz_1", ort: "Kessel", schluessel: "brenner", bezeichnung: "", werte: [0.6, 1, nil, nil]),
            .init(geraet: "fbh_1", ort: "EG", schluessel: "raum.2.ist", bezeichnung: "", werte: [nil, 20.4, nil, -0.0001]),
            .init(geraet: "fbh_1", ort: "EG", schluessel: "raum.10.ist", bezeichnung: "", werte: [nil, 19, nil, nil]),
        ])
    }

    @Test func dateienNachKatalog() throws {
        let ereignisse = [
            Ereignis(zeit: beginn.addingTimeInterval(600), geraet: "heiz_1", art: "neustart", felder: ["grund": "panic", "laufzeit_vorher_s": 3600]),
            Ereignis(zeit: beginn.addingTimeInterval(700), geraet: "heiz_1", art: "befund", felder: ["code": "flow_swapped", "stand": "neu"]),
        ]
        let p = Analysepaket.inhalt(auszug: auszug(), ereignisse: ereignisse, bild: Anlagenbild.beispiel, katalog: "# Katalog",
                                    von: beginn, bis: beginn.addingTimeInterval(3600), erstellt: beginn)
        let kessel = String(decoding: try #require(p.dateien["heiz_1.csv"]), as: UTF8.self)
        // Spalten nach Schlüssel geordnet, Zeilen ohne jeden Wert fehlen, Anteile bleiben 0–1.
        #expect(kessel == """
            zeit,brenner,fuehler.kessel_vl
            2026-09-23T00:00:00Z,0.6,61.25
            2026-09-23T00:15:00Z,1,
            2026-09-23T00:30:00Z,,62

            """)
        #expect(p.zeilen == ["heiz_1": 3, "fbh_1": 2])
        let eg = String(decoding: try #require(p.dateien["fbh_1.csv"]), as: UTF8.self)
        #expect(eg.hasPrefix("zeit,raum.2.ist,raum.10.ist\n"), "Nummern der Größe nach")
        #expect(eg.hasSuffix("2026-09-23T00:45:00Z,0,\n"), "−0 wird zu 0")
        let e = String(decoding: try #require(p.dateien["ereignisse.csv"]), as: UTF8.self)
        #expect(e.contains("2026-09-23T00:10:00Z,heiz_1,neustart,Neustart,\"Absturz, lief vorher 1 h\","))
        #expect(e.contains("Vorlauf und Rücklauf sind vermutlich vertauscht"))
        #expect(e.contains("\"{\"\"grund\"\":\"\"panic\"\""), "Felder als JSON, Anführungszeichen verdoppelt")
        #expect(p.dateien["katalog-messgroessen.md"] != nil)
        let anlage = try JSONWert.lesen(try #require(p.dateien["anlage.json"]))
        #expect(anlage["heizkreise"]?[0]?["vorlauf"] == "fuehler.hk1_vl")
        let kennungen = (anlage["geraete"]?.alsListe ?? []).compactMap { $0["kennung"]?.alsText }
        #expect(kennungen.count == Set(kennungen).count, "jede Kennung einmal")
        let lies = String(decoding: try #require(p.dateien["LIESMICH.txt"]), as: UTF8.self)
        #expect(lies.contains("Raster 15 min") && lies.contains("- heiz_1.csv: 3 Zeilen") && lies.contains("2 Ereignisse"))
        #expect(!lies.contains("192.168"))
    }

    @Test func zipEntsteht() throws {
        let p = Analysepaket.inhalt(auszug: auszug(), ereignisse: [], bild: Anlagenbild.beispiel, katalog: nil,
                                    von: beginn, bis: beginn.addingTimeInterval(3600))
        let tmp = FileManager.default.temporaryDirectory.appending(path: "analyse-\(UUID())")
        let ordner = tmp.appending(path: "Heizung Analysepaket")
        try Analysepaket.schreiben(p, nach: ordner)
        let zip = tmp.appending(path: "paket.zip")
        try Analysepaket.zip(ordner, nach: zip)
        let kopf = try FileHandle(forReadingFrom: zip).read(upToCount: 2)
        #expect(kopf == Data("PK".utf8))
        try? FileManager.default.removeItem(at: tmp)
    }
}
