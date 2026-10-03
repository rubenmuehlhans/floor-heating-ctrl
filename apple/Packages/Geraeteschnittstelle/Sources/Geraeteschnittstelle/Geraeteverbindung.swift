import Foundation

extension URLSession {
    /// Sitzung für die Geräte im Heimnetz: ohne Zwischenspeicher, damit ein 304 beim Aufrufer
    /// ankommt, und mit kurzen Zeitgrenzen – ein Gerät im Haus antwortet schnell oder gar nicht.
    public static let geraete: URLSession = {
        let k = URLSessionConfiguration.ephemeral
        k.requestCachePolicy = .reloadIgnoringLocalCacheData
        k.urlCache = nil
        k.timeoutIntervalForRequest = 6
        k.waitsForConnectivity = false
        k.httpMaximumConnectionsPerHost = 2
        return URLSession(configuration: k)
    }()
}

/// HTTP-Verbindung zu einem Gerät.
///
/// Je Gerät läuft höchstens eine Anfrage gleichzeitig: Die ESP32 halten nur wenige Verbindungen
/// offen und bedienen nebenher ihre Weboberfläche und die Nachbargeräte. Weiterleitungen werden
/// nie verfolgt, weil ein Gerät im Zugangspunktbetrieb jeden unbekannten Pfad umleitet.
public actor Geraeteverbindung {
    public nonisolated let basis: URL
    private let sitzung: URLSession
    private var etags: [String: String] = [:]
    private var warteschlange: Task<Void, Never>?

    public init(basis: URL, sitzung: URLSession = .geraete) {
        self.basis = basis
        self.sitzung = sitzung
    }

    // MARK: Lesen

    /// GET mit ETag. Hat sich seit der letzten Abfrage nichts geändert, antwortet das Gerät mit
    /// 304, und es gilt der bisherige Stand. Nur der Verteiler sendet ein ETag.
    public func abfragen<Wert: Decodable & Sendable>(
        _ pfad: String, als typ: Wert.Type = Wert.self, etagNutzen: Bool = true
    ) async throws -> Abfrage<Wert> {
        var kopf: [String: String] = [:]
        if etagNutzen, let etag = etags[pfad] {
            kopf["If-None-Match"] = etag
        }
        let (daten, antwort) = try await ausfuehren(try anfrage(pfad, kopf: kopf))
        if antwort.statusCode == 304 {
            return .unveraendert
        }
        try Self.pruefen(daten, antwort)
        if let etag = antwort.value(forHTTPHeaderField: "ETag") {
            etags[pfad] = etag
        }
        return .neu(try Self.dekodieren(typ, daten), roh: try Self.rohdaten(daten))
    }

    public func holen<Wert: Decodable & Sendable>(_ pfad: String, als typ: Wert.Type = Wert.self) async throws -> Wert {
        let (daten, antwort) = try await ausfuehren(try anfrage(pfad))
        try Self.pruefen(daten, antwort)
        return try Self.dekodieren(typ, daten)
    }

    /// Antwort ohne Auswertung, etwa CSV oder eine Sicherungsdatei.
    public func holenRoh(_ pfad: String, zeitlimit: TimeInterval? = nil) async throws -> Data {
        let (daten, antwort) = try await ausfuehren(try anfrage(pfad, zeitlimit: zeitlimit))
        try Self.pruefen(daten, antwort)
        return daten
    }

    /// Eine Datei ab Stelle `ab`, für Dateien, die nur wachsen, wie das Protokoll des
    /// Leitstands. Ist nichts Neues da (416), kommt ein leerer Inhalt zurück.
    public func holenAb(_ pfad: String, ab: Int, zeitlimit: TimeInterval? = nil) async throws -> Data {
        let kopf = ab > 0 ? ["Range": "bytes=\(ab)-"] : [:]
        let (daten, antwort) = try await ausfuehren(try anfrage(pfad, kopf: kopf, zeitlimit: zeitlimit))
        if antwort.statusCode == 416 { return Data() }
        try Self.pruefen(daten, antwort)
        // Ein Gerät, das Range nicht kennt, schickt die ganze Datei; dann gilt sie ab `ab`.
        if ab > 0, antwort.statusCode == 200 {
            return daten.count > ab ? daten.subdata(in: ab..<daten.count) : Data()
        }
        return daten
    }

    // MARK: Schreiben

    /// Befehl oder Konfigurationsänderung. Die Firmware antwortet mit `{"ok":true}` oder einer
    /// deutschen Fehlermeldung. Einige Befehle bestätigt sie, ohne dass sie wirken – wer sicher
    /// sein will, liest den Zustand zurück.
    @discardableResult
    public func senden(
        _ pfad: String, methode: String = "POST", json: JSONWert? = nil,
        daten rumpf: Data? = nil, inhaltstyp: String? = nil, zeitlimit: TimeInterval? = nil
    ) async throws -> Data {
        var a = try anfrage(pfad, methode: methode, zeitlimit: zeitlimit)
        if let json {
            a.httpBody = try json.daten()
            a.setValue("application/json", forHTTPHeaderField: "Content-Type")
        } else if let rumpf {
            a.httpBody = rumpf
            a.setValue(inhaltstyp ?? "application/octet-stream", forHTTPHeaderField: "Content-Type")
        }
        let (daten, antwort) = try await ausfuehren(a)
        try Self.pruefen(daten, antwort)
        return daten
    }

    /// Nach einem Neustart oder einer Konfigurationsänderung beginnt der Änderungszähler neu.
    public func etagsVergessen() {
        etags.removeAll()
    }

    // MARK: Ablauf

    private func anfrage(
        _ pfad: String, methode: String = "GET", kopf: [String: String] = [:], zeitlimit: TimeInterval? = nil
    ) throws -> URLRequest {
        guard let url = URL(string: pfad, relativeTo: basis)?.absoluteURL else {
            throw Geraetefehler.unzulaessig("Ungültiger Pfad: \(pfad)")
        }
        var a = URLRequest(url: url)
        a.httpMethod = methode
        a.cachePolicy = .reloadIgnoringLocalCacheData
        if let zeitlimit {
            a.timeoutInterval = zeitlimit
        }
        for (k, v) in kopf {
            a.setValue(v, forHTTPHeaderField: k)
        }
        return a
    }

    private func ausfuehren(_ anfrage: URLRequest) async throws -> (Data, HTTPURLResponse) {
        let sitzung = self.sitzung
        return try await nacheinander {
            do {
                let (daten, antwort) = try await sitzung.data(for: anfrage, delegate: KeineWeiterleitung.einzige)
                guard let http = antwort as? HTTPURLResponse else {
                    throw Geraetefehler.unerwarteteAntwort("keine HTTP-Antwort")
                }
                return (daten, http)
            } catch let fehler as URLError {
                switch fehler.code {
                case .cancelled: throw CancellationError()
                case .timedOut: throw Geraetefehler.zeitueberschreitung
                case .appTransportSecurityRequiresSecureConnection:
                    // Ohne TLS erlaubt das System nur Adressen im lokalen Netz.
                    throw Geraetefehler.nichtErreichbar("Ohne Verschlüsselung spricht die App nur Geräte im lokalen Netz an, also unter einer IP-Adresse oder einem Namen wie kessel.local.")
                default: throw Geraetefehler.nichtErreichbar(fehler.localizedDescription)
                }
            }
        }
    }

    /// Reiht die Arbeit hinter die laufende Anfrage dieses Geräts.
    private func nacheinander<T: Sendable>(_ arbeit: @escaping @Sendable () async throws -> T) async throws -> T {
        let vorher = warteschlange
        let aufgabe = Task {
            await vorher?.value
            try Task.checkCancellation()
            return try await arbeit()
        }
        warteschlange = Task { _ = await aufgabe.result }
        return try await withTaskCancellationHandler {
            try await aufgabe.value
        } onCancel: {
            aufgabe.cancel()
        }
    }

    // MARK: Auswertung

    static func pruefen(_ daten: Data, _ antwort: HTTPURLResponse) throws {
        let status = antwort.statusCode
        if (300..<400).contains(status) {
            throw Geraetefehler.weiterleitung(ziel: antwort.value(forHTTPHeaderField: "Location"))
        }
        let abgelehnt = ablehnung(in: daten)
        guard (200..<300).contains(status) else {
            throw abgelehnt.map { Geraetefehler.meldung($0, status: status) } ?? Geraetefehler.status(status)
        }
        if let abgelehnt {
            throw Geraetefehler.meldung(abgelehnt, status: status)
        }
    }

    /// Begründung aus `{"ok":false,"error":"…"}`, sonst nil.
    static func ablehnung(in daten: Data) -> String? {
        guard daten.first == UInt8(ascii: "{"), let wert = try? JSONWert.lesen(daten),
              wert["ok"]?.alsBool == false else { return nil }
        return wert["error"]?.alsText ?? "Das Gerät hat die Anfrage abgelehnt."
    }

    static func dekodieren<Wert: Decodable>(_ typ: Wert.Type, _ daten: Data) throws -> Wert {
        do {
            return try JSONDecoder().decode(typ, from: daten)
        } catch let fehler as DecodingError {
            throw Geraetefehler.unerwarteteAntwort(fehler.kurzbeschreibung)
        }
    }

    static func rohdaten(_ daten: Data) throws -> JSONWert {
        do {
            return try JSONWert.lesen(daten)
        } catch {
            throw Geraetefehler.unerwarteteAntwort("kein JSON")
        }
    }
}

/// Lässt Weiterleitungen unbeantwortet; URLSession liefert dann die 3xx-Antwort selbst.
private final class KeineWeiterleitung: NSObject, URLSessionTaskDelegate, @unchecked Sendable {
    static let einzige = KeineWeiterleitung()

    func urlSession(
        _ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
        newRequest request: URLRequest
    ) async -> URLRequest? {
        nil
    }
}

private extension DecodingError {
    var kurzbeschreibung: String {
        let (kontext, art): (DecodingError.Context?, String) = switch self {
        case .typeMismatch(_, let k): (k, "falscher Typ")
        case .valueNotFound(_, let k): (k, "Wert fehlt")
        case .keyNotFound(let s, let k): (k, "Schlüssel \(s.stringValue) fehlt")
        case .dataCorrupted(let k): (k, "beschädigt")
        @unknown default: (nil, "unbekannt")
        }
        let pfad = kontext?.codingPath.map(\.stringValue).joined(separator: ".") ?? ""
        return pfad.isEmpty ? art : "\(art) bei \(pfad)"
    }
}
