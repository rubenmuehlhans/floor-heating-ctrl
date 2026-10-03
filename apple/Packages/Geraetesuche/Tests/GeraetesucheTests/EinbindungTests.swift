import Foundation
import Geraeteschnittstelle
import Testing
@testable import Geraetesuche

struct KurznameTests {
    @Test(arguments: [
        ("Erdgeschoss", "erdgeschoss"),
        ("Küche / Bad", "kueche-bad"),
        ("Größe 2", "groesse-2"),
        ("  -Keller- ", "keller"),
        ("", "anlage"),
        ("!!!", "anlage"),
        ("Obergeschoß Süd", "obergeschoss-sued"),
    ])
    func wieDieWeboberflaeche(_ ort: String, _ erwartet: String) {
        #expect(Einbindung.kurzname(ort) == erwartet)
    }
}

struct KonfigurationTests {
    @Test func leitstandIstKeineGeraeteart() throws {
        let zustand = try JSONDecoder().decode(JSONWert.self, from: Data(#"{"device":{"id":"lst_c0ffee","role":"station","site":"Leitstand"}}"#.utf8))
        #expect(Einbindung.istLeitstand(zustand))
        #expect(Einbindung.art(zustand) == nil)
    }

    @Test func leitstandWirdEingebunden() throws {
        let zustand = try JSONDecoder().decode(JSONWert.self, from: Data(#"{"device":{"id":"lst_c0ffee","role":"station"}}"#.utf8))
        #expect(Einbindung.einbindungsart(zustand) == .leitstand)
        #expect(Einbindung.praefix(.leitstand) == "leitstand-")
        #expect(Einbindung.werkskennwort(.leitstand) == "")
        #expect(Einbindung.werkskennwort(.heizung) == Einbindung.werkskennwort)
    }

    /// Der Leitstand behält `leitstand`; einen MQTT-Präfix hat er nicht.
    @Test func leitstandBehaeltSeinenNamen() {
        let g = Einbindung.Geraet(art: .leitstand, hostname: "leitstand")
        let t = Einbindung.konfiguration(g, netz: "Heimnetz", kennwort: "geheim123", ort: "Heizungsraum")
        #expect(t == [
            "site": "Heizungsraum",
            "wifi": ["ssid": "Heimnetz", "pass": "geheim123"],
        ])
    }

    @Test func verteilerAbWerk() {
        let g = Einbindung.Geraet(art: .verteiler, hostname: "floor-heating", mqttPraefix: "fbh")
        let t = Einbindung.konfiguration(g, netz: "Heimnetz", kennwort: "geheim123", ort: "Erdgeschoss")
        #expect(t == [
            "site": "Erdgeschoss",
            "wifi": ["ssid": "Heimnetz", "pass": "geheim123", "hostname": "floor-heating-erdgeschoss"],
            "mqtt": ["prefix": "fbh_erdgeschoss"],
        ])
    }

    @Test func heizungsgeraetAbWerk() {
        let g = Einbindung.Geraet(art: .heizung, hostname: "heizung", mqttPraefix: "heiz")
        let t = Einbindung.konfiguration(g, netz: "Heimnetz", kennwort: "x", ort: "Kessel")
        #expect(t["wifi"]?["hostname"] == "heizung-kessel")
        #expect(t["mqtt"] == ["prefix": "heiz_kessel"])
    }

    /// Wer schon einen eigenen Namen vergeben hat, behält ihn.
    @Test func eigenerNameBleibt() {
        let g = Einbindung.Geraet(art: .verteiler, hostname: "fbh-oben", mqttPraefix: "haus_oben")
        let t = Einbindung.konfiguration(g, netz: "Heimnetz", kennwort: "x", ort: "Obergeschoss")
        #expect(t["wifi"]?["hostname"] == nil)
        #expect(t["mqtt"] == nil)
    }

    /// Ein leeres Kennwort würde das gespeicherte löschen; es geht deshalb gar nicht erst mit.
    @Test func leeresKennwortGehtNichtMit() {
        let g = Einbindung.Geraet(art: .verteiler, hostname: "floor-heating")
        let t = Einbindung.konfiguration(g, netz: "Gastnetz", kennwort: "", ort: "Keller")
        #expect(t["wifi"]?["pass"] == nil)
        #expect(t["wifi"]?["ssid"] == "Gastnetz")
    }

    @Test func netzeGeordnet() {
        let netze = [
            Netzsuche.Netz(name: "Nachbar", signal: -80, verschluesselt: true),
            Netzsuche.Netz(name: "Heimnetz", signal: -70, verschluesselt: true),
            Netzsuche.Netz(name: "Heimnetz", signal: -52, verschluesselt: true),
            Netzsuche.Netz(name: "", signal: -40, verschluesselt: false),
        ]
        let g = Einbindung.geordnet(netze)
        #expect(g.map(\.name) == ["Heimnetz", "Nachbar"])
        #expect(g.first?.signal == -52)
    }
}

/// Ablauf gegen ein vorgetäuschtes Gerät, das die Zustände der Firmware durchläuft:
/// Zugangspunkt, Suchlauf, neue Zugangsdaten, Verbindung.
struct AblaufTests {
    @Test func vomZugangspunktInsHeimnetz() async throws {
        let geraet = Geraetezustand()
        let einbindung = Einbindung(
            adresse: URL(string: "http://ablauf.test")!, sitzung: Geraetestand.sitzung(host: "ablauf.test", geraet), takt: .milliseconds(10))

        let g = try await einbindung.erkennen()
        #expect(g.art == .verteiler)
        #expect(g.id == "fbh_a1b2c3")
        #expect(g.hostname == "floor-heating")

        let netze = try await einbindung.netzeSuchen()
        #expect(netze.first?.name == "Heimnetz")

        try await einbindung.einrichten(g, netz: "Heimnetz", kennwort: "geheim123", ort: "Erdgeschoss")
        #expect(geraet.gespeichert?["wifi"]?["hostname"] == "floor-heating-erdgeschoss")

        let ip = try await einbindung.imHeimnetz(.verteiler, zeitgrenze: .seconds(10))
        #expect(ip == "192.168.0.213")
    }

    @Test func falschesKennwort() async throws {
        let geraet = Geraetezustand()
        geraet.verbindetNie = true
        let einbindung = Einbindung(
            adresse: URL(string: "http://kennwort.test")!, sitzung: Geraetestand.sitzung(host: "kennwort.test", geraet), takt: .milliseconds(10))
        await #expect(throws: Einbindungsfehler.self) {
            _ = try await einbindung.imHeimnetz(.verteiler, zeitgrenze: .milliseconds(1500))
        }
    }

    @Test func keinGeraetErreichbar() async {
        let einbindung = Einbindung(adresse: URL(string: "http://127.0.0.1:1")!, takt: .milliseconds(50))
        await #expect(throws: Einbindungsfehler.keinGeraet) {
            _ = try await einbindung.erkennen(zeitgrenze: .milliseconds(300))
        }
    }
}

/// Zustand eines Verteilers im Einrichtungsbetrieb, wie ihn die Firmware meldet.
final class Geraetezustand: @unchecked Sendable {
    private let sperre = NSLock()
    private var suchlaufAbfragen = 0
    private var verbindetAb: Date?
    private var _gespeichert: JSONWert?
    var verbindetNie = false

    var gespeichert: JSONWert? { sperre.withLock { _gespeichert } }

    func antwort(_ methode: String, _ pfad: String, _ rumpf: Data) -> (Int, String) {
        sperre.withLock {
            switch (methode, pfad) {
            case ("GET", "/api/state"):
                let verbunden = verbindetAb.map { Date() >= $0 } ?? false
                return (200, verbunden
                    ? #"{"device":{"id":"fbh_a1b2c3","site":""},"net":{"connected":true,"ip":"192.168.0.213","ap_active":true,"ap_ip":"192.168.4.1"},"rooms":[]}"#
                    : #"{"device":{"id":"fbh_a1b2c3","site":""},"net":{"connected":false,"ip":"","ap_active":true,"ap_ip":"192.168.4.1"},"rooms":[]}"#)
            case ("GET", "/api/config"):
                return (200, #"{"site":"","wifi":{"ssid":"","hostname":"floor-heating","pass_set":false},"mqtt":{"prefix":"fbh"}}"#)
            case ("POST", "/api/wifi/scan"):
                suchlaufAbfragen = 0
                return (200, #"{"ok":true}"#)
            case ("GET", "/api/wifi/scan"):
                suchlaufAbfragen += 1
                // Während des Suchlaufs ist der Zugangspunkt kurz stumm.
                if suchlaufAbfragen == 1 { return (503, "") }
                if suchlaufAbfragen < 3 { return (200, #"{"running":true,"networks":[]}"#) }
                return (200, #"{"running":false,"networks":[{"ssid":"Nachbar","rssi":-81,"secure":true},{"ssid":"Heimnetz","rssi":-55,"secure":true}]}"#)
            case ("PUT", "/api/config"):
                _gespeichert = try? JSONWert.lesen(rumpf)
                if !verbindetNie { verbindetAb = Date().addingTimeInterval(0.2) }
                return (200, #"{"ok":true}"#)
            default:
                return (404, #"{"ok":false,"error":"Unbekannt"}"#)
            }
        }
    }
}

/// Leitet Anfragen je Host an einen Gerätezustand.
final class Geraetestand: URLProtocol, @unchecked Sendable {
    nonisolated(unsafe) private static var geraete: [String: Geraetezustand] = [:]
    private static let sperre = NSLock()

    static func sitzung(host: String, _ geraet: Geraetezustand) -> URLSession {
        sperre.withLock { geraete[host] = geraet }
        let k = URLSessionConfiguration.ephemeral
        k.protocolClasses = [Geraetestand.self]
        return URLSession(configuration: k)
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        guard let url = request.url, let host = url.host(),
              let geraet = Self.sperre.withLock({ Self.geraete[host] }) else { return }
        var rumpf = request.httpBody ?? Data()
        if rumpf.isEmpty, let strom = request.httpBodyStream {
            strom.open()
            var puffer = [UInt8](repeating: 0, count: 8192)
            while strom.hasBytesAvailable {
                let n = strom.read(&puffer, maxLength: puffer.count)
                if n <= 0 { break }
                rumpf.append(puffer, count: n)
            }
            strom.close()
        }
        let (status, text) = geraet.antwort(request.httpMethod ?? "GET", url.path(), rumpf)
        let http = HTTPURLResponse(url: url, statusCode: status, httpVersion: "HTTP/1.1", headerFields: ["Content-Type": "application/json"])!
        client?.urlProtocol(self, didReceive: http, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Data(text.utf8))
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}
