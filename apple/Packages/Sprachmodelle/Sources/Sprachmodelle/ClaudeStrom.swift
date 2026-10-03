import Foundation
import FoundationModels

/// Ereignisse des Datenstroms der Messages-API, auf das Nötige verdichtet.
enum ClaudeEreignis: Equatable, Sendable {
    case nachrichtBeginn(modell: String?, verbrauch: Verbrauch)
    case blockBeginn(index: Int, block: Block)
    case textDelta(index: Int, text: String)
    case denkDelta(index: Int, text: String)
    case signatur(index: Int, signatur: String)
    case argumentDelta(index: Int, json: String)
    case blockEnde(index: Int)
    case nachrichtDelta(stopGrund: String?, details: Stopdetails?, verbrauch: Verbrauch)
    case nachrichtEnde
    case fehler(art: String, meldung: String)
    case ping

    enum Block: Equatable, Sendable {
        case text
        case denken
        /// `redacted_thinking`: verschlüsselt, nur zum Zurückspielen
        case verborgenesDenken(daten: String)
        case werkzeug(id: String, name: String)
        /// Ein Ersatzmodell übernimmt (`fallbacks`); Grenze im Inhalt, kein eigenes Ereignis
        case ersatzmodell(von: String?, zu: String?)
        case unbekannt(String)
    }

    struct Verbrauch: Equatable, Sendable {
        var eingabe: Int?
        var ausgabe: Int?
        var cacheGelesen: Int?
        var cacheGeschrieben: Int?
    }

    struct Stopdetails: Equatable, Sendable {
        var kategorie: String?
        var erklaerung: String?
    }
}

enum ClaudeStrom {
    /// Wertet eine Zeile aus. Inhalt tragen nur `data:`-Zeilen; ihr JSON nennt die Art selbst,
    /// die `event:`-Zeile ist deshalb entbehrlich.
    static func auswerten(zeile: String) throws -> ClaudeEreignis? {
        guard zeile.hasPrefix("data:") else { return nil }
        let json = zeile.dropFirst(5).trimmingCharacters(in: .whitespaces)
        guard !json.isEmpty else { return nil }
        let e = try JSONDecoder().decode(Roh.self, from: Data(json.utf8))
        let index = e.index ?? 0
        switch e.type {
        case "message_start":
            return .nachrichtBeginn(modell: e.message?.model, verbrauch: e.message?.usage?.verbrauch ?? .init())
        case "content_block_start":
            let b = e.content_block
            let block: ClaudeEreignis.Block = switch b?.type {
            case "text": .text
            case "thinking": .denken
            case "redacted_thinking": .verborgenesDenken(daten: b?.data ?? "")
            case "tool_use": .werkzeug(id: b?.id ?? "", name: b?.name ?? "")
            case "fallback": .ersatzmodell(von: b?.from?.model, zu: b?.to?.model)
            default: .unbekannt(b?.type ?? "")
            }
            return .blockBeginn(index: index, block: block)
        case "content_block_delta":
            switch e.delta?.type {
            case "text_delta": return .textDelta(index: index, text: e.delta?.text ?? "")
            case "thinking_delta": return .denkDelta(index: index, text: e.delta?.thinking ?? "")
            case "signature_delta": return .signatur(index: index, signatur: e.delta?.signature ?? "")
            case "input_json_delta": return .argumentDelta(index: index, json: e.delta?.partial_json ?? "")
            default: return nil
            }
        case "content_block_stop":
            return .blockEnde(index: index)
        case "message_delta":
            let details = e.delta?.stop_details.map {
                ClaudeEreignis.Stopdetails(kategorie: $0.category, erklaerung: $0.explanation)
            }
            return .nachrichtDelta(stopGrund: e.delta?.stop_reason, details: details, verbrauch: e.usage?.verbrauch ?? .init())
        case "message_stop":
            return .nachrichtEnde
        case "error":
            return .fehler(art: e.error?.type ?? "error", meldung: e.error?.message ?? "")
        case "ping":
            return .ping
        default:
            return nil
        }
    }

    // Aufbau der Ereignisse, wie ihn die API sendet; nur die benötigten Felder
    private struct Roh: Decodable {
        var type: String
        var index: Int?
        var message: Nachricht?
        var content_block: Inhalt?
        var delta: Delta?
        var usage: Verbrauch?
        var error: Fehler?

        struct Nachricht: Decodable {
            var model: String?
            var usage: Verbrauch?
        }

        struct Inhalt: Decodable {
            var type: String?
            var id: String?
            var name: String?
            var data: String?
            var from: Modell?
            var to: Modell?
        }

        struct Modell: Decodable {
            var model: String?
        }

        struct Delta: Decodable {
            var type: String?
            var text: String?
            var thinking: String?
            var signature: String?
            var partial_json: String?
            var stop_reason: String?
            var stop_details: Details?
        }

        struct Details: Decodable {
            var category: String?
            var explanation: String?
        }

        struct Verbrauch: Decodable {
            var input_tokens: Int?
            var output_tokens: Int?
            var cache_read_input_tokens: Int?
            var cache_creation_input_tokens: Int?

            var verbrauch: ClaudeEreignis.Verbrauch {
                .init(eingabe: input_tokens, ausgabe: output_tokens,
                      cacheGelesen: cache_read_input_tokens, cacheGeschrieben: cache_creation_input_tokens)
            }
        }

        struct Fehler: Decodable {
            var type: String?
            var message: String?
        }
    }
}

// MARK: - Übersetzung in Kanalereignisse

/// Was im Kanal der Sitzung ankommt, in prüfbarer Form. Einträge werden über ihre Kennung
/// auseinandergehalten: je Inhaltsblock eine, für alle Werkzeugaufrufe eines Zugs eine gemeinsame.
enum Kanalaktion: Equatable, Sendable {
    case text(eintrag: String, text: String)
    case denken(eintrag: String, text: String)
    case signatur(eintrag: String, signatur: String)
    case verborgenesDenken(eintrag: String, daten: String)
    case denkenVerwerfen(eintrag: String)
    case werkzeug(eintrag: String, id: String, name: String, argumente: String)
    case werkzeugEntfernen(eintrag: String, id: String)
    case verbrauch(eintrag: String, art: Art, eingabe: Int, cache: Int, ausgabe: Int)

    enum Art: Equatable, Sendable {
        case antwort, denken, werkzeuge
    }

    var ereignis: LanguageModelExecutorGenerationChannel.Event {
        switch self {
        case .text(let e, let t):
            .response(entryID: e, action: .appendText(t, tokenCount: 0))
        case .denken(let e, let t):
            .reasoning(entryID: e, action: .appendText(t, tokenCount: 0))
        case .signatur(let e, let s):
            .reasoning(entryID: e, action: .updateSignature(Data(s.utf8), tokenCount: 0))
        case .verborgenesDenken(let e, let d):
            .reasoning(entryID: e, action: .updateMetadata([ClaudeAnfrage.verborgen: d]))
        case .denkenVerwerfen(let e):
            .reasoning(entryID: e, action: .updateMetadata([ClaudeAnfrage.verworfen: true]))
        case .werkzeug(let e, let id, let name, let argumente):
            .toolCalls(entryID: e, action: .toolCall(id: id, name: name, action: .appendArguments(argumente, tokenCount: 0)))
        case .werkzeugEntfernen(let e, let id):
            .toolCalls(entryID: e, action: .removeToolCall(id: id))
        case .verbrauch(let e, let art, let eingabe, let cache, let ausgabe):
            switch art {
            case .antwort:
                .response(entryID: e, action: .updateUsage(input: .init(totalTokenCount: eingabe, cachedTokenCount: cache), output: .init(totalTokenCount: ausgabe, reasoningTokenCount: 0)))
            case .denken:
                .reasoning(entryID: e, action: .updateUsage(input: .init(totalTokenCount: eingabe, cachedTokenCount: cache), output: .init(totalTokenCount: ausgabe, reasoningTokenCount: 0)))
            case .werkzeuge:
                .toolCalls(entryID: e, action: .updateUsage(input: .init(totalTokenCount: eingabe, cachedTokenCount: cache), output: .init(totalTokenCount: ausgabe, reasoningTokenCount: 0)))
            }
        }
    }
}

/// Verfolgt eine Antwort über den Datenstrom und entscheidet, was in den Kanal geht.
struct Stromzustand {
    let praefix: String
    private(set) var beendet = false
    private var bloecke: [Int: ClaudeEreignis.Block] = [:]
    private var offeneWerkzeuge: Set<Int> = []
    private var argumenteErhalten: Set<Int> = []
    private var stopGrund: String?
    private var stopdetails: ClaudeEreignis.Stopdetails?
    private var verbrauch = ClaudeEreignis.Verbrauch()
    private var letzterEintrag: (id: String, art: Kanalaktion.Art)?

    init(praefix: String) {
        self.praefix = praefix
    }

    private var werkzeugeintrag: String { "\(praefix)-werkzeuge" }

    private func eintrag(_ index: Int) -> String { "\(praefix)-\(index)" }

    mutating func verarbeiten(_ e: ClaudeEreignis) throws -> [Kanalaktion] {
        switch e {
        case .nachrichtBeginn(_, let v):
            verbrauch = v
            return []

        case .blockBeginn(let i, let block):
            bloecke[i] = block
            switch block {
            case .werkzeug:
                offeneWerkzeuge.insert(i)
                letzterEintrag = (werkzeugeintrag, .werkzeuge)
                return []
            case .verborgenesDenken(let daten):
                letzterEintrag = (eintrag(i), .denken)
                return [.verborgenesDenken(eintrag: eintrag(i), daten: daten)]
            case .ersatzmodell:
                return ersatzUebernimmt(vor: i)
            default:
                return []
            }

        case .textDelta(let i, let t):
            guard !t.isEmpty else { return [] }
            letzterEintrag = (eintrag(i), .antwort)
            return [.text(eintrag: eintrag(i), text: t)]

        case .denkDelta(let i, let t):
            guard !t.isEmpty else { return [] }
            letzterEintrag = (eintrag(i), .denken)
            return [.denken(eintrag: eintrag(i), text: t)]

        case .signatur(let i, let s):
            letzterEintrag = (eintrag(i), .denken)
            return [.signatur(eintrag: eintrag(i), signatur: s)]

        case .argumentDelta(let i, let json):
            guard case .werkzeug(let id, let name) = bloecke[i], !json.isEmpty else { return [] }
            argumenteErhalten.insert(i)
            return [.werkzeug(eintrag: werkzeugeintrag, id: id, name: name, argumente: json)]

        case .blockEnde(let i):
            guard case .werkzeug(let id, let name) = bloecke[i] else { return [] }
            offeneWerkzeuge.remove(i)
            // Ein Werkzeug ohne Parameter bekommt ein leeres Objekt.
            return argumenteErhalten.contains(i) ? [] : [.werkzeug(eintrag: werkzeugeintrag, id: id, name: name, argumente: "{}")]

        case .nachrichtDelta(let grund, let details, let v):
            stopGrund = grund ?? stopGrund
            stopdetails = details ?? stopdetails
            verbrauch.ausgabe = v.ausgabe ?? verbrauch.ausgabe
            return []

        case .nachrichtEnde:
            beendet = true
            guard let letzterEintrag else { return [] }
            let eingabe = (verbrauch.eingabe ?? 0) + (verbrauch.cacheGelesen ?? 0) + (verbrauch.cacheGeschrieben ?? 0)
            return [.verbrauch(eintrag: letzterEintrag.id, art: letzterEintrag.art, eingabe: eingabe,
                               cache: verbrauch.cacheGelesen ?? 0, ausgabe: verbrauch.ausgabe ?? 0)]

        case .fehler(let art, let meldung):
            throw ClaudeFehler.imStrom(art: art, meldung: meldung)

        case .ping:
            return []
        }
    }

    /// Nach einem Wechsel zum Ersatzmodell mitten in der Antwort gehen Denkschritte und
    /// Werkzeugaufrufe vor der Grenze nicht zurück an die API; Text bleibt stehen.
    private mutating func ersatzUebernimmt(vor grenze: Int) -> [Kanalaktion] {
        var aktionen: [Kanalaktion] = []
        for (i, block) in bloecke.sorted(by: { $0.key < $1.key }) where i < grenze {
            switch block {
            case .denken, .verborgenesDenken:
                aktionen.append(.denkenVerwerfen(eintrag: eintrag(i)))
            case .werkzeug(let id, _):
                aktionen.append(.werkzeugEntfernen(eintrag: werkzeugeintrag, id: id))
                offeneWerkzeuge.remove(i)
            default:
                break
            }
        }
        return aktionen
    }

    /// Prüft nach dem Ende des Stroms, ob die Antwort verwendbar ist.
    func abschluss() throws {
        guard beendet else { throw ClaudeFehler.unvollstaendig }
        switch stopGrund {
        case "refusal":
            throw LanguageModelError.refusal(.init(
                explanation: stopdetails?.erklaerung ?? "Die Anfrage wurde aus Sicherheitsgründen abgelehnt.",
                debugDescription: "stop_reason refusal, Kategorie \(stopdetails?.kategorie ?? "keine")"))
        case "max_tokens" where !offeneWerkzeuge.isEmpty:
            throw ClaudeFehler.werkzeugaufrufAbgeschnitten
        case "model_context_window_exceeded":
            throw LanguageModelError.contextSizeExceeded(.init(
                contextSize: 1_000_000, tokenCount: verbrauch.eingabe ?? 0,
                debugDescription: "stop_reason model_context_window_exceeded"))
        default:
            break
        }
    }
}
