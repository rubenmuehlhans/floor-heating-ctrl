import Foundation
import FoundationModels
import Geraeteschnittstelle
import Sprachmodelle
import Testing
@testable import Assistent

private let umgebung = ProcessInfo.processInfo.environment

struct AnlagenstatusTests {
    /// Ein Gerät, das nicht antwortet, verhindert die übrigen Werte nicht.
    @Test func ausgefallenesGeraet() async throws {
        let anlage = try Testanlage()
        await anlage.aendern("fbh_d4e5f6") { s in
            s.erreichbar = false
            s.fehler = "Zeitüberschreitung"
        }
        let text = try await AnlagenstatusWerkzeug(zugriff: anlage).call(arguments: .init(bereich: "alle"))
        let json = try JSONWert.lesen(Data(text.utf8))
        let og = json["verteiler"]?.alsListe?.first { $0["geraet"] == "fbh_d4e5f6" }
        #expect(og?["erreichbar"] == false)
        #expect(og?["fehler"] == "Zeitüberschreitung")
        #expect(json["heizungsgeraete"]?.alsListe?.count == 2)
    }

    @Test func ohneZugangsdaten() async throws {
        let anlage = try Testanlage()
        let werkzeug = AnlagenstatusWerkzeug(zugriff: anlage)
        for argumente in [AnlagenstatusWerkzeug.Argumente(bereich: "alle"),
                          .init(bereich: "alle", geraet: "Erdgeschoss", rohdaten: true)] {
            let text = try await werkzeug.call(arguments: argumente)
            for schluessel in JSONWert.zugangsschluessel {
                #expect(!text.contains("\"\(schluessel)\":"), "\(schluessel)")
            }
        }
    }

    @Test func nurHeizungUndRollen() async throws {
        let json = try JSONWert.lesen(Data(try await AnlagenstatusWerkzeug(zugriff: try Testanlage()).call(arguments: .init(bereich: "heizung")).utf8))
        #expect(json["verteiler"] == nil)
        #expect(Set((json["heizungsgeraete"]?.alsListe ?? []).compactMap { $0["rolle"]?.alsText }) == ["Kessel", "Pufferspeicher"])
        #expect(json["heizkreise_versorgen"]?[1]?["versorgt"] == ["Obergeschoss"])
    }

    @Test func unbekanntesGeraet() async throws {
        let text = try await AnlagenstatusWerkzeug(zugriff: try Testanlage()).call(arguments: .init(bereich: "alle", geraet: "Dachboden"))
        #expect(text.contains("Kein Gerät „Dachboden“"))
    }
}

/// Der Durchstich durch alle Schichten: Sitzung → Claude-Ausführer → Werkzeuge → Anlage und
/// zurück. Die API ist vorgetäuscht: erst ein Vorschlag, dann die Antwort.
struct DurchstichTests {
    @Test func claudeLegtEinenVorschlagAn() async throws {
        let anlage = try Testanlage()
        let argumente = #"{"titel":"Speicher voll: 62 → 68 °C","aenderungen":[{"parameter":"buffer.voll_c","wert":"68"}],"begruendung":"Die Ladungen enden bei 68,5 °C.","wirkung":"Der Ladezustand stimmt oben."}"#
        let transport = APIStub.transport { rumpf in
            if rumpf.contains("tool_result") {
                return APIStub.antwort("Ich schlage vor, voll auf 68 °C zu setzen.")
            }
            return APIStub.werkzeugaufruf(argumente, name: "aenderung_vorschlagen")
        }
        let modell = ClaudeSprachmodell(konfiguration: .init(
            schluessel: "sk-test", adresse: URL(string: "https://api.test/v1/messages")!, transport: transport))
        let sitzung = LanguageModelSession(model: modell, tools: Werkzeugsatz.claude(anlage)) { Anweisungen.claude(Wissen(dokumente: [])) }

        let antwort = try await sitzung.respond(to: "Stimmt die Ladeanzeige?")
        #expect(antwort.content == "Ich schlage vor, voll auf 68 °C zu setzen.")
        let vorschlaege = await anlage.vorschlaege
        try #require(vorschlaege.count == 1)
        #expect(vorschlaege[0].ziele.count == 2)
        let bausteine = Darstellung.bausteine(sitzung.transcript)
        #expect(bausteine.contains(.vorschlag(vorschlaege[0].id)))
        #expect(bausteine.contains { if case .werkzeug(let w) = $0 { w.beschreibung == "Vorschlag angelegt" } else { false } })
        #expect(bausteine.contains(.ueberlegung("Ich lese den Speicher.")))

        // Der Präfix trägt alle Werkzeuge und die Anweisungen in Sie-Form.
        let erste = try #require(APIStub.rumpfe().first)
        for name in ["anlage_status", "befunde", "verlauf", "protokolle", "kennzahlen", "einstellungen_lesen", "aenderung_vorschlagen", "aktion_vorschlagen"] {
            #expect(erste.contains("\"name\":\"\(name)\""), "\(name)")
        }
        #expect(!erste.contains("wissen_suchen"), "Claude hat das Wissen in den Anweisungen")
    }
}

/// Schlüssel für den Live-Test: aus `ANTHROPIC_API_KEY` oder aus dem Schlüsselbund, abgelegt mit
///
///     security add-generic-password -a "$USER" -s de.simplytech.heizung.entwicklung -w
///
/// (fragt den Schlüssel verdeckt ab; er landet weder in der Shell-Historie noch in einer Datei).
/// Unter iOS gibt es kein `Process`; dort zählt nur die Umgebung.
private let apiSchluessel: String? = umgebung["ANTHROPIC_API_KEY"] ?? schluesselAusDemSchluesselbund()

private func schluesselAusDemSchluesselbund() -> String? {
    #if os(macOS)
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/security")
    p.arguments = ["find-generic-password", "-s", "de.simplytech.heizung.entwicklung", "-w"]
    let ausgabe = Pipe()
    p.standardOutput = ausgabe
    p.standardError = Pipe()
    guard (try? p.run()) != nil else { return nil }
    p.waitUntilExit()
    let text = String(decoding: ausgabe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        .trimmingCharacters(in: .whitespacesAndNewlines)
    return p.terminationStatus == 0 && !text.isEmpty ? text : nil
    #else
    return nil
    #endif
}

/// Gegen die echte API, mit der Testanlage aus den Aufnahmen. Kostet Geld und läuft deshalb nur
/// auf ausdrücklichen Wunsch:
///
///     KI_LIVE=1 swift test --filter LiveTests
@Suite(.enabled(if: umgebung["KI_LIVE"] == "1" && apiSchluessel != nil))
struct LiveTests {
    @Test func frageZurAnlage() async throws {
        let modell = ClaudeSprachmodell(schluessel: try #require(apiSchluessel))
        let sitzung = LanguageModelSession(model: modell, tools: Werkzeugsatz.claude(try Testanlage())) {
            Anweisungen.claude(Wissen(ordner: Aufnahme.dokumente))
        }
        let antwort = try await sitzung.respond(
            to: "Wie warm ist der Pufferspeicher gerade?", contextOptions: ContextOptions(reasoningLevel: .light))
        #expect(!antwort.content.isEmpty)
        let werkzeugGenutzt = sitzung.transcript.contains { eintrag in
            if case .toolCalls(let aufrufe) = eintrag { return aufrufe.contains { $0.toolName == "anlage_status" } }
            return false
        }
        #expect(werkzeugGenutzt)
        print("Antwort von Claude:", antwort.content)
    }
}

/// Vorgetäuschte Messages-API für den Durchstich.
final class APIStub: URLProtocol, @unchecked Sendable {
    private static let sperre = NSLock()
    nonisolated(unsafe) private static var antwortgeber: (@Sendable (String) -> Data)?
    nonisolated(unsafe) private static var eingegangen: [String] = []

    static func transport(_ antwort: @escaping @Sendable (String) -> Data) -> ClaudeTransport {
        sperre.withLock { antwortgeber = antwort; eingegangen = [] }
        let k = URLSessionConfiguration.ephemeral
        k.protocolClasses = [APIStub.self]
        return ClaudeTransport(sitzung: URLSession(configuration: k))
    }

    static func rumpfe() -> [String] {
        sperre.withLock { eingegangen }
    }

    static func strom(_ ereignisse: [String]) -> Data {
        Data(ereignisse.map { "data: \($0)\n\n" }.joined().utf8)
    }

    static let beginn = #"{"type":"message_start","message":{"model":"claude-opus-5","usage":{"input_tokens":900,"output_tokens":1}}}"#

    static func antwort(_ text: String) -> Data {
        strom([beginn,
               #"{"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}"#,
               #"{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"\#(text)"}}"#,
               #"{"type":"content_block_stop","index":0}"#,
               #"{"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":12}}"#,
               #"{"type":"message_stop"}"#])
    }

    static func werkzeugaufruf(_ argumente: String, name: String = "anlage_status") -> Data {
        let maskiert = argumente.replacingOccurrences(of: "\"", with: "\\\"")
        return strom([beginn,
                      #"{"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":""}}"#,
                      #"{"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"Ich lese den Speicher."}}"#,
                      #"{"type":"content_block_delta","index":0,"delta":{"type":"signature_delta","signature":"sig"}}"#,
                      #"{"type":"content_block_stop","index":0}"#,
                      #"{"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_1","name":"\#(name)","input":{}}}"#,
                      #"{"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"\#(maskiert)"}}"#,
                      #"{"type":"content_block_stop","index":1}"#,
                      #"{"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":40}}"#,
                      #"{"type":"message_stop"}"#])
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        var rumpf = request.httpBody ?? Data()
        if rumpf.isEmpty, let strom = request.httpBodyStream {
            strom.open()
            var puffer = [UInt8](repeating: 0, count: 65_536)
            while strom.hasBytesAvailable {
                let n = strom.read(&puffer, maxLength: puffer.count)
                if n <= 0 { break }
                rumpf.append(puffer, count: n)
            }
            strom.close()
        }
        let text = String(decoding: rumpf, as: UTF8.self)
        let geber = Self.sperre.withLock { () -> (@Sendable (String) -> Data)? in
            Self.eingegangen.append(text)
            return Self.antwortgeber
        }
        let http = HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: "HTTP/1.1",
                                   headerFields: ["Content-Type": "text/event-stream"])!
        client?.urlProtocol(self, didReceive: http, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: geber?(text) ?? Data())
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}
