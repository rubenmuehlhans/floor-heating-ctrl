import Foundation
import FoundationModels
import Testing
@testable import Sprachmodelle

/// Der Durchstich: eine echte `LanguageModelSession` mit Claude als Modell und einem Werkzeug.
/// Die API ist vorgetäuscht; geprüft wird, dass die Sitzung das Werkzeug ausführt und das
/// Ergebnis samt signiertem Denkschritt in der zweiten Anfrage zurückschickt.
struct SitzungTests {
    @Test func werkzeugschleifeUeberDieSitzung() async throws {
        let host = "schleife.test"
        let modell = APIAttrappe.modell(host: host) { eingang in
            let rumpf = String(decoding: eingang.rumpf, as: UTF8.self)
            if rumpf.contains("tool_result") {
                return .init(rumpf: SSE.strom([SSE.beginn] + SSE.text(0, ["Der Speicher ", "hat 53 °C."]) + SSE.ende("end_turn")))
            }
            return .init(rumpf: SSE.strom(
                [SSE.beginn] + SSE.denken(0, "Ich lese den Zustand.", signatur: "sig-1")
                    + SSE.werkzeug(1, id: "toolu_1", name: "anlage_status", argumente: [#"{"bereich":"heizung"}"#])
                    + SSE.ende("tool_use")))
        }
        let sitzung = LanguageModelSession(model: modell, tools: [Statuswerkzeug()]) {
            "Antworten Sie knapp und in Sie-Form."
        }

        let antwort = try await sitzung.respond(to: "Wie warm ist der Speicher?")
        #expect(antwort.content == "Der Speicher hat 53 °C.")

        let eingaenge = APIAttrappe.eingaenge(host: host)
        try #require(eingaenge.count == 2)
        let erste = try #require(eingaenge[0].json)
        #expect(eingaenge[0].kopf["x-api-key"] == "sk-test")
        #expect(eingaenge[0].kopf["anthropic-version"] == "2023-06-01")
        #expect(eingaenge[0].kopf["anthropic-beta"] == "server-side-fallback-2026-07-01")
        #expect(erste["system"]?[0]?["text"] == "Antworten Sie knapp und in Sie-Form.")
        #expect(erste["tools"]?[0]?["name"] == "anlage_status")

        // Zweite Anfrage: Denkschritt mit Signatur und Aufruf zurück, Ergebnis des Werkzeugs dazu.
        let zweite = try #require(eingaenge[1].json)
        guard case .liste(let nachrichten)? = zweite["messages"] else {
            Issue.record("keine Nachrichten")
            return
        }
        #expect(nachrichten.count == 3)
        #expect(nachrichten[1]["content"]?[0] == ["type": "thinking", "thinking": "Ich lese den Zustand.", "signature": "sig-1"])
        #expect(nachrichten[1]["content"]?[1]?["type"] == "tool_use")
        let ergebnis = try #require(nachrichten[2]["content"]?[0])
        #expect(ergebnis["type"] == "tool_result")
        #expect(ergebnis["tool_use_id"] == "toolu_1")
        #expect(ergebnis["content"]?[0]?["text"]?.alsText?.contains(#""puffer_c":53"#) == true)
    }

    /// Ohne Ersatzmodell geht auch die Kopfzeile der Beta nicht mit.
    @Test func sonnetOhneBetaKopfzeile() async throws {
        let host = "sonnet.test"
        let modell = ClaudeSprachmodell(konfiguration: ClaudeKonfiguration(
            schluessel: "sk-test", modell: ClaudeKonfiguration.sonnet, adresse: URL(string: "https://\(host)/v1/messages")!,
            transport: APIAttrappe.transport(host: host) { _ in
                .init(rumpf: SSE.strom([SSE.beginn] + SSE.text(0, ["OK"]) + SSE.ende("end_turn")))
            }))
        let sitzung = LanguageModelSession(model: modell) { "Anweisungen" }
        #expect(try await sitzung.respond(to: "?").content == "OK")
        let eingang = try #require(APIAttrappe.eingaenge(host: host).first)
        #expect(eingang.kopf["anthropic-beta"] == nil)
        #expect(eingang.json?["model"] == "claude-sonnet-5")
    }

    @Test func ablehnungKommtAlsFehlerDerSitzung() async throws {
        let modell = APIAttrappe.modell(host: "ablehnung.test") { _ in
            .init(rumpf: SSE.strom([SSE.beginn] + SSE.ende("refusal", details: #"{"category":"cyber","explanation":null}"#)))
        }
        let sitzung = LanguageModelSession(model: modell) { "Anweisungen" }
        do {
            _ = try await sitzung.respond(to: "?")
            Issue.record("Ablehnung erwartet")
        } catch let fehler as LanguageModelError {
            guard case .refusal = fehler else {
                Issue.record("falscher Fehler: \(fehler)")
                return
            }
        }
    }

    @Test func falscherSchluessel() async throws {
        let modell = APIAttrappe.modell(host: "schluessel.test") { _ in
            .init(status: 401, kopf: ["Content-Type": "application/json"],
                  rumpf: Data(#"{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}"#.utf8))
        }
        let sitzung = LanguageModelSession(model: modell) { "Anweisungen" }
        await #expect(throws: ClaudeFehler.schluesselUngueltig) {
            _ = try await sitzung.respond(to: "?")
        }
    }

    @Test func ratenbegrenzung() async throws {
        let modell = APIAttrappe.modell(host: "rate.test") { _ in
            .init(status: 429, kopf: ["Content-Type": "application/json", "retry-after": "30"],
                  rumpf: Data(#"{"type":"error","error":{"type":"rate_limit_error","message":"Rate limited"}}"#.utf8))
        }
        let sitzung = LanguageModelSession(model: modell) { "Anweisungen" }
        do {
            _ = try await sitzung.respond(to: "?")
            Issue.record("Ratenbegrenzung erwartet")
        } catch let fehler as LanguageModelError {
            guard case .rateLimited(let details) = fehler else {
                Issue.record("falscher Fehler: \(fehler)")
                return
            }
            #expect(details.resetDate != nil)
        }
    }
}

/// Überlastung und Serverfehler: Die App wiederholt, solange noch nichts angekommen ist.
struct WiederholungTests {
    private static let ueberlastet = #"{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}"#
    private static let fertig = SSE.strom([SSE.beginn] + SSE.text(0, ["OK"]) + SSE.ende("end_turn"))

    private func modell(_ host: String, _ tabelle: @escaping @Sendable (APIAttrappe.Eingang) -> APIAttrappe.Antwort) -> ClaudeSprachmodell {
        ClaudeSprachmodell(konfiguration: ClaudeKonfiguration(
            schluessel: "sk-test", adresse: URL(string: "https://\(host)/v1/messages")!,
            wartezeiten: [.zero, .zero], transport: APIAttrappe.transport(host: host, tabelle)))
    }

    /// So kam es am 22.09. bei der Verbindungsprüfung: HTTP 200, dann das Fehlerereignis.
    @Test func ueberlastungImStrom() async throws {
        let host = "strom-ueberlast.test"
        let sitzung = LanguageModelSession(model: modell(host) { _ in
            APIAttrappe.eingaenge(host: host).count == 1
                ? .init(rumpf: SSE.strom([SSE.beginn, Self.ueberlastet]))
                : .init(rumpf: Self.fertig)
        }) { "Anweisungen" }
        let antwort = try await sitzung.respond(to: "?")
        #expect(antwort.content == "OK")
        #expect(APIAttrappe.eingaenge(host: host).count == 2)
    }

    @Test func ueberlastungAlsStatus() async throws {
        let host = "status-ueberlast.test"
        let sitzung = LanguageModelSession(model: modell(host) { _ in
            APIAttrappe.eingaenge(host: host).count == 1
                ? .init(status: 529, kopf: ["Content-Type": "application/json"], rumpf: Data(Self.ueberlastet.utf8))
                : .init(rumpf: Self.fertig)
        }) { "Anweisungen" }
        let antwort = try await sitzung.respond(to: "?")
        #expect(antwort.content == "OK")
        #expect(APIAttrappe.eingaenge(host: host).count == 2)
    }

    /// Bleibt es überlastet, gibt es nach zwei Wiederholungen eine deutsche Meldung.
    @Test func dauerhafteUeberlastung() async throws {
        let host = "dauer-ueberlast.test"
        let sitzung = LanguageModelSession(model: modell(host) { _ in
            .init(rumpf: SSE.strom([SSE.beginn, Self.ueberlastet]))
        }) { "Anweisungen" }
        await #expect(throws: ClaudeFehler.ueberlastet(status: 529)) {
            _ = try await sitzung.respond(to: "?")
        }
        #expect(APIAttrappe.eingaenge(host: host).count == 3)
        #expect(ClaudeFehler.ueberlastet(status: 529).localizedDescription.hasPrefix("Claude ist gerade überlastet"))
    }

    /// Steht schon Text im Kanal, würde eine Wiederholung ihn doppeln.
    @Test func keineWiederholungNachBeginnDerAntwort() async throws {
        let host = "halb-ueberlast.test"
        let sitzung = LanguageModelSession(model: modell(host) { _ in
            .init(rumpf: SSE.strom([SSE.beginn, #"{"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}"#,
                                    #"{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Der Spei"}}"#,
                                    Self.ueberlastet]))
        }) { "Anweisungen" }
        await #expect(throws: ClaudeFehler.ueberlastet(status: 529)) {
            _ = try await sitzung.respond(to: "?")
        }
        #expect(APIAttrappe.eingaenge(host: host).count == 1)
    }
}
