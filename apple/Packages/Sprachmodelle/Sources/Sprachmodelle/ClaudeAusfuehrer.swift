import Foundation
import FoundationModels

/// Führt Anfragen einer `LanguageModelSession` über die Messages-API aus.
///
/// Werkzeuge ruft nicht dieser Ausführer auf, sondern die Sitzung: Er meldet die Aufrufe in den
/// Kanal, die Sitzung führt sie aus und stellt die nächste Anfrage mit den Ergebnissen.
public struct ClaudeAusfuehrer: LanguageModelExecutor {
    public typealias Configuration = ClaudeKonfiguration
    public typealias Model = ClaudeSprachmodell

    let konfiguration: ClaudeKonfiguration

    public init(configuration: ClaudeKonfiguration) throws {
        konfiguration = configuration
    }

    public nonisolated(nonsending) func respond(
        to request: LanguageModelExecutorGenerationRequest, model: ClaudeSprachmodell,
        streamingInto channel: LanguageModelExecutorGenerationChannel
    ) async throws {
        let rumpf = try ClaudeAnfrage(anfrage: request, konfiguration: konfiguration).rumpf().daten()
        // Überlastung und Serverfehler kommen als HTTP-Status oder als Fehlerereignis im Strom.
        // Beides wird wiederholt, solange noch nichts im Kanal steht; danach ließe sich die
        // Antwort nicht mehr ohne Doppelungen neu beginnen.
        var versuch = 0
        while true {
            var gesendet = false
            do {
                let zeilen = try await verbinden(rumpf)
                // Eigene Kennung je Aufruf: `request.id` bleibt über alle Runden einer
                // Werkzeugschleife gleich, und doppelte Eintragskennungen bringen die Sitzung
                // durcheinander.
                var zustand = Stromzustand(praefix: UUID().uuidString)
                for try await zeile in zeilen {
                    guard let ereignis = try ClaudeStrom.auswerten(zeile: zeile) else { continue }
                    for aktion in try zustand.verarbeiten(ereignis) {
                        gesendet = true
                        await channel.send(aktion.ereignis)
                    }
                    if zustand.beendet { break }
                }
                try zustand.abschluss()
                return
            } catch let fehler as ClaudeFehler
                where fehler.voruebergehend && !gesendet && versuch < konfiguration.wartezeiten.count {
                try await Task.sleep(for: konfiguration.wartezeiten[versuch])
                versuch += 1
            } catch ClaudeFehler.imStrom(art: "overloaded_error", meldung: _) {
                throw ClaudeFehler.ueberlastet(status: 529)
            }
        }
    }

    /// Stellt die Anfrage und gibt die Zeilen des Datenstroms zurück.
    private func verbinden(_ rumpf: Data) async throws -> AsyncLineSequence<URLSession.AsyncBytes> {
        var anfrage = URLRequest(url: konfiguration.adresse)
        anfrage.httpMethod = "POST"
        anfrage.httpBody = rumpf
        anfrage.setValue("application/json", forHTTPHeaderField: "content-type")
        anfrage.setValue(konfiguration.schluessel, forHTTPHeaderField: "x-api-key")
        anfrage.setValue("2023-06-01", forHTTPHeaderField: "anthropic-version")
        if konfiguration.ersatzmodell {
            anfrage.setValue("server-side-fallback-2026-07-01", forHTTPHeaderField: "anthropic-beta")
        }

        let (bytes, antwort) = try await konfiguration.transport.sitzung.bytes(for: anfrage)
        guard let http = antwort as? HTTPURLResponse else {
            throw ClaudeFehler.unvollstaendig
        }
        if http.statusCode == 200 {
            return bytes.lines
        }
        let (art, meldung) = try await Self.fehlermeldung(bytes)
        switch http.statusCode {
        case 401:
            throw ClaudeFehler.schluesselUngueltig
        case 403:
            throw ClaudeFehler.keineBerechtigung(meldung)
        case 429:
            throw LanguageModelError.rateLimited(.init(resetDate: Self.wiederAb(http), debugDescription: meldung))
        case 400 where meldung.localizedCaseInsensitiveContains("prompt is too long"):
            throw LanguageModelError.contextSizeExceeded(.init(contextSize: 1_000_000, tokenCount: 0, debugDescription: meldung))
        case 500...:
            throw ClaudeFehler.ueberlastet(status: http.statusCode)
        default:
            throw ClaudeFehler.anfrage(status: http.statusCode, art: art, meldung: meldung)
        }
    }

    /// `{"type":"error","error":{"type":…,"message":…}}`
    private static func fehlermeldung(_ bytes: URLSession.AsyncBytes) async throws -> (String, String) {
        var daten = Data()
        for try await b in bytes {
            daten.append(b)
            if daten.count > 64_000 { break }
        }
        let fehler = (try? JSON.lesen(String(decoding: daten, as: UTF8.self)))?["error"]
        return (fehler?["type"]?.alsText ?? "unbekannt", fehler?["message"]?.alsText ?? String(decoding: daten.prefix(500), as: UTF8.self))
    }

    private static func wiederAb(_ http: HTTPURLResponse) -> Date? {
        if let s = http.value(forHTTPHeaderField: "retry-after").flatMap(Double.init) {
            return Date(timeIntervalSinceNow: s)
        }
        return http.value(forHTTPHeaderField: "anthropic-ratelimit-requests-reset").flatMap {
            try? Date($0, strategy: .iso8601)
        }
    }
}
