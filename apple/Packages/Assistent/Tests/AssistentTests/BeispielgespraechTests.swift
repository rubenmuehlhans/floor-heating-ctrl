import Testing
@testable import Assistent
import Anlage

@Suite("Beispielgespräch")
struct BeispielgespraechTests {
    @Test func vorschlaegeImGespraechSindBekannt() {
        let kennungen = Set(Vorschlag.beispiele.map(\.id))
        for beitrag in Gespraech.beispiel.beitraege {
            for case .vorschlag(let id) in beitrag.bausteine {
                #expect(kennungen.contains(id), "\(id)")
            }
        }
    }

    @Test func lageberichtVerweistAufBefundeOderVorschlaege() {
        let befunde = Set(Anlagenbild.beispiel.befunde.map(\.id))
        let vorschlaege = Set(Vorschlag.beispiele.map(\.id))
        for hinweis in Lagebericht.beispiel.hinweise {
            let bezug = hinweis.bezug ?? ""
            #expect(befunde.contains(bezug) || vorschlaege.contains(bezug), "\(bezug)")
        }
        #expect(Lagebericht.beispiel.hinweise.count <= 3)
    }

    @Test func gespraechWechseltDieRollen() {
        let rollen = Gespraech.beispiel.beitraege.map(\.rolle)
        for (a, b) in zip(rollen, rollen.dropFirst()) {
            #expect(a != b)
        }
    }
}
