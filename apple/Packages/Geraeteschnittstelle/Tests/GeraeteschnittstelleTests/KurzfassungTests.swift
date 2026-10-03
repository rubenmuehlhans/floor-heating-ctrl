import Foundation
import Testing
@testable import Geraeteschnittstelle

struct KurzfassungTests {
    @Test func verteiler() throws {
        let z = try Fixture.dekodiert("mitschnitt/erdgeschoss-state-neu.json", als: Verteilerzustand.self)
        let k = z.kurzfassung
        #expect(k["ort"] == "Erdgeschoss")
        #expect(k["raeume"]?[0]?["name"] == "Küche")
        #expect(k["raeume"]?[0]?["soll_c"] == 23.5)
        // Gleitkomma-Artefakte der Firmware sind gerundet: 23.600000381469727 → 23.6
        #expect(k["raeume"]?[0]?["ist_c"] == 23.6)
        #expect(k["kanaele"]?.alsListe?.count == 11)
        #expect(k["gegenspannungen"] == nil)
    }

    @Test func heizgeraet() throws {
        let z = try Fixture.dekodiert("mitschnitt/kessel-state-neu.json", als: Heizgeraetezustand.self)
        let k = z.kurzfassung
        #expect(k["ort"] == "Kessel")
        #expect(k["befunde"]?[0]?["code"] == "backflow")
        #expect(k["brenner"]?["laeuft"] != nil)
        #expect(k["fuehler"]?.alsListe?.isEmpty == false)
    }

    /// Die Kurzfassung ist deutlich kleiner als die Rohantwort; sie geht bei jeder Frage an die KI.
    @Test(arguments: ["mitschnitt/erdgeschoss-state-neu.json", "mitschnitt/puffer-state-neu.json"])
    func umfang(_ datei: String) throws {
        let roh = try Fixture.daten(datei)
        let kurz: JSONWert = datei.contains("erdgeschoss")
            ? try Fixture.dekodiert(datei, als: Verteilerzustand.self).kurzfassung
            : try Fixture.dekodiert(datei, als: Heizgeraetezustand.self).kurzfassung
        let groesse = try kurz.daten().count
        #expect(groesse * 2 < roh.count, "\(groesse) statt \(roh.count) Byte")
    }
}
