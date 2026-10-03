import Foundation
import Testing
@testable import Verlauf

/// Fünfminutendatei, wie der Leitstand sie schreibt (docs/katalog-messgroessen.md)
private let datei = """
zeit,fuehler.puffer,brenner,brenner.starts
2026-09-23T00:00:00Z,64.444,0.6,3
2026-09-23T00:05:00Z,65,,3
2026-09-23T00:10:00Z,,,

"""

@Suite("Protokollabgleich")
struct ProtokollabgleichTests {
    @Test func ganzeDatei() {
        let s = Protokollabgleich.lesen(Data(datei.utf8), stand: .init())
        #expect(s.stand.spalten == ["fuehler.puffer", "brenner", "brenner.starts"])
        #expect(s.stand.stelle == datei.utf8.count)
        // Die dritte Zeile hat keinen Wert und ergibt keinen Platz.
        #expect(s.zeilen.count == 2)
        let beginn = Raster.index(Protokollabgleich.iso("2026-09-23T00:00:00Z")!)
        #expect(s.zeilen[0].index == beginn)
        #expect(s.zeilen[0].werte == ["fuehler.puffer": 64.444, "brenner": 0.6, "brenner.starts": 3])
        #expect(s.zeilen[1].index == beginn + 1)
        #expect(s.zeilen[1].werte["brenner"] == nil, "eine leere Zelle ist kein Wert")
    }

    @Test func fortsetzungAbStelle() {
        let bytes = Data(datei.utf8)
        // Erst bis in die Mitte der zweiten Datenzeile: Sie bleibt liegen.
        let schnitt = datei.utf8.count - 30
        let erst = Protokollabgleich.lesen(bytes.prefix(schnitt), stand: .init())
        #expect(erst.zeilen.count == 1)
        #expect(erst.stand.stelle < schnitt, "die angefangene Zeile zählt nicht")
        // Dann der Rest ab der gemerkten Stelle, wie ihn eine Range-Anfrage liefert.
        let rest = Protokollabgleich.lesen(bytes.suffix(from: erst.stand.stelle), stand: erst.stand)
        #expect(rest.zeilen.count == 1)
        #expect(rest.zeilen[0].werte["fuehler.puffer"] == 65)
        #expect(rest.stand.stelle == bytes.count)
        // Nichts Neues: kein Platz, Stand unverändert.
        let nichts = Protokollabgleich.lesen(Data(), stand: rest.stand)
        #expect(nichts.zeilen.isEmpty && nichts.stand == rest.stand)
    }

    @Test func abgebrocheneZeileOhneEnde() {
        let text = "zeit,a\n2026-09-23T00:00:00Z,1\n2026-09-23T00:05:00Z,2"
        let s = Protokollabgleich.lesen(Data(text.utf8), stand: .init())
        #expect(s.zeilen.count == 1)
        #expect(s.stand.stelle == text.utf8.count - "2026-09-23T00:05:00Z,2".utf8.count)
    }

    @Test func falscheSpaltenzahlWirdVerworfen() {
        let text = "zeit,a,b\n2026-09-23T00:00:00Z,1\n2026-09-23T00:05:00Z,1,2\n"
        let s = Protokollabgleich.lesen(Data(text.utf8), stand: .init())
        #expect(s.verworfen == 1)
        #expect(s.zeilen.count == 1)
    }

    @Test func tageInUTC() {
        let t = Protokollabgleich.iso("2026-09-22T23:58:00Z")!
        #expect(Protokollabgleich.tag(t) == "2026-09-22")
        #expect(Protokollabgleich.tagesbeginn("2026-09-23") == Protokollabgleich.iso("2026-09-23T00:00:00Z"))
    }

    @Test func standUeberstehtSichern() throws {
        var stand = Abgleichstand()
        stand.dateien["2026-09-23"] = ["heiz_1.5min.csv": .init(stelle: 1234, spalten: ["a", "b"])]
        stand.abgeschlossen = ["2026-09-22"]
        stand.zuletzt = Date(timeIntervalSince1970: 1_790_141_877)
        let datei = FileManager.default.temporaryDirectory.appending(path: "abgleich-\(UUID()).json")
        try stand.sichern(datei)
        #expect(Abgleichstand.laden(datei) == stand)
        try? FileManager.default.removeItem(at: datei)
        #expect(Abgleichstand.laden(datei) == Abgleichstand(), "fehlt die Datei, beginnt der Abgleich von vorn")
    }

    /// Zweimal derselbe Tag ergibt dieselben Plätze, nicht doppelte: Die Werte des Leitstands
    /// überschreiben, statt sich zu addieren.
    @Test func keineDoppeltenPlaetze() async throws {
        let speicher = try Verlaufsspeicher.oeffnen(datei: nil)
        let s = Protokollabgleich.lesen(Data(datei.utf8), stand: .init())
        let zeilen = s.zeilen.map { Messzeile(geraet: "heiz_1", ort: "Kessel", index: $0.index, werte: $0.werte, bezeichnungen: [:]) }
        try await speicher.schreiben(zeilen)
        try await speicher.schreiben(zeilen)
        let von = Protokollabgleich.iso("2026-09-23T00:00:00Z")!
        let a = try await speicher.auszug(von: von, bis: von.addingTimeInterval(900), schritt: 300, geraet: "heiz_1")
        let puffer = a.reihe("heiz_1", "fuehler.puffer")
        #expect(puffer?.werte.compactMap { $0 }.count == 2)
        #expect(abs((puffer?.werte[0] ?? 0) - 64.44) < 0.01)
    }
}
