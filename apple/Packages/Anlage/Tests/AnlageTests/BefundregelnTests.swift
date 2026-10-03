import Foundation
import Testing
import Geraeteschnittstelle
@testable import Anlage

/// Die Prüfungen der App an der Anlage der Attrappen, jeweils mit einer gezielten Abweichung.
@Suite("Befundregeln")
struct BefundregelnTests {
    func geraet(_ id: String, _ ort: String) -> BekanntesGeraet {
        BekanntesGeraet(id: id, art: id.hasPrefix("fbh_") ? .verteiler : .heizung, ort: ort, adresse: URL(string: "http://127.0.0.1")!)
    }

    func verteiler(_ aendern: (inout Verteilerzustand) -> Void = { _ in }) throws -> Geraetestand {
        var z: Verteilerzustand = try Aufnahme.lesen("attrappe/verteiler-state.json")
        aendern(&z)
        return Geraetestand(geraet: geraet("fbh_a1b2c3", "Erdgeschoss"), erreichbar: true, letzterKontakt: .now,
                            verteiler: z, konfiguration: try Aufnahme.json("attrappe/verteiler-config.json"))
    }

    func speicher(_ aendern: (inout Heizgeraetezustand) -> Void = { _ in }, konfiguration: ((inout JSONWert) -> Void)? = nil) throws -> Geraetestand {
        var z: Heizgeraetezustand = try Aufnahme.lesen("attrappe/speicher-state.json")
        aendern(&z)
        var k = try Aufnahme.json("attrappe/speicher-config.json")
        konfiguration?(&k)
        return Geraetestand(geraet: geraet("heiz_3f21ac", "Pufferspeicher"), erreichbar: true, letzterKontakt: .now,
                            heizgeraet: z, konfiguration: k)
    }

    func kessel(_ aendern: (inout Heizgeraetezustand) -> Void = { _ in }) throws -> Geraetestand {
        var z: Heizgeraetezustand = try Aufnahme.lesen("attrappe/kessel-state.json")
        aendern(&z)
        return Geraetestand(geraet: geraet("heiz_9a1b2c", "Kessel"), erreichbar: true, letzterKontakt: .now, heizgeraet: z)
    }

    func befunde(_ staende: [Geraetestand], sicherungen: [String: Date]? = nil) -> [String: Befund] {
        Dictionary(Zusammenfuehrung.bild(staende, sicherungen: sicherungen).befunde.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
    }

    @Test func raeumeUndFuehler() throws {
        let b = befunde([try verteiler()])
        #expect(b["thermometer:fbh_a1b2c3/3"]?.schwere == .warnung, "Flur/WC ohne Thermometer")
        #expect(b["vorlauffuehler:fbh_a1b2c3"]?.mindestdauer == 600)
        #expect(b["kalibrierung:fbh_a1b2c3"]?.titel == "8 belegte Kanäle ohne Messfahrt")
        #expect(b.keys.filter { $0.hasPrefix("messwert:") }.isEmpty)

        let leer = befunde([try verteiler { z in
            z.raeume?[0].batterie = 4
            z.raeume?[1].batterie = 12
            z.raeume?[1].messwertGueltig = false
            z.raeume?[1].messwertAlterS = 1800
        }])
        #expect(leer["batterie:fbh_a1b2c3/1"]?.schwere == .warnung)
        #expect(leer["batterie:fbh_a1b2c3/2"]?.schwere == .hinweis)
        #expect(leer["messwert:fbh_a1b2c3/2"]?.text.contains("30\u{00A0}min") == true)
    }

    @Test func netzUndFirmware() throws {
        let b = befunde([try verteiler { z in
            z.netz?.uhrzeitGueltig = false
            z.netz?.signal = -84
        }])
        #expect(b["uhrzeit:fbh_a1b2c3"]?.schwere == .warnung)
        #expect(b["wlan:fbh_a1b2c3"]?.schwere == .hinweis)
        #expect(befunde([try verteiler { $0.netz?.signal = -90 }])["wlan:fbh_a1b2c3"]?.schwere == .warnung)

        var zweiter = try verteiler { z in
            z.version = "0.9.0"
            z.geraet?.ort = "Obergeschoss"
        }
        zweiter.geraet = geraet("fbh_d4e5f6", "Obergeschoss")
        let f = befunde([try verteiler { $0.version = "1.0.0" }, zweiter])
        #expect(f["firmware:verteiler"]?.text.contains("Obergeschoss 0.9.0") == true)
    }

    /// Lange im Handbetrieb erst nach zwölf Stunden, das entscheidet das Befundgedächtnis.
    @Test func handbetrieb() throws {
        let b = befunde([try verteiler { $0.kanaele?[1].handbetrieb = true }])
        let h = try #require(b["handbetrieb:fbh_a1b2c3/2"])
        #expect(h.mindestdauer == 12 * 3600)
        #expect(h.titel == "Kanal 2 (Küche) seit langem im Handbetrieb")
    }

    @Test func heizkreise() throws {
        // Der Erdgeschoss-Verteiler ist Heizkreis 1 zugeordnet.
        #expect(befunde([try verteiler(), try speicher()])["verteiler-ohne-kreis:fbh_a1b2c3"] == nil)
        let ohne = befunde([try verteiler(), try speicher(konfiguration: { k in
            k["circuits"] = .liste((k["circuits"]?.alsListe ?? []).map { var c = $0; c["peers"] = []; return c })
        })])
        #expect(ohne["verteiler-ohne-kreis:fbh_a1b2c3"]?.schwere == .warnung)
        #expect(ohne["kreis-ohne-verteiler:1"] != nil)

        let gestoert = befunde([try speicher { z in
            z.heizkreise?[0].relais?.abweichung = true
            z.heizkreise?[1].abnehmerGesehen = false
        }])
        #expect(gestoert["relais-abweichung:1"]?.mindestdauer == 300)
        #expect(gestoert["kreis-ohne-bedarf:2"] != nil)
    }

    /// Eine Platine ohne Räume empfängt nur ein Funkthermometer; sie meldet nie Bedarf und
    /// braucht deshalb keinen Heizkreis.
    @Test func verteilerOhneRaeumeBrauchtKeinenKreis() throws {
        var z: Verteilerzustand = try Aufnahme.lesen("attrappe/verteiler-state.json")
        z.raeume = []
        var k = try Aufnahme.json("attrappe/verteiler-config.json")
        k["rooms"] = .liste([])
        let bruecke = Geraetestand(geraet: geraet("fbh_a1b2c3", "Funkbrücke"), erreichbar: true,
                                   letzterKontakt: .now, verteiler: z, konfiguration: k)
        let b = befunde([bruecke, try speicher(konfiguration: { k in
            k["circuits"] = .liste((k["circuits"]?.alsListe ?? []).map { var c = $0; c["peers"] = []; return c })
        })])
        #expect(b["verteiler-ohne-kreis:fbh_a1b2c3"] == nil)
    }

    /// Solange die Konfiguration des Speichergeräts fehlt, kennt die App die Zuordnung nicht.
    @Test func ohneKonfigurationKeinKreisbefund() throws {
        var s = try speicher()
        s.konfiguration = nil
        #expect(befunde([try verteiler(), s]).keys.filter { $0.hasPrefix("kreis-ohne-verteiler") || $0.hasPrefix("verteiler-ohne-kreis") }.isEmpty)
    }

    @Test func speicherUndBrenner() throws {
        let b = befunde([
            try speicher({ z in
                z.ladung?.warmwasserWarnung = true
                z.ladung?.begrenzt = true
                z.ladung?.fuellstand = 0.9
            }),
            try kessel { z in
                z.brenner?.taktet = true
                z.ladung?.fuellstand = 0.6
            },
        ])
        #expect(b["warmwasser"]?.schwere == .warnung)
        #expect(b["ladung-geschaetzt"]?.mindestdauer == 1800)
        #expect(b["taktung"] != nil)
        #expect(b["fuellstand-abweichung"]?.text.contains("90\u{00A0}%") == true)
    }

    /// Wie in der realen Anlage: voll steht auf 62 °C, die Ladungen enden bei 68,5 bis 68,9 °C.
    @Test func vollUnterLadungsende() throws {
        let s = try speicher(konfiguration: { $0["buffer"] = ["voll_c": 62, "leer_c": 51.5] })
        var bild = Zusammenfuehrung.bild([s])
        bild.ladungen = [68.5, 68.9, 68.7, 61.0].enumerated().map { i, ende in
            Ladungssatz(beginn: Date(timeIntervalSince1970: 1_790_000_000 - Double(i) * 86_400), dauer: 4000, brenner: 3100, starts: 1,
                        speicherVorher: 51, speicherNachher: ende, kesselVorlaufMax: 77, abgasMax: 85, aussenMittel: 14, liter: 1.9)
        }
        let befunde = Zusammenfuehrung.speicherbefunde([s], bild: bild, speicher: s, protokollgeraet: s)
        let v = try #require(befunde.first { $0.id == "voll-unter-ladungsende" })
        #expect(v.text.contains("62,0 °C"))
        #expect(v.text.contains("61,0 bis 68,9 °C"))
    }

    @Test func sicherungen() throws {
        let staende = [try verteiler(), try kessel()]
        #expect(befunde(staende).keys.filter { $0.hasPrefix("sicherung:") }.isEmpty, "ohne Ablage keine Prüfung")
        let keine = befunde(staende, sicherungen: [:])
        #expect(keine["sicherung:fbh_a1b2c3"]?.mindestdauer == 3600)
        let alt = befunde(staende, sicherungen: ["fbh_a1b2c3": .now.addingTimeInterval(-20 * 86_400), "heiz_9a1b2c": .now])
        #expect(alt["sicherung:fbh_a1b2c3"]?.titel == "Letzte Sicherung von Verteiler Erdgeschoss vor 20 Tagen")
        #expect(alt["sicherung:heiz_9a1b2c"] == nil)
    }
}
