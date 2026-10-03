import Foundation
import Testing
import Verlauf
@testable import Anlage

@Suite("Wärmepumpen-Check")
struct WaermepumpencheckTests {
    /// 0,4 Brennerstunden je Heizgradtag, 1,5 Stunden Warmwasser; Tage bis 16 Heizgradtage (Mittel 4 °C)
    private func linie(steigung: Double = 0.4, bestimmtheit: Double = 0.9) -> Verbrauchslinie {
        Verbrauchslinie(gueltig: true, erfassteTage: 20, grund: nil, steigung: steigung, grundlast: 1.5,
                        bestimmtheit: bestimmtheit,
                        tage: (0..<20).map { .init(nummer: $0, heizgradtage: Double($0) * 16 / 19, laufzeitStunden: 0, auffaellig: false) })
    }

    @Test func heizlastAusDerVerbrauchslinie() throws {
        var a = Waermepumpencheck.Annahmen()
        a.normaussen = -12
        let h = try #require(Waermepumpencheck.heizlast(linie(), duese: 2.2, gemessen: nil, annahmen: a))
        // 2,2 l/h × 10 kWh/l × 0,88 = 19,36 kW Kesselleistung
        #expect(abs(h.kesselleistung - 19.36) < 1e-9)
        // 0,4 h je Heizgradtag × 19,36 kW = 7,744 kWh je Heizgradtag; bei −12 °C 32 Heizgradtage
        #expect(abs(h.kwhJeHeizgradtag - 7.744) < 1e-9)
        #expect(abs(h.kilowatt - 7.744 * 32 / 24) < 1e-9)
        #expect(abs(h.wattJeKelvin - 322.67) < 0.01)
        #expect(abs(h.warmwasserKWhJeTag - 29.04) < 1e-9)
        #expect(abs(h.stundenAmAuslegungstag - (1.5 + 0.4 * 32)) < 1e-9)
        #expect(h.kaeltesterTag == 4)
        #expect(!h.gemessen)
        #expect(h.hinweise.contains { $0.contains("angenommen") })
        #expect(h.hinweise.contains { $0.contains("kälteste erfasste Tag") }, "16 K über der Normaußentemperatur")
        // Gemessener Durchsatz ersetzt die Düse und nimmt den Hinweis zurück.
        let g = try #require(Waermepumpencheck.heizlast(linie(), duese: 2.2, gemessen: 1.9, annahmen: a))
        #expect(g.gemessen && abs(g.kesselleistung - 1.9 * 8.8) < 1e-9)
        #expect(!g.hinweise.contains { $0.contains("angenommen") })
    }

    @Test func ohneLinieOderDurchsatzNichts() {
        var ungueltig = linie()
        ungueltig.gueltig = false
        #expect(Waermepumpencheck.heizlast(ungueltig, duese: 2.2, gemessen: nil, annahmen: .init()) == nil)
        #expect(Waermepumpencheck.heizlast(linie(), duese: nil, gemessen: nil, annahmen: .init()) == nil)
        let viel = Waermepumpencheck.heizlast(linie(steigung: 0.8), duese: 2.2, gemessen: nil, annahmen: .init())
        #expect(viel?.hinweise.contains { $0.contains("mehr als ein Tag hat") } == true)
    }

    @Test func tankablesungen() throws {
        let t0 = Date(timeIntervalSince1970: 1_790_000_000)
        let a = [
            Waermepumpencheck.Tankablesung(datum: t0.addingTimeInterval(86_400 * 60), liter: 1400, nachgetankt: 1000),
            Waermepumpencheck.Tankablesung(datum: t0, liter: 800),
            Waermepumpencheck.Tankablesung(datum: t0.addingTimeInterval(86_400 * 30), liter: 600),
        ]
        // 800 → 600: 200 l; dann 1000 nachgetankt, 600 + 1000 − 1400 = 200 l
        let v = try #require(Waermepumpencheck.verbrauch(a))
        #expect(v.liter == 400 && v.von == t0)
        let k = Waermepumpencheck.kalibrierung(liter: 400, stunden: 200, abdeckung: 0.97)
        #expect(k.durchsatz == 2 && k.grund == nil)
        #expect(Waermepumpencheck.kalibrierung(liter: 400, stunden: 200, abdeckung: 0.6).durchsatz == nil)
        #expect(Waermepumpencheck.kalibrierung(liter: 10, stunden: 5, abdeckung: 1).grund?.contains("mindestens 20") == true)
        #expect(Waermepumpencheck.verbrauch(Array(a.prefix(1))) == nil)
    }

    @Test func vorlaufUndRaeume() throws {
        let bild = Anlagenbild.beispiel
        let kreis = try #require(bild.heizkreise.first)
        let etage = try #require(bild.etagen.first)
        let raum = try #require(etage.raeume.first)
        let n = 200
        let aussen = (0..<n).map { Optional(-5 + 15 * Double($0) / Double(n - 1)) }
        let vl = aussen.map { $0.map { 36 - 0.5 * $0 } }
        // Der Raum bleibt bei Kälte 1 K unter dem Sollwert, obwohl die Ventile offen stehen.
        let ist = aussen.map { $0.map { $0 < 0 ? 19.0 : 20.5 } }
        let soll = aussen.map { _ in Optional(20.0) }
        let stellung = aussen.map { $0.map { $0 < 0 ? 1.0 : 0.4 } }
        let a = Verlaufsauszug(beginn: Date(timeIntervalSince1970: 1_790_000_000), schritt: 3600, anzahl: n, reihen: [
            .init(geraet: "heiz_x", ort: "", schluessel: Messgroesse.fuehler(kreis.vorlaufRolle), bezeichnung: "", werte: vl),
            .init(geraet: etage.id, ort: etage.name, schluessel: "aussen", bezeichnung: "", werte: aussen),
            .init(geraet: etage.id, ort: etage.name, schluessel: Messgroesse.raum(raum.nummer, "ist"), bezeichnung: "", werte: ist),
            .init(geraet: etage.id, ort: etage.name, schluessel: Messgroesse.raum(raum.nummer, "soll"), bezeichnung: "", werte: soll),
            .init(geraet: etage.id, ort: etage.name, schluessel: Messgroesse.raum(raum.nummer, "stellung"), bezeichnung: "", werte: stellung),
        ])
        var annahmen = Waermepumpencheck.Annahmen()
        annahmen.normaussen = -12
        let v = try #require(Waermepumpencheck.vorlauf(kreis, auszug: a, annahmen: annahmen))
        #expect(abs((v.beiNormaussen ?? 0) - 42) < 0.01, "36 + 0,5 × 12")
        #expect(v.tiefsteAussentemperatur == -5)
        #expect(v.belastbar, "enge Kurve, gemessen bis 7 K über der Normaußentemperatur")
        // Nur milde Tage: dieselbe Kurve ist nicht belastbar.
        let mild = Verlaufsauszug(beginn: a.beginn, schritt: 3600, anzahl: n, reihen: a.reihen.map { r in
            var r = r
            if r.schluessel == "aussen" { r.werte = r.werte.map { $0.map { $0 + 12 } } }
            if r.schluessel.hasSuffix("_vl") { r.werte = aussen.map { $0.map { 36 - 0.5 * ($0 + 12) } } }
            return r
        })
        let vm = try #require(Waermepumpencheck.vorlauf(kreis, auszug: mild, annahmen: annahmen))
        #expect(!vm.belastbar && vm.vorbehalt?.contains("nur bis 7,0 °C") == true)
        #expect((v.hoechsterBeiKaelte ?? 0) > 38 && (v.hoechsterBeiKaelte ?? 0) <= 38.5)
        #expect(Waermepumpencheck.bewertung(42) == "günstig")
        #expect(Waermepumpencheck.bewertung(35) == "sehr günstig für eine Wärmepumpe")

        let r = Waermepumpencheck.raeume(bild, auszug: a, kaelteBis: 0)
        let befund = try #require(r.first)
        #expect(befund.nummer == raum.nummer && befund.anteilUnterversorgt == 1 && befund.mittlereStellung == 1)
        #expect(abs(befund.mittlereAbweichung + 1) < 1e-9)
    }

    @Test func brennerstundenAusDemVerlauf() {
        let a = Verlaufsauszug(beginn: .now, schritt: 3600, anzahl: 4, reihen: [
            .init(geraet: "k", ort: "", schluessel: "brenner", bezeichnung: "", werte: [0.5, 1, nil, 0.25]),
        ])
        let b = Waermepumpencheck.brennerstunden(a)
        #expect(b.stunden == 1.75 && b.abdeckung == 0.75)
    }
}
