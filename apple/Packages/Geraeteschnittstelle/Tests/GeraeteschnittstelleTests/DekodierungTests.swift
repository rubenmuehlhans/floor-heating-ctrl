import Foundation
import Testing
@testable import Geraeteschnittstelle

/// Die Modelle müssen die Antworten aller Firmwarestände lesen: den ältesten und den jüngsten
/// Datensatz des Mitschnitts (v0.3.0-8 bis -12) und den Aufbau der Attrappen.
struct DekodierungTests {
    @Test(arguments: ["mitschnitt/erdgeschoss-state-alt.json", "mitschnitt/erdgeschoss-state-neu.json", "attrappe/verteiler-state.json"])
    func verteilerzustand(_ datei: String) throws {
        let z = try Fixture.dekodiert(datei, als: Verteilerzustand.self)
        let raeume = try #require(z.raeume)
        #expect(!raeume.isEmpty)
        #expect(raeume.allSatisfy { $0.id != nil && $0.name != nil })
        #expect(z.kanaele?.count == 11)
        #expect(z.geraet?.art == .verteiler)
        #expect(z.revision != nil)
    }

    @Test func verteilerAusDemMitschnitt() throws {
        let z = try Fixture.dekodiert("mitschnitt/erdgeschoss-state-neu.json", als: Verteilerzustand.self)
        #expect(z.geraet?.id == "fbh_5d20e7")
        #expect(z.geraet?.ort == "Erdgeschoss")
        #expect(z.raeume?.map(\.name) == ["Küche", "Wohnzimmer", "Kinderzimmer EG", "Flur", "WC"])
        #expect(z.aussen?.zugeordnet == true)
        #expect(z.bordfuehler?.klima?.gueltig == true)
        #expect(z.messfahrt?.zustand == "idle")
    }

    @Test(arguments: [
        "mitschnitt/puffer-state-alt.json", "mitschnitt/puffer-state-neu.json",
        "mitschnitt/kessel-state-alt.json", "mitschnitt/kessel-state-neu.json",
        "attrappe/speicher-state.json", "attrappe/kessel-state.json",
    ])
    func heizgeraetezustand(_ datei: String) throws {
        let z = try Fixture.dekodiert(datei, als: Heizgeraetezustand.self)
        #expect(z.geraet?.art == .heizung)
        #expect(z.brenner != nil)
        #expect(z.ladung != nil)
        #expect(!(z.fuehler ?? []).isEmpty)
        // Die Firmware schreibt die ROM mit der Familie 28 am Ende, die Attrappe am Anfang.
        #expect((z.fuehler ?? []).allSatisfy { $0.rom?.count == 16 })
    }

    @Test func pufferspeicherAusDemMitschnitt() throws {
        let z = try Fixture.dekodiert("mitschnitt/puffer-state-neu.json", als: Heizgeraetezustand.self)
        #expect(z.geraet?.id == "heiz_6e03b5")
        #expect(z.heizkreise?.map(\.name) == ["Heizkreis 1", "Heizkreis 2"])
        #expect(z.befunde?.isEmpty != false)
        #expect(z.bedarfsquellen?.contains { $0.ort == "Keller" } == true)
        #expect(z.fremdwerte?["kessel_vl"] != nil)
        #expect(z.ladung?.kalibrierung?.hoechstwertC ?? 0 > 68)
    }

    @Test func kesselMeldetRueckstroemung() throws {
        let z = try Fixture.dekodiert("mitschnitt/kessel-state-neu.json", als: Heizgeraetezustand.self)
        let befund = try #require(z.befunde?.first)
        #expect(befund.code == "backflow")
        #expect(befund.ereignisse == 3)
        #expect(befund.anstiegK ?? 0 > 3)
    }

    @Test func aeltereFirmwareOhneNeuereFelder() throws {
        let alt = try Fixture.dekodiert("mitschnitt/puffer-state-alt.json", als: Heizgeraetezustand.self)
        let neu = try Fixture.dekodiert("mitschnitt/puffer-state-neu.json", als: Heizgeraetezustand.self)
        #expect(alt.version != neu.version)
        #expect(neu.verbrauchslinie != nil)
    }

    @Test(arguments: ["mitschnitt/erdgeschoss-demand-alt.json", "mitschnitt/erdgeschoss-demand-neu.json", "attrappe/verteiler-demand.json"])
    func bedarf(_ datei: String) throws {
        let b = try Fixture.dekodiert(datei, als: Bedarfsantwort.self)
        #expect(b.id?.hasPrefix("fbh_") == true)
        #expect(b.bedarf != nil)
    }

    /// Ein verschlüsselt sendendes Thermometer ohne Schlüssel: gelistet, aber ohne Werte
    @Test func verschluesseltesThermometer() throws {
        let liste = try Fixture.dekodiert("attrappe/verteiler-ble.json", als: Thermometerliste.self)
        let klimasat = try #require(liste.geraete?.first { $0.format == "bthome" })
        #expect(klimasat.verschluesselt == true)
        #expect(klimasat.schluessel == "missing")
        #expect(klimasat.temperaturC == nil)
        #expect(klimasat.feuchte == nil)
        #expect(liste.schluessel == ["C0:FF:EE:65:43:21"])
        // Die älteren Formate kennen die Felder nicht.
        #expect(liste.geraete?.first { $0.format == "pvvx" }?.verschluesselt == nil)
    }

    @Test func leitstand() throws {
        let z = try Fixture.dekodiert("attrappe/leitstand-state.json", als: Leitstandzustand.self)
        #expect(z.geraet?.id == "lst_c0ffee")
        #expect(z.geraet?.rolle == Leitstand.rolle)
        #expect(z.geraet?.platine == "M5Stack Core")
        #expect(z.netz?.verbunden == true)
        #expect(z.aussen?.zugeordnet == true)
        #expect(z.aussen?.gueltig == true)
        #expect(z.aussen?.temperaturC != nil)
        #expect(z.funk?.geraete == 3)
        #expect(z.anlage?.erreichbar == 5)
        #expect(z.anzeige?.seite == "anlage")
        #expect((z.tiefsterSpeicher ?? 0) > 0)

        let liste = try Fixture.dekodiert("attrappe/leitstand-ble.json", als: Thermometerliste.self)
        #expect(liste.aussenfuehler == "C0:FF:EE:12:34:56")
        #expect(liste.schluessel == ["C0:FF:EE:12:34:56"])
        let klimasat = try #require(liste.geraete?.first { $0.format == "bthome" })
        #expect(klimasat.schluessel == "ok")
        #expect(klimasat.temperaturC != nil)
    }

    @Test func leitstandErkennung() throws {
        #expect(Leitstand.istLeitstand(rolle: "station", kennung: nil))
        #expect(Leitstand.istLeitstand(rolle: nil, kennung: "lst_a1b2c3"))
        #expect(!Leitstand.istLeitstand(rolle: "manifold", kennung: "lst_a1b2c3"))
        #expect(!Leitstand.istLeitstand(rolle: nil, kennung: "fbh_a1b2c3"))
        // Der Verteiler kennt das Feld nicht.
        #expect(try Fixture.dekodiert("attrappe/verteiler-ble.json", als: Thermometerliste.self).aussenfuehler == nil)
    }

    @Test func weitereAntwortenDerAttrappen() throws {
        #expect(try Fixture.dekodiert("attrappe/verteiler-ble.json", als: Thermometerliste.self).geraete?.isEmpty == false)
        #expect(try Fixture.dekodiert("attrappe/verteiler-calib.json", als: Messreihe.self).stand?.zustand == "idle")
        #expect(try Fixture.dekodiert("attrappe/verteiler-peers.json", als: Nachbarliste.self).nachbarn?.isEmpty == false)
        #expect(try Fixture.dekodiert("attrappe/verteiler-wifi-scan.json", als: Netzsuche.self).netze?.isEmpty == false)
        #expect(try Fixture.dekodiert("attrappe/speicher-measurements.json", als: Messwertantwort.self).fuehler?.isEmpty == false)
    }

    @Test func verlauf() throws {
        let v = try Fixture.dekodiert("attrappe/speicher-history.json", als: Verlaufsantwort.self)
        let rollen = try #require(v.rollen)
        let punkte = try #require(v.punkte)
        for rolle in rollen {
            #expect(v.reihen?[rolle]?.count == punkte, "Reihe \(rolle)")
        }
        // Der jüngste Punkt steht zuletzt und trägt newest_epoch.
        let letzter = try #require(v.zeitpunkt(punkte - 1, anzahl: punkte))
        #expect(letzter.timeIntervalSince1970 == Double(try #require(v.juengsterEpoch)))
    }
}
