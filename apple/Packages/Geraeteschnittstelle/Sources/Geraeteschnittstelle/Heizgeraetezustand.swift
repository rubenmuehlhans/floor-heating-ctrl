import Foundation

/// `GET /api/state` des Heizungsgeräts (`apps/heatsource/main/app_web.c`).
///
/// Dasselbe Gerät sitzt am Kessel oder am Pufferspeicher; welche Rolle es spielt, folgt aus den
/// zugeordneten Fühlern. Der Aufbau ist zwischen Firmwareständen gewachsen (`findings`, `record`,
/// `trend`, `zapfung` kamen später), deshalb ist jedes Feld optional.
public struct Heizgeraetezustand: Decodable, Sendable, Equatable {
    public var geraet: Geraetekennung?
    public var version: String?
    public var laufzeitSekunden: Int?
    public var freierSpeicher: Int?
    /// Der Einrichtungsassistent der Weboberfläche ist offen, weil noch kein Ort eingetragen ist.
    public var einrichtungOffen: Bool?
    public var netz: Netz?
    public var einwire: Einwirebus?
    public var fuehler: [Fuehler]?
    public var abgeleitet: Abgeleitet?
    public var heizkreise: [Heizkreis]?
    public var bedarfsquellen: [Bedarfsquelle]?
    public var mqttVerbunden: Bool?
    public var brenner: Brenner?
    public var ladung: Ladung?
    public var kesselkreispumpe: Kesselkreispumpe?
    public var befunde: [Befund]?
    public var heiznachbarn: [Heiznachbar]?
    public var verlaufLaenge: Int?
    public var protokolle: Protokollstand?
    public var aufzeichnung: Aufzeichnungsstand?
    /// Werte, die vom Nachbargerät oder einem Verteiler kommen, nach Rolle
    public var fremdwerte: [String: Double]?
    public var aussen: Aussenwert?
    public var abgas: Abgasauswertung?
    public var verbrauchslinie: Verbrauchslinie?
    public var rueckstroemung: Rueckstroemung?
    public var zapfung: Zapfung?

    enum CodingKeys: String, CodingKey {
        case geraet = "device"
        case version
        case laufzeitSekunden = "uptime_s"
        case freierSpeicher = "heap"
        case einrichtungOffen = "setup_open"
        case netz = "net"
        case einwire = "onewire"
        case fuehler = "probes"
        case abgeleitet = "derived"
        case heizkreise = "circuits"
        case bedarfsquellen = "demand_sources"
        case mqttVerbunden = "mqtt_connected"
        case brenner = "burner"
        case ladung = "charge"
        case kesselkreispumpe = "boiler_pump"
        case befunde = "findings"
        case heiznachbarn = "heat_peers"
        case verlaufLaenge = "history_len"
        case protokolle = "log"
        case aufzeichnung = "record"
        case fremdwerte = "remote_probes"
        case aussen = "outdoor"
        case abgas = "flue"
        case verbrauchslinie = "trend"
        case rueckstroemung
        case zapfung
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

    public struct Einwirebus: Decodable, Sendable, Equatable {
        public var zugeordnet: Int?
        public var gefunden: Int?
        public var anschluesse: [Int]?
        public var abtastS: Int?
        public var rundeMs: Int?
        public var runden: Int?

        enum CodingKeys: String, CodingKey {
            case zugeordnet = "assigned"
            case gefunden = "found"
            case anschluesse = "pins"
            case abtastS = "poll_s"
            case rundeMs = "round_ms"
            case runden = "rounds"
        }
    }

    public struct Fuehler: Decodable, Sendable, Equatable {
        public var rom: String?
        /// `abgas`, `kessel_vl`, `kessel_rl`, `puffer`, `puffer_unten`, `hk1_vl` …, `aussen`
        public var rolle: String?
        public var rollenname: String?
        public var name: String?
        public var bus: Int?
        public var zugeordnet: Bool?
        public var temperaturC: Double?
        public var rohC: Double?
        public var korrekturK: Double?
        /// Änderung in 30 s – so findet man einen Fühler, indem man ihn mit der Hand erwärmt.
        public var aenderung30sK: Double?
        public var alterS: Int?
        public var fehler: Int?
        public var messungen: Int?

        enum CodingKeys: String, CodingKey {
            case rom
            case rolle = "role"
            case rollenname = "role_label"
            case name, bus
            case zugeordnet = "assigned"
            case temperaturC = "temp_c"
            case rohC = "raw_c"
            case korrekturK = "offset_k"
            case aenderung30sK = "delta30_c"
            case alterS = "age_s"
            case fehler = "errors"
            case messungen = "reads"
        }
    }

    public struct Abgeleitet: Decodable, Sendable, Equatable {
        public var kesselSpreizungK: Double?

        enum CodingKeys: String, CodingKey {
            case kesselSpreizungK = "kessel_spreizung_k"
        }
    }

    public struct Relais: Decodable, Sendable, Equatable {
        public var bekannt: Bool?
        public var ein: Bool?
        public var erreichbar: Bool?
        /// Das Relais meldet einen anderen Zustand als geschaltet.
        public var abweichung: Bool?
        public var status: Int?
        public var alterS: Int?
        /// Keine Verbindung: Das Relais hat keinen Strom, die vorgeschaltete Regelung hat den
        /// Pumpenausgang abgeschaltet. Kein Fehler.
        public var stromlos: Bool?
        public var stromlosS: Int?

        enum CodingKeys: String, CodingKey {
            case bekannt = "known"
            case ein = "on"
            case erreichbar = "online"
            case abweichung = "mismatch"
            case status
            case alterS = "age_s"
            case stromlos = "unpowered"
            case stromlosS = "unpowered_s"
        }
    }

    public struct Heizkreis: Decodable, Sendable, Equatable {
        public var id: Int?
        public var name: String?
        public var aktiv: Bool?
        /// `auto`, `ein`, `aus`
        public var betriebsart: String?
        public var pumpeEin: Bool?
        /// Schaltweg: `mqtt`, `http`, `keiner`
        public var weg: String?
        public var grund: String?
        public var bedarf: Bool?
        public var veraltet: Bool?
        public var abnehmerGesehen: Bool?
        public var vorlaufC: Double?
        public var ruecklaufC: Double?
        public var spreizungK: Double?
        public var seitS: Int?
        public var relais: Relais?
        /// Die Pumpe soll laufen, die vorgeschaltete Regelung gibt sie nicht frei.
        public var gesperrt: Bool?

        enum CodingKeys: String, CodingKey {
            case id, name
            case aktiv = "enabled"
            case betriebsart = "mode"
            case pumpeEin = "on"
            case weg = "path"
            case grund = "reason"
            case bedarf = "demand"
            case veraltet = "stale"
            case abnehmerGesehen = "any_seen"
            case vorlaufC = "vl_c"
            case ruecklaufC = "rl_c"
            case spreizungK = "spread_k"
            case seitS = "since_s"
            case relais = "relay"
            case gesperrt = "blocked"
        }
    }

    /// Verteiler, deren Bedarf dieses Gerät abfragt.
    public struct Bedarfsquelle: Decodable, Sendable, Equatable {
        public var id: String?
        public var ort: String?
        public var adresse: String?
        public var gesehen: Bool?
        public var bedarf: Bool?
        public var hoechsteZielstellung: Double?
        public var offeneKanaele: Int?
        public var raeumeMitBedarf: Int?
        public var ungeregelt: Int?
        public var alterS: Int?
        public var fehler: Int?

        enum CodingKeys: String, CodingKey {
            case id
            case ort = "site"
            case adresse = "host"
            case gesehen = "seen"
            case bedarf = "demand"
            case hoechsteZielstellung = "max_target"
            case offeneKanaele = "open_channels"
            case raeumeMitBedarf = "rooms_calling"
            case ungeregelt = "unregulated"
            case alterS = "age_s"
            case fehler = "errors"
        }
    }

    public struct Brenner: Decodable, Sendable, Equatable {
        public var erkannt: Bool?
        public var laeuft: Bool?
        /// Der Abgasfühler sitzt am Nachbargerät.
        public var fremdgemessen: Bool?
        public var abgasC: Double?
        public var bezugslinieC: Double?
        public var seitS: Int?
        public var laufzeitHeuteS: Int?
        public var laufzeitGesternS: Int?
        public var startsHeute: Int?
        public var startsGestern: Int?
        /// Schätzung aus Laufzeit und Düsendurchsatz, keine Messung
        public var literHeute: Double?
        public var taktet: Bool?

        enum CodingKeys: String, CodingKey {
            case erkannt = "known"
            case laeuft = "running"
            case fremdgemessen = "remote"
            case abgasC = "abgas_c"
            case bezugslinieC = "baseline_c"
            case seitS = "since_s"
            case laufzeitHeuteS = "runtime_today_s"
            case laufzeitGesternS = "runtime_yesterday_s"
            case startsHeute = "starts_today"
            case startsGestern = "starts_yesterday"
            case literHeute = "litres_today"
            case taktet = "short_cycling"
        }
    }

    public struct Ladung: Decodable, Sendable, Equatable {
        /// Klartext der Firmware, etwa „keine Ladung“ oder „wird geladen“
        public var phase: String?
        /// Füllstand 0…1, geschätzt aus dem Pufferfühler
        public var fuellstand: Double?
        public var begrenzt: Bool?
        public var warmwasserWarnung: Bool?
        public var spreizungK: Double?
        public var seitS: Int?
        public var kesselFremd: Bool?
        public var pufferFremd: Bool?
        public var kalibrierung: Kalibrierung?

        enum CodingKeys: String, CodingKey {
            case phase
            case fuellstand = "level"
            case begrenzt = "limited"
            case warmwasserWarnung = "warn_dhw"
            case spreizungK = "spread_k"
            case seitS = "since_s"
            case kesselFremd = "kessel_remote"
            case pufferFremd = "puffer_remote"
            case kalibrierung
        }

        public struct Kalibrierung: Decodable, Sendable, Equatable {
            public var hoechstwertC: Double?
            public var punkte: Int?
            /// Speicherwert beim zuletzt gelernten Leerpunkt
            public var letzterC: Double?

            enum CodingKeys: String, CodingKey {
                case hoechstwertC = "peak_c"
                case punkte
                case letzterC = "letzter_c"
            }
        }
    }

    public struct Kesselkreispumpe: Decodable, Sendable, Equatable {
        public var aktiv: Bool?
        public var betriebsart: String?
        public var ein: Bool?
        public var weg: String?
        public var grund: String?
        public var grundSchluessel: String?
        public var seitS: Int?
        public var relais: Relais?

        enum CodingKeys: String, CodingKey {
            case aktiv = "enabled"
            case betriebsart = "mode"
            case ein = "on"
            case weg = "path"
            case grund = "reason"
            case grundSchluessel = "reason_key"
            case seitS = "since_s"
            case relais = "relay"
        }
    }

    /// Meldung der Auswertung im Gerät. Die Kennungen stehen im Handbuch unter „Befunde“:
    /// `backflow`, `flow_swapped`, `probe_errors`, `day_above_trend`, `flue_gap_rising`.
    public struct Befund: Decodable, Sendable, Equatable {
        public var code: String?
        public var ort: String?
        public var text: String?
        public var gehaltenS: Int?
        public var ereignisse: Int?
        public var anstiegK: Double?
        public var fehler: Int?
        public var messungen: Int?
        /// `day_above_trend`: Brennerlauf des letzten Tags, erwarteter Wert, Abstand in Streuungen
        public var stunden: Double?
        public var erwartetH: Double?
        public var streuungenAbstand: Double?
        /// `flue_gap_rising`: Abstand jetzt, nach der Reinigung, Unterschied
        public var jetztK: Double?
        public var bezugK: Double?
        public var deltaK: Double?

        enum CodingKeys: String, CodingKey {
            case code
            case ort = "where"
            case text
            case gehaltenS = "held_s"
            case ereignisse = "events"
            case anstiegK = "rise_k"
            case fehler = "errors"
            case messungen = "reads"
            case stunden = "hours"
            case erwartetH = "expected_h"
            case streuungenAbstand = "sigma_off"
            case jetztK = "now_k"
            case bezugK = "ref_k"
            case deltaK = "delta_k"
        }
    }

    /// Das andere Heizungsgerät, mit dem dieses Messwerte austauscht.
    public struct Heiznachbar: Decodable, Sendable, Equatable {
        public var id: String?
        public var ort: String?
        public var adresse: String?
        public var gesehen: Bool?
        public var alterS: Int?
        /// Bitfeld der Rollen, die der Nachbar liefert
        public var rollen: Int?
        public var fehler: Int?

        enum CodingKeys: String, CodingKey {
            case id
            case ort = "site"
            case adresse = "host"
            case gesehen = "seen"
            case alterS = "age_s"
            case rollen = "roles"
            case fehler = "errors"
        }
    }

    public struct Protokollstand: Decodable, Sendable, Equatable {
        public var ladungen: Int?
        public var tage: Int?

        enum CodingKeys: String, CodingKey {
            case ladungen = "charges"
            case tage = "days"
        }
    }

    public struct Aufzeichnungsstand: Decodable, Sendable, Equatable {
        /// `aus`, `scharf`, `laeuft`, `fertig`
        public var zustand: String?
        public var quelle: String?
        public var automatisch: Bool?
        public var bytes: Int?
        public var spalten: Int?
        public var rasterS: Int?
        public var messpunkte: Int?
        public var beginnEpoch: Int?
        public var nachlauf: Bool?
        public var nachlaufRestS: Int?
        public var wartetAufAus: Bool?

        enum CodingKeys: String, CodingKey {
            case zustand = "state"
            case quelle = "source"
            case automatisch = "auto"
            case bytes
            case spalten = "cols"
            case rasterS = "period_s"
            case messpunkte = "samples"
            case beginnEpoch = "started_epoch"
            case nachlauf = "tail"
            case nachlaufRestS = "tail_left_s"
            case wartetAufAus = "wait_off"
        }
    }

    public struct Aussenwert: Decodable, Sendable, Equatable {
        public var alterS: Int?
        public var quelle: String?
        public var temperaturC: Double?

        enum CodingKeys: String, CodingKey {
            case alterS = "age_s"
            case quelle = "source"
            case temperaturC = "temp_c"
        }
    }

    /// Abstand zwischen Abgas- und Kesselvorlauf-Höchstwert je Ladung, als Median. Ab zehn
    /// Ladungen; „Bezug“ sind die ersten Ladungen nach der letzten Reinigung.
    public struct Abgasauswertung: Decodable, Sendable, Equatable {
        public var ladungen: Int?
        public var uebersprungen: Int?
        public var wartungEpoch: Int?
        public var jetztK: Double?
        public var bezugK: Double?
        public var bezugLadungen: Int?
        public var deltaK: Double?

        enum CodingKeys: String, CodingKey {
            case ladungen = "charges"
            case uebersprungen = "skipped"
            case wartungEpoch = "wartung_epoch"
            case jetztK = "now_k"
            case bezugK = "ref_k"
            case bezugLadungen = "ref_charges"
            case deltaK = "delta_k"
        }
    }

    /// Brennerlaufzeit über Heizgradtagen. Gültig ab 14 Tagen mit genug Spannweite; dann sendet
    /// das Gerät die Gerade mit.
    public struct Verbrauchslinie: Decodable, Sendable, Equatable {
        public var tage: Int?
        public var gueltig: Bool?
        public var uebersprungen: Int?
        /// Stunden Brennerlauf je Heizgradtag
        public var steigung: Double?
        /// Stunden je Tag ohne Heizbedarf
        public var grundlast: Double?
        public var streuung: Double?
        public var bestimmtheit: Double?
        public var letzterTag: Tag?

        enum CodingKeys: String, CodingKey {
            case tage = "days"
            case gueltig = "valid"
            case uebersprungen = "skipped"
            case steigung = "slope_h_per_gt"
            case grundlast = "base_h"
            case streuung = "sigma_h"
            case bestimmtheit = "r2"
            case letzterTag = "last_day"
        }

        public struct Tag: Decodable, Sendable, Equatable {
            public var gradtage: Double?
            public var stunden: Double?
            public var erwartetH: Double?
            public var streuungenAbstand: Double?

            enum CodingKeys: String, CodingKey {
                case gradtage
                case stunden = "hours"
                case erwartetH = "expected_h"
                case streuungenAbstand = "sigma_off"
            }
        }
    }

    public struct Rueckstroemung: Decodable, Sendable, Equatable {
        public var aktiv: Bool?
        public var ereignisse: Int?
        public var letzteK: Double?

        enum CodingKeys: String, CodingKey {
            case aktiv = "active"
            case ereignisse = "events"
            case letzteK = "last_k"
        }
    }

    public struct Zapfung: Decodable, Sendable, Equatable {
        public var aktiv: Bool?
        public var anzahl: Int?
        public var letzteK: Double?
        public var summeK: Double?
        public var summeKwh: Double?

        enum CodingKeys: String, CodingKey {
            case aktiv = "active"
            case anzahl = "count"
            case letzteK = "last_k"
            case summeK = "sum_k"
            case summeKwh = "sum_kwh"
        }
    }
}

/// `GET /api/measurements`: eigene Fühler mit Rolle und Wert, für das Nachbargerät.
public struct Messwertantwort: Decodable, Sendable, Equatable {
    public var id: String?
    public var ort: String?
    public var laufzeitSekunden: Int?
    public var fuehler: [Messwert]?

    enum CodingKeys: String, CodingKey {
        case id
        case ort = "site"
        case laufzeitSekunden = "uptime_s"
        case fuehler = "probes"
    }

    public struct Messwert: Decodable, Sendable, Equatable {
        public var rolle: String?
        public var temperaturC: Double?
        public var alterS: Int?

        enum CodingKeys: String, CodingKey {
            case rolle = "role"
            case temperaturC = "c"
            case alterS = "age_s"
        }
    }
}

/// `GET /api/history?step=&max=`: Verlauf der letzten 24 Stunden im Zwei-Minuten-Raster.
public struct Verlaufsantwort: Decodable, Sendable, Equatable {
    /// Raster des Geräts in Minuten (2)
    public var rasterMin: Int?
    /// Abstand der gelieferten Punkte in Minuten
    public var schrittMin: Int?
    public var punkte: Int?
    /// Zeitpunkt des jüngsten Punkts; er steht in jeder Reihe zuletzt.
    public var juengsterEpoch: Int?
    public var rollen: [String]?
    /// Werte je Rolle, alt → jung; `nil`, wo kein Messwert vorlag
    public var reihen: [String: [Double?]]?

    enum CodingKeys: String, CodingKey {
        case rasterMin = "period_min"
        case schrittMin = "step_min"
        case punkte = "points"
        case juengsterEpoch = "newest_epoch"
        case rollen = "roles"
        case reihen = "series"
    }

    /// Zeitpunkt des Punkts `index` einer Reihe.
    public func zeitpunkt(_ index: Int, anzahl: Int) -> Date? {
        guard let juengsterEpoch, let schrittMin else { return nil }
        let abstand = (anzahl - 1 - index) * schrittMin * 60
        return Date(timeIntervalSince1970: TimeInterval(juengsterEpoch - abstand))
    }
}
