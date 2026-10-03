import Foundation

/// Beliebiger JSON-Wert für den Rumpf der Messages-API: Schemata und Werkzeugargumente kommen als
/// JSON aus FoundationModels und müssen unverändert in die Anfrage.
enum JSON: Sendable, Equatable {
    case null
    case bool(Bool)
    case zahl(Double)
    case text(String)
    case liste([JSON])
    case objekt([String: JSON])

    subscript(schluessel: String) -> JSON? {
        get { if case .objekt(let o) = self { o[schluessel] } else { nil } }
        set {
            guard case .objekt(var o) = self else { return }
            o[schluessel] = newValue
            self = .objekt(o)
        }
    }

    subscript(index: Int) -> JSON? {
        if case .liste(let l) = self, l.indices.contains(index) { l[index] } else { nil }
    }

    var alsText: String? {
        if case .text(let t) = self { t } else { nil }
    }

    static func lesen(_ text: String) throws -> JSON {
        try JSONDecoder().decode(JSON.self, from: Data(text.utf8))
    }

    static func aus<T: Encodable>(_ wert: T) throws -> JSON {
        try JSONDecoder().decode(JSON.self, from: try JSONEncoder().encode(wert))
    }

    func daten() throws -> Data {
        let e = JSONEncoder()
        // Sortierte Schlüssel: gleiche Anfrage, gleiche Bytes – Voraussetzung für Treffer im
        // Zwischenspeicher der API.
        e.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        return try e.encode(self)
    }
}

extension JSON: Codable {
    init(from decoder: any Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() {
            self = .null
        } else if let b = try? c.decode(Bool.self) {
            self = .bool(b)
        } else if let z = try? c.decode(Double.self) {
            self = .zahl(z)
        } else if let t = try? c.decode(String.self) {
            self = .text(t)
        } else if let l = try? c.decode([JSON].self) {
            self = .liste(l)
        } else {
            self = .objekt(try c.decode([String: JSON].self))
        }
    }

    func encode(to encoder: any Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .null: try c.encodeNil()
        case .bool(let b): try c.encode(b)
        case .zahl(let z) where z.rounded() == z && abs(z) < 1e15: try c.encode(Int64(z))
        case .zahl(let z): try c.encode(z)
        case .text(let t): try c.encode(t)
        case .liste(let l): try c.encode(l)
        case .objekt(let o): try c.encode(o)
        }
    }
}

extension JSON: ExpressibleByBooleanLiteral, ExpressibleByIntegerLiteral, ExpressibleByStringLiteral,
    ExpressibleByArrayLiteral, ExpressibleByDictionaryLiteral {
    init(booleanLiteral value: Bool) { self = .bool(value) }
    init(integerLiteral value: Int) { self = .zahl(Double(value)) }
    init(stringLiteral value: String) { self = .text(value) }
    init(arrayLiteral elements: JSON...) { self = .liste(elements) }
    init(dictionaryLiteral elements: (String, JSON)...) {
        self = .objekt(Dictionary(elements, uniquingKeysWith: { _, neu in neu }))
    }
}

// MARK: - Schemata

extension JSON {
    /// Schema für ein Werkzeug: ohne die Zusätze von FoundationModels (`title`, `x-order`).
    func werkzeugschema() -> JSON {
        bereinigt(grenzenEntfernen: false)
    }

    /// Schema für `output_config.format`. Die strukturierte Ausgabe kennt keine Zahlen- und
    /// Längengrenzen; sie wandern in die Beschreibung, wie es die offiziellen SDKs halten.
    func ausgabeschema() -> JSON {
        bereinigt(grenzenEntfernen: true)
    }

    private static let grenzen = [
        "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum", "multipleOf",
        "minLength", "maxLength", "minItems", "maxItems", "pattern",
    ]

    private func bereinigt(grenzenEntfernen: Bool) -> JSON {
        switch self {
        case .objekt(let o):
            var neu: [String: JSON] = [:]
            var hinweise: [String] = []
            for (k, v) in o where k != "title" && k != "x-order" {
                if grenzenEntfernen, Self.grenzen.contains(k) {
                    hinweise.append("\(k): \(v.kurz)")
                    continue
                }
                neu[k] = v.bereinigt(grenzenEntfernen: grenzenEntfernen)
            }
            if !hinweise.isEmpty {
                let bisher = neu["description"]?.alsText.map { $0 + " " } ?? ""
                neu["description"] = .text(bisher + "(" + hinweise.sorted().joined(separator: ", ") + ")")
            }
            return .objekt(neu)
        case .liste(let l):
            return .liste(l.map { $0.bereinigt(grenzenEntfernen: grenzenEntfernen) })
        default:
            return self
        }
    }

    private var kurz: String {
        switch self {
        case .zahl(let z) where z.rounded() == z: String(Int64(z))
        case .zahl(let z): String(z)
        case .text(let t): t
        default: (try? daten()).map { String(decoding: $0, as: UTF8.self) } ?? ""
        }
    }
}
