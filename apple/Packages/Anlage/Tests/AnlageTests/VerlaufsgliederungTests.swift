import Foundation
import Testing
import Verlauf
@testable import Anlage

@Suite("Verlaufsgliederung")
struct VerlaufsgliederungTests {
    let anlage = Anlagenbild.beispiel

    /// Der Beispielverlauf erscheint in der Form des gespeicherten Verlaufs, zugeordnet zu den
    /// Geräten der Beispielanlage.
    @Test func beispielverlauf() throws {
        let a = Verlaufsauszug.beispiel(anlage)
        let kessel = try #require(anlage.geraete.first { $0.art == .kessel })
        let abgas = try #require(a.reihe(kessel.id, "fuehler.abgas"))
        #expect(abgas.werte.count == a.anzahl)
        #expect(a.schritt == 600)
        #expect(a.reihen.contains { $0.schluessel == "brenner" })
        let kueche = try #require(a.reihen.first { $0.bezeichnung == "Küche" })
        #expect(kueche.schluessel.hasPrefix("raum.") && kueche.schluessel.hasSuffix(".ist"))
        #expect(Verlaufsgliederung.gruppe(kueche) == .raeume)
    }

    @Test func gruppenUndBezeichnungen() {
        let a = Verlaufsauszug.beispiel(anlage)
        let erzeugung = Verlaufsgliederung.reihen(.waermeerzeugung, in: a).map(\.schluessel)
        #expect(erzeugung == ["fuehler.abgas", "fuehler.kessel_vl", "fuehler.kessel_rl", "fuehler.puffer"])
        #expect(Verlaufsgliederung.reihen(.heizkreise, in: a).count == 4)
        // Jeder Heizkreis der Beispielanlage findet seine Reihen, dazu die Außentemperatur.
        for k in anlage.heizkreise {
            let r = Verlaufsgliederung.heizkreis(k, in: a)
            #expect(r.vorlauf != nil && r.ruecklauf != nil, "Heizkreis \(k.nummer)")
        }
        #expect(Verlaufsgliederung.aussen(in: a) != nil)
        // Brenner und Füllstand sind Anteile und gehören ins zweite Diagramm.
        #expect(a.reihen.filter { Verlaufsgliederung.gruppe($0) == nil }.map(\.schluessel).sorted() == ["brenner", "fuellstand"])
        let rl = a.reihen.first { $0.schluessel == "fuehler.hk1_rl" }!
        #expect(Verlaufsgliederung.bezeichnung(rl, in: a) == Parameterkatalog.rollenname("hk1_rl"))
    }
}
