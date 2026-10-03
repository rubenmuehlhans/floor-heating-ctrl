import Foundation
import Testing
import Geraeteschnittstelle
@testable import Anlage

@Suite("Parameterkatalog")
struct ParameterkatalogTests {
    @Test func kennungenEindeutig() {
        let ids = Parameterkatalog.alle.map(\.id)
        #expect(Set(ids).count == ids.count)
    }

    @Test func vorgabenImBereich() {
        for p in Parameterkatalog.alle {
            #expect(Schreibweg.bereichsfehler(p, p.vorgabe) == nil, "\(p.id)")
        }
    }

    /// Die Freigabeliste aus dem Plan: Sicherheitsgrenzen, Zugänge und Kanalwerte nie.
    @Test func freigabeFuerDieKI() {
        let gesperrt = [
            "heizung/circuits[].frost_c", "heizung/boiler_pump.emergency_c", "verteiler/channels[].bemf_mv",
            "verteiler/channels[].open_ms", "heizung/probes[].offset_k", "heizung/onewire_pin[0]",
            "verteiler/timezone", "verteiler/display_brightness",
        ]
        for id in gesperrt {
            #expect(Parameterkatalog.parameter(id)?.kiFreigegeben == false, "\(id)")
        }
        for p in Parameterkatalog.alle where p.istKennwort || p.pfad.first == "wifi" || p.pfad.first == "mqtt" {
            #expect(!p.kiFreigegeben, "\(p.id)")
        }
        let frei = ["verteiler/rooms[].p_band_k", "heizung/buffer.voll_c", "heizung/circuits[].min_buffer_c", "heizung/burner.duese_l_h"]
        for id in frei {
            #expect(Parameterkatalog.parameter(id)?.kiFreigegeben == true, "\(id)")
        }
    }

    @Test func schluesselLesbar() {
        #expect(Parameterkatalog.parameter("verteiler/touch.thresholds[1]") != nil)
        #expect(Parameterkatalog.parameter("heizung/circuits[].pump.relay") != nil)
    }

    /// Die Konfigurationen der Attrappen liegen im Bereich des Katalogs.
    @Test func attrappenImBereich() throws {
        for (datei, art) in [("attrappe/verteiler-config.json", Geraeteart.verteiler), ("attrappe/speicher-config.json", .heizung)] {
            let k = try Aufnahme.json(datei)
            let fehler = Schreibweg.pruefen(k, art).filter { !$0.contains("Pufferfühler") }
            #expect(fehler.isEmpty, "\(datei): \(fehler)")
        }
    }
}

@Suite("Schreibweg")
struct SchreibwegTests {
    let verteiler: JSONWert = [
        "site": "Erdgeschoss",
        "rooms": [
            ["id": 1, "name": "Küche", "channels": [1, 2], "sensor_mac": "A4:C1:38:11:22:33", "mode": "heat",
             "target_c": 20, "p_band_k": 1, "interval_s": 30, "min_delta": 0.01, "step": 0.1],
            ["id": 2, "name": "Bad", "channels": [3], "sensor_mac": "A4:C1:38:44:55:66", "mode": "heat",
             "target_c": 22, "p_band_k": 1, "interval_s": 30, "min_delta": 0.01, "step": 0.1],
        ],
        "channels": [
            ["id": 1, "open_ms": 39000, "close_ms": 40000, "max_ms": 45000, "blank_ms": 2000, "bemf_mv": 190, "bemf_hyst_mv": 30],
            ["id": 2, "open_ms": 39000, "close_ms": 40000, "max_ms": 45000, "blank_ms": 2000, "bemf_mv": 190, "bemf_hyst_mv": 30],
        ],
        "touch": ["enabled": true, "thresholds": [1000, 870, 1000]],
    ]

    let heizung: JSONWert = [
        "onewire_pin": [13, -1],
        "probes": [
            ["rom": "28AA000000000001", "role": "puffer", "name": "Puffer", "offset_k": 2.6],
            ["rom": "28AA000000000002", "role": "hk1_vl", "name": "HK1 VL", "offset_k": 0],
        ],
        "circuits": [
            ["id": 1, "name": "Heizkreis 1", "peers": ["fbh_3a91c4"], "overrun_s": 300, "pump": ["host": "192.168.0.60", "relay": 1]],
            ["id": 2, "name": "Heizkreis 2", "peers": [], "overrun_s": 300, "pump": ["host": "192.168.0.61", "relay": 1]],
        ],
        "burner": ["delta_on_k": 12, "delta_off_k": 6],
        "buffer": ["voll_c": 72, "leer_c": 51.5],
    ]

    func p(_ id: String) throws -> Parameter { try #require(Parameterkatalog.parameter(id)) }

    /// Ein geänderter Raum: alle Räume gehen vollständig mit, das Thermometer bleibt.
    @Test func raeumeVollstaendig() throws {
        let teil = Schreibweg.teil([Wertaenderung(try p("verteiler/rooms[].p_band_k"), eintrag: 2, wert: 1.5)], konfiguration: verteiler)
        let raeume = try #require(teil["rooms"]?.alsListe)
        #expect(raeume.count == 2)
        #expect(raeume[1]["p_band_k"]?.alsZahl == 1.5)
        #expect(raeume[1]["sensor_mac"]?.alsText == "A4:C1:38:44:55:66")
        #expect(raeume[0] == verteiler["rooms"]?[0])
        #expect(teil["site"] == nil, "nur die geänderte Gruppe")
    }

    @Test func kanaeleEinzeln() throws {
        let teil = Schreibweg.teil([Wertaenderung(try p("verteiler/channels[].max_ms"), eintrag: 2, wert: 50000)], konfiguration: verteiler)
        #expect(teil["channels"] == [["id": 2, "max_ms": 50000]])
    }

    /// Heizkreise ersetzen die Liste; ohne die übrigen Kennungen fiele Heizkreis 1 weg.
    @Test func heizkreiseMitAllenKennungen() throws {
        let teil = Schreibweg.teil([Wertaenderung(try p("heizung/circuits[].overrun_s"), eintrag: 2, wert: 600)], konfiguration: heizung)
        #expect(teil["circuits"] == [["id": 1], ["id": 2, "overrun_s": 600]])
        let neu = Schreibweg.zusammengefuehrt(heizung, teil)
        #expect(neu["circuits"]?[0]?["peers"] == ["fbh_3a91c4"])
        #expect(neu["circuits"]?[1]?["overrun_s"]?.alsZahl == 600)
    }

    @Test func fuehlerMitAllenKennungen() throws {
        let teil = Schreibweg.teil([Wertaenderung(try p("heizung/probes[].offset_k"), eintrag: "28AA000000000002", wert: 0.5)],
                                   konfiguration: heizung)
        #expect(teil["probes"] == [["rom": "28AA000000000001"], ["rom": "28AA000000000002", "offset_k": 0.5]])
    }

    /// `onewire_pin` übernimmt die Firmware platzweise; der andere Platz bleibt, wie er war.
    @Test func listenplaetze() throws {
        let teil = Schreibweg.teil([Wertaenderung(try p("heizung/onewire_pin[1]"), wert: 14)], konfiguration: heizung)
        #expect(teil["onewire_pin"] == [13, 14])
        let tasten = Schreibweg.teil([Wertaenderung(try p("verteiler/touch.thresholds[1]"), wert: 900)], konfiguration: verteiler)
        #expect(tasten["touch"]?["thresholds"] == [1000, 900, 1000])
    }

    @Test func gruppeNurGeaendert() throws {
        let teil = Schreibweg.teil([Wertaenderung(try p("heizung/buffer.voll_c"), wert: 68)], konfiguration: heizung)
        #expect(teil == ["buffer": ["voll_c": 68]])
        #expect(Schreibweg.zusammengefuehrt(heizung, teil)["buffer"]?["leer_c"]?.alsZahl == 51.5)
    }

    @Test func pruefungWieDieFirmware() throws {
        func fehler(_ id: String, _ wert: JSONWert, eintrag: JSONWert? = nil, _ k: JSONWert, _ art: Geraeteart) throws -> [String] {
            let teil = Schreibweg.teil([Wertaenderung(try p(id), eintrag: eintrag, wert: wert)], konfiguration: k)
            return Schreibweg.pruefen(Schreibweg.zusammengefuehrt(k, teil), art)
        }
        #expect(try fehler("heizung/burner.delta_off_k", 12, heizung, .heizung).contains { $0.contains("Einschaltschwelle des Brenners") })
        #expect(try fehler("heizung/buffer.leer_c", 72, heizung, .heizung).contains { $0.contains("„voll“") })
        #expect(try fehler("heizung/probes[].role", "puffer", eintrag: "28AA000000000002", heizung, .heizung).contains { $0.contains("zweimal") })
        #expect(try fehler("heizung/onewire_pin[0]", 12, heizung, .heizung).contains { $0.contains("GPIO 12") })
        #expect(try fehler("verteiler/channels[].max_ms", 30000, eintrag: 1, verteiler, .verteiler).contains { $0.contains("Maximallaufzeit") })
        #expect(try fehler("verteiler/rooms[].target_c", 40, eintrag: 1, verteiler, .verteiler).contains { $0.contains("Sollwert") })
        #expect(try fehler("verteiler/rooms[].p_band_k", 2, eintrag: 1, verteiler, .verteiler).isEmpty)
    }

    /// Das Zurücklesen vergleicht Wert für Wert; Kennwörter lassen sich nicht zurücklesen.
    @Test func abweichungen() {
        let k: JSONWert = ["buffer": ["voll_c": 68.00000762939453], "wifi": ["ssid": "Heimnetz", "pass_set": true],
                           "circuits": [["id": 1, "overrun_s": 300], ["id": 2, "overrun_s": 600]]]
        #expect(Anlagenbetrieb.abweichungen(["buffer": ["voll_c": 68]], k).isEmpty)
        #expect(Anlagenbetrieb.abweichungen(["wifi": ["ssid": "Heimnetz", "pass": "geheim"]], k).isEmpty)
        #expect(Anlagenbetrieb.abweichungen(["buffer": ["voll_c": 70]], k) == ["buffer.voll_c"])
        #expect(Anlagenbetrieb.abweichungen(["circuits": [["id": 1], ["id": 2, "overrun_s": 600]]], k).isEmpty)
        #expect(Anlagenbetrieb.abweichungen(["circuits": [["id": 2, "overrun_s": 900]]], k) == ["circuits[2].overrun_s"])
    }
}
