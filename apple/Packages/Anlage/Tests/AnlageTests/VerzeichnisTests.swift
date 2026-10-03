import CryptoKit
import Foundation
import Geraeteschnittstelle
import Testing
@testable import Anlage

@MainActor
struct GeraeteverzeichnisTests {
    private func datei() -> URL {
        FileManager.default.temporaryDirectory.appending(path: "verzeichnis-\(UUID().uuidString).json")
    }

    @Test func aufnehmenUndWiederLaden() throws {
        let d = datei()
        defer { try? FileManager.default.removeItem(at: d) }
        let v = Geraeteverzeichnis(datei: d)
        v.aufnehmen(BekanntesGeraet(id: "heiz_19c8a2", art: .heizung, ort: "Kessel", adresse: URL(string: "http://192.168.0.179")!))
        v.aufnehmen(BekanntesGeraet(id: "fbh_5d20e7", art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://192.168.0.213")!))

        let wieder = Geraeteverzeichnis(datei: d)
        #expect(wieder.geraete.map(\.id) == ["heiz_19c8a2", "fbh_5d20e7"])
        #expect(wieder.verteiler.first?.bezeichnung == "Verteiler Erdgeschoss")
        #expect(wieder.heizgeraete.first?.bezeichnung == "Kessel")
    }

    /// Dieselbe Kennung ergibt keinen zweiten Eintrag; der Zeitpunkt der Aufnahme bleibt.
    @Test func erneutesAufnehmenAktualisiert() {
        let v = Geraeteverzeichnis(datei: nil)
        let frueher = Date(timeIntervalSince1970: 1_780_000_000)
        v.aufnehmen(BekanntesGeraet(id: "fbh_3a91c4", art: .verteiler, ort: "Keller", adresse: URL(string: "http://192.168.0.250")!, aufgenommen: frueher))
        v.aufnehmen(BekanntesGeraet(id: "fbh_3a91c4", art: .verteiler, ort: "Keller", adresse: URL(string: "http://192.168.0.251")!))
        #expect(v.geraete.count == 1)
        #expect(v.geraete.first?.adresse.host() == "192.168.0.251")
        #expect(v.geraete.first?.aufgenommen == frueher)
    }

    /// Ort und Firmware folgen dem, was das Gerät meldet; leere Angaben ändern nichts.
    @Test func gemeldetFolgtDemGeraet() {
        let v = Geraeteverzeichnis(datei: nil)
        v.aufnehmen(BekanntesGeraet(id: "fbh_a1b2c3", art: .verteiler, ort: "Dachgeschoss",
                                    adresse: URL(string: "http://127.0.0.1:8321")!, firmware: "0.9.0"))
        v.gemeldet("fbh_a1b2c3", ort: "Erdgeschoss", firmware: "1.0.0")
        #expect(v.geraete.first?.ort == "Erdgeschoss")
        #expect(v.geraete.first?.firmware == "1.0.0")
        v.gemeldet("fbh_a1b2c3", ort: "", firmware: nil)
        #expect(v.geraete.first?.ort == "Erdgeschoss")
        #expect(v.geraete.first?.firmware == "1.0.0")
    }

    @Test func gesehenUndEntfernen() {
        let v = Geraeteverzeichnis(datei: nil)
        v.aufnehmen(BekanntesGeraet(id: "fbh_7b44f1", art: .verteiler, ort: "Obergeschoss", adresse: URL(string: "http://192.168.0.46")!))
        v.gesehen("fbh_7b44f1", unter: URL(string: "http://192.168.0.47")!)
        #expect(v.geraete.first?.zuletztGesehen != nil)
        #expect(v.geraete.first?.adresse.host() == "192.168.0.47")
        v.entfernen("fbh_7b44f1")
        #expect(v.geraete.isEmpty)
    }
}

struct SicherungsablageTests {
    private func ablage(_ schluessel: SymmetricKey = SymmetricKey(size: .bits256)) -> Sicherungsablage {
        Sicherungsablage(verzeichnis: FileManager.default.temporaryDirectory.appending(path: "sicherungen-\(UUID().uuidString)"), schluessel: schluessel)
    }

    private let inhalt = Data(#"{"app":"floor-heating-ctrl","config":{"wifi":{"ssid":"Heimnetz","pass":"geheim"}}}"#.utf8)

    @Test func hinUndZurueck() throws {
        let a = ablage()
        let e = try a.sichern(inhalt, geraet: "fbh_5d20e7")
        #expect(try a.lesen(e) == inhalt)
        // Auf dem Datenträger steht das Kennwort nicht im Klartext.
        let roh = try Data(contentsOf: e.datei)
        #expect(roh.range(of: Data("geheim".utf8)) == nil)
    }

    @Test func andererSchluesselLiestNichts() throws {
        let a = ablage()
        let e = try a.sichern(inhalt, geraet: "fbh_5d20e7")
        let fremd = Sicherungsablage(verzeichnis: a.verzeichnis, schluessel: SymmetricKey(size: .bits256))
        #expect(throws: (any Error).self) { try fremd.lesen(e) }
    }

    /// Eine umbenannte Datei lässt sich keinem anderen Gerät unterschieben.
    @Test func gebundenAnDasGeraet() throws {
        let a = ablage()
        let e = try a.sichern(inhalt, geraet: "fbh_5d20e7")
        let umbenannt = Sicherungseintrag(geraet: "fbh_7b44f1", datum: e.datum, datei: e.datei)
        #expect(throws: (any Error).self) { try a.lesen(umbenannt) }
    }

    @Test func eintraegeUndAufraeumen() throws {
        let a = ablage()
        for tag in 0..<12 {
            try a.sichern(inhalt, geraet: "heiz_19c8a2", datum: Date(timeIntervalSince1970: 1_780_000_000 + Double(tag) * 86_400))
        }
        try a.sichern(inhalt, geraet: "fbh_5d20e7")
        #expect(try a.eintraege(geraet: "heiz_19c8a2").count == 12)
        try a.aufraeumen(behalten: 10)
        let rest = try a.eintraege(geraet: "heiz_19c8a2")
        #expect(rest.count == 10)
        #expect(rest.first?.datum == Date(timeIntervalSince1970: 1_780_000_000 + 11 * 86_400))
        #expect(try a.eintraege(geraet: "fbh_5d20e7").count == 1)
    }
}

@MainActor
struct LeitstandverzeichnisTests {
    private func datei(_ name: String) -> URL {
        FileManager.default.temporaryDirectory.appending(path: "\(name)-\(UUID().uuidString).json")
    }

    /// Leitstände liegen in einer eigenen Datei; die Gerätedatei bleibt, wie ältere Fassungen der
    /// App sie lesen.
    @Test func eigeneDateiUndWiederLaden() throws {
        let g = datei("geraete"), l = datei("leitstaende")
        defer {
            try? FileManager.default.removeItem(at: g)
            try? FileManager.default.removeItem(at: l)
        }
        let v = Geraeteverzeichnis(datei: g, leitstandDatei: l)
        v.aufnehmen(BekanntesGeraet(id: "heiz_19c8a2", art: .heizung, ort: "Kessel", adresse: URL(string: "http://192.168.0.179")!))
        v.leitstandAufnehmen(BekannterLeitstand(id: "lst_c0ffee", ort: "Leitstand", adresse: URL(string: "http://192.168.0.249")!))

        let geraete = try JSONSerialization.jsonObject(with: Data(contentsOf: g)) as? [[String: Any]]
        #expect(geraete?.count == 1)
        #expect(geraete?.first?["id"] as? String == "heiz_19c8a2")

        let wieder = Geraeteverzeichnis(datei: g, leitstandDatei: l)
        #expect(wieder.leitstaende.map(\.id) == ["lst_c0ffee"])
        #expect(wieder.kenntLeitstand("lst_c0ffee"))
        #expect(!wieder.kennt("lst_c0ffee"))
        #expect(wieder.geraete.map(\.id) == ["heiz_19c8a2"])
    }

    @Test func gemeldetUndEntfernen() {
        let v = Geraeteverzeichnis(datei: nil)
        let frueher = Date(timeIntervalSince1970: 1_780_000_000)
        v.leitstandAufnehmen(BekannterLeitstand(id: "lst_c0ffee", ort: "Leitstand", adresse: URL(string: "http://192.168.0.249")!, aufgenommen: frueher))
        v.leitstandAufnehmen(BekannterLeitstand(id: "lst_c0ffee", ort: "Leitstand", adresse: URL(string: "http://192.168.0.250")!))
        #expect(v.leitstaende.count == 1)
        #expect(v.leitstaende.first?.aufgenommen == frueher)
        #expect(v.leitstaende.first?.adresse.host() == "192.168.0.250")

        v.leitstandGemeldet("lst_c0ffee", ort: "Heizraum", firmware: "v0.5.0")
        #expect(v.leitstaende.first?.ort == "Heizraum")
        #expect(v.leitstaende.first?.firmware == "v0.5.0")

        v.leitstandEntfernen("lst_c0ffee")
        #expect(v.leitstaende.isEmpty)
    }
}

@MainActor
struct AdressnachfuehrungTests {
    private func datei(_ name: String) -> URL {
        FileManager.default.temporaryDirectory.appending(path: "\(name)-\(UUID().uuidString).json")
    }

    /// Nach einem Neustart des Routers: Die Suche trifft Gerät und Leitstand unter neuen Adressen
    /// an, zugeordnet über die Kennung. Beide Dateien halten die neue Adresse.
    @Test func folgtNeuerAdresseUndSpeichert() throws {
        let g = datei("geraete"), l = datei("leitstaende")
        defer {
            try? FileManager.default.removeItem(at: g)
            try? FileManager.default.removeItem(at: l)
        }
        let v = Geraeteverzeichnis(datei: g, leitstandDatei: l)
        v.aufnehmen(BekanntesGeraet(id: "fbh_5d20e7", art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://192.168.0.213")!))
        v.aufnehmen(BekanntesGeraet(id: "heiz_19c8a2", art: .heizung, ort: "Kessel", adresse: URL(string: "http://192.168.0.179:80")!))
        v.leitstandAufnehmen(BekannterLeitstand(id: "lst_c0ffee", ort: "Leitstand", adresse: URL(string: "http://192.168.0.249")!))

        let zeitpunkt = Date(timeIntervalSince1970: 1_790_000_000)
        let geaendert = v.nachfuehren([
            "fbh_5d20e7": URL(string: "http://192.168.0.31:80")!,
            "heiz_19c8a2": URL(string: "http://192.168.0.179:80")!,
            "lst_c0ffee": URL(string: "http://192.168.0.32:80")!,
        ], am: zeitpunkt)
        #expect(geaendert == ["fbh_5d20e7", "lst_c0ffee"])

        let wieder = Geraeteverzeichnis(datei: g, leitstandDatei: l)
        let verteiler = try #require(wieder.geraete.first { $0.id == "fbh_5d20e7" })
        #expect(verteiler.adresse.host() == "192.168.0.31")
        #expect(verteiler.zuletztGesehen == zeitpunkt)
        // Angetroffen, aber unter derselben Adresse: nur der Zeitpunkt ändert sich.
        #expect(wieder.geraete.first { $0.id == "heiz_19c8a2" }?.zuletztGesehen == zeitpunkt)
        let leitstand = try #require(wieder.leitstaende.first)
        #expect(leitstand.adresse.host() == "192.168.0.32")
        #expect(leitstand.zuletztGesehen == zeitpunkt)
    }

    /// Aufgenommen wird nur in der Einrichtung; ein unbekanntes Gerät im Netz ändert nichts.
    @Test func unbekannteKennungBleibtDraussen() {
        let v = Geraeteverzeichnis(datei: nil)
        v.aufnehmen(BekanntesGeraet(id: "fbh_5d20e7", art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://192.168.0.213")!))
        let geaendert = v.nachfuehren(["fbh_7b44f1": URL(string: "http://192.168.0.46:80")!])
        #expect(geaendert.isEmpty)
        #expect(v.geraete.map(\.id) == ["fbh_5d20e7"])
        #expect(v.leitstaende.isEmpty)
        #expect(v.geraete.first?.zuletztGesehen == nil)
    }

    /// Die Suche schreibt den Port immer aus, die Einrichtung meist nicht. Das ist dieselbe
    /// Adresse; die Abfragen sollen deshalb nicht neu beginnen.
    @Test func ausgeschriebenerPortIstDieselbeAdresse() {
        let url = { (s: String) in URL(string: s)! }
        #expect(Geraeteverzeichnis.gleicheAdresse(url("http://192.168.0.213"), url("http://192.168.0.213:80")))
        #expect(Geraeteverzeichnis.gleicheAdresse(url("http://Heizung.local"), url("http://heizung.local:80")))
        #expect(!Geraeteverzeichnis.gleicheAdresse(url("http://127.0.0.1:8321"), url("http://127.0.0.1:8331")))
        #expect(!Geraeteverzeichnis.gleicheAdresse(url("http://192.168.0.213"), url("http://192.168.0.214")))

        let v = Geraeteverzeichnis(datei: nil)
        v.leitstandAufnehmen(BekannterLeitstand(id: "lst_c0ffee", ort: "Leitstand", adresse: url("http://192.168.0.249")))
        #expect(!v.leitstandGesehen("lst_c0ffee", unter: url("http://192.168.0.249:80")))
        #expect(v.leitstaende.first?.adresse == url("http://192.168.0.249"))
        #expect(v.leitstaende.first?.zuletztGesehen != nil)
        #expect(v.leitstandGesehen("lst_c0ffee", unter: url("http://127.0.0.1:8335")))
        #expect(v.leitstaende.first?.adresse == url("http://127.0.0.1:8335"))
        #expect(!v.leitstandGesehen("lst_unbekannt", unter: url("http://127.0.0.1:8335")))
    }

    /// Die Leitstanddatei älterer Fassungen kennt `zuletztGesehen` nicht.
    @Test func aeltereLeitstanddateiBleibtLesbar() throws {
        let l = datei("leitstaende")
        defer { try? FileManager.default.removeItem(at: l) }
        try Data("""
        [{"adresse":"http://192.168.0.249","aufgenommen":"2026-09-01T10:00:00Z","id":"lst_c0ffee","ort":"Leitstand"}]
        """.utf8).write(to: l)
        let v = Geraeteverzeichnis(datei: nil, leitstandDatei: l)
        #expect(v.leitstaende.map(\.id) == ["lst_c0ffee"])
        #expect(v.leitstaende.first?.zuletztGesehen == nil)
    }

    /// Antwortet ein Gerät nicht mehr, meldet der Betrieb es; nach dem Nachführen fragt er es unter
    /// der neuen Adresse ab.
    @Test func betriebMeldetAusfallUndFolgtNeuerAdresse() async throws {
        let nachbildung = try Verteilernachbildung()
        let id = Verteilernachbildung.kennung
        let sitzung = Netzattrappe.sitzung(host: "alt.nachfuehrung.test") { _ in .fehler(503, "weg") }
        _ = Netzattrappe.sitzung(host: "neu.nachfuehrung.test") { nachbildung.antwort($0) }
        var takt = AnlagenbetriebTests.takt
        takt.verteiler = .milliseconds(20)
        let b = Anlagenbetrieb(sitzung: sitzung, takt: takt)
        defer { b.beenden() }
        var gemeldet: [String] = []
        b.unerreichbar = { gemeldet.append($0) }

        let v = Geraeteverzeichnis(datei: nil)
        v.aufnehmen(BekanntesGeraet(id: id, art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://alt.nachfuehrung.test")!))
        b.abgleichen(v.geraete)
        var runden = 0
        while gemeldet.isEmpty, runden < 300 {
            try await Task.sleep(for: .milliseconds(10))
            runden += 1
        }
        #expect(gemeldet.first == id)
        #expect(b.staende[id]?.erreichbar == false)

        #expect(v.nachfuehren([id: URL(string: "http://neu.nachfuehrung.test:80")!]) == [id])
        b.abgleichen(v.geraete)
        runden = 0
        while b.staende[id]?.erreichbar != true, runden < 300 {
            try await Task.sleep(for: .milliseconds(10))
            runden += 1
        }
        #expect(b.staende[id]?.erreichbar == true)
        #expect(b.staende[id]?.geraet.adresse.host() == "neu.nachfuehrung.test")
        #expect(!Netzattrappe.eingaenge(host: "neu.nachfuehrung.test").isEmpty)
    }
}
