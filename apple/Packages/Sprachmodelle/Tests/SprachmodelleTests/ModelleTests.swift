import Foundation
import Testing
@testable import Sprachmodelle

struct ModelleTests {
    private func seite(_ modelle: [(String, String, String)], weiter: String? = nil) -> Data {
        let eintraege = modelle.map { #"{"type":"model","id":"\#($0.0)","display_name":"\#($0.1)","created_at":"\#($0.2)"}"# }
        let rest = weiter.map { #""has_more":true,"last_id":"\#($0)""# } ?? #""has_more":false"#
        return Data(#"{"data":[\#(eintraege.joined(separator: ","))],\#(rest)}"#.utf8)
    }

    /// Das jüngste Modell jeder Reihe, unabhängig von der Reihenfolge; Fable und Haiku zählen nicht.
    @Test func neuestesJeReihe() async throws {
        let host = "modelle.test"
        let transport = APIAttrappe.transport(host: host) { _ in
            .init(kopf: ["Content-Type": "application/json"], rumpf: seite([
                ("claude-fable-5-1", "Claude Fable 5.1", "2026-09-20T00:00:00Z"),
                ("claude-sonnet-5", "Claude Sonnet 5", "2026-04-01T00:00:00Z"),
                ("claude-opus-5-5", "Claude Opus 5.5", "2026-09-01T00:00:00.000Z"),
                ("claude-sonnet-5-5", "Claude Sonnet 5.5", "2026-09-10T00:00:00Z"),
                ("claude-opus-5", "Claude Opus 5", "2026-05-01T00:00:00Z"),
                ("claude-haiku-4-5-20251001", "Claude Haiku 4.5", "2025-10-01T00:00:00Z"),
            ]))
        }
        let neueste = try await ClaudeModelle.neueste(
            schluessel: "sk-test", adresse: URL(string: "https://\(host)/v1/models")!, transport: transport)
        #expect(neueste[.opus]?.kennung == "claude-opus-5-5")
        #expect(neueste[.opus]?.name == "Claude Opus 5.5")
        #expect(neueste[.sonnet]?.kennung == "claude-sonnet-5-5")
        #expect(neueste[.sonnet]?.name == "Claude Sonnet 5.5")

        let eingang = try #require(APIAttrappe.eingaenge(host: host).first)
        #expect(eingang.kopf["x-api-key"] == "sk-test")
        #expect(eingang.kopf["anthropic-version"] == "2023-06-01")
    }

    /// Haiku mit Datum in der Kennung; die Fähigkeiten kommen aus `capabilities`.
    @Test func haikuMitFaehigkeiten() throws {
        let json = #"""
        {"data":[
          {"type":"model","id":"claude-haiku-4-5-20251001","display_name":"Claude Haiku 4.5","created_at":"2025-10-01T00:00:00Z",
           "max_input_tokens":200000,"capabilities":{"thinking":{"supported":true,"types":{"enabled":{"supported":true},"adaptive":{"supported":false}}},"effort":{"supported":false}}},
          {"type":"model","id":"claude-opus-5-5","display_name":"Claude Opus 5.5","created_at":"2026-09-01T00:00:00Z",
           "max_input_tokens":1000000,"capabilities":{"thinking":{"supported":true,"types":{"adaptive":{"supported":true}}},"effort":{"supported":true}}},
          {"type":"model","id":"claude-sonnet-5-5","display_name":"Claude Sonnet 5.5","created_at":"2026-09-10T00:00:00Z"}
        ],"has_more":false}
        """#
        let seite = try JSONDecoder.iso8601.decode(ClaudeModelle.Seite.self, from: Data(json.utf8))
        let neueste = ClaudeModelle.auswahl(seite.data)
        #expect(neueste[.haiku]?.kennung == "claude-haiku-4-5-20251001")
        #expect(neueste[.haiku]?.faehigkeiten == ClaudeFaehigkeiten(adaptivesDenken: false, aufwand: false, kontext: 200_000))
        #expect(neueste[.opus]?.faehigkeiten == ClaudeFaehigkeiten(adaptivesDenken: true, aufwand: true, kontext: 1_000_000))
        #expect(neueste[.sonnet]?.faehigkeiten == nil)
        #expect(ClaudeReihe(kennung: "claude-fable-5-1") == nil)
    }

    /// Ein neueres Modell auf der zweiten Seite gewinnt.
    @Test func blaettertWeiter() async throws {
        let host = "seiten.test"
        let transport = APIAttrappe.transport(host: host) { eingang in
            let zweite = eingang.url?.query()?.contains("after_id=claude-opus-5") == true
            return .init(kopf: ["Content-Type": "application/json"], rumpf: zweite
                ? seite([("claude-opus-6", "Claude Opus 6", "2027-02-01T00:00:00Z")])
                : seite([("claude-opus-5", "Claude Opus 5", "2026-05-01T00:00:00Z")], weiter: "claude-opus-5"))
        }
        let neueste = try await ClaudeModelle.neueste(
            schluessel: "sk-test", adresse: URL(string: "https://\(host)/v1/models")!, transport: transport)
        #expect(neueste[.opus]?.kennung == "claude-opus-6")
        #expect(neueste[.sonnet] == nil)
        #expect(APIAttrappe.eingaenge(host: host).count == 2)
    }

    @Test func abgewiesenerSchluessel() async throws {
        let host = "abgewiesen.test"
        let transport = APIAttrappe.transport(host: host) { _ in
            .init(status: 401, kopf: ["Content-Type": "application/json"],
                  rumpf: Data(#"{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}"#.utf8))
        }
        await #expect(throws: ClaudeFehler.schluesselUngueltig) {
            try await ClaudeModelle.neueste(schluessel: "sk-falsch", adresse: URL(string: "https://\(host)/v1/models")!, transport: transport)
        }
    }
}

extension JSONDecoder {
    static var iso8601: JSONDecoder {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }
}
