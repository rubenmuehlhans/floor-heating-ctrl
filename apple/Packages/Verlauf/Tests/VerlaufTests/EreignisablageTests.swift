import Foundation
import Testing
import Geraeteschnittstelle
@testable import Verlauf

/// Ereignisse, wie der Leitstand sie schreibt (docs/katalog-messgroessen.md)
private let zeilen = """
{"zeit":"2026-09-23T05:51:56Z","geraet":"heiz_1","art":"neustart","laufzeit_vorher_s":81234,"grund":"panic"}
{"zeit":"2026-09-23T05:52:30Z","geraet":"heiz_1","art":"brenner","ein":true}
{"zeit":"2026-09-23T06:04:30Z","geraet":"heiz_1","art":"brenner","ein":false,"dauer_vorher_s":720}
{"zeit":"2026-09-23T07:00:00Z","geraet":"vert_1","art":"sollwert","raum":2,"alt":20.5,"neu":21}

"""

@Suite("Ereignisablage")
struct EreignisablageTests {
    @Test func zeileLesen() throws {
        let e = try #require(Ereignis.lesen(zeilen.split(separator: "\n")[0]))
        #expect(e.art == "neustart" && e.geraet == "heiz_1")
        #expect(e.felder["grund"] == .text("panic"))
        #expect(e.felder["zeit"] == nil, "Zeit, Gerät und Art stehen nicht doppelt in den Feldern")
        #expect(Ereignis.lesen("kein json") == nil)
        #expect(Ereignis.lesen(#"{"geraet":"x","art":"y"}"#) == nil, "ohne Zeit kein Ereignis")
    }

    @Test func anhaengenAbStelle() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        let bytes = Data(zeilen.utf8)
        // Erst bis in die dritte Zeile: Die angefangene Zeile bleibt draußen.
        let schnitt = bytes.firstRange(of: Data("06:04".utf8))!.lowerBound
        #expect(try await s.ereignisseAnhaengen(quelle: "lst_1", tag: "2026-09-23", daten: bytes.prefix(schnitt), ab: 0) == 2)
        let stelle = try await s.ereignisstelle(quelle: "lst_1", tag: "2026-09-23")
        #expect(stelle < schnitt)
        // Eine Antwort für eine überholte Stelle wird verworfen, statt doppelt abzulegen.
        #expect(try await s.ereignisseAnhaengen(quelle: "lst_1", tag: "2026-09-23", daten: bytes, ab: 0) == 0)
        #expect(try await s.ereignisseAnhaengen(quelle: "lst_1", tag: "2026-09-23", daten: bytes.suffix(from: stelle), ab: stelle) == 2)
        #expect(try await s.ereignisstelle(quelle: "lst_1", tag: "2026-09-23") == bytes.count)

        let von = Protokollabgleich.iso("2026-09-23T00:00:00Z")!
        let alle = try await s.ereignisse(von: von, bis: von.addingTimeInterval(86_400))
        #expect(alle.map(\.art) == ["neustart", "brenner", "brenner", "sollwert"])
        #expect(Set(alle.map(\.id)).count == 4)
        let brenner = try await s.ereignisse(von: von, bis: von.addingTimeInterval(86_400), geraet: "heiz_1", arten: ["brenner"])
        #expect(brenner.map { $0.felder["ein"] } == [.bool(true), .bool(false)])
        // Zeitraum schneidet innerhalb des Tages.
        let frueh = try await s.ereignisse(von: von, bis: Protokollabgleich.iso("2026-09-23T06:00:00Z")!)
        #expect(frueh.count == 2)
        // Karte getauscht: Der Tag wird verworfen und beginnt bei null.
        try await s.ereignisseVerwerfen(quelle: "lst_1", tag: "2026-09-23")
        #expect(try await s.ereignisstelle(quelle: "lst_1", tag: "2026-09-23") == 0)
        #expect(try await s.ereignisseAnhaengen(quelle: "lst_1", tag: "2026-09-23", daten: bytes, ab: 0) == 4)
        let umfang = try await s.ereignisumfang()
        #expect(umfang.tage == 1 && umfang.ereignisse == 4)
        try await s.loeschen()
        #expect(try await s.ereignisumfang().ereignisse == 0)
    }

    @Test func texte() throws {
        let l = zeilen.split(separator: "\n").compactMap { Ereignis.lesen($0) }
        #expect(Ereignistext.titel(l[0]) == "Neustart")
        #expect(Ereignistext.einzelheiten(l[0]) == "Absturz, lief vorher 22\u{00A0}h 33\u{00A0}min")
        #expect(Ereignistext.titel(l[2]) == "Brenner aus")
        #expect(Ereignistext.einzelheiten(l[2]) == "vorher 12\u{00A0}min")
        #expect(Ereignistext.einzelheiten(l[3]) == "Raum 2: 20,5\u{00A0}°C → 21,0\u{00A0}°C")
    }

    @Test func ausfaelle() throws {
        let t = { (s: String) in Protokollabgleich.iso("2026-09-23T\(s)Z")! }
        let l = [
            Ereignis(zeit: t("01:00:00"), geraet: "a", art: "nicht_erreichbar"),
            Ereignis(zeit: t("01:10:00"), geraet: "a", art: "erreichbar", felder: ["dauer_s": .zahl(690)]),
            Ereignis(zeit: t("02:00:00"), geraet: "b", art: "nicht_erreichbar"),
        ]
        let a = Ereignistext.ausfaelle(l, bis: t("03:00:00"))
        #expect(a.count == 2)
        #expect(a[0].geraet == "a" && a[0].von == t("00:58:30") && a[0].bis == t("01:10:00"))
        #expect(a[1].geraet == "b" && a[1].bis == t("03:00:00"), "ohne Antwort reicht der Ausfall bis jetzt")
    }
}
