import Foundation
import Testing
@testable import Geraeteschnittstelle

struct VerbindungTests {
    private func verteiler(_ host: String, _ tabelle: @escaping @Sendable (Netzattrappe.Eingang) -> Netzattrappe.Antwort) -> Verteiler {
        Verteiler(adresse: URL(string: "http://\(host)")!, sitzung: Netzattrappe.sitzung(host: host, tabelle))
    }

    @Test func etagErspartDieUebertragung() async throws {
        let zustand = try Fixture.daten("attrappe/verteiler-state.json")
        let v = verteiler("etag.test") { e in
            e.kopf["If-None-Match"] == "\"7\""
                ? .init(status: 304, kopf: ["ETag": "\"7\""], rumpf: Data())
                : .init(kopf: ["ETag": "\"7\""], rumpf: zustand)
        }
        let erste = try await v.zustand()
        #expect(erste.wert?.raeume?.isEmpty == false)
        guard case .unveraendert = try await v.zustand() else {
            Issue.record("zweite Abfrage hätte 304 ergeben müssen")
            return
        }
        // Ohne ETag – für Werte, die den Änderungszähler nicht erhöhen – kommt alles.
        #expect(try await v.zustand(etagNutzen: false).wert != nil)
        #expect(Netzattrappe.eingaenge(host: "etag.test").map { $0.kopf["If-None-Match"] } == [nil, "\"7\"", nil])
    }

    @Test func meldungDerFirmwareKommtUnveraendertAn() async {
        let v = verteiler("meldung.test") { _ in
            .init(status: 409, rumpf: Data(#"{"ok":false,"error":"Es laeuft bereits eine Kalibrierung"}"#.utf8))
        }
        await #expect(throws: Geraetefehler.meldung("Es laeuft bereits eine Kalibrierung", status: 409)) {
            try await v.messfahrtStarten(kanal: 3)
        }
    }

    @Test func weiterleitungWirdNichtVerfolgt() async {
        let v = verteiler("umleitung.test") { _ in
            .init(status: 302, kopf: ["Location": "http://192.168.4.1/"], rumpf: Data())
        }
        await #expect(throws: Geraetefehler.weiterleitung(ziel: "http://192.168.4.1/")) {
            try await v.bedarf()
        }
    }

    @Test func befehleSendenDenRumpfDerFirmware() async throws {
        let v = verteiler("befehle.test") { _ in .init() }
        try await v.sollwert(raum: 2, 21.5)
        try await v.betriebsart(raum: 2, heizen: false)
        try await v.kanal(4, .stellung(0.37))
        try await v.kanal(4, .regeln)
        try await v.schutzfahrt(.alle)
        let e = Netzattrappe.eingaenge(host: "befehle.test")
        #expect(e.map(\.pfad) == ["/api/room/2/target", "/api/room/2/mode", "/api/channel/4/cmd", "/api/channel/4/cmd", "/api/system/seize-all"])
        #expect(try JSONWert.lesen(e[0].rumpf) == ["target_c": 21.5])
        #expect(try JSONWert.lesen(e[1].rumpf) == ["mode": "off"])
        #expect(try JSONWert.lesen(e[2].rumpf) == ["cmd": "position", "position": 0.37])
        #expect(try JSONWert.lesen(e[3].rumpf) == ["cmd": "auto"])
        #expect(e[0].kopf["Content-Type"] == "application/json")
    }

    @Test func zwischenstellungFuerAlleWirdGarNichtGesendet() async {
        let v = verteiler("alle.test") { _ in .init() }
        await #expect(throws: Geraetefehler.self) {
            try await v.alleKanaele(.stellung(0.5))
        }
        #expect(Netzattrappe.eingaenge(host: "alle.test").isEmpty)
    }

    @Test func heizgeraetBefehle() async throws {
        let h = Heizgeraet(adresse: URL(string: "http://heiz.test")!, sitzung: Netzattrappe.sitzung(host: "heiz.test") { _ in .init() })
        try await h.heizkreis(2, .aus)
        try await h.kesselkreispumpe(.automatik)
        try await h.aufzeichnung(.scharfSchalten)
        let e = Netzattrappe.eingaenge(host: "heiz.test")
        #expect(e.map(\.pfad) == ["/api/circuit/2/mode", "/api/boilerpump/auto", "/api/record/arm"])
        #expect(try JSONWert.lesen(e[0].rumpf) == ["mode": "aus"])
    }

    @Test func anfragenLaufenNacheinander() async throws {
        let zustand = try Fixture.daten("attrappe/verteiler-state.json")
        let v = verteiler("reihe.test") { _ in
            Thread.sleep(forTimeInterval: 0.05)
            return .init(rumpf: zustand)
        }
        let beginn = Date()
        try await withThrowingTaskGroup(of: Void.self) { gruppe in
            for _ in 0..<4 {
                gruppe.addTask { _ = try await v.zustand(etagNutzen: false) }
            }
            try await gruppe.waitForAll()
        }
        // vier Anfragen zu je 50 ms, nicht gleichzeitig
        #expect(Date().timeIntervalSince(beginn) >= 0.19)
    }
}

struct FirmwaredateiTests {
    private func abbild(projekt: String, version: String = "v0.4.0") -> Data {
        var d = Data(count: 32 + 256)
        d[0] = 0xE9
        withUnsafeBytes(of: UInt32(0xABCD_5432).littleEndian) { d.replaceSubrange(32..<36, with: $0) }
        d.replaceSubrange(48..<(48 + version.utf8.count), with: Data(version.utf8))
        d.replaceSubrange(80..<(80 + projekt.utf8.count), with: Data(projekt.utf8))
        return d
    }

    @Test func projektUndFassung() throws {
        let f = try Firmwaredatei(daten: abbild(projekt: "floor-heating-ctrl"))
        #expect(f.projekt == "floor-heating-ctrl")
        #expect(f.version == "v0.4.0")
    }

    @Test func falscheGeraeteartWirdAbgewiesen() async throws {
        let f = try Firmwaredatei(daten: abbild(projekt: "heat-source-ctrl"))
        let v = Verteiler(adresse: URL(string: "http://ota.test")!, sitzung: Netzattrappe.sitzung(host: "ota.test") { _ in .init() })
        await #expect(throws: Geraetefehler.falscheFirmware(erwartet: "floor-heating-ctrl", gefunden: "heat-source-ctrl")) {
            try await v.firmwareEinspielen(f)
        }
        #expect(Netzattrappe.eingaenge(host: "ota.test").isEmpty)
    }

    @Test func keinAbbild() {
        #expect(throws: Geraetefehler.self) {
            try Firmwaredatei(daten: Data("<html>".utf8))
        }
    }
}
