import Foundation
import Geraeteschnittstelle
import Network
import Testing
@testable import Geraetesuche

struct AuswertungTests {
    private let endpunkt = NWEndpoint.service(name: "Kessel", type: "_fbhctrl._tcp", domain: "local.", interface: nil)

    @Test func vollstaendigerEintrag() {
        let g = Geraetesuche.auswerten(name: "Kessel", txt: ["id": "heiz_19c8a2", "site": "Kessel", "role": "heat"], endpunkt: endpunkt)
        #expect(g.id == "heiz_19c8a2")
        #expect(g.ort == "Kessel")
        #expect(g.art == .heizung)
    }

    @Test func aeltereFirmwareOhneRolle() {
        let g = Geraetesuche.auswerten(name: "Keller", txt: ["id": "fbh_3a91c4", "site": "Keller", "role": ""], endpunkt: endpunkt)
        #expect(g.art == .verteiler)
    }

    /// Der Leitstand ist kein Regelgerät: ohne Art, aber als Leitstand erkennbar.
    @Test func leitstand() {
        let g = Geraetesuche.auswerten(name: "Leitstand", txt: ["id": "lst_c0ffee", "site": "Leitstand", "role": "station"], endpunkt: endpunkt)
        #expect(g.art == nil)
        #expect(g.rolle == "station")
        #expect(g.istLeitstand)
        let k = Geraetesuche.auswerten(name: "Kessel", txt: ["id": "heiz_19c8a2", "site": "Kessel", "role": "heat"], endpunkt: endpunkt)
        #expect(!k.istLeitstand)
    }

    @Test func ohneTXT() {
        let g = Geraetesuche.auswerten(name: "floor-heating", txt: [:], endpunkt: endpunkt)
        #expect(g.id == "floor-heating")
        #expect(g.art == nil)
        #expect(g.ort == nil)
    }

    @Test func adresseAusEndpunkt() {
        let v4 = NWEndpoint.hostPort(host: .ipv4(IPv4Address("192.168.0.213")!), port: 80)
        #expect(Geraetesuche.url(aus: v4) == URL(string: "http://192.168.0.213:80"))
        let lokal = NWEndpoint.hostPort(host: .ipv4(.loopback), port: 8321)
        #expect(Geraetesuche.url(aus: lokal)?.host() == "127.0.0.1")
    }

    /// So kommt eine aufgelöste Adresse aus NWConnection zurück: mit Schnittstelle.
    @Test func adresseMitSchnittstelle() throws {
        let mitSchnittstelle = try #require(IPv4Address("192.168.0.213%lo0"))
        let url = Geraetesuche.url(aus: .hostPort(host: .ipv4(mitSchnittstelle), port: 80))
        #expect(url == URL(string: "http://192.168.0.213:80"))
        let v6 = try #require(IPv6Address("fe80::1%lo0"))
        #expect(Geraetesuche.url(aus: .hostPort(host: .ipv6(v6), port: 80)) != nil)
    }
}

/// Findet die Attrappen, die `apple/Werkzeuge/attrappen.sh` lokal per Bonjour anmeldet.
@Suite(.enabled(if: ProcessInfo.processInfo.environment["ATTRAPPEN"] == "1"))
@MainActor
struct SucheGegenAttrappenTests {
    @Test func findetUndLoestAuf() async throws {
        let suche = Geraetesuche()
        suche.starten()
        defer { suche.beenden() }
        let erwartet: Set<String> = ["fbh_a1b2c3", "heiz_3f21ac", "heiz_9a1b2c"]
        for _ in 0..<50 {
            let aufgeloest = suche.geraete.filter { erwartet.contains($0.id) && $0.adresse != nil }
            if aufgeloest.count == erwartet.count { break }
            try await Task.sleep(for: .milliseconds(100))
        }
        #expect(suche.zustand == .sucht, "Zustand der Suche: \(suche.zustand), gefunden: \(suche.geraete.map(\.name))")
        let attrappen = suche.geraete.filter { erwartet.contains($0.id) }
        #expect(Set(attrappen.map(\.id)) == erwartet)
        let alleLokal = attrappen.allSatisfy { $0.istAttrappe }
        #expect(alleLokal)
        #expect(attrappen.first { $0.id == "fbh_a1b2c3" }?.adresse?.port == 8321)
        #expect(attrappen.first { $0.id == "heiz_9a1b2c" }?.art == .heizung)
    }
}
