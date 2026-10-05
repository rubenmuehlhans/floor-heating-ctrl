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

@Suite("Wärmepumpen-Check: Heizlast aus der Speicherwärme")
struct WaermelastTests {
    private let kalender: Calendar = {
        var k = Calendar(identifier: .gregorian)
        k.timeZone = TimeZone(identifier: "Europe/Berlin")!
        return k
    }()

    private func tag(_ i: Int) -> Date {
        kalender.date(from: DateComponents(year: 2026, month: 11, day: 1 + i))!
    }

    private func gradtage(_ i: Int) -> Double { 2 + 12 * Double(i % 7) / 6 }

    /// Ein Haus mit 12 kWh Sockel je Tag und 0,9 kWh je Heizgradtag; je Tag eine Ladung um 3 Uhr,
    /// eine Stunde lang, die den Speicher wieder auf 70 °C bringt.
    private func anlage(tage n: Int = 20, volumen: Double = 850) -> (ladungen: [Ladungssatz], tage: [Tagessatz]) {
        let kapazitaet = volumen * Waermepumpencheck.wasser
        let tage = (0...n).map { i in
            Tagessatz(datum: tag(i), laufzeit: 3000, starts: 1, liter: 1.8, heizgradtage: gradtage(i),
                      aussenMin: 20 - gradtage(i) - 3, aussenMax: 20 - gradtage(i) + 3)
        }
        var ladungen: [Ladungssatz] = []
        var vorher = 55.0
        for i in 0..<n {
            let beginn = tag(i).addingTimeInterval(3 * 3600)
            ladungen.append(Ladungssatz(beginn: beginn, dauer: 3600, brenner: 3000, starts: 1, speicherVorher: vorher,
                                        speicherNachher: 70, kesselVorlaufMax: 79, abgasMax: 86, aussenMittel: nil, liter: 1.8))
            // Bis zur nächsten Ladung: 20 Stunden dieses Tages, 3 des nächsten
            let hgt = gradtage(i) * 20 / 24 + gradtage(i + 1) * 3 / 24
            let dauer = 23.0 / 24
            let kwh = (12 + 0.9 * hgt / dauer) * dauer
            vorher = 70 - kwh / kapazitaet
        }
        return (ladungen, tage)
    }

    @Test func geradeAusDenEntladungen() throws {
        let a = anlage()
        var annahmen = Waermepumpencheck.Annahmen()
        annahmen.normaussen = -12
        let w = Waermepumpencheck.waermelast(ladungen: a.ladungen, tage: a.tage, volumen: 850, annahmen: annahmen,
                                              kalender: kalender)
        #expect(w.grund == nil)
        #expect(w.entladungen.count == 19)
        let steigung = try #require(w.kwhJeHeizgradtag)
        #expect(abs(steigung - 0.9) < 1e-6)
        #expect(abs((w.sockelKWhJeTag ?? 0) - 12) < 1e-6)
        #expect(abs((w.kilowatt ?? 0) - 0.9 * 32 / 24) < 1e-6)
        #expect(abs((w.wattJeKelvin ?? 0) - 37.5) < 1e-6)
        #expect((w.bestimmtheit ?? 0) > 0.999)
        // Ladeleistung: 15 K in 850 l in 50 Minuten Brenner, im Mittel über alle Ladungen
        #expect((w.ladeleistung ?? 0) > 10)
    }

    @Test func nachzuendenGehoertZurLadung() {
        var a = anlage()
        // Eine halbe Stunde nach der dritten Ladung zündet der Kessel nach und hebt den Speicher.
        let dritte = a.ladungen[2]
        a.ladungen.insert(Ladungssatz(beginn: dritte.beginn.addingTimeInterval(5400), dauer: 1200, brenner: 600, starts: 0,
                                      speicherVorher: 69.5, speicherNachher: 71, kesselVorlaufMax: 79, abgasMax: 58,
                                      aussenMittel: nil, liter: 0.4), at: 3)
        let w = Waermepumpencheck.waermelast(ladungen: a.ladungen, tage: a.tage, volumen: 850, annahmen: .init(),
                                              kalender: kalender)
        #expect(w.entladungen.count == 19, "die Nachzündung beginnt keine eigene Entladung")
        #expect(w.grund == nil)
    }

    @Test func unvollstaendigeTageFehlen() {
        var a = anlage()
        // Tag 5 mit nur einem Außenwert: 13 °C, aber keine Heizgradtage
        a.tage[5] = Tagessatz(datum: tag(5), laufzeit: 3000, starts: 1, liter: 1.8, heizgradtage: 0,
                              aussenMin: 13, aussenMax: 13)
        let w = Waermepumpencheck.waermelast(ladungen: a.ladungen, tage: a.tage, volumen: 850, annahmen: .init(),
                                              kalender: kalender)
        // Die Entladungen, die Tag 5 berühren, fallen weg.
        #expect(w.entladungen.count == 17)
        #expect(abs((w.kwhJeHeizgradtag ?? 0) - 0.9) < 1e-6)
    }

    @Test func ohneGrundlageEinGrund() {
        let a = anlage(tage: 5)
        let wenig = Waermepumpencheck.waermelast(ladungen: a.ladungen, tage: a.tage, volumen: 850, annahmen: .init(),
                                                  kalender: kalender)
        #expect(wenig.kilowatt == nil && wenig.grund?.contains("mindestens 8") == true)
        let ohne = Waermepumpencheck.waermelast(ladungen: a.ladungen, tage: a.tage, volumen: nil, annahmen: .init(),
                                                 kalender: kalender)
        #expect(ohne.grund?.contains("Inhalt des Speichers") == true)
    }
}
