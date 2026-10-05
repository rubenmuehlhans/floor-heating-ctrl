import Foundation
import FoundationModels
@testable import Sprachmodelle

@Generable
struct StatusArgumente {
    @Guide(description: "Bereich der Anlage: alle, verteiler oder heizung")
    var bereich: String
    @Guide(description: "Zeitraum in Stunden", .range(1...24))
    var stunden: Int?
}

/// Lesendes Werkzeug wie `anlage_status`, mit festem Ergebnis.
struct Statuswerkzeug: Tool {
    let name = "anlage_status"
    let description = "Liest den Zustand der Anlage."

    @concurrent func call(arguments: StatusArgumente) async throws -> String {
        #"{"bereich":"\#(arguments.bereich)","puffer_c":53.0}"#
    }
}

/// Datenstrom der Messages-API, wie er über die Leitung kommt.
enum SSE {
    static func strom(_ ereignisse: [String]) -> Data {
        Data(ereignisse.map { json in
            let art = (try? JSON.lesen(json))?["type"]?.alsText ?? "message"
            return "event: \(art)\ndata: \(json)\n\n"
        }.joined().utf8)
    }

    static let beginn = #"{"type":"message_start","message":{"id":"msg_1","type":"message","role":"assistant","model":"claude-opus-5","content":[],"stop_reason":null,"usage":{"input_tokens":1200,"cache_read_input_tokens":800,"cache_creation_input_tokens":40,"output_tokens":1}}}"#

    static func ende(_ grund: String, ausgabe: Int = 87, details: String = "null") -> [String] {
        [
            #"{"type":"message_delta","delta":{"stop_reason":"\#(grund)","stop_sequence":null,"stop_details":\#(details)},"usage":{"output_tokens":\#(ausgabe)}}"#,
            #"{"type":"message_stop"}"#,
        ]
    }

    static func denken(_ index: Int, _ text: String, signatur: String) -> [String] {
        [
            #"{"type":"content_block_start","index":\#(index),"content_block":{"type":"thinking","thinking":"","signature":""}}"#,
            #"{"type":"content_block_delta","index":\#(index),"delta":{"type":"thinking_delta","thinking":"\#(text)"}}"#,
            #"{"type":"content_block_delta","index":\#(index),"delta":{"type":"signature_delta","signature":"\#(signatur)"}}"#,
            #"{"type":"content_block_stop","index":\#(index)}"#,
        ]
    }

    static func text(_ index: Int, _ teile: [String]) -> [String] {
        [#"{"type":"content_block_start","index":\#(index),"content_block":{"type":"text","text":""}}"#]
            + teile.map { #"{"type":"content_block_delta","index":\#(index),"delta":{"type":"text_delta","text":"\#($0)"}}"# }
            + [#"{"type":"content_block_stop","index":\#(index)}"#]
    }

    static func werkzeug(_ index: Int, id: String, name: String, argumente: [String], schliessen: Bool = true) -> [String] {
        [#"{"type":"content_block_start","index":\#(index),"content_block":{"type":"tool_use","id":"\#(id)","name":"\#(name)","input":{}}}"#]
            + argumente.map { teil in
                let maskiert = teil.replacingOccurrences(of: "\"", with: "\\\"")
                return #"{"type":"content_block_delta","index":\#(index),"delta":{"type":"input_json_delta","partial_json":"\#(maskiert)"}}"#
            }
            + (schliessen ? [#"{"type":"content_block_stop","index":\#(index)}"#] : [])
    }

    static let ping = #"{"type":"ping"}"#
}

/// Vorgetäuschte Messages-API: beantwortet Anfragen je Host und merkt sich die Rümpfe.
final class APIAttrappe: URLProtocol, @unchecked Sendable {
    struct Antwort: Sendable {
        var status = 200
        var kopf = ["Content-Type": "text/event-stream"]
        var rumpf: Data
    }

    struct Eingang: Sendable {
        var url: URL?
        var kopf: [String: String]
        var rumpf: Data
        var json: JSON? { try? JSON.lesen(String(decoding: rumpf, as: UTF8.self)) }
    }

    private static let sperre = NSLock()
    nonisolated(unsafe) private static var tabellen: [String: @Sendable (Eingang) -> Antwort] = [:]
    nonisolated(unsafe) private static var eingaenge: [String: [Eingang]] = [:]

    static func transport(host: String, _ tabelle: @escaping @Sendable (Eingang) -> Antwort) -> ClaudeTransport {
        sperre.withLock { tabellen[host] = tabelle; eingaenge[host] = [] }
        let k = URLSessionConfiguration.ephemeral
        k.protocolClasses = [APIAttrappe.self]
        return ClaudeTransport(sitzung: URLSession(configuration: k))
    }

    static func eingaenge(host: String) -> [Eingang] {
        sperre.withLock { eingaenge[host] ?? [] }
    }

    static func modell(host: String, _ tabelle: @escaping @Sendable (Eingang) -> Antwort) -> ClaudeSprachmodell {
        ClaudeSprachmodell(konfiguration: ClaudeKonfiguration(
            schluessel: "sk-test", adresse: URL(string: "https://\(host)/v1/messages")!,
            transport: transport(host: host, tabelle)))
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        guard let url = request.url, let host = url.host() else { return }
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
        let eingang = Eingang(url: url, kopf: request.allHTTPHeaderFields ?? [:], rumpf: rumpf)
        let tabelle = Self.sperre.withLock { () -> (@Sendable (Eingang) -> Antwort)? in
            Self.eingaenge[host, default: []].append(eingang)
            return Self.tabellen[host]
        }
        let antwort = tabelle?(eingang) ?? Antwort(status: 404, rumpf: Data())
        let http = HTTPURLResponse(url: url, statusCode: antwort.status, httpVersion: "HTTP/1.1", headerFields: antwort.kopf)!
        client?.urlProtocol(self, didReceive: http, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: antwort.rumpf)
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}
