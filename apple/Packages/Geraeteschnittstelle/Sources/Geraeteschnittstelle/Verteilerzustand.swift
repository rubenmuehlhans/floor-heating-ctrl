import Foundation

/// `GET /api/state` der Verteilerplatine (`apps/manifold/main/app_web.c`, `state_get`).
///
/// Alle Felder sind optional: Der Aufbau hat sich zwischen Firmwareständen geändert, und ein
/// fehlendes Feld soll die übrigen nicht mitreißen. Unbekannte Felder werden übergangen; wer sie
/// braucht, liest die Rohdaten.
public struct Verteilerzustand: Decodable, Sendable, Equatable {
    /// Änderungszähler; zugleich das ETag.
    public var revision: Int?
    public var laufzeitSekunden: Int?
    public var freierSpeicher: Int?
    public var version: String?
    /// Erst neuere Firmware sendet es; bis dahin kennt nur der Bonjour-Eintrag die Kennung.
    public var geraet: Geraetekennung?
    public var netz: Netz?
    public var raeume: [Raum]?
    public var kanaele: [Kanal]?
    public var schutzfahrt: Schutzfahrt?
    public var aussen: Aussenfuehler?
    public var gegenspannungenMv: [Double]?
    public var tastenRohwerte: [Double]?
    public var bordfuehler: Bordfuehler?
    public var messfahrt: Messfahrtstand?

    enum CodingKeys: String, CodingKey {
        case revision
        case laufzeitSekunden = "uptime_s"
        case freierSpeicher = "heap"
        case version
        case geraet = "device"
        case netz = "net"
        case raeume = "rooms"
        case kanaele = "channels"
        case schutzfahrt = "seize"
        case aussen = "outdoor"
        case gegenspannungenMv = "bemf_mv"
        case tastenRohwerte = "touch_raw"
        case bordfuehler = "local_sensors"
        case messfahrt = "calib"
    }

    public struct Netz: Decodable, Sendable, Equatable {
        public var verbunden: Bool?
        public var adresse: String?
        public var zugangspunktAktiv: Bool?
        public var zugangspunktAdresse: String?
        public var signal: Int?
        public var uhrzeitGueltig: Bool?

        enum CodingKeys: String, CodingKey {
            case verbunden = "connected"
            case adresse = "ip"
            case zugangspunktAktiv = "ap_active"
            case zugangspunktAdresse = "ap_ip"
            case signal = "rssi"
            case uhrzeitGueltig = "time_valid"
        }
    }

    public struct Raum: Decodable, Sendable, Equatable {
        public var id: Int?
        public var name: String?
        public var kanaele: [Int]?
        /// `heat` oder `off`
        public var betriebsart: String?
        public var sollC: Double?
        public var thermometerZugeordnet: Bool?
        public var messwertGueltig: Bool?
        public var temperaturC: Double?
        public var messwertAlterS: Int?
        public var feuchte: Double?
        public var batterie: Int?
        /// Stellung, die die Regelung anstrebt, 0…1
        public var zielstellung: Double?
        public var naechstePruefungS: Int?

        enum CodingKeys: String, CodingKey {
            case id, name
            case kanaele = "channels"
            case betriebsart = "mode"
            case sollC = "target_c"
            case thermometerZugeordnet = "sensor_set"
            case messwertGueltig = "temp_valid"
            case temperaturC = "temp_c"
            case messwertAlterS = "temp_age_s"
            case feuchte = "humidity"
            case batterie = "battery"
            case zielstellung = "target_position"
            case naechstePruefungS = "next_check_s"
        }
    }

    public struct Kanal: Decodable, Sendable, Equatable {
        public var id: Int?
        public var gegenspannungMv: Double?
        public var kalibriert: Bool?
        public var gruppe: Int?
        public var stellungBekannt: Bool?
        public var letzteBewegungMs: Int?
        public var letzterHalt: String?
        public var handbetrieb: Bool?
        public var seitSchutzfahrtBewegt: Bool?
        /// `idle`, `opening`, `closing`
        public var vorgang: String?
        public var befehlWartet: Bool?
        /// 0 zu … 1 auf
        public var stellung: Double?
        /// Durch eine Messfahrt belegt; Befehle bleiben dann ohne Wirkung.
        public var belegt: Bool?
        public var schutzfahrtSchritt: Int?

        enum CodingKeys: String, CodingKey {
            case id
            case gegenspannungMv = "bemf_mv"
            case kalibriert = "calibrated"
            case gruppe = "group"
            case stellungBekannt = "known"
            case letzteBewegungMs = "last_move_ms"
            case letzterHalt = "last_stop"
            case handbetrieb = "manual"
            case seitSchutzfahrtBewegt = "moved_since_seize"
            case vorgang = "op"
            case befehlWartet = "pending"
            case stellung = "position"
            case belegt = "reserved"
            case schutzfahrtSchritt = "seize_step"
        }
    }

    public struct Schutzfahrt: Decodable, Sendable, Equatable {
        public var tageBisFaellig: Int?
        public var erledigt: Int?
        public var stunde: Int?
        public var offen: Int?
        public var laeuft: Bool?
        /// 0 = Sonntag
        public var wochentag: Int?

        enum CodingKeys: String, CodingKey {
            case tageBisFaellig = "days_left"
            case erledigt = "done"
            case stunde = "hour"
            case offen = "pending"
            case laeuft = "running"
            case wochentag = "weekday"
        }
    }

    public struct Aussenfuehler: Decodable, Sendable, Equatable {
        public var alterS: Int?
        public var batterieMv: Int?
        public var feuchte: Double?
        public var luftdruckHpa: Double?
        /// Ein Außenfühler ist an diesem Verteiler eingetragen.
        public var zugeordnet: Bool?
        public var temperaturC: Double?
        public var gueltig: Bool?

        enum CodingKeys: String, CodingKey {
            case alterS = "age_s"
            case batterieMv = "battery_mv"
            case feuchte = "humidity"
            case luftdruckHpa = "pressure_hpa"
            case zugeordnet = "set"
            case temperaturC = "temp_c"
            case gueltig = "valid"
        }
    }

    public struct Bordfuehler: Decodable, Sendable, Equatable {
        public var klima: Klimafuehler?
        public var einwire: [Einwirefuehler]?

        enum CodingKeys: String, CodingKey {
            case klima = "hdc1080"
            case einwire = "ds18b20"
        }

        public struct Klimafuehler: Decodable, Sendable, Equatable {
            public var feuchte: Double?
            public var temperaturC: Double?
            public var gueltig: Bool?

            enum CodingKeys: String, CodingKey {
                case feuchte = "humidity"
                case temperaturC = "temp_c"
                case gueltig = "valid"
            }
        }

        public struct Einwirefuehler: Decodable, Sendable, Equatable {
            public var adresse: String?
            public var temperaturC: Double?
            public var gueltig: Bool?

            enum CodingKeys: String, CodingKey {
                case adresse = "address"
                case temperaturC = "temp_c"
                case gueltig = "valid"
            }
        }
    }
}

/// Stand der Messfahrt, in `/api/state` unter `calib` und in `GET /api/calib`.
public struct Messfahrtstand: Decodable, Sendable, Equatable {
    /// `idle`, `running`, `done`, `failed`
    public var zustand: String?
    public var kanal: Int?
    public var gruppe: Int?
    public var phase: Int?
    public var messpunkte: Int?
    public var abtastMs: Int?
    public var meldung: String?
    public var marken: Marken?
    /// Erst nach einer auswertbaren Fahrt; „Übernehmen“ schreibt genau diese Werte.
    public var vorschlag: Vorschlag?

    enum CodingKeys: String, CodingKey {
        case zustand = "state"
        case kanal = "channel"
        case gruppe = "group"
        case phase
        case messpunkte = "sample_count"
        case abtastMs = "sample_period_ms"
        case meldung = "message"
        case marken = "marks"
        case vorschlag = "suggestion"
    }

    /// Ergebnis der Auswertung in `app_calib.c`: Maximallaufzeit mit rund 15 % Reserve,
    /// Schwelle in der Mitte zwischen Fahr- und Blockierspannung, Hysterese ein Viertel davon.
    public struct Vorschlag: Decodable, Sendable, Equatable {
        public var zuMs: Int?
        public var aufMs: Int?
        public var maximalMs: Int?
        public var schwelleMv: Int?
        public var hystereseMv: Int?

        enum CodingKeys: String, CodingKey {
            case zuMs = "close_ms"
            case aufMs = "open_ms"
            case maximalMs = "max_ms"
            case schwelleMv = "bemf_mv"
            case hystereseMv = "hyst_mv"
        }
    }

    /// Indizes und Schwellen, aus denen die Fahrzeiten folgen.
    public struct Marken: Decodable, Sendable, Equatable {
        public var zuVon: Int?
        public var zuBis: Int?
        public var aufVon: Int?
        public var aufBis: Int?
        public var grundZuMv: Double?
        public var anschlagZuMv: Double?
        public var grundAufMv: Double?
        public var anschlagAufMv: Double?

        enum CodingKeys: String, CodingKey {
            case zuVon = "close_from"
            case zuBis = "close_to"
            case aufVon = "open_from"
            case aufBis = "open_to"
            case grundZuMv = "baseline_close_mv"
            case anschlagZuMv = "stall_close_mv"
            case grundAufMv = "baseline_open_mv"
            case anschlagAufMv = "stall_open_mv"
        }
    }
}

/// `GET /api/calib?from=N`: Messreihe der Gegenspannung ab Punkt N, seitenweise zu 512 Punkten.
public struct Messreihe: Decodable, Sendable, Equatable {
    public var stand: Messfahrtstand?
    public var ab: Int?
    public var werteMv: [Double]?

    enum CodingKeys: String, CodingKey {
        case stand = "calib"
        case ab = "from"
        case werteMv = "samples"
    }
}

/// `GET /api/demand`: Wärmebedarf des Verteilers, wie ihn das Heizungsgerät abfragt.
public struct Bedarfsantwort: Decodable, Sendable, Equatable {
    public var id: String?
    public var ort: String?
    public var bedarf: Bool?
    public var hoechsteZielstellung: Double?
    public var offeneKanaele: Int?
    public var ungeregelt: Int?
    public var raeumeMitBedarf: Int?
    public var kaeltesterRaumC: Double?
    public var aussenC: Double?
    public var aussenAlterS: Int?
    public var thermometerInOrdnung: Bool?

    enum CodingKeys: String, CodingKey {
        case id
        case ort = "site"
        case bedarf = "demand"
        case hoechsteZielstellung = "max_target"
        case offeneKanaele = "open_channels"
        case ungeregelt = "unregulated"
        case raeumeMitBedarf = "rooms_calling"
        case kaeltesterRaumC = "min_room_c"
        case aussenC = "outdoor_c"
        case aussenAlterS = "outdoor_age_s"
        case thermometerInOrdnung = "sensor_ok"
    }
}

/// `GET /api/ble`: empfangene Bluetooth-Thermometer, höchstens 24.
public struct Thermometerliste: Decodable, Sendable, Equatable {
    public var geraete: [Funkthermometer]?
    /// Adressen, für die ein Schlüssel hinterlegt ist, auch außer Reichweite. Die Schlüssel
    /// selbst gibt der Verteiler nur in der Sicherung heraus.
    public var schluessel: [String]?
    /// Nur der Leitstand: Adresse des zugeordneten Außenfühlers, leer ohne Zuordnung. Der
    /// Verteiler führt sie in seiner Konfiguration (`outdoor_mac`).
    public var aussenfuehler: String?

    enum CodingKeys: String, CodingKey {
        case geraete = "devices"
        case schluessel = "keys"
        case aussenfuehler = "outdoor"
    }

    public struct Funkthermometer: Decodable, Sendable, Equatable {
        public var mac: String?
        public var name: String?
        public var signal: Int?
        public var temperaturC: Double?
        public var feuchte: Double?
        public var batterie: Int?
        public var batterieMv: Int?
        public var pakete: Int?
        public var format: String?
        /// Nur BTHome: sendet verschlüsselt
        public var verschluesselt: Bool?
        /// Nur verschlüsselt: `ok`, `missing` oder `wrong`. Ohne passenden Schluessel fehlen
        /// Temperatur und Feuchte.
        public var schluessel: String?

        enum CodingKeys: String, CodingKey {
            case mac, name
            case signal = "rssi"
            case temperaturC = "temp_c"
            case feuchte = "humidity"
            case batterie = "battery"
            case batterieMv = "battery_mv"
            case pakete = "packets"
            case format
            case verschluesselt = "encrypted"
            case schluessel = "key"
        }
    }
}
