import Foundation
import Testing
@testable import Geraeteschnittstelle

enum Fixture {
    static func daten(_ name: String) throws -> Data {
        let url = try #require(Bundle.module.url(forResource: name, withExtension: nil, subdirectory: "Fixtures"))
        return try Data(contentsOf: url)
    }

    static func text(_ name: String) throws -> String {
        String(decoding: try daten(name), as: UTF8.self)
    }

    static func dekodiert<T: Decodable>(_ name: String, als typ: T.Type = T.self) throws -> T {
        try JSONDecoder().decode(typ, from: try daten(name))
    }
}

/// Vorgetäuschtes Netz: beantwortet Anfragen je Host aus einer Tabelle und merkt sich, was ankam.
/// Jeder Test nimmt einen eigenen Host, damit parallel laufende Tests sich nicht stören.
final class Netzattrappe: URLProtocol, @unchecked Sendable {
    struct Antwort: Sendable {
        var status = 200
        var kopf: [String: String] = [:]
        var rumpf = Data(#"{"ok":true}"#.utf8)
    }

    struct Eingang: Sendable {
        var methode: String
        var pfad: String
        var kopf: [String: String]
        var rumpf: Data
    }

    private static let sperre = NSLock()
    nonisolated(unsafe) private static var tabellen: [String: @Sendable (Eingang) -> Antwort] = [:]
    nonisolated(unsafe) private static var eingaenge: [String: [Eingang]] = [:]

    /// Sitzung, deren Anfragen an `host` die Tabelle beantwortet.
    static func sitzung(host: String, _ tabelle: @escaping @Sendable (Eingang) -> Antwort) -> URLSession {
        sperre.withLock { tabellen[host] = tabelle; eingaenge[host] = [] }
        let k = URLSessionConfiguration.ephemeral
        k.protocolClasses = [Netzattrappe.self]
        k.urlCache = nil
        return URLSession(configuration: k)
    }

    static func eingaenge(host: String) -> [Eingang] {
        sperre.withLock { eingaenge[host] ?? [] }
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        guard let url = request.url, let host = url.host() else { return }
        let eingang = Eingang(
            methode: request.httpMethod ?? "GET",
            pfad: url.path() + (url.query().map { "?" + $0 } ?? ""),
            kopf: request.allHTTPHeaderFields ?? [:],
            rumpf: request.httpBody ?? Self.lesen(request.httpBodyStream)
        )
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

    private static func lesen(_ strom: InputStream?) -> Data {
        guard let strom else { return Data() }
        strom.open()
        defer { strom.close() }
        var daten = Data()
        var puffer = [UInt8](repeating: 0, count: 4096)
        while strom.hasBytesAvailable {
            let n = strom.read(&puffer, maxLength: puffer.count)
            if n <= 0 { break }
            daten.append(puffer, count: n)
        }
        return daten
    }
}
