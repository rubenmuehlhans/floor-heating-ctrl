import Foundation
import Testing
import CryptoKit
import Geraeteschnittstelle
@testable import Anlage

/// Befehle gehen an eine Nachbildung des Verteilers; geprüft wird, was ankommt und ob die App
/// merkt, wenn die Firmware einen Befehl quittiert, ohne ihn auszuführen.
@MainActor
@Suite("Befehle mit Rücklesen")
struct AnlagenbetriebTests {
    let id = Verteilernachbildung.kennung

    /// Abgefragt wird nur zu Beginn und auf Anstoß; zurückgelesen wird ohne Wartezeit.
    static var takt: Anlagenbetrieb.Takt {
        var t = Anlagenbetrieb.Takt()
        t.verteiler = .seconds(3600)
        t.beschaeftigt = .seconds(3600)
        t.ruecklesen = .milliseconds(20)
        t.sicherungAbstand = nil
        return t
    }

    /// Bindet die Nachbildung ein und wartet, bis Zustand und Konfiguration gelesen sind.
    func betrieb(_ host: String, _ geraet: Verteilernachbildung, sicherungen: Sicherungsablage? = nil) async throws -> Anlagenbetrieb {
        let sitzung = Netzattrappe.sitzung(host: host) { geraet.antwort($0) }
        let b = Anlagenbetrieb(sitzung: sitzung, sicherungen: sicherungen, takt: Self.takt)
        b.abgleichen([BekanntesGeraet(id: id, art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://\(host)")!)])
        var runden = 0
        while b.staende[id]?.konfiguration == nil || b.staende[id]?.verteiler == nil, runden < 300 {
            try await Task.sleep(for: .milliseconds(10))
            runden += 1
        }
        try #require(b.staende[id]?.verteiler != nil)
        Netzattrappe.vergessen(host: host)
        return b
    }

    /// Außerhalb 5–35 °C quittiert die Firmware, ohne zu übernehmen; die App begrenzt vorher und
    /// rundet auf halbe Grad wie die Weboberfläche.
    @Test func sollwertBegrenztUndGerundet() async throws {
        let g = try Verteilernachbildung()
        let b = try await betrieb("sollwert.test", g)
        defer { b.beenden() }
        try await b.sollwert(raum: "\(id)/1", 40.3)
        try await b.sollwert(raum: "\(id)/2", 21.74)
        #expect(Netzattrappe.befehle(host: "sollwert.test") == [
            #"POST /api/room/1/target {"target_c":35}"#,
            #"POST /api/room/2/target {"target_c":21.5}"#,
        ])
        let raeume = b.staende[id]?.verteiler?.raeume ?? []
        #expect(raeume.first { $0.id == 1 }?.sollC == 35)
        #expect(raeume.first { $0.id == 2 }?.sollC == 21.5)
        #expect(b.bild?.etagen.first?.raeume.first { $0.nummer == 2 }?.soll == 21.5)
    }

    @Test func stillVerworfenerBefehlWirdGemeldet() async throws {
        let g = try Verteilernachbildung()
        let b = try await betrieb("verworfen.test", g)
        defer { b.beenden() }
        g.uebernimmt = false
        await #expect(throws: Befehlsfehler.nichtUebernommen("Der Verteiler hat den Sollwert nicht übernommen.")) {
            try await b.sollwert(raum: "\(id)/1", 23)
        }
        await #expect(throws: Befehlsfehler.nichtUebernommen("Der Verteiler hat die Betriebsart nicht übernommen.")) {
            try await b.betriebsart(raum: "\(id)/1", heizen: false)
        }
        // Je Befehl drei Versuche, weil manche Befehle erst mit dem nächsten Regeldurchlauf wirken
        let gelesen = Netzattrappe.eingaenge(host: "verworfen.test").filter { $0.pfad == "/api/state" }.count
        #expect(gelesen >= 6)
    }

    /// Ein Befehl an einen fahrenden Kanal wartet, bis die Fahrt endet; zum Umkehren hält die App
    /// deshalb erst an.
    @Test func umkehrenHaeltErstAn() async throws {
        let g = try Verteilernachbildung()
        g.aendern { z, _ in Verteilernachbildung.element(&z["channels"], 2) { $0["op"] = "opening" } }
        let b = try await betrieb("umkehren.test", g)
        defer { b.beenden() }
        try await b.kanal(etage: id, nummer: 2, .zu)
        try await b.kanal(etage: id, nummer: 2, .auf)
        #expect(Netzattrappe.befehle(host: "umkehren.test") == [
            #"POST /api/channel/2/cmd {"cmd":"stop"}"#,
            #"POST /api/channel/2/cmd {"cmd":"close"}"#,
            #"POST /api/channel/2/cmd {"cmd":"open"}"#,
        ])
        #expect(b.staende[id]?.verteiler?.kanaele?.first { $0.id == 2 }?.handbetrieb == true)
        try await b.kanal(etage: id, nummer: 2, .regeln)
        #expect(b.staende[id]?.verteiler?.kanaele?.first { $0.id == 2 }?.handbetrieb == false)
    }

    /// Auf und Zu blieben an einem Kanal in der Messfahrt wirkungslos, Anhalten bräche sie ab.
    @Test func messfahrtBleibtUnberuehrt() async throws {
        let g = try Verteilernachbildung()
        g.aendern { z, _ in
            Verteilernachbildung.element(&z["channels"], 3) { $0["reserved"] = true }
            z["calib"]?["state"] = "running"
            z["calib"]?["channel"] = 3
        }
        let b = try await betrieb("messfahrt.test", g)
        defer { b.beenden() }
        await #expect(throws: Befehlsfehler.kanalBelegt(3)) { try await b.kanal(etage: id, nummer: 3, .auf) }
        await #expect(throws: Befehlsfehler.messfahrtLaeuft) { try await b.alleKanaele(etage: id, .anhalten) }
        #expect(Netzattrappe.befehle(host: "messfahrt.test").isEmpty)
        // „Alle zu“ bleibt erlaubt: Der Kanal in der Messfahrt nimmt es nicht an, und das ist kein Fehler.
        try await b.alleKanaele(etage: id, .zu)
        #expect(Netzattrappe.befehle(host: "messfahrt.test") == [#"POST /api/channel/all/cmd {"cmd":"close"}"#])
        let kanaele = b.staende[id]?.verteiler?.kanaele ?? []
        #expect(kanaele.filter { $0.handbetrieb == true }.count == kanaele.count - 1)
    }

    /// Vor dem Schreiben sichert die App das Gerät, danach vergleicht sie Wert für Wert.
    @Test func einstellungMitSicherungUndRuecklesen() async throws {
        let ordner = FileManager.default.temporaryDirectory.appending(path: "anlagenbetrieb-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: ordner) }
        let ablage = Sicherungsablage(verzeichnis: ordner, schluessel: SymmetricKey(size: .bits256))
        let g = try Verteilernachbildung()
        let b = try await betrieb("einstellung.test", g, sicherungen: ablage)
        defer { b.beenden() }

        try await b.konfigurationAendern(geraet: id, ["sensor_timeout_s": 900])
        let wege = Netzattrappe.eingaenge(host: "einstellung.test").map { "\($0.methode) \($0.pfad)" }
        let sicherung = try #require(wege.firstIndex(of: "GET /api/config/backup"))
        let schreiben = try #require(wege.firstIndex(of: "PUT /api/config"))
        #expect(sicherung < schreiben)
        #expect(wege[(schreiben + 1)...].contains("GET /api/config"), "zurückgelesen")
        #expect(g.jetzigeKonfiguration["sensor_timeout_s"]?.alsZahl == 900)
        #expect(try ablage.eintraege(geraet: id).count == 1)

        // Eine frische Sicherung genügt auch für die nächste Änderung.
        try await b.konfigurationAendern(geraet: id, ["sensor_timeout_s": 1200])
        #expect(try ablage.eintraege(geraet: id).count == 1)

        g.uebernimmt = false
        await #expect(throws: Befehlsfehler.nichtUebernommen("Das Gerät hat nicht alle Werte übernommen: sensor_timeout_s.")) {
            try await b.konfigurationAendern(geraet: id, ["sensor_timeout_s": 1800])
        }
    }

    /// Unveränderte Antworten erzeugen kein neues Bild; sonst baute die Oberfläche nach jeder
    /// Abfrage neu auf, und das Scrollen ruckelte.
    @Test func unveraenderteAbfragenOhneNeuesBild() async throws {
        let g = try Verteilernachbildung()
        let sitzung = Netzattrappe.sitzung(host: "ruhig.test") { g.antwort($0) }
        var takt = Self.takt
        takt.verteiler = .milliseconds(15)
        let b = Anlagenbetrieb(sitzung: sitzung, takt: takt)
        defer { b.beenden() }
        var bilder = 0
        b.neuesBild = { _ in bilder += 1 }
        b.abgleichen([BekanntesGeraet(id: id, art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://ruhig.test")!)])
        try await Task.sleep(for: .milliseconds(800))
        #expect(Netzattrappe.eingaenge(host: "ruhig.test").filter { $0.pfad.hasPrefix("/api/state") }.count > 20, "die Abfrage läuft weiter")
        // Einbinden, erste Antworten mit Zustand, Konfiguration und Thermometern: wenige Bilder.
        #expect(bilder <= 4, "\(bilder) Bilder")
        let vorher = bilder
        try await Task.sleep(for: .milliseconds(500))
        #expect(bilder == vorher, "ohne Änderung kein neues Bild")
        // Eine Änderung am Gerät kommt mit der nächsten Abfrage an.
        g.aendern { zustand, _ in
            var raeume = zustand["rooms"]?.alsListe ?? []
            raeume[0]["target_c"] = 23.5
            zustand["rooms"] = .liste(raeume)
        }
        try await Task.sleep(for: .milliseconds(400))
        #expect(bilder > vorher)
        #expect(b.bild?.etagen.first?.raeume.first?.soll == 23.5)
    }

    /// Ohne vorhandene Sicherung sichert die App ein Gerät bei der ersten Abfrage selbst.
    @Test func woechentlicheSicherung() async throws {
        let ordner = FileManager.default.temporaryDirectory.appending(path: "anlagenbetrieb-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: ordner) }
        let ablage = Sicherungsablage(verzeichnis: ordner, schluessel: SymmetricKey(size: .bits256))
        let g = try Verteilernachbildung()
        let sitzung = Netzattrappe.sitzung(host: "woechentlich.test") { g.antwort($0) }
        var takt = Self.takt
        takt.sicherungAbstand = .seconds(7 * 86_400)
        let b = Anlagenbetrieb(sitzung: sitzung, sicherungen: ablage, takt: takt)
        defer { b.beenden() }
        b.abgleichen([BekanntesGeraet(id: id, art: .verteiler, ort: "Erdgeschoss", adresse: URL(string: "http://woechentlich.test")!)])
        var runden = 0
        while (try ablage.eintraege(geraet: id)).isEmpty, runden < 300 {
            try await Task.sleep(for: .milliseconds(10))
            runden += 1
        }
        #expect(try ablage.eintraege(geraet: id).count == 1)
        // Die Prüfung sieht die Sicherung und meldet keine fehlende, sobald das nächste Bild
        // entstanden ist; die App fasst Bilder höchstens alle Viertelsekunde zusammen.
        runden = 0
        while b.bild?.befunde.contains(where: { $0.id == "sicherung:\(id)" }) != false, runden < 100 {
            try await Task.sleep(for: .milliseconds(10))
            runden += 1
        }
        #expect(b.bild?.befunde.contains { $0.id == "sicherung:\(id)" } == false)
    }

    /// Nach „Übernehmen“ bliebe die Messfahrt auf „fertig“ stehen; die App räumt wie die
    /// Weboberfläche mit „Verwerfen“ auf, aber erst, wenn die Werte gespeichert sind.
    @Test func messfahrtUebernehmen() async throws {
        let g = try Verteilernachbildung()
        g.aendern { z, _ in
            z["calib"]?["state"] = "done"
            z["calib"]?["channel"] = 4
        }
        let b = try await betrieb("uebernehmen.test", g)
        defer { b.beenden() }
        try await b.messfahrtUebernehmen(etage: id)
        #expect(Netzattrappe.befehle(host: "uebernehmen.test") == ["POST /api/calib/accept", "POST /api/calib/discard"])
        #expect(g.jetzigeKonfiguration["channels"]?.alsListe?.first { $0["id"]?.alsGanzzahl == 4 }?["calibrated"] == true)
        #expect(b.staende[id]?.verteiler?.messfahrt?.zustand == "idle")
    }

    @Test func messfahrtNichtUebernommenBleibtStehen() async throws {
        let g = try Verteilernachbildung()
        g.aendern { z, _ in
            z["calib"]?["state"] = "done"
            z["calib"]?["channel"] = 4
        }
        let b = try await betrieb("vorschlag.test", g)
        defer { b.beenden() }
        g.uebernimmt = false
        await #expect(throws: Befehlsfehler.nichtUebernommen("Der Verteiler hat die Werte der Messfahrt nicht übernommen.")) {
            try await b.messfahrtUebernehmen(etage: id)
        }
        // Der Vorschlag bleibt stehen, damit man ihn erneut übernehmen kann.
        #expect(Netzattrappe.befehle(host: "vorschlag.test") == ["POST /api/calib/accept"])
    }

    /// Im Hintergrund ruhen die laufenden Abfragen; eine einmalige Abfrage holt den neuen Stand
    /// trotzdem und meldet ein neues Bild.
    @Test func einmalAbfragen() async throws {
        let g = try Verteilernachbildung()
        let b = try await betrieb("einmal.test", g)
        defer { b.beenden() }
        var bilder = 0
        b.neuesBild = { _ in bilder += 1 }
        #expect(b.vollstaendigAbgefragt)
        g.aendern { z, _ in Verteilernachbildung.element(&z["rooms"], 1) { $0["temp_c"] = 23.5 } }
        await b.einmalAbfragen()
        #expect(b.staende[id]?.verteiler?.raeume?.first { $0.id == 1 }?.temperaturC == 23.5)
        #expect(bilder >= 1)
    }

    @Test func unbekanntesGeraet() async throws {
        let b = Anlagenbetrieb(sitzung: Netzattrappe.sitzung(host: "leer.test") { _ in .ok }, takt: Self.takt)
        await #expect(throws: Befehlsfehler.geraetUnbekannt) { try await b.sollwert(raum: "fbh_000000/1", 21) }
        await #expect(throws: Befehlsfehler.geraetUnbekannt) { try await b.sollwert(raum: "ohne-nummer", 21) }
        #expect(Netzattrappe.eingaenge(host: "leer.test").isEmpty)
    }
}
