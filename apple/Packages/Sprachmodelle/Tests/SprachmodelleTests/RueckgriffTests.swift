import Foundation
import FoundationModels
import Synchronization
import Testing
@testable import Sprachmodelle

/// Ein neues Modell, das die Anfrageform nicht annimmt: Die Anfrage geht an das bewährte.
struct RueckgriffTests {
    private final class Protokoll: Sendable {
        let ereignisse = Mutex<[ClaudeBeobachter.Ereignis]>([])
        var alle: [ClaudeBeobachter.Ereignis] { ereignisse.withLock { $0 } }
    }

    private func konfiguration(host: String, rueckgriff: Bool = true, beobachter: ClaudeBeobachter,
                               opus6: APIAttrappe.Antwort, opus55: APIAttrappe.Antwort) -> ClaudeKonfiguration {
        ClaudeKonfiguration(
            schluessel: "sk-test", modell: "claude-opus-6",
            rueckgriff: rueckgriff ? ClaudeModelleintrag(kennung: "claude-opus-5-5", name: "Claude Opus 5.5") : nil,
            beobachter: beobachter, adresse: URL(string: "https://\(host)/v1/messages")!,
            transport: APIAttrappe.transport(host: host) { eingang in
                eingang.json?["model"] == "claude-opus-6" ? opus6 : opus55
            })
    }

    private static let abgewiesen = APIAttrappe.Antwort(
        status: 400, kopf: ["Content-Type": "application/json"],
        rumpf: Data(#"{"type":"error","error":{"type":"invalid_request_error","message":"thinking.display: unknown field"}}"#.utf8))
    private static let ok = APIAttrappe.Antwort(rumpf: SSE.strom([SSE.beginn] + SSE.text(0, ["OK"]) + SSE.ende("end_turn")))

    @Test func abgewiesenGehtAnDasBewaehrteModell() async throws {
        let host = "rueckgriff.test"
        let protokoll = Protokoll()
        let beobachter = ClaudeBeobachter { e in protokoll.ereignisse.withLock { $0.append(e) } }
        let k = konfiguration(host: host, beobachter: beobachter, opus6: Self.abgewiesen, opus55: Self.ok)

        let sitzung = LanguageModelSession(model: ClaudeSprachmodell(konfiguration: k)) { "Anweisungen" }
        #expect(try await sitzung.respond(to: "?").content == "OK")
        #expect(APIAttrappe.eingaenge(host: host).compactMap { $0.json?["model"]?.alsText } == ["claude-opus-6", "claude-opus-5-5"])
        #expect(protokoll.alle == [.abgewiesen("claude-opus-6", meldung: "thinking.display: unknown field"), .bewaehrt("claude-opus-5-5")])

        // Die nächste Anfrage geht gleich an das bewährte Modell.
        #expect(try await sitzung.respond(to: "?").content == "OK")
        #expect(APIAttrappe.eingaenge(host: host).compactMap { $0.json?["model"]?.alsText }.last == "claude-opus-5-5")
        #expect(APIAttrappe.eingaenge(host: host).count == 3)
    }

    /// Weist auch das bewährte Modell ab, lag es an der Anfrage; abgewiesen ist dann keines.
    @Test func scheitertAuchDerRueckgriffBleibtDasModell() async throws {
        let host = "beide.test"
        let protokoll = Protokoll()
        let beobachter = ClaudeBeobachter { e in protokoll.ereignisse.withLock { $0.append(e) } }
        let k = konfiguration(host: host, beobachter: beobachter, opus6: Self.abgewiesen, opus55: Self.abgewiesen)
        let sitzung = LanguageModelSession(model: ClaudeSprachmodell(konfiguration: k)) { "Anweisungen" }
        await #expect(throws: ClaudeFehler.self) { try await sitzung.respond(to: "?") }
        #expect(APIAttrappe.eingaenge(host: host).count == 2)
        #expect(protokoll.alle.isEmpty)
        #expect(!beobachter.istAbgewiesen("claude-opus-6"))
    }

    @Test func ohneRueckgriffBleibtDerFehler() async throws {
        let host = "ohne.test"
        let protokoll = Protokoll()
        let beobachter = ClaudeBeobachter { e in protokoll.ereignisse.withLock { $0.append(e) } }
        let k = konfiguration(host: host, rueckgriff: false, beobachter: beobachter, opus6: Self.abgewiesen, opus55: Self.ok)
        let sitzung = LanguageModelSession(model: ClaudeSprachmodell(konfiguration: k)) { "Anweisungen" }
        await #expect(throws: ClaudeFehler.self) { try await sitzung.respond(to: "?") }
        #expect(APIAttrappe.eingaenge(host: host).count == 1)
    }
}
