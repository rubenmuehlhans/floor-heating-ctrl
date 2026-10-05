import Foundation
import FoundationModels
import Testing
@testable import Sprachmodelle

struct AnfrageTests {
    private let konfiguration = ClaudeKonfiguration(schluessel: "sk-test")
    private let definition = Transcript.ToolDefinition(
        name: "anlage_status", description: "Liest den Zustand der Anlage.", parameters: StatusArgumente.generationSchema)

    private func anfrage(
        _ eintraege: [Transcript.Entry], werkzeuge: [Transcript.ToolDefinition] = [],
        stufe: ContextOptions.ReasoningLevel? = nil, schema: GenerationSchema? = nil,
        optionen: GenerationOptions = GenerationOptions()
    ) -> LanguageModelExecutorGenerationRequest {
        LanguageModelExecutorGenerationRequest(
            id: UUID(), transcript: Transcript(entries: eintraege), enabledTools: werkzeuge, schema: schema,
            generationOptions: optionen, contextOptions: ContextOptions(reasoningLevel: stufe), metadata: [:])
    }

    private func text(_ t: String) -> Transcript.Segment {
        .text(.init(content: t))
    }

    @Test func gespraechMitWerkzeugaufruf() throws {
        let eintraege: [Transcript.Entry] = [
            .instructions(.init(segments: [text("Antworten Sie knapp.")], toolDefinitions: [definition])),
            .prompt(.init(segments: [text("Wie warm ist der Speicher?")])),
            .reasoning(.init(segments: [text("Ich lese den Zustand.")], signature: Data("sig-1".utf8))),
            .response(.init(segments: [text("Ich sehe nach.")])),
            .toolCalls(.init([Transcript.ToolCall(id: "toolu_1", toolName: "anlage_status", arguments: try GeneratedContent(json: #"{"bereich":"heizung"}"#))])),
            .toolOutput(.init(id: "toolu_1", toolName: "anlage_status", segments: [text(#"{"puffer_c":53.0}"#)])),
        ]
        let r = try ClaudeAnfrage(anfrage: anfrage(eintraege, werkzeuge: [definition], stufe: .moderate), konfiguration: konfiguration).rumpf()

        #expect(r["model"] == "claude-opus-5-5")
        #expect(r["max_tokens"] == 64000)
        #expect(r["stream"] == true)
        #expect(r["thinking"] == ["type": "adaptive", "display": "summarized"])
        #expect(r["output_config"] == ["effort": "medium"])
        #expect(r["fallbacks"] == "default")
        #expect(r["cache_control"] == ["type": "ephemeral"])
        #expect(r["system"] == [["type": "text", "text": "Antworten Sie knapp.", "cache_control": ["type": "ephemeral"]]])
        #expect(r["tool_choice"] == nil)

        let nachrichten = try #require(r["messages"])
        #expect(nachrichten == [
            ["role": "user", "content": [["type": "text", "text": "Wie warm ist der Speicher?"]]],
            ["role": "assistant", "content": [
                ["type": "thinking", "thinking": "Ich lese den Zustand.", "signature": "sig-1"],
                ["type": "text", "text": "Ich sehe nach."],
                ["type": "tool_use", "id": "toolu_1", "name": "anlage_status", "input": ["bereich": "heizung"]],
            ]],
            ["role": "user", "content": [
                ["type": "tool_result", "tool_use_id": "toolu_1", "content": [["type": "text", "text": #"{"puffer_c":53.0}"#]]],
            ]],
        ])
    }

    /// Sonnet 5.5 hat dieselbe Anfrageform wie Opus 5.5, das Ersatzmodell in der Form `default`
    /// eingeschlossen.
    @Test func sonnetMitErsatzmodell() throws {
        let sonnet = ClaudeKonfiguration(schluessel: "sk-test", modell: ClaudeKonfiguration.sonnet)
        let eintraege: [Transcript.Entry] = [
            .instructions(.init(segments: [text("Antworten Sie knapp.")], toolDefinitions: [])),
            .prompt(.init(segments: [text("Wie warm ist der Speicher?")])),
        ]
        let r = try ClaudeAnfrage(anfrage: anfrage(eintraege, stufe: .moderate), konfiguration: sonnet).rumpf()
        #expect(r["model"] == "claude-sonnet-5-5")
        #expect(r["fallbacks"] == "default")
        #expect(r["thinking"] == ["type": "adaptive", "display": "summarized"])
        #expect(r["output_config"] == ["effort": "medium"])
        #expect(r["temperature"] == nil)
    }

    /// Haiku 4.5 kennt weder adaptives Denken noch Aufwand und kein Ersatzmodell.
    @Test func haikuOhneDenkenUndAufwand() throws {
        let haiku = ClaudeKonfiguration(schluessel: "sk-test", modell: "claude-haiku-4-5")
        let r = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))], stufe: .moderate), konfiguration: haiku).rumpf()
        #expect(r["model"] == "claude-haiku-4-5")
        #expect(r["thinking"] == nil)
        #expect(r["output_config"] == nil)
        #expect(r["fallbacks"] == nil)
        #expect(r["cache_control"] == ["type": "ephemeral"])
    }

    /// Die Fähigkeiten laut API gehen der Vermutung nach der Kennung vor.
    @Test func faehigkeitenLautAPI() throws {
        let haiku5 = ClaudeKonfiguration(schluessel: "sk-test", modell: "claude-haiku-5",
                                         faehigkeiten: ClaudeFaehigkeiten(adaptivesDenken: true, aufwand: true))
        let r = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))], stufe: .light), konfiguration: haiku5).rumpf()
        #expect(r["thinking"] == ["type": "adaptive", "display": "summarized"])
        #expect(r["output_config"] == ["effort": "low"])
        #expect(ClaudeFaehigkeiten.vermutet("claude-haiku-4-5-20251001").adaptivesDenken == false)
        #expect(ClaudeFaehigkeiten.vermutet("claude-opus-6").adaptivesDenken)
    }

    @Test(arguments: [
        ("claude-opus-5-5", true), ("claude-opus-5", true), ("claude-opus-6", true), ("claude-opus-4-8", false),
        ("claude-opus-4-5-20251101", false), ("claude-sonnet-5-5", true), ("claude-sonnet-6", true),
        ("claude-sonnet-5", false), ("claude-sonnet-4-6", false), ("claude-haiku-4-5", false), ("claude-fable-5-1", false),
    ])
    func ersatzmodellJeModell(_ modell: String, _ erwartet: Bool) {
        #expect(ClaudeKonfiguration.mitErsatzmodell(modell) == erwartet)
        #expect(ClaudeKonfiguration(schluessel: "sk-test", modell: modell).ersatzmodell == erwartet)
    }

    @Test func werkzeugschemaOhneZusaetzeVonFoundationModels() throws {
        let r = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))], werkzeuge: [definition]), konfiguration: konfiguration).rumpf()
        guard case .liste(let werkzeuge)? = r["tools"], let schema = werkzeuge.first?["input_schema"] else {
            Issue.record("kein Werkzeug im Rumpf")
            return
        }
        #expect(schema["title"] == nil)
        #expect(schema["x-order"] == nil)
        #expect(schema["type"] == "object")
        #expect(schema["required"] == ["bereich"])
        // Im Werkzeugschema bleiben Grenzen als Hinweis stehen.
        #expect(schema["properties"]?["stunden"]?["maximum"] == 24)
    }

    @Test func ausgabeschemaOhneGrenzen() throws {
        let r = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))], schema: StatusArgumente.generationSchema), konfiguration: konfiguration).rumpf()
        let schema = try #require(r["output_config"]?["format"]?["schema"])
        #expect(r["output_config"]?["format"]?["type"] == "json_schema")
        #expect(schema["additionalProperties"] == false)
        let stunden = try #require(schema["properties"]?["stunden"])
        #expect(stunden["maximum"] == nil)
        #expect(stunden["minimum"] == nil)
        #expect(stunden["description"] == "Zeitraum in Stunden (maximum: 24, minimum: 1)")
    }

    @Test(arguments: [
        (ContextOptions.ReasoningLevel.light, "low"), (.moderate, "medium"), (.deep, "high"), (.custom("xhigh"), "xhigh"),
    ])
    func denkstufe(_ stufe: ContextOptions.ReasoningLevel, _ aufwand: String) {
        #expect(ClaudeAnfrage.aufwand(stufe) == aufwand)
    }

    @Test func ohneDenkstufeGiltDieVorgabeDerAPI() throws {
        let r = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))]), konfiguration: konfiguration).rumpf()
        #expect(r["output_config"] == nil)
    }

    /// Opus 5.5 und Sonnet 5.5 kennen keinen Werkzeugzwang; `.required` bleibt bei `auto`.
    @Test func werkzeugzwangUndVerbot() throws {
        var optionen = GenerationOptions()
        optionen.toolCallingMode = .required
        let erzwungen = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))], werkzeuge: [definition], optionen: optionen), konfiguration: konfiguration).rumpf()
        #expect(erzwungen["tool_choice"] == nil)
        optionen.toolCallingMode = .disallowed
        let verboten = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))], werkzeuge: [definition], optionen: optionen), konfiguration: konfiguration).rumpf()
        #expect(verboten["tool_choice"] == ["type": "none"])
    }

    @Test func denkschritteOhneSignaturOderVerworfenGehenNichtZurueck() throws {
        let eintraege: [Transcript.Entry] = [
            .prompt(.init(segments: [text("?")])),
            .reasoning(.init(segments: [text("ohne Signatur")])),
            .reasoning(.init(metadata: [ClaudeAnfrage.verworfen: true], segments: [text("vor dem Wechsel")], signature: Data("s".utf8))),
            .reasoning(.init(metadata: [ClaudeAnfrage.verborgen: "EmwKAhgB"], segments: [])),
            .response(.init(segments: [text("Antwort")])),
            .prompt(.init(segments: [text("Weiter?")])),
        ]
        let r = try ClaudeAnfrage(anfrage: anfrage(eintraege), konfiguration: konfiguration).rumpf()
        #expect(r["messages"] == [
            ["role": "user", "content": [["type": "text", "text": "?"]]],
            ["role": "assistant", "content": [
                ["type": "redacted_thinking", "data": "EmwKAhgB"],
                ["type": "text", "text": "Antwort"],
            ]],
            ["role": "user", "content": [["type": "text", "text": "Weiter?"]]],
        ])
    }

    @Test func mehrereWerkzeugergebnisseInEinerNachricht() throws {
        let aufrufe = Transcript.ToolCalls([
            Transcript.ToolCall(id: "a", toolName: "anlage_status", arguments: try GeneratedContent(json: #"{"bereich":"alle"}"#)),
            Transcript.ToolCall(id: "b", toolName: "anlage_status", arguments: try GeneratedContent(json: #"{"bereich":"heizung"}"#)),
        ])
        let eintraege: [Transcript.Entry] = [
            .prompt(.init(segments: [text("?")])),
            .toolCalls(aufrufe),
            .toolOutput(.init(id: "a", toolName: "anlage_status", segments: [text("1")])),
            .toolOutput(.init(id: "b", toolName: "anlage_status", segments: [text("2")])),
        ]
        let r = try ClaudeAnfrage(anfrage: anfrage(eintraege), konfiguration: konfiguration).rumpf()
        guard case .liste(let n)? = r["messages"] else { return }
        #expect(n.count == 3)
        #expect(n[2]["content"] == [
            ["type": "tool_result", "tool_use_id": "a", "content": [["type": "text", "text": "1"]]],
            ["type": "tool_result", "tool_use_id": "b", "content": [["type": "text", "text": "2"]]],
        ])
    }

    @Test func ohneErsatzmodellKeinFallback() throws {
        var k = konfiguration
        k.ersatzmodell = false
        let r = try ClaudeAnfrage(anfrage: anfrage([.prompt(.init(segments: [text("?")]))]), konfiguration: k).rumpf()
        #expect(r["fallbacks"] == nil)
    }
}
