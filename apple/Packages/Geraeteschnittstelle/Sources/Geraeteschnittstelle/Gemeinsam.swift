import Foundation

/// Die beiden Gerätearten. Der Rohwert ist die Rolle, wie sie im Bonjour-Eintrag und in
/// `device.role` steht.
public enum Geraeteart: String, Sendable, Codable, CaseIterable {
    case verteiler = "manifold"
    case heizung = "heat"

    /// Kennungen beginnen mit `fbh_` (Verteiler) oder `heiz_` (Heizungsgerät). Ältere Firmware
    /// lässt die Rolle im Bonjour-Eintrag leer; dann entscheidet die Kennung.
    public init?(kennung: String) {
        if kennung.hasPrefix("fbh_") {
            self = .verteiler
        } else if kennung.hasPrefix("heiz_") {
            self = .heizung
        } else {
            return nil
        }
    }

    /// Projektname im App-Deskriptor der Firmware; ihn prüft auch das Zurückspielen einer
    /// Sicherung (`backup.app`).
    public var projekt: String {
        switch self {
        case .verteiler: "floor-heating-ctrl"
        case .heizung: "heat-source-ctrl"
        }
    }
}

/// `device` in `/api/state`.
public struct Geraetekennung: Decodable, Sendable, Equatable {
    public var id: String?
    public var mac: String?
    public var ort: String?
    public var modell: String?
    public var rolle: String?
    public var kanaele: Int?

    enum CodingKeys: String, CodingKey {
        case id, mac
        case ort = "site"
        case modell = "model"
        case rolle = "role"
        case kanaele = "channels"
    }

    public var art: Geraeteart? {
        rolle.flatMap(Geraeteart.init(rawValue:)) ?? id.flatMap(Geraeteart.init(kennung:))
    }
}

/// `GET /api/peers`: Geräte im Haus, die dieses Gerät per Bonjour gefunden hat.
public struct Nachbarliste: Decodable, Sendable, Equatable {
    public var nachbarn: [Nachbar]?

    enum CodingKeys: String, CodingKey {
        case nachbarn = "peers"
    }

    public struct Nachbar: Decodable, Sendable, Equatable {
        public var id: String?
        public var ort: String?
        public var rolle: String?
        public var adresse: String?
        public var hostname: String?

        enum CodingKeys: String, CodingKey {
            case id
            case ort = "site"
            case rolle = "role"
            case adresse = "host"
            case hostname
        }
    }
}

/// `GET /api/wifi/scan`: Stand des Suchlaufs, den `POST /api/wifi/scan` anstößt.
public struct Netzsuche: Decodable, Sendable, Equatable {
    public var laeuft: Bool?
    public var netze: [Netz]?

    enum CodingKeys: String, CodingKey {
        case laeuft = "running"
        case netze = "networks"
    }

    public struct Netz: Decodable, Sendable, Equatable, Hashable {
        public var name: String?
        public var signal: Int?
        public var verschluesselt: Bool?

        public init(name: String?, signal: Int?, verschluesselt: Bool?) {
            self.name = name
            self.signal = signal
            self.verschluesselt = verschluesselt
        }

        enum CodingKeys: String, CodingKey {
            case name = "ssid"
            case signal = "rssi"
            case verschluesselt = "secure"
        }
    }
}

/// Antwort auf eine Abfrage mit ETag.
public enum Abfrage<Wert: Sendable>: Sendable {
    /// Neuer Stand, dazu die Rohdaten für die KI und für Felder, die das Modell nicht kennt.
    case neu(Wert, roh: JSONWert)
    /// Das Gerät hat mit 304 geantwortet; es gilt der bisherige Stand.
    case unveraendert

    public var wert: Wert? {
        if case .neu(let w, _) = self { w } else { nil }
    }
}
