import Foundation
import Testing
import Geraeteschnittstelle
@testable import Verlauf

/// Die Aufnahmen liegen bei der Geräteschnittstelle; hier werden sie nur gelesen.
enum Aufnahme {
    static let ordner = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .appending(path: "Geraeteschnittstelle/Tests/GeraeteschnittstelleTests/Fixtures")

    static func daten(_ name: String) throws -> Data {
        try Data(contentsOf: ordner.appending(path: name))
    }

    static func lesen<T: Decodable>(_ name: String, als: T.Type = T.self) throws -> T {
        try JSONDecoder().decode(T.self, from: daten(name))
    }
}

/// 22.09.2026 06:00:00 UTC, auf einer Platzgrenze
let morgen = Date(timeIntervalSince1970: 1_790_056_800)

@Suite("Raster und Tagesmatrix")
struct RasterTests {
    @Test func plaetzeUndTage() {
        let i = Raster.index(morgen)
        #expect(Raster.platz(i) == 72)
        #expect(Raster.beginn(i) == morgen)
        #expect(Raster.index(morgen.addingTimeInterval(299)) == i)
        #expect(Raster.index(morgen.addingTimeInterval(300)) == i + 1)
        #expect(Raster.tag(i) * Raster.plaetzeJeTag + Raster.platz(i) == i)
    }

    @Test func packenUndEntpacken() {
        var m = Tagesmatrix()
        m.setzen(platz: 0, "fuehler.abgas", 88.86)
        m.setzen(platz: 287, "brenner", 0.6)
        m.setzen(platz: 5, "fuehler.puffer", -3.25)
        let wieder = Tagesmatrix(spalten: m.spalten, daten: m.daten)
        #expect(wieder == m)
        #expect(wieder.wert(platz: 0, "fuehler.abgas") == 88.86)
        #expect(wieder.wert(platz: 287, "brenner") == 0.6)
        #expect(wieder.wert(platz: 5, "fuehler.puffer") == -3.25)
        #expect(wieder.wert(platz: 1, "fuehler.abgas") == nil)
        #expect(m.daten.count == 3 * 288 * 2)
    }

    /// Eine neue Messgröße mitten am Tag behält die vorhandenen Werte.
    @Test func neueSpalte() {
        var m = Tagesmatrix()
        m.setzen(platz: 10, "a", 1)
        m.setzen(platz: 11, "b", 2)
        #expect(m.wert(platz: 10, "a") == 1)
        #expect(m.wert(platz: 11, "b") == 2)
        #expect(m.wert(platz: 11, "a") == nil)
        m.setzen(platz: 10, "a", 5, nurLuecken: true)
        #expect(m.wert(platz: 10, "a") == 1)
        #expect(Tagesmatrix.kodiert(.nan) == Tagesmatrix.fehlt)
        #expect(Tagesmatrix.kodiert(1000) == Int16.max)
    }
}

@Suite("Abtastung")
struct AbtastungTests {
    @Test func verteiler() throws {
        let z: Verteilerzustand = try Aufnahme.lesen("attrappe/verteiler-state.json")
        let a = Abtastung.verteiler(z, geraet: "fbh_a1b2c3", zeit: morgen)
        #expect(a.ort == "Erdgeschoss")
        #expect(a.werte["raum.1.ist"] == 20.45)
        #expect(a.werte["raum.1.soll"] == 20)
        #expect(a.werte["raum.1.stellung"] == 0.3)
        #expect(a.bezeichnungen["raum.1.ist"] == "Küche")
        // Ohne gültigen Messwert kein Ist, im ausgeschalteten Raum kein Soll
        #expect(a.werte["raum.3.ist"] == nil)
        #expect(a.werte["raum.3.soll"] == 19)
        #expect(a.werte["raum.4.soll"] == nil)
        #expect(a.werte["raum.4.ist"] == 21.55)
        #expect(a.werte["kanal.4.stellung"] == 0.7)
        #expect(a.werte["aussen"] == 10.83)
        #expect(a.werte["vorlauf.0"] == 34.6)
        #expect(a.werte["vorlauf.3"] == nil, "ungültiger Fühler")
    }

    /// Brenner und Füllstand zählen bei dem Gerät, das den Fühler hat.
    @Test func heizgeraete() throws {
        let speicher = Abtastung.heizgeraet(try Aufnahme.lesen("attrappe/speicher-state.json"), geraet: "heiz_3f21ac", zeit: morgen)
        #expect(speicher.werte["fuehler.puffer"] == 65.54)
        #expect(speicher.werte["fuehler.hk2_rl"] == 28.68)
        #expect(speicher.werte["fuellstand"] == 1)
        #expect(speicher.werte["brenner"] == nil)
        #expect(speicher.werte["pumpe.1"] == 1)
        #expect(speicher.werte["pumpe.2"] == 0)
        #expect(speicher.werte["kkp"] == nil, "keine Kesselkreispumpe am Speicher")

        let kessel = Abtastung.heizgeraet(try Aufnahme.lesen("attrappe/kessel-state.json"), geraet: "heiz_9a1b2c", zeit: morgen)
        #expect(kessel.werte["fuehler.abgas"] == 88.86)
        #expect(kessel.werte["brenner"] == 0)
        #expect(kessel.werte["fuellstand"] == nil)
        #expect(kessel.werte["kkp"] == 0)
        #expect(kessel.werte["fuehler.puffer"] == nil)
    }
}

@Suite("Verlaufsspeicher")
struct SpeicherTests {
    func zeile(_ geraet: String, _ minuten: Double, _ werte: [String: Double]) -> Messzeile {
        Messzeile(geraet: geraet, ort: "Kessel", index: Raster.index(morgen.addingTimeInterval(minuten * 60)),
                  werte: werte, bezeichnungen: werte.keys.reduce(into: [:]) { $0[$1] = "Name \($1)" })
    }

    @Test func schreibenUndLesen() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        try await s.schreiben([
            zeile("heiz_1", 0, ["fuehler.abgas": 80]),
            zeile("heiz_1", 5, ["fuehler.abgas": 90, "brenner": 1]),
            zeile("heiz_1", 10, ["fuehler.abgas": 100, "brenner": 0]),
        ])
        let a = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(15 * 60), schritt: 300)
        let abgas = try #require(a.reihe("heiz_1", "fuehler.abgas"))
        #expect(abgas.werte == [80, 90, 100])
        #expect(abgas.bezeichnung == "Name fuehler.abgas")
        #expect(abgas.ort == "Kessel")
        #expect(a.reihe("heiz_1", "brenner")?.werte == [nil, 1, 0])
        #expect(a.punkte(abgas).first?.zeit == morgen.addingTimeInterval(150))

        // Zusammengefasst auf 15 Minuten
        let grob = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(15 * 60), schritt: 900)
        #expect(grob.reihe("heiz_1", "fuehler.abgas")?.werte == [90])
        #expect(grob.reihe("heiz_1", "brenner")?.werte == [0.5])
    }

    /// Lücken füllen überschreibt nichts; eigene Werte gehen vor.
    @Test func nurLuecken() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        try await s.schreiben([zeile("heiz_1", 0, ["fuehler.puffer": 60])])
        try await s.auffuellen(geraet: "heiz_1", ort: "Kessel", reihen: [
            "fuehler.puffer": [0, 2, 4, 6, 8].map { Messpunkt(zeit: morgen.addingTimeInterval($0 * 60), wert: 50 + $0) },
        ], bezeichnungen: ["fuehler.puffer": "anders"])
        let a = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(600), schritt: 300)
        let r = try #require(a.reihe("heiz_1", "fuehler.puffer"))
        #expect(r.werte[0] == 60)
        #expect(r.werte[1] == 57, "Mittel aus 56 und 58")
        #expect(r.bezeichnung == "Name fuehler.puffer")
    }

    @Test func ueberMitternacht() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        let mitternacht = Date(timeIntervalSince1970: 1_790_121_600)   // 23.09.2026 00:00 UTC
        try await s.schreiben([
            Messzeile(geraet: "fbh_1", ort: "EG", index: Raster.index(mitternacht) - 1, werte: ["raum.1.ist": 20], bezeichnungen: [:]),
            Messzeile(geraet: "fbh_1", ort: "EG", index: Raster.index(mitternacht), werte: ["raum.1.ist": 21], bezeichnungen: [:]),
        ])
        let a = try await s.auszug(von: mitternacht.addingTimeInterval(-300), bis: mitternacht.addingTimeInterval(300), schritt: 300)
        #expect(a.reihe("fbh_1", "raum.1.ist")?.werte == [20, 21])
        let umfang = try await s.umfang()
        #expect(umfang.bloecke == 2)
        #expect(umfang.geraete == 1)
        #expect(umfang.tage == 2)
        #expect(umfang.von == mitternacht.addingTimeInterval(-300))
        #expect(umfang.bis == mitternacht.addingTimeInterval(300))
        try await s.loeschen()
        #expect(try await s.umfang().bloecke == 0)
    }

    /// Eine Lücke trennt die Linie.
    @Test func abschnitte() {
        let a = Verlaufsauszug(beginn: morgen, schritt: 300, anzahl: 5, reihen: [
            .init(geraet: "g", ort: "", schluessel: "aussen", bezeichnung: "Außen", werte: [1, 2, nil, 4, nil]),
        ])
        let stuecke = a.abschnitte(a.reihen[0])
        #expect(stuecke.map { $0.map(\.wert) } == [[1, 2], [4]])
    }

    @Test func einschraenken() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        try await s.schreiben([
            zeile("fbh_1", 0, ["raum.1.ist": 20, "raum.1.soll": 21, "raum.2.ist": 19]),
            zeile("heiz_1", 0, ["fuehler.abgas": 80]),
        ])
        let a = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(300), schritt: 300,
                                   geraet: "fbh_1", schluessel: ["raum.1.ist", "raum.1.soll"])
        #expect(Set(a.reihen.map(\.schluessel)) == ["raum.1.ist", "raum.1.soll"])
    }
}

@MainActor
@Suite("Aufzeichnung")
struct AufzeichnungTests {
    @Test func mittelJePlatz() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        let aufzeichnung = Verlaufsaufzeichnung(speicher: s)
        func abtastung(_ sekunden: Double, _ abgas: Double, brenner: Double) -> Abtastung {
            Abtastung(geraet: "heiz_1", ort: "Kessel", zeit: morgen.addingTimeInterval(sekunden),
                      werte: ["fuehler.abgas": abgas, "brenner": brenner], bezeichnungen: ["fuehler.abgas": "Abgas"])
        }
        aufzeichnung.abtasten(abtastung(0, 80, brenner: 1))
        aufzeichnung.abtasten(abtastung(100, 90, brenner: 1))
        aufzeichnung.abtasten(abtastung(200, 100, brenner: 0))
        aufzeichnung.abtasten(abtastung(310, 120, brenner: 0))   // nächster Platz
        await aufzeichnung.sichern()

        let a = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(600), schritt: 300)
        #expect(a.reihe("heiz_1", "fuehler.abgas")?.werte == [90, 120])
        let brenner = try #require(a.reihe("heiz_1", "brenner")?.werte.first ?? nil)
        #expect(abs(brenner - 2.0 / 3.0) < 0.01)
        #expect(aufzeichnung.revision >= 2)
    }

    /// Übernommen werden nur die eigenen Fühler; den Kesselvorlauf im Verlauf des Speichers
    /// misst der Kessel.
    @Test func geraeteverlaufNurEigeneFuehler() async throws {
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        let aufzeichnung = Verlaufsaufzeichnung(speicher: s)
        let zustand: Heizgeraetezustand = try Aufnahme.lesen("attrappe/speicher-state.json")
        let verlauf: Verlaufsantwort = try Aufnahme.lesen("attrappe/speicher-history.json")
        aufzeichnung.verlaufUebernehmen(geraet: "heiz_3f21ac", zustand: zustand, antwort: verlauf)
        await aufzeichnung.sichern()
        let ende = Date(timeIntervalSince1970: TimeInterval(verlauf.juengsterEpoch ?? 0))
        let a = try await s.auszug(von: ende.addingTimeInterval(-86_400), bis: ende, schritt: 3600)
        let schluessel = Set(a.reihen.map(\.schluessel))
        #expect(schluessel.contains("fuehler.puffer"))
        #expect(schluessel.contains("fuehler.hk1_vl"))
        #expect(!schluessel.contains("fuehler.kessel_vl"))
        #expect(!schluessel.contains("fuehler.abgas"))
        #expect((a.reihe("heiz_3f21ac", "fuehler.puffer")?.werte.compactMap { $0 }.count ?? 0) >= 23)
    }
}

@Suite("Mitschnittimport")
struct ImportTests {
    /// Ein kleiner Mitschnitt aus den Zuständen der Attrappen: drei Zeilen im Abstand von
    /// 30 Sekunden, eine fehlgeschlagene Abfrage, eine unlesbare Zeile.
    func mitschnitt() throws -> URL {
        let verteiler = String(decoding: try Aufnahme.daten("attrappe/verteiler-state.json"), as: UTF8.self)
        let kessel = String(decoding: try Aufnahme.daten("attrappe/kessel-state.json"), as: UTF8.self)
        let speicher = String(decoding: try Aufnahme.daten("attrappe/speicher-state.json"), as: UTF8.self)
        func kompakt(_ json: String) -> String {
            String(decoding: try! JSONSerialization.data(withJSONObject: try! JSONSerialization.jsonObject(with: Data(json.utf8))), as: UTF8.self)
        }
        let v = kompakt(verteiler), k = kompakt(kessel), p = kompakt(speicher)
        var zeilen: [String] = []
        for i in 0..<3 {
            let epoch = Int(morgen.timeIntervalSince1970) + i * 30
            zeilen.append(#"{"zeit":"x","epoch":\#(epoch),"kessel":\#(k),"puffer":\#(p),"erdgeschoss":\#(v),"erdgeschoss_demand":{"demand":true}}"#)
        }
        zeilen.append(#"{"zeit":"x","epoch":\#(Int(morgen.timeIntervalSince1970) + 400),"kessel":{"fehler":"TimeoutError"},"erdgeschoss":\#(v)}"#)
        zeilen.append("{kaputt")
        let url = FileManager.default.temporaryDirectory.appending(path: "mitschnitt-\(UUID().uuidString).jsonl")
        try zeilen.joined(separator: "\n").appending("\n").write(to: url, atomically: true, encoding: .utf8)
        return url
    }

    @Test func importieren() async throws {
        let url = try mitschnitt()
        defer { try? FileManager.default.removeItem(at: url) }
        let s = try Verlaufsspeicher.oeffnen(datei: nil)
        let e = try await Mitschnittimport.importieren(url, in: s)
        #expect(e.zeilen == 5)
        #expect(e.unlesbar == 1)
        #expect(Set(e.geraete.keys) == ["fbh_a1b2c3", "heiz_9a1b2c", "heiz_3f21ac"])
        #expect(e.geraete["heiz_9a1b2c"] == "Kessel")
        #expect(e.plaetze == 4, "je Gerät ein Platz, der Verteiler zwei")

        let a = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(600), schritt: 300)
        #expect(a.reihe("fbh_a1b2c3", "raum.1.ist")?.werte == [20.45, 20.45])
        #expect(a.reihe("heiz_9a1b2c", "fuehler.abgas")?.werte == [88.86, nil])
        #expect(a.reihe("heiz_3f21ac", "pumpe.1")?.werte.first == 1)

        // Ein zweiter Import ändert nichts.
        _ = try await Mitschnittimport.importieren(url, in: s)
        let b = try await s.auszug(von: morgen, bis: morgen.addingTimeInterval(600), schritt: 300)
        #expect(a == b)
    }
}
