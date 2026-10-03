import Foundation
import Testing
import Anlage
@testable import Diagnose

@MainActor
@Suite("Befundgedächtnis")
struct BefundgedaechtnisTests {
    let beginn = Date(timeIntervalSince1970: 1_790_056_800)
    let alle: Set<Befund.Schwere> = [.stoerung, .warnung, .hinweis]

    func befund(_ id: String, _ schwere: Befund.Schwere = .warnung, mindestdauer: TimeInterval = 0) -> Befund {
        Befund(id: id, schwere: schwere, titel: "Titel \(id)", text: "Text", ort: "Keller", quelle: "Prüfung der App",
               mindestdauer: mindestdauer)
    }

    func minuten(_ m: Double) -> Date { beginn.addingTimeInterval(m * 60) }

    /// Ein Befund meldet sich einmal, auch wenn er über viele Abfragen ansteht.
    @Test func einmalMelden() {
        let g = Befundgedaechtnis(datei: nil)
        let erste = g.abgleichen([befund("relais")], jetzt: minuten(0), vollstaendig: true, melden: alle)
        #expect(erste.mitteilungen.map(\.id) == ["relais"])
        #expect(erste.sichtbar.first?.seit == minuten(0))
        let zweite = g.abgleichen([befund("relais")], jetzt: minuten(1), vollstaendig: true, melden: alle)
        #expect(zweite.mitteilungen.isEmpty)
        #expect(zweite.sichtbar.first?.seit == minuten(0))
    }

    /// Erst nach der Mindestdauer sichtbar; der Beginn zählt vom ersten Auftreten.
    @Test func mindestdauer() {
        let g = Befundgedaechtnis(datei: nil)
        let b = befund("handbetrieb:fbh_1/3", .hinweis, mindestdauer: 3600)
        #expect(g.abgleichen([b], jetzt: minuten(0), vollstaendig: true, melden: alle).sichtbar.isEmpty)
        #expect(g.abgleichen([b], jetzt: minuten(59), vollstaendig: true, melden: alle).sichtbar.isEmpty)
        let spaeter = g.abgleichen([b], jetzt: minuten(60), vollstaendig: true, melden: alle)
        #expect(spaeter.sichtbar.first?.seit == minuten(0))
        #expect(spaeter.mitteilungen.count == 1)
        // Verschwindet er vorher, beginnt die Mindestdauer neu, sobald die Ruhezeit vorbei ist.
        let g2 = Befundgedaechtnis(datei: nil)
        g2.abgleichen([b], jetzt: minuten(0), vollstaendig: true, melden: alle)
        let weg = g2.abgleichen([], jetzt: minuten(30), vollstaendig: true, melden: alle)
        #expect(weg.erledigt.isEmpty, "nie sichtbar gewesen, also nichts zurückzunehmen")
        #expect(g2.abgleichen([b], jetzt: minuten(120), vollstaendig: true, melden: alle).sichtbar.isEmpty)
    }

    /// Kurzes Verschwinden setzt den Ablauf fort; nach der Ruhezeit beginnt ein neuer.
    @Test func ruhezeit() {
        let g = Befundgedaechtnis(datei: nil)
        g.abgleichen([befund("wlan:heiz_1")], jetzt: minuten(0), vollstaendig: true, melden: alle)
        let weg = g.abgleichen([], jetzt: minuten(5), vollstaendig: true, melden: alle)
        #expect(weg.erledigt == ["wlan:heiz_1"])
        #expect(g.erledigte.map(\.id) == ["wlan:heiz_1"])
        // Die zurückgenommene Mitteilung kehrt still zurück, nur einmal.
        let zurueck = g.abgleichen([befund("wlan:heiz_1")], jetzt: minuten(20), vollstaendig: true, melden: alle)
        #expect(zurueck.mitteilungen.map(\.still) == [true])
        #expect(zurueck.sichtbar.first?.seit == minuten(0))
        #expect(g.abgleichen([befund("wlan:heiz_1")], jetzt: minuten(21), vollstaendig: true, melden: alle).mitteilungen.isEmpty)
        g.abgleichen([], jetzt: minuten(30), vollstaendig: true, melden: alle)
        let neu = g.abgleichen([befund("wlan:heiz_1")], jetzt: minuten(100), vollstaendig: true, melden: alle)
        #expect(neu.mitteilungen.map(\.still) == [false])
        #expect(neu.sichtbar.first?.seit == minuten(100))
    }

    /// Solange nicht alle Geräte geantwortet haben, gilt nichts als erledigt.
    @Test func unvollstaendigerStand() {
        let g = Befundgedaechtnis(datei: nil)
        g.abgleichen([befund("kalibrierung:fbh_1", .hinweis)], jetzt: minuten(0), vollstaendig: true, melden: alle)
        let start = g.abgleichen([], jetzt: minuten(1), vollstaendig: false, melden: alle)
        #expect(start.erledigt.isEmpty)
        #expect(g.erledigte.isEmpty)
    }

    /// Stumm heißt: sichtbar, aber ohne Mitteilung. Die Stummschaltung gilt auch für einen
    /// späteren Ablauf.
    @Test func stumm() {
        let g = Befundgedaechtnis(datei: nil)
        g.abgleichen([befund("kreis-ohne-verteiler:2")], jetzt: minuten(0), vollstaendig: true, melden: [])
        g.stummSchalten("kreis-ohne-verteiler:2", true)
        #expect(g.sichtbar.first?.stumm == true)
        g.abgleichen([], jetzt: minuten(1), vollstaendig: true, melden: alle)
        let wieder = g.abgleichen([befund("kreis-ohne-verteiler:2", .stoerung)], jetzt: minuten(200), vollstaendig: true, melden: alle)
        #expect(wieder.mitteilungen.isEmpty)
        #expect(wieder.sichtbar.first?.stumm == true)
    }

    /// Was bei ausgeschalteter Schwere nicht gemeldet wurde, holt ein späteres Einschalten nicht
    /// nach; eine höhere Schwere wird gemeldet.
    @Test func schwereUndEinstellungen() {
        let g = Befundgedaechtnis(datei: nil)
        let ohneHinweise: Set<Befund.Schwere> = [.stoerung, .warnung]
        #expect(g.abgleichen([befund("batterie:fbh_1/2", .hinweis)], jetzt: minuten(0), vollstaendig: true, melden: ohneHinweise).mitteilungen.isEmpty)
        #expect(g.abgleichen([befund("batterie:fbh_1/2", .hinweis)], jetzt: minuten(1), vollstaendig: true, melden: alle).mitteilungen.isEmpty)
        let hoeher = g.abgleichen([befund("batterie:fbh_1/2", .warnung)], jetzt: minuten(2), vollstaendig: true, melden: alle)
        #expect(hoeher.mitteilungen.map(\.schwere) == [.warnung])
        #expect(g.abgleichen([befund("batterie:fbh_1/2", .warnung)], jetzt: minuten(3), vollstaendig: true, melden: alle).mitteilungen.isEmpty)
    }

    @Test func speichernUndLaden() throws {
        let datei = FileManager.default.temporaryDirectory.appending(path: "befunde-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: datei) }
        let g = Befundgedaechtnis(datei: datei)
        g.abgleichen([befund("relais", .stoerung)], jetzt: minuten(0), vollstaendig: true, melden: alle)
        g.stummSchalten("relais", true)
        let wieder = Befundgedaechtnis(datei: datei)
        #expect(wieder.eintraege["relais"]?.stumm == true)
        #expect(wieder.eintraege["relais"]?.erstmals == minuten(0))
        // Nach einem Neustart der App setzt der Ablauf fort, statt neu zu melden.
        let nachStart = wieder.abgleichen([befund("relais", .stoerung)], jetzt: minuten(10), vollstaendig: true, melden: alle)
        #expect(nachStart.mitteilungen.isEmpty)
        #expect(nachStart.sichtbar.first?.seit == minuten(0))
    }

    /// Beim allerersten Lauf keine Flut von Mitteilungen; was danach hinzukommt, wird gemeldet,
    /// auch nach einem Neustart ohne je einen Befund.
    @Test func ersterLauf() throws {
        let datei = FileManager.default.temporaryDirectory.appending(path: "befunde-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: datei) }
        let g = Befundgedaechtnis(datei: datei)
        let erste = g.abgleichen([befund("thermometer:fbh_1/3"), befund("kalibrierung:fbh_1", .hinweis)],
                                 jetzt: minuten(0), vollstaendig: true, melden: alle)
        #expect(erste.mitteilungen.isEmpty)
        #expect(erste.sichtbar.count == 2)
        let neu = g.abgleichen([befund("thermometer:fbh_1/3"), befund("relais", .stoerung)], jetzt: minuten(1), vollstaendig: true, melden: alle)
        #expect(neu.mitteilungen.map(\.id) == ["relais"])

        let leer = FileManager.default.temporaryDirectory.appending(path: "befunde-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: leer) }
        Befundgedaechtnis(datei: leer).abgleichen([], jetzt: minuten(0), vollstaendig: true, melden: alle)
        let spaeter = Befundgedaechtnis(datei: leer).abgleichen([befund("relais", .stoerung)], jetzt: minuten(5), vollstaendig: true, melden: alle)
        #expect(spaeter.mitteilungen.map(\.id) == ["relais"])
    }

    @Test func aufbewahrung() {
        let g = Befundgedaechtnis(datei: nil)
        g.abgleichen([befund("uhrzeit:fbh_1")], jetzt: minuten(0), vollstaendig: true, melden: alle)
        g.abgleichen([], jetzt: minuten(1), vollstaendig: true, melden: alle)
        #expect(g.erledigte.count == 1)
        g.abgleichen([], jetzt: minuten(1 + 31 * 24 * 60), vollstaendig: true, melden: alle)
        #expect(g.erledigte.isEmpty)
    }
}
