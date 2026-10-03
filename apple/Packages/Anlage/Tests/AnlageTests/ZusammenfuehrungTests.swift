import Foundation
import Testing
import Geraeteschnittstelle
@testable import Anlage

/// Die Aufnahmen liegen bei der Geräteschnittstelle; hier werden sie nur gelesen.
enum Aufnahme {
    static let ordner = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .appending(path: "Geraeteschnittstelle/Tests/GeraeteschnittstelleTests/Fixtures")

    static func daten(_ name: String) throws -> Data {
        try Data(contentsOf: ordner.appending(path: name))
    }

    static func lesen<T: Decodable>(_ name: String, als: T.Type = T.self) throws -> T {
        try JSONDecoder().decode(T.self, from: daten(name))
    }

    static func json(_ name: String) throws -> JSONWert {
        try JSONWert.lesen(daten(name))
    }
}

private func geraet(_ id: String, _ ort: String, port: Int = 80) -> BekanntesGeraet {
    BekanntesGeraet(
        id: id, art: id.hasPrefix("fbh_") ? .verteiler : .heizung, ort: ort,
        adresse: URL(string: "http://192.168.0.10:\(port)")!)
}

/// Kessel, Pufferspeicher und Erdgeschoss wie im Mitschnitt vom 28.08.
@Suite("Zusammenführung aus dem Mitschnitt")
struct MitschnittTests {
    func staende() throws -> [Geraetestand] {
        [
            Geraetestand(geraet: geraet("heiz_19c8a2", "Kessel"), erreichbar: true, letzterKontakt: .now,
                         heizgeraet: try Aufnahme.lesen("mitschnitt/kessel-state-neu.json")),
            Geraetestand(geraet: geraet("heiz_6e03b5", "Pufferspeicher"), erreichbar: true, letzterKontakt: .now,
                         heizgeraet: try Aufnahme.lesen("mitschnitt/puffer-state-neu.json")),
            Geraetestand(geraet: geraet("fbh_5d20e7", "Erdgeschoss"), erreichbar: true, letzterKontakt: .now,
                         verteiler: try Aufnahme.lesen("mitschnitt/erdgeschoss-state-neu.json")),
        ]
    }

    /// Beide Geräte melden Brenner und Ladung; maßgeblich ist, wer den Fühler selbst hat.
    @Test func rollenFolgenDenFuehlern() throws {
        let heizung = try staende().filter { $0.geraet.art == .heizung }
        #expect(Zusammenfuehrung.kesselgeraet(heizung)?.geraet.id == "heiz_19c8a2")
        #expect(Zusammenfuehrung.speichergeraet(heizung)?.geraet.id == "heiz_6e03b5")
        // Die Reihenfolge spielt keine Rolle.
        #expect(Zusammenfuehrung.kesselgeraet(heizung.reversed())?.geraet.id == "heiz_19c8a2")
    }

    @Test func waermeerzeugung() throws {
        let bild = Zusammenfuehrung.bild(try staende())
        let kessel = try #require(bild.kessel)
        #expect(abs((kessel.vorlauf ?? 0) - 27.94) < 0.01)
        #expect(abs((kessel.abgas ?? 0) - 27.44) < 0.01)
        #expect(kessel.laufzeitGestern == 3430)
        #expect(kessel.startsGestern == 2)
        #expect(kessel.abgasAbstand.ladungen == 5)
        #expect(kessel.abgasAbstand.jetzt == nil, "erst ab zehn Ladungen")

        let speicher = try #require(bild.speicher)
        #expect(abs((speicher.temperatur ?? 0) - 53.04) < 0.01)
        #expect(abs((speicher.ladung ?? 0) - 0.075) < 0.001)
        #expect(speicher.phase == "keine Ladung")
        #expect(speicher.rueckstroemungen == 3, "vom Kessel, der den Rücklauf misst")
    }

    @Test func pumpen() throws {
        let bild = Zusammenfuehrung.bild(try staende())
        #expect(bild.heizkreise.map(\.name) == ["Heizkreis 1", "Heizkreis 2"])
        let erreichbar = bild.heizkreise.map(\.relais.erreichbar)
        #expect(erreichbar == [false, false])
        let kreis1 = try #require(bild.heizkreise.first)
        #expect(abs((kreis1.vorlauf ?? 0) - 29.44) < 0.01)
        #expect(kreis1.betriebsart == .automatik)
        let pumpe = try #require(bild.kesselkreispumpe, "am Kessel eingerichtet")
        #expect(pumpe.relais.weg == "HTTP")
        #expect(!pumpe.laeuft)
    }

    @Test func befunde() throws {
        let bild = Zusammenfuehrung.bild(try staende())
        let rueckstroemung = try #require(bild.befunde.first { $0.id.contains(":backflow:") })
        #expect(rueckstroemung.schwere == .warnung)
        #expect(rueckstroemung.text.hasPrefix("3-mal"))
        #expect(bild.befunde.contains { $0.id == "relais" && $0.schwere == .stoerung })
    }

    @Test func etage() throws {
        let bild = Zusammenfuehrung.bild(try staende())
        let eg = try #require(bild.etagen.first)
        #expect(eg.name == "Erdgeschoss")
        #expect(eg.raeume.map(\.name) == ["Küche", "Wohnzimmer", "Kinderzimmer EG", "Flur", "WC"])
        #expect(eg.kanaele.count == 11)
        #expect(eg.raeume.first?.betriebsart == .aus, "im Sommer ausgeschaltet")
        #expect(eg.raeume.first?.id == "fbh_5d20e7/1")
        #expect(eg.traegtAussenfuehler)
        #expect(eg.messfahrt == nil, "keine Messfahrt im Mitschnitt")
        #expect(eg.schutzfahrt.wochentag == 6)
        #expect(bild.aussen?.quelle == "Außenfühler am Verteiler Erdgeschoss")
    }

    @Test func geraete() throws {
        let bild = Zusammenfuehrung.bild(try staende())
        #expect(bild.geraete.first { $0.id == "heiz_19c8a2" }?.art == .kessel)
        #expect(bild.geraete.first { $0.id == "heiz_6e03b5" }?.art == .speicher)
        #expect(bild.geraete.filter { $0.art == .relais }.count == 3)
        let kessel = try #require(bild.geraete.first { $0.id == "heiz_19c8a2" })
        #expect(kessel.fuehler.count == 3)
    }
}

@Suite("Zusammenführung mit Konfiguration")
struct KonfigurierteAnlageTests {
    func verteiler() throws -> Geraetestand {
        Geraetestand(
            geraet: geraet("fbh_a1b2c3", "Erdgeschoss", port: 8321), erreichbar: true, letzterKontakt: .now,
            verteiler: try Aufnahme.lesen("attrappe/verteiler-state.json"),
            konfiguration: try Aufnahme.json("attrappe/verteiler-config.json"),
            thermometer: try Aufnahme.lesen("attrappe/verteiler-ble.json", als: Thermometerliste.self).geraete ?? [],
            thermometerSchluessel: try Aufnahme.lesen("attrappe/verteiler-ble.json", als: Thermometerliste.self).schluessel ?? [])
    }

    /// Der Climate-Sat ohne Schlüssel: ohne Namen im Rundruf, ohne Werte, mit Hinweis
    @Test func verschluesseltesThermometer() throws {
        let geraet = try #require(Zusammenfuehrung.bild([try verteiler()]).geraete.first)
        let klimasat = try #require(geraet.funkthermometer.first { $0.format == "bthome" })
        #expect(klimasat.name == "C0:FF:EE:12:34:56")
        #expect(klimasat.formatbezeichnung == "BTHome")
        #expect(klimasat.verschluesselt)
        #expect(klimasat.schluessel == .fehlt)
        #expect(klimasat.brauchtSchluessel)
        #expect(klimasat.temperatur == nil)
        #expect(klimasat.batterie == nil)
        #expect(geraet.funkschluessel == ["C0:FF:EE:65:43:21"])
        // Der RuuviTag meldet keine Ladung in Prozent; 0 heißt dort keine Angabe.
        #expect(geraet.funkthermometer.first { $0.format == "ruuvi" }?.batterie == nil)
        #expect(geraet.funkthermometer.first { $0.name == "ATC_Kueche" }?.brauchtSchluessel == false)
    }

    @Test func thermometerUndRegelparameter() throws {
        let bild = Zusammenfuehrung.bild([try verteiler()])
        let eg = try #require(bild.etagen.first)
        #expect(eg.raeume.first { $0.name == "Küche" }?.thermometer == "ATC_Kueche")
        #expect(eg.raeume.first { $0.name == "Flur/WC" }?.thermometer == nil)
        #expect(eg.raeume.first?.regelung.proportionalband == 1.0)
        let kanal = try #require(eg.kanaele.first { $0.nummer == 1 })
        #expect(kanal.fahrzeitAuf > 0)
        #expect(kanal.maximal >= kanal.fahrzeitAuf)
        let geraet = try #require(bild.geraete.first)
        #expect(geraet.adresse == "192.168.0.10:8321")
        #expect(geraet.funkthermometer.first { $0.name == "Ruuvi Aussen" }?.zuordnung == "Außenfühler")
        #expect(bild.kessel == nil)
        #expect(bild.speicher == nil)
    }

    @Test func verlaufAusDerHistorie() throws {
        let stand = Geraetestand(
            geraet: geraet("heiz_3f21ac", "Pufferspeicher"), erreichbar: true, letzterKontakt: .now,
            heizgeraet: try Aufnahme.lesen("attrappe/speicher-state.json"),
            verlauf: try Aufnahme.lesen("attrappe/speicher-history.json"))
        let verlauf = Zusammenfuehrung.bild([stand]).verlauf
        #expect(verlauf.punkte(.speicher).count > 200)
        #expect(verlauf.schritt == 300)
        #expect(verlauf.punkte(.hk1Vorlauf).count > 200)
    }

    @Test func unerreichbaresGeraetIstStoerung() throws {
        var stand = try verteiler()
        stand.erreichbar = false
        stand.letzterKontakt = Date(timeIntervalSinceNow: -600)
        // Noch nicht abgefragt ist nicht ausgefallen.
        #expect(!Zusammenfuehrung.bild([stand]).befunde.contains { $0.id == "offline:fbh_a1b2c3" })
        stand.fehler = "Zeitüberschreitung"
        let befund = try #require(Zusammenfuehrung.bild([stand]).befunde.first { $0.id == "offline:fbh_a1b2c3" })
        #expect(befund.schwere == .stoerung)
        #expect(befund.text.contains("10\u{00A0}min"))
    }

    /// Kessel und Speicher schätzen denselben Speicher; ihre Grenzen müssen gleich sein.
    @Test func abweichendeSpeichergrenzen() throws {
        func heizung(_ id: String, voll: Double) throws -> Geraetestand {
            Geraetestand(geraet: geraet(id, id), erreichbar: true,
                         heizgeraet: try Aufnahme.lesen("attrappe/speicher-state.json"),
                         konfiguration: ["buffer": ["voll_c": .zahl(voll), "leer_c": 51.5]])
        }
        let bild = Zusammenfuehrung.bild([try heizung("heiz_000001", voll: 72), try heizung("heiz_000002", voll: 68)])
        #expect(bild.befunde.contains { $0.id == "speichergrenze:voll_c" })
        #expect(!bild.befunde.contains { $0.id == "speichergrenze:leer_c" })
    }

    /// Beide Heizungsgeräte melden denselben Befund; er erscheint einmal.
    @Test func gleicheBefundeEinmal() throws {
        func heizung(_ id: String) throws -> Geraetestand {
            Geraetestand(geraet: geraet(id, id), erreichbar: true, heizgeraet: try Aufnahme.lesen("attrappe/speicher-state.json"))
        }
        let einzeln = Zusammenfuehrung.bild([try heizung("heiz_000001")]).befunde.filter { $0.id.hasPrefix("geraet:") }.count
        let doppelt = Zusammenfuehrung.bild([try heizung("heiz_000001"), try heizung("heiz_000002")]).befunde.filter { $0.id.hasPrefix("geraet:") }.count
        #expect(einzeln > 0)
        #expect(doppelt == einzeln)
    }

    /// Nicht eingebundene Verteiler heißen, wie das Heizungsgerät sie im Netz sieht.
    @Test func versorgteVerteilerMitNamen() throws {
        var z: Heizgeraetezustand = try Aufnahme.lesen("attrappe/speicher-state.json")
        z.heizkreise?[1].abnehmerGesehen = false
        z.heizkreise?[0].veraltet = true
        let stand = Geraetestand(
            geraet: geraet("heiz_3f21ac", "Pufferspeicher"), erreichbar: true, heizgeraet: z,
            konfiguration: try Aufnahme.json("attrappe/speicher-config.json"),
            nachbarn: try Aufnahme.lesen("attrappe/speicher-peers.json", als: Nachbarliste.self).nachbarn ?? [])
        let bild = Zusammenfuehrung.bild([stand, try verteiler()])
        let kreise = bild.heizkreise.sorted { $0.nummer < $1.nummer }
        #expect(kreise.first?.versorgteVerteiler == ["Keller", "Erdgeschoss"])
        #expect(kreise.last?.versorgteVerteiler == ["Obergeschoss"])
        #expect(kreise.first?.bedarfVeraltet == true)
        #expect(kreise.first?.keinVerteilerErreicht == false)
        #expect(kreise.last?.keinVerteilerErreicht == true)
    }

    @Test func freierSpeicher() throws {
        let geraet = try #require(Zusammenfuehrung.bild([try verteiler()]).geraete.first)
        #expect((geraet.freierSpeicher ?? 0) > 0)
    }

    @Test func kreisOhneVerteiler() throws {
        let kreis = Heizkreis(
            nummer: 2, name: "Heizkreis 2", betriebsart: .automatik, pumpeLaeuft: false, grund: "", bedarf: false,
            vorlauf: nil, ruecklauf: nil, versorgteVerteiler: [],
            relais: Relais(adresse: "", kanal: 1, weg: "kein Weg", erreichbar: false, ein: false),
            nachlauf: 0, mindestlaufzeit: 0, mindestpause: 0, mindestSpeicher: 0, frostgrenze: 0)
        var bild = Zusammenfuehrung.bild([])
        bild.heizkreise = [kreis]
        let befunde = Zusammenfuehrung.befunde([], bild: bild, jetzt: .now)
        #expect(befunde.map(\.id) == ["kreis-ohne-verteiler:2"])
    }
}
