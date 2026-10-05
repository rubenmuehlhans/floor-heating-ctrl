import Foundation

/// Die Modellreihen, die die App anbietet. Welches Modell einer Reihe sie verwendet, steht nicht
/// fest, sondern kommt aus der Models-API: jeweils das zuletzt erschienene.
public enum ClaudeReihe: String, CaseIterable, Codable, Sendable {
    case opus, sonnet, haiku

    /// Präfix der Modellkennungen, etwa `claude-opus-5-5`
    var praefix: String { "claude-\(rawValue)-" }

    public init?(kennung: String) {
        guard let reihe = Self.allCases.first(where: { kennung.hasPrefix($0.praefix) }) else { return nil }
        self = reihe
    }
}

/// Was ein Modell an der Anfrageform der App verträgt
public struct ClaudeFaehigkeiten: Hashable, Codable, Sendable {
    /// `thinking: {type: "adaptive"}`; ohne denkt das Modell nicht, ein festes Budget setzt die
    /// App nicht.
    public var adaptivesDenken: Bool
    /// `output_config.effort`
    public var aufwand: Bool
    /// Kontextfenster in Token
    public var kontext: Int?

    public init(adaptivesDenken: Bool, aufwand: Bool, kontext: Int? = nil) {
        self.adaptivesDenken = adaptivesDenken
        self.aufwand = aufwand
        self.kontext = kontext
    }

    /// Ohne Angabe der API: Haiku vor Version 5 kennt weder adaptives Denken noch Aufwand, Opus
    /// und Sonnet beides.
    public static func vermutet(_ modell: String) -> ClaudeFaehigkeiten {
        if let v = ClaudeKonfiguration.version(modell, .haiku), v.lexicographicallyPrecedes([5]) {
            return ClaudeFaehigkeiten(adaptivesDenken: false, aufwand: false, kontext: 200_000)
        }
        return ClaudeFaehigkeiten(adaptivesDenken: true, aufwand: true)
    }
}

public struct ClaudeModelleintrag: Hashable, Codable, Sendable {
    /// Kennung für die Messages-API, etwa `claude-opus-5-5`
    public var kennung: String
    /// Anzeigename laut API, etwa „Claude Opus 5.5“
    public var name: String
    public var erschienen: Date?
    /// Laut API; fehlen sie, gilt `ClaudeFaehigkeiten.vermutet`.
    public var faehigkeiten: ClaudeFaehigkeiten?

    public init(kennung: String, name: String, erschienen: Date? = nil, faehigkeiten: ClaudeFaehigkeiten? = nil) {
        self.kennung = kennung
        self.name = name
        self.erschienen = erschienen
        self.faehigkeiten = faehigkeiten
    }
}

/// Fragt die Models-API nach den neuesten Modellen je Reihe.
public enum ClaudeModelle {
    public static let modelsAPI = URL(string: "https://api.anthropic.com/v1/models")!

    /// Stand beim Bau der App; gilt, bis die API zum ersten Mal geantwortet hat.
    public static let bekannt: [ClaudeReihe: ClaudeModelleintrag] = [
        .opus: ClaudeModelleintrag(kennung: "claude-opus-5-5", name: "Claude Opus 5.5"),
        .sonnet: ClaudeModelleintrag(kennung: "claude-sonnet-5-5", name: "Claude Sonnet 5.5"),
        .haiku: ClaudeModelleintrag(kennung: "claude-haiku-4-5", name: "Claude Haiku 4.5"),
    ]

    /// Das zuletzt erschienene Modell jeder Reihe, die die API für diesen Schlüssel führt.
    public static func neueste(
        schluessel: String, adresse: URL = modelsAPI, transport: ClaudeTransport = .standard
    ) async throws -> [ClaudeReihe: ClaudeModelleintrag] {
        var modelle: [Seite.Modell] = []
        var nach: String?
        repeat {
            var komponenten = URLComponents(url: adresse, resolvingAgainstBaseURL: false)!
            komponenten.queryItems = [URLQueryItem(name: "limit", value: "1000")]
                + (nach.map { [URLQueryItem(name: "after_id", value: $0)] } ?? [])
            var anfrage = URLRequest(url: komponenten.url!)
            anfrage.setValue(schluessel, forHTTPHeaderField: "x-api-key")
            anfrage.setValue("2023-06-01", forHTTPHeaderField: "anthropic-version")
            let (daten, antwort) = try await transport.sitzung.data(for: anfrage)
            switch (antwort as? HTTPURLResponse)?.statusCode {
            case 200: break
            case 401: throw ClaudeFehler.schluesselUngueltig
            case let status: throw ClaudeFehler.anfrage(status: status ?? 0, art: "models", meldung: String(decoding: daten.prefix(500), as: UTF8.self))
            }
            let seite = try decoder.decode(Seite.self, from: daten)
            modelle += seite.data
            nach = seite.has_more == true ? seite.last_id : nil
        } while nach != nil
        return auswahl(modelle)
    }

    /// Je Reihe das Modell mit dem jüngsten Erscheinungsdatum; ohne Datum zählt die Reihenfolge der
    /// API, die das neueste zuerst nennt.
    static func auswahl(_ modelle: [Seite.Modell]) -> [ClaudeReihe: ClaudeModelleintrag] {
        var ergebnis: [ClaudeReihe: ClaudeModelleintrag] = [:]
        for reihe in ClaudeReihe.allCases {
            let passend = modelle.enumerated().filter { $0.element.id.hasPrefix(reihe.praefix) }
            let neuestes = passend.max { a, b in
                let da = a.element.created_at ?? .distantPast, db = b.element.created_at ?? .distantPast
                return da != db ? da < db : a.offset > b.offset
            }?.element
            if let m = neuestes {
                ergebnis[reihe] = ClaudeModelleintrag(kennung: m.id, name: m.display_name ?? m.id, erschienen: m.created_at,
                                                      faehigkeiten: m.faehigkeiten)
            }
        }
        return ergebnis
    }

    /// Eine Seite von `GET /v1/models`
    struct Seite: Decodable {
        struct Modell: Decodable {
            var id: String
            var display_name: String?
            var created_at: Date?
            var max_input_tokens: Int?
            var capabilities: JSON?

            var faehigkeiten: ClaudeFaehigkeiten? {
                guard let c = capabilities else { return nil }
                return ClaudeFaehigkeiten(
                    adaptivesDenken: c["thinking"]?["types"]?["adaptive"]?["supported"] == .bool(true),
                    aufwand: c["effort"]?["supported"] == .bool(true),
                    kontext: max_input_tokens)
            }
        }

        var data: [Modell]
        var has_more: Bool?
        var last_id: String?
    }

    private static let decoder: JSONDecoder = {
        let d = JSONDecoder()
        // RFC 3339, mit und ohne Sekundenbruchteile
        d.dateDecodingStrategy = .custom { decoder in
            let text = try decoder.singleValueContainer().decode(String.self)
            if let datum = try? Date(text, strategy: .iso8601) { return datum }
            if let datum = try? Date(text, strategy: Date.ISO8601FormatStyle(includingFractionalSeconds: true)) { return datum }
            throw DecodingError.dataCorrupted(.init(codingPath: decoder.codingPath, debugDescription: "Datum \(text)"))
        }
        return d
    }()
}
