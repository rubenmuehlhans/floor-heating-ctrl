import Foundation

/// Ein beliebiger JSON-Wert. Die App hält damit die Rohantworten der Geräte – für die KI und für
/// Felder, die eine neuere Firmware mitbringt – und baut Teilangaben für `PUT /api/config`.
public enum JSONWert: Sendable, Hashable {
    case null
    case bool(Bool)
    case zahl(Double)
    case text(String)
    case liste([JSONWert])
    case objekt([String: JSONWert])
}

extension JSONWert: Codable {
    public init(from decoder: any Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() {
            self = .null
        } else if let b = try? c.decode(Bool.self) {
            self = .bool(b)
        } else if let z = try? c.decode(Double.self) {
            self = .zahl(z)
        } else if let t = try? c.decode(String.self) {
            self = .text(t)
        } else if let l = try? c.decode([JSONWert].self) {
            self = .liste(l)
        } else {
            self = .objekt(try c.decode([String: JSONWert].self))
        }
    }

    public func encode(to encoder: any Encoder) throws {
        var c = encoder.singleValueContainer()
        switch self {
        case .null: try c.encodeNil()
        case .bool(let b): try c.encode(b)
        // Ganze Zahlen ohne Nachkommastelle, wie cJSON sie schreibt
        case .zahl(let z) where z.rounded() == z && abs(z) < 1e15: try c.encode(Int64(z))
        case .zahl(let z): try c.encode(z)
        case .text(let t): try c.encode(t)
        case .liste(let l): try c.encode(l)
        case .objekt(let o): try c.encode(o)
        }
    }
}

// MARK: - Zugriff

extension JSONWert {
    public subscript(schluessel: String) -> JSONWert? {
        get { if case .objekt(let o) = self { o[schluessel] } else { nil } }
        set {
            guard case .objekt(var o) = self else { return }
            o[schluessel] = newValue
            self = .objekt(o)
        }
    }

    public subscript(index: Int) -> JSONWert? {
        if case .liste(let l) = self, l.indices.contains(index) { l[index] } else { nil }
    }

    public var alsZahl: Double? {
        if case .zahl(let z) = self { z } else { nil }
    }

    public var alsGanzzahl: Int? {
        alsZahl.flatMap { Int(exactly: $0) }
    }

    public var alsText: String? {
        if case .text(let t) = self { t } else { nil }
    }

    public var alsBool: Bool? {
        if case .bool(let b) = self { b } else { nil }
    }

    public var alsListe: [JSONWert]? {
        if case .liste(let l) = self { l } else { nil }
    }

    public var alsObjekt: [String: JSONWert]? {
        if case .objekt(let o) = self { o } else { nil }
    }
}

// MARK: - Lesen und Schreiben

extension JSONWert {
    public static func lesen(_ daten: Data) throws -> JSONWert {
        try JSONDecoder().decode(JSONWert.self, from: daten)
    }

    /// Kompakte Ausgabe mit sortierten Schlüsseln – gleicher Inhalt ergibt gleiche Bytes, was dem
    /// Zwischenspeicher der KI zugutekommt.
    public func daten() throws -> Data {
        let e = JSONEncoder()
        e.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        return try e.encode(self)
    }

    public var kompakt: String {
        (try? daten()).map { String(decoding: $0, as: UTF8.self) } ?? "null"
    }
}

// MARK: - Zugangsdaten

extension JSONWert {
    /// Schlüssel, die nie an die KI gehen: Kennwörter, Benutzer, WLAN-Name, Adresse des
    /// MQTT-Brokers (sie kann Zugangsdaten enthalten), Adressen und Themen der Pumpenrelais und die
    /// Schlüssel verschlüsselter Thermometer (`bindkey`, nur in Sicherungen).
    public static let zugangsschluessel: Set<String> = [
        "pass", "ap_pass", "password", "user", "ssid", "uri", "host", "topic", "bindkey",
    ]

    /// Kopie ohne Zugangsdaten. Die Kennzeichen `pass_set` und `ap_pass_set` bleiben stehen: Dass
    /// ein Kennwort gesetzt ist, darf die KI wissen.
    public func ohneZugangsdaten() -> JSONWert {
        switch self {
        case .objekt(let o):
            var neu: [String: JSONWert] = [:]
            for (k, v) in o where !Self.zugangsschluessel.contains(k) {
                neu[k] = v.ohneZugangsdaten()
            }
            return .objekt(neu)
        case .liste(let l):
            return .liste(l.map { $0.ohneZugangsdaten() })
        default:
            return self
        }
    }
}

// MARK: - Literale

extension JSONWert: ExpressibleByNilLiteral, ExpressibleByBooleanLiteral, ExpressibleByIntegerLiteral,
    ExpressibleByFloatLiteral, ExpressibleByStringLiteral, ExpressibleByArrayLiteral,
    ExpressibleByDictionaryLiteral {
    public init(nilLiteral: ()) { self = .null }
    public init(booleanLiteral value: Bool) { self = .bool(value) }
    public init(integerLiteral value: Int) { self = .zahl(Double(value)) }
    public init(floatLiteral value: Double) { self = .zahl(value) }
    public init(stringLiteral value: String) { self = .text(value) }
    public init(arrayLiteral elements: JSONWert...) { self = .liste(elements) }
    public init(dictionaryLiteral elements: (String, JSONWert)...) {
        self = .objekt(Dictionary(elements, uniquingKeysWith: { _, neu in neu }))
    }
}
