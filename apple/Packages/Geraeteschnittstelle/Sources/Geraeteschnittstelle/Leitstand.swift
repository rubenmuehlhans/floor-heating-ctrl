import Foundation

/// Zugang zum Leitstand (`apps/station`).
///
/// Der Leitstand ist kein Regelgerät: Er empfängt Funkthermometer in Kesselnähe, reicht die
/// Außentemperatur an die Heizungsgeräte weiter und zeigt den Zustand der Anlage auf seinem
/// Bildschirm. Die App liest seinen Zustand, verwaltet die Schlüssel seiner Funkthermometer und
/// ordnet den Außenfühler zu. Deshalb steht er neben `Geraeteart` und nicht darin.
public struct Leitstand: Sendable {
    /// Rolle im Bonjour-Eintrag und in `device.role`
    public static let rolle = "station"

    /// Rolle `station` oder, bei fehlender Rolle, eine Kennung mit `lst_`
    public static func istLeitstand(rolle: String?, kennung: String?) -> Bool {
        if let rolle, !rolle.isEmpty { return rolle == Self.rolle }
        return kennung?.hasPrefix("lst_") ?? false
    }

    /// Seiten der Anzeige am Gerät, wie `POST /api/display` sie annimmt
    /// Seiten der Anzeige. Räume, Verlauf, Meldungen und Geräte gibt es nur mit PSRAM (Core2).
    public enum Seite: String, Sendable, CaseIterable {
        case anlage
        case raeume
        case verlauf
        case meldungen
        case geraete
        case leitstand

        public var titel: String {
            switch self {
            case .anlage: "Anlage"
            case .raeume: "Räume"
            case .verlauf: "Verlauf"
            case .meldungen: "Meldungen"
            case .geraete: "Geräte"
            case .leitstand: "Leitstand"
            }
        }

        /// Die Seiten, die ein Gerät zeigt: mit PSRAM alle sechs, sonst Anlage und Leitstand
        public static func verfuegbar(psram: Bool) -> [Seite] {
            psram ? allCases : [.anlage, .leitstand]
        }
    }

    public let verbindung: Geraeteverbindung

    public init(adresse: URL, sitzung: URLSession = .geraete) {
        verbindung = Geraeteverbindung(basis: adresse, sitzung: sitzung)
    }

    public func zustand() async throws -> Leitstandzustand {
        try await verbindung.holen("/api/state")
    }

    /// Dieselbe Form wie am Verteiler, dazu die Adresse des Außenfühlers.
    public func thermometerliste() async throws -> Thermometerliste {
        try await verbindung.holen("/api/ble", als: Thermometerliste.self)
    }

    /// Schlüssel eines verschlüsselt sendenden Thermometers hinterlegen; `nil` entfernt ihn.
    public func thermometerSchluessel(mac: String, schluessel: String?) async throws {
        try await verbindung.senden("/api/ble/key", json: ["mac": .text(mac), "bindkey": .text(schluessel ?? "")])
    }

    /// Außenfühler zuordnen; `nil` hebt die Zuordnung auf.
    public func aussenfuehler(_ mac: String?) async throws {
        try await verbindung.senden("/api/config", methode: "PUT", json: ["outdoor": ["mac": .text(mac ?? "")]])
    }

    /// Der Bildschirm als BMP, Zeile für Zeile aus der Anzeige gelesen
    public func bildschirm() async throws -> Data {
        try await verbindung.holenRoh("/api/screen", zeitlimit: 15)
    }

    public func seiteZeigen(_ seite: Seite) async throws {
        try await verbindung.senden("/api/display", json: ["page": .text(seite.rawValue)])
    }

    /// Löscht alle Kopplungen mit Home; der Code bleibt.
    public func kopplungenLoeschen() async throws {
        try await verbindung.senden("/api/homekit/reset", json: ["bestaetigung": "KOPPLUNGEN LOESCHEN"])
    }

    public func neustart() async throws {
        try await verbindung.senden("/api/system/restart")
    }
}

/// `GET /api/state` des Leitstands (`apps/station/main/st_web.c`, `state_get`).
public struct Leitstandzustand: Decodable, Sendable, Equatable {
    public var geraet: Kennung?
    public var version: String?
    public var laufzeitSekunden: Int?
    public var freierSpeicher: Int?
    /// Tiefster Stand seit dem Start
    public var tiefsterSpeicher: Int?
    public var groessterBlock: Int?
    public var psramFrei: Int?
    /// Der Einrichtungszugang ist offen und kein WLAN verbunden.
    public var einrichtungOffen: Bool?
    public var netz: Netz?
    public var aussen: Aussen?
    public var funk: Funk?
    public var anlage: Anlage?
    public var anzeige: Anzeige?
    /// Fehlt bei einer Firmware ohne HomeKit-Schicht
    public var homekit: HomeKit?

    enum CodingKeys: String, CodingKey {
        case geraet = "device"
        case version
        case laufzeitSekunden = "uptime_s"
        case freierSpeicher = "heap"
        case tiefsterSpeicher = "heap_min"
        case groessterBlock = "heap_block"
        case psramFrei = "psram_free"
        case einrichtungOffen = "setup_open"
        case netz = "net"
        case aussen = "outdoor"
        case funk = "ble"
        case anlage = "plant"
        case anzeige = "display"
        case homekit
    }

    /// HomeKit-Brücke; den Code zum Koppeln zeigt nur die Anzeige am Gerät.
    public struct HomeKit: Decodable, Sendable, Equatable {
        public var aktiv: Bool?
        /// Warum die Brücke nicht läuft, etwa ohne PSRAM
        public var grund: String?
        /// Gekoppelte Geräte; 0 heißt noch nicht zu Home hinzugefügt
        public var steuerungen: Int?
        public var zubehoer: Int?

        enum CodingKeys: String, CodingKey {
            case aktiv = "active"
            case grund = "reason"
            case steuerungen = "controllers"
            case zubehoer = "accessories"
        }
    }

    public struct Kennung: Decodable, Sendable, Equatable {
        public var id: String?
        public var mac: String?
        public var ort: String?
        public var modell: String?
        public var rolle: String?
        /// Erkanntes Gerät, etwa „M5Stack Core“ oder „M5Stack Core2“
        public var platine: String?

        enum CodingKeys: String, CodingKey {
            case id, mac
            case ort = "site"
            case modell = "model"
            case rolle = "role"
            case platine = "board"
        }
    }

    public struct Netz: Decodable, Sendable, Equatable {
        public var verbunden: Bool?
        public var zugangspunktAktiv: Bool?
        public var adresse: String?
        public var signal: Int?
        public var uhrzeitGueltig: Bool?

        enum CodingKeys: String, CodingKey {
            case verbunden = "sta"
            case zugangspunktAktiv = "ap"
            case adresse = "ip"
            case signal = "rssi"
            case uhrzeitGueltig = "time_valid"
        }
    }

    /// Der zugeordnete Außenfühler. Ohne Zuordnung nur `zugeordnet`, ohne Empfang ohne Werte.
    public struct Aussen: Decodable, Sendable, Equatable {
        public var zugeordnet: Bool?
        public var mac: String?
        public var gueltig: Bool?
        public var temperaturC: Double?
        public var feuchte: Double?
        public var batterie: Int?
        public var signal: Int?
        public var alterSekunden: Int?
        public var name: String?

        enum CodingKeys: String, CodingKey {
            case zugeordnet = "assigned"
            case mac
            case gueltig = "valid"
            case temperaturC = "temp_c"
            case feuchte = "humidity"
            case batterie = "battery"
            case signal = "rssi"
            case alterSekunden = "age_s"
            case name
        }
    }

    public struct Funk: Decodable, Sendable, Equatable {
        public var empfaengt: Bool?
        public var geraete: Int?
        public var schluessel: Int?

        enum CodingKeys: String, CodingKey {
            case empfaengt = "running"
            case geraete = "devices"
            case schluessel = "keys"
        }
    }

    /// Geräte der Anlage, die der Leitstand gefunden hat und abfragt
    public struct Anlage: Decodable, Sendable, Equatable {
        public var heizungsgeraete: Int?
        public var verteiler: Int?
        public var erreichbar: Int?

        enum CodingKeys: String, CodingKey {
            case heizungsgeraete = "heat"
            case verteiler = "manifolds"
            case erreichbar = "reachable"
        }
    }

    public struct Anzeige: Decodable, Sendable, Equatable {
        public var seite: String?
        public var an: Bool?

        enum CodingKeys: String, CodingKey {
            case seite = "page"
            case an = "on"
        }
    }
}
