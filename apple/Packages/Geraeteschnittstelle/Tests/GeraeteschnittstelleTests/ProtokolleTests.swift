import Foundation
import Testing
@testable import Geraeteschnittstelle

struct ProtokolleTests {
    @Test func ladungen() throws {
        let l = try Protokolle.ladungen(try Fixture.text("handgeschrieben/speicher-log-charges.csv"))
        #expect(l.count == 3)
        #expect(l[0].beginn == Date(timeIntervalSince1970: 1_787_834_100))
        #expect(l[0].pufferVorherC == 52.0)
        #expect(l[0].pufferNachherC == 72.3)
        #expect(l[0].liter == 2.09)
        // leeres Feld: kein Außenwert während dieser Ladung
        #expect(l[1].aussenMittelC == nil)
        #expect(l[2].aussenMittelC == -2.5)
    }

    @Test func tage() throws {
        let t = try Protokolle.tage(try Fixture.text("handgeschrieben/speicher-log-days.csv"))
        #expect(t.count == 4)
        #expect(t[0].datum == DateComponents(year: 2026, month: 8, day: 28))
        #expect(t[0].heizgradtage == nil)
        #expect(t[1].laufzeitS == 3420)
        #expect(t[3].aussenMinC == -6.2)
        #expect(t[3].heizgradtage == 18.3)
    }

    @Test func fehlendeSpalteWirdBenannt() {
        #expect(throws: Geraetefehler.unerwarteteAntwort("Spalte liter fehlt")) {
            try Protokolle.ladungen("beginn,dauer_s,brenner_s,starts\n1,2,3,4\n")
        }
    }

    @Test func spaltenfolgeSpieltKeineRolle() throws {
        let t = try Protokolle.tage("starts,datum,liter,laufzeit_s\n2,2026-08-27,1.500,600\n")
        #expect(t.first?.starts == 2)
        #expect(t.first?.laufzeitS == 600)
    }

    @Test func aufzeichnungAlsTabelle() throws {
        let (kopf, zeilen) = try Protokolle.tabelle("t_s,abgas,kessel_vl\n0,31.2,40.1\n5,,40.3\n")
        #expect(kopf == ["t_s", "abgas", "kessel_vl"])
        #expect(zeilen[1] == [5, nil, 40.3])
    }
}
