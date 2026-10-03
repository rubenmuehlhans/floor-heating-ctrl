import Foundation
import Geraeteschnittstelle
@testable import Anlage

/// Vorgetäuschtes Netz: beantwortet Anfragen je Host aus einer Tabelle und merkt sich, was ankam.
/// Jeder Test nimmt einen eigenen Host, damit parallel laufende Tests sich nicht stören.
final class Netzattrappe: URLProtocol, @unchecked Sendable {
    struct Antwort: Sendable {
        var status = 200
        var kopf: [String: String] = [:]
        var rumpf = Data(#"{"ok":true}"#.utf8)

        static let ok = Antwort()

        static func json(_ wert: JSONWert, etag: String? = nil) -> Antwort {
            var kopf = ["Content-Type": "application/json"]
            kopf["ETag"] = etag
            return Antwort(kopf: kopf, rumpf: (try? wert.daten()) ?? Data())
        }

        static func fehler(_ status: Int, _ meldung: String) -> Antwort {
            .init(status: status, kopf: ["Content-Type": "application/json"],
                  rumpf: (try? JSONWert.objekt(["ok": false, "error": .text(meldung)]).daten()) ?? Data())
        }
    }

    struct Eingang: Sendable {
        var methode: String
        var pfad: String
        var rumpf: Data

        /// „POST /api/room/1/target {"target_c":35}“ – der Rumpf kompakt mit sortierten Schlüsseln
        var kurz: String {
            let json = (try? JSONWert.lesen(rumpf))?.kompakt
            return "\(methode) \(pfad)" + (json.map { " \($0)" } ?? "")
        }
    }

    private static let sperre = NSLock()
    nonisolated(unsafe) private static var tabellen: [String: @Sendable (Eingang) -> Antwort] = [:]
    nonisolated(unsafe) private static var eingaenge: [String: [Eingang]] = [:]

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

    /// Nur Befehle und Schreibzugriffe, ohne die laufenden Abfragen
    static func befehle(host: String) -> [String] {
        eingaenge(host: host).filter { $0.methode != "GET" }.map(\.kurz)
    }

    static func vergessen(host: String) {
        sperre.withLock { eingaenge[host] = [] }
    }

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        guard let url = request.url, let host = url.host() else { return }
        let eingang = Eingang(
            methode: request.httpMethod ?? "GET",
            pfad: url.path() + (url.query().map { "?" + $0 } ?? ""),
            rumpf: request.httpBody ?? Self.lesen(request.httpBodyStream))
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

/// Ein Verteiler im Speicher, ausgehend von den Antworten der Attrappe. Er antwortet wie die
/// Firmware, auch mit ihren stillen Nulloperationen: Ein Sollwert außerhalb 5–35 °C und ein
/// Befehl an einen Kanal in der Messfahrt werden quittiert, aber nicht ausgeführt. Mit
/// `uebernimmt = false` verhält er sich bei jedem Befehl so.
final class Verteilernachbildung: @unchecked Sendable {
    static let kennung = "fbh_a1b2c3"

    private let sperre = NSLock()
    private var zustand: JSONWert
    private var konfiguration: JSONWert
    private var revision = 1
    private var _uebernimmt = true

    var uebernimmt: Bool {
        get { sperre.withLock { _uebernimmt } }
        set { sperre.withLock { _uebernimmt = newValue } }
    }

    init() throws {
        zustand = try Aufnahme.json("attrappe/verteiler-state.json")
        konfiguration = try Aufnahme.json("attrappe/verteiler-config.json")
    }

    /// Zustand und Konfiguration von außen setzen, etwa einen fahrenden Kanal
    func aendern(_ aenderung: (inout JSONWert, inout JSONWert) -> Void) {
        sperre.withLock {
            aenderung(&zustand, &konfiguration)
            revision += 1
        }
    }

    var jetzigeKonfiguration: JSONWert { sperre.withLock { konfiguration } }

    func antwort(_ e: Netzattrappe.Eingang) -> Netzattrappe.Antwort {
        sperre.withLock { beantworten(e) }
    }

    private func beantworten(_ e: Netzattrappe.Eingang) -> Netzattrappe.Antwort {
        let pfad = String(e.pfad.split(separator: "?").first ?? "")
        let rumpf = (try? JSONWert.lesen(e.rumpf)) ?? .null
        switch (e.methode, pfad) {
        case ("GET", "/api/state"): return .json(zustand, etag: "\"\(revision)\"")
        case ("GET", "/api/config"): return .json(konfiguration)
        case ("GET", "/api/ble"): return .json(["devices": []])
        case ("GET", "/api/calib"): return .json(["from": 0, "samples": []])
        case ("GET", "/api/config/backup"):
            var sicherung = konfiguration
            sicherung["backup"] = ["app": "floor-heating-ctrl", "device_id": .text(Self.kennung)]
            return .json(sicherung)
        case ("PUT", "/api/config"):
            if _uebernimmt { konfiguration = Schreibweg.zusammengefuehrt(konfiguration, rumpf) }
        case ("POST", "/api/calib/accept"):
            let kanal = zustand["calib"]?["channel"]?.alsGanzzahl ?? 0
            if _uebernimmt, zustand["calib"]?["state"]?.alsText == "done" {
                Self.element(&konfiguration["channels"], kanal) { $0["calibrated"] = true }
            }
        case ("POST", "/api/calib/discard"):
            if _uebernimmt { zustand["calib"]?["state"] = "idle" }
        default:
            let teile = pfad.split(separator: "/").map(String.init)
            guard teile.count == 4, teile[0] == "api" else { return .fehler(404, "Unbekannter Pfad") }
            switch (teile[1], teile[3]) {
            case ("room", "target"):
                // Die Firmware quittiert Werte außerhalb 5–35 °C, ohne sie zu übernehmen.
                if _uebernimmt, let n = Int(teile[2]), let t = rumpf["target_c"]?.alsZahl, (5...35).contains(t) {
                    Self.element(&zustand["rooms"], n) { $0["target_c"] = .zahl(t) }
                }
            case ("room", "mode"):
                if _uebernimmt, let n = Int(teile[2]), let m = rumpf["mode"] {
                    Self.element(&zustand["rooms"], n) { $0["mode"] = m }
                }
            case ("channel", "cmd"):
                // Auf, Zu und Anhalten setzen den Handbetrieb, „auto“ hebt ihn auf, eine
                // Zwischenstellung lässt ihn, wie er ist.
                let hand: JSONWert? = switch rumpf["cmd"]?.alsText {
                case "open", "close", "stop": true
                case "auto": false
                default: nil
                }
                let alle = zustand["channels"]?.alsListe?.compactMap { $0["id"]?.alsGanzzahl } ?? []
                let ziele = teile[2] == "all" ? alle : Int(teile[2]).map({ [$0] }) ?? []
                for n in ziele where _uebernimmt {
                    // Ein Kanal in der Messfahrt nimmt keine Befehle an.
                    Self.element(&zustand["channels"], n) { k in
                        if k["reserved"]?.alsBool != true, let hand { k["manual"] = hand }
                    }
                }
            default:
                return .fehler(404, "Unbekannter Pfad")
            }
        }
        revision += 1
        return .ok
    }

    /// Ändert das Listenelement mit der Kennung `id`.
    static func element(_ liste: inout JSONWert?, _ id: Int, _ aenderung: (inout JSONWert) -> Void) {
        guard case .liste(var l)? = liste else { return }
        for i in l.indices where l[i]["id"]?.alsGanzzahl == id {
            aenderung(&l[i])
        }
        liste = .liste(l)
    }
}
