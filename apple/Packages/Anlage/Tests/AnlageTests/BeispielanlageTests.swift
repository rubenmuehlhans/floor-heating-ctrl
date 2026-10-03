import Foundation
import Testing
@testable import Anlage

@Suite("Beispielanlage")
struct BeispielanlageTests {
    let anlage = Anlagenbild.beispiel

    @Test func entsprichtDemMitschnitt() {
        #expect(anlage.etagen.map(\.name) == ["Keller", "Erdgeschoss", "Obergeschoss"])
        #expect(anlage.raeume.count == 11)
        #expect(anlage.etagen.flatMap(\.kanaele).count == 33)
        #expect(anlage.etagen.flatMap(\.kanaele).filter(\.kalibriert).count == 16)
    }

    @Test func raumkanaeleSindBelegt() {
        for etage in anlage.etagen {
            for raum in etage.raeume {
                for nummer in raum.kanaele {
                    let kanal = etage.kanaele.first { $0.nummer == nummer }
                    #expect(kanal?.raum == raum.name, "\(etage.name) Kanal \(nummer)")
                }
            }
        }
    }

    @Test func zustaendeKommenVor() {
        let zustaende = Set(anlage.raeume.map(\.zustand))
        #expect(zustaende.contains(.heizt))
        #expect(zustaende.contains(.sollErreicht))
        #expect(zustaende.contains(.ausgeschaltet))
        #expect(zustaende.contains(.keinThermometer))
    }

    @Test func regelgesetzWieFirmware() throws {
        let bad = try #require(anlage.raeume.first { $0.name == "Bad" })
        // (24 − 23,5) / 1 K → 0,75, gerastert auf 0,1 → 0,8
        #expect(abs(bad.berechneZielstellung() - 0.8) < 0.001)
        // (23,5 − 23,6) / 1 K → 0,45; in Gleitkomma knapp darunter, also 0,4 – wie roundf()
        let kueche = try #require(anlage.raeume.first { $0.name == "Küche" })
        #expect(abs(kueche.berechneZielstellung() - 0.4) < 0.001)
        #expect(abs(kueche.zielstellung - 0.4) < 0.001)
    }

    @Test func verlaufUmfasstEinenTag() {
        let speicher = anlage.verlauf.punkte(.speicher)
        #expect(speicher.count == 144)
        #expect(speicher.map(\.wert).max() ?? 0 > 70)
        #expect(anlage.verlauf.punkte(.aussen).count == 144)
    }

    @Test func messfahrtEndetInBeidenRichtungenMitAnstieg() throws {
        let m = try #require(anlage.etage("fbh_5d20e7")?.messfahrt)
        let grenze = try #require(m.oeffnenAb)
        let schwelle = try #require(m.vorschlag?.schwelle)
        let zu = m.werte[..<grenze].reversed().drop { $0 == 0 }.reversed()
        let auf = m.werte[grenze...]
        #expect(zu.suffix(3).allSatisfy { $0 > schwelle })
        #expect(auf.suffix(3).allSatisfy { $0 > schwelle })
    }
}
