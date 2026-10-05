import CoreGraphics
import Foundation
import FoundationModels
import ImageIO
import UniformTypeIdentifiers

/// Übersetzt eine Anfrage der Sitzung in den Rumpf der Messages-API.
///
/// - Anweisungen werden zum Systemtext; sein letzter Block trägt eine feste Cache-Marke, dazu
///   kommt die automatische Marke für den wachsenden Gesprächsverlauf.
/// - Aufeinanderfolgende Einträge einer Rolle werden eine Nachricht: Denkschritte, Antwort und
///   Werkzeugaufrufe eines Zugs bilden eine Assistentennachricht, die Ergebnisse aller Aufrufe
///   eine Nutzernachricht mit mehreren `tool_result`.
/// - Denkschritte gehen mit ihrer Signatur unverändert zurück. Ohne Signatur, oder wenn ein
///   Ersatzmodell mitten in der Antwort übernommen hat, entfallen sie.
struct ClaudeAnfrage {
    let anfrage: LanguageModelExecutorGenerationRequest
    let konfiguration: ClaudeKonfiguration

    /// Metadaten eines Denkschritts, der nach einem Wechsel zum Ersatzmodell nicht zurückgeht
    static let verworfen = "claude_verworfen"
    /// Metadaten eines verborgenen Denkschritts (`redacted_thinking`) mit seinen Daten
    static let verborgen = "claude_verborgen"

    private enum Rolle: String {
        case nutzer = "user"
        case assistent = "assistant"
    }

    func rumpf() throws -> JSON {
        var anweisungen: [String] = []
        var nachrichten: [(rolle: Rolle, bloecke: [JSON])] = []

        func anhaengen(_ rolle: Rolle, _ bloecke: [JSON]) {
            guard !bloecke.isEmpty else { return }
            if let letzte = nachrichten.last, letzte.rolle == rolle {
                nachrichten[nachrichten.count - 1].bloecke += bloecke
            } else {
                nachrichten.append((rolle, bloecke))
            }
        }

        for eintrag in anfrage.transcript {
            switch eintrag {
            case .instructions(let a):
                let text = Self.text(a.segments)
                if !text.isEmpty { anweisungen.append(text) }
            case .prompt(let p):
                anhaengen(.nutzer, try Self.inhalt(p.segments))
            case .toolOutput(let o):
                anhaengen(.nutzer, [Self.werkzeugergebnis(o)])
            case .reasoning(let r):
                if let block = Self.denkblock(r) { anhaengen(.assistent, [block]) }
            case .response(let r):
                anhaengen(.assistent, try Self.inhalt(r.segments))
            case .toolCalls(let aufrufe):
                anhaengen(.assistent, try aufrufe.map(Self.werkzeugaufruf))
            @unknown default:
                throw LanguageModelError.unsupportedTranscriptContent(.init(
                    unsupportedContent: [eintrag], debugDescription: "Unbekannter Transkripteintrag"))
            }
        }

        var rumpf: JSON = [
            "model": .text(konfiguration.modell),
            "max_tokens": .zahl(Double(anfrage.generationOptions.maximumResponseTokens ?? konfiguration.maximaleToken)),
            "stream": true,
            "cache_control": ["type": "ephemeral"],
            "messages": .liste(nachrichten.map { n in
                // Werkzeugergebnisse stehen am Anfang ihrer Nachricht.
                let bloecke = n.rolle == .nutzer
                    ? n.bloecke.filter { $0["type"] == "tool_result" } + n.bloecke.filter { $0["type"] != "tool_result" }
                    : n.bloecke
                return ["role": .text(n.rolle.rawValue), "content": .liste(bloecke)]
            }),
        ]
        // Ohne adaptives Denken (Haiku 4.5) denkt das Modell nicht; ein festes Budget müsste
        // unter `max_tokens` bleiben und bräuchte für Werkzeugschleifen eine eigene Beta.
        if konfiguration.faehigkeiten.adaptivesDenken {
            rumpf["thinking"] = ["type": "adaptive", "display": "summarized"]
        }
        if !anweisungen.isEmpty {
            rumpf["system"] = [[
                "type": "text",
                "text": .text(anweisungen.joined(separator: "\n\n")),
                "cache_control": ["type": "ephemeral"],
            ]]
        }
        if !anfrage.enabledToolDefinitions.isEmpty {
            rumpf["tools"] = .liste(try anfrage.enabledToolDefinitions.map { d in
                [
                    "name": .text(d.name),
                    "description": .text(d.description),
                    "input_schema": try JSON.aus(d.parameters).werkzeugschema(),
                ]
            })
            // Einen Werkzeugzwang (`any`, `tool`) weisen Opus 5.5 und Sonnet 5.5 mit 400 ab;
            // `.required` bleibt deshalb bei `auto`, die Anweisungen nennen das Werkzeug.
            if case .disallowed? = anfrage.generationOptions.toolCallingMode {
                rumpf["tool_choice"] = ["type": "none"]
            }
        }
        var ausgabe: JSON = [:]
        if konfiguration.faehigkeiten.aufwand, let aufwand = Self.aufwand(anfrage.contextOptions.reasoningLevel) {
            ausgabe["effort"] = .text(aufwand)
        }
        if let schema = anfrage.schema {
            ausgabe["format"] = ["type": "json_schema", "schema": try JSON.aus(schema).ausgabeschema()]
        }
        if ausgabe != [:] {
            rumpf["output_config"] = ausgabe
        }
        if konfiguration.ersatzmodell {
            rumpf["fallbacks"] = "default"
        }
        return rumpf
    }

    /// Denkstufe der Sitzung als `effort`. Ohne Angabe gilt die Vorgabe des Modells (Opus 5.5
    /// `medium`, Sonnet 5.5 `high`).
    static func aufwand(_ stufe: ContextOptions.ReasoningLevel?) -> String? {
        switch stufe {
        case .light: "low"
        case .moderate: "medium"
        case .deep: "high"
        case .custom(let wert): wert
        case nil: nil
        @unknown default: nil
        }
    }

    // MARK: Blöcke

    private static func text(_ segmente: [Transcript.Segment]) -> String {
        segmente.compactMap { s -> String? in
            switch s {
            case .text(let t): t.content
            case .structure(let s): s.content.jsonString
            default: nil
            }
        }.joined(separator: "\n")
    }

    private static func inhalt(_ segmente: [Transcript.Segment]) throws -> [JSON] {
        try segmente.compactMap { s -> JSON? in
            switch s {
            case .text(let t):
                t.content.isEmpty ? nil : ["type": "text", "text": .text(t.content)]
            case .structure(let s):
                ["type": "text", "text": .text(s.content.jsonString)]
            case .attachment(let a):
                switch a.content {
                case .image(let bild): try bildblock(bild)
                @unknown default: nil
                }
            @unknown default:
                nil
            }
        }
    }

    private static func denkblock(_ r: Transcript.Reasoning) -> JSON? {
        if r.metadata[verworfen]?.jsonString == "true" {
            return nil
        }
        if let daten = r.metadata[verborgen], let text = try? String(daten) {
            return ["type": "redacted_thinking", "data": .text(text)]
        }
        guard let signatur = r.signature, !signatur.isEmpty else { return nil }
        return [
            "type": "thinking",
            "thinking": .text(text(r.segments)),
            "signature": .text(String(decoding: signatur, as: UTF8.self)),
        ]
    }

    private static func werkzeugaufruf(_ a: Transcript.ToolCall) throws -> JSON {
        ["type": "tool_use", "id": .text(a.id), "name": .text(a.toolName), "input": try JSON.lesen(a.arguments.jsonString)]
    }

    private static func werkzeugergebnis(_ o: Transcript.ToolOutput) -> JSON {
        let text = text(o.segments)
        return [
            "type": "tool_result",
            "tool_use_id": .text(o.id),
            "content": .liste([["type": "text", "text": .text(text.isEmpty ? "(leer)" : text)]]),
        ]
    }

    private static func bildblock(_ bild: Transcript.ImageAttachment) throws -> JSON {
        let daten = NSMutableData()
        guard let ziel = CGImageDestinationCreateWithData(daten, UTType.jpeg.identifier as CFString, 1, nil) else {
            throw LanguageModelError.unsupportedTranscriptContent(.init(unsupportedContent: [], debugDescription: "Bild nicht kodierbar"))
        }
        CGImageDestinationAddImage(ziel, bild.cgImage, [kCGImageDestinationLossyCompressionQuality: 0.85] as CFDictionary)
        guard CGImageDestinationFinalize(ziel) else {
            throw LanguageModelError.unsupportedTranscriptContent(.init(unsupportedContent: [], debugDescription: "Bild nicht kodierbar"))
        }
        return [
            "type": "image",
            "source": ["type": "base64", "media_type": "image/jpeg", "data": .text((daten as Data).base64EncodedString())],
        ]
    }
}
