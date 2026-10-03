import Foundation
import Testing
import Verlauf
@testable import Anlage

@Suite("Heizkurve")
struct HeizkurveTests {
    private func kreis(_ n: Int, vl: String? = nil) -> Heizkreis {
        Heizkreis(nummer: n, name: "Kreis \(n)", betriebsart: .automatik, pumpeLaeuft: true, grund: "", bedarf: true,
                  vorlauf: nil, ruecklauf: nil, versorgteVerteiler: [],
                  relais: Relais(adresse: "", kanal: 1, weg: "HTTP", erreichbar: true, ein: true),
                  nachlauf: 0, mindestlaufzeit: 0, mindestpause: 0, mindestSpeicher: 0, frostgrenze: 0,
                  vorlaufRolle: vl)
    }

    /// 48 Plätze: Außen von −4 bis 12 °C, Vorlauf 36 − 0,7 · Außen; die Pumpe steht in den
    /// ersten zwölf Plätzen, dort liegt der Vorlauf weit unter der Kurve.
    private func auszug() -> Verlaufsauszug {
        let n = 48
        let aussen = (0..<n).map { Optional(-4 + 16 * Double($0) / Double(n - 1)) }
        let pumpe = (0..<n).map { Optional($0 < 12 ? 0.0 : 1.0) }
        let vl = (0..<n).map { i in Optional(i < 12 ? 22.0 : 36 - 0.7 * aussen[i]!) }
        let rl = vl.map { $0.map { $0 - 4 } }
        return Verlaufsauszug(beginn: Date(timeIntervalSince1970: 1_790_000_000), schritt: 300, anzahl: n, reihen: [
            .init(geraet: "heiz_2", ort: "Speicher", schluessel: "fuehler.hk1_vl", bezeichnung: "", werte: vl),
            .init(geraet: "heiz_2", ort: "Speicher", schluessel: "fuehler.hk1_rl", bezeichnung: "", werte: rl),
            .init(geraet: "heiz_2", ort: "Speicher", schluessel: "pumpe.1", bezeichnung: "", werte: pumpe),
            .init(geraet: "fbh_1", ort: "EG", schluessel: "aussen", bezeichnung: "", werte: aussen),
        ])
    }

    @Test func reihenNachRolle() throws {
        let a = auszug()
        let r = Verlaufsgliederung.heizkreis(kreis(1), in: a)
        #expect(r.vorlauf?.schluessel == "fuehler.hk1_vl" && r.ruecklauf != nil && r.pumpe?.schluessel == "pumpe.1")
        #expect(Verlaufsgliederung.heizkreis(kreis(2), in: a).leer)
        // Eine abweichende Rolle aus der Konfiguration gilt vor der Vorgabe.
        #expect(Verlaufsgliederung.heizkreis(kreis(2, vl: "hk1_vl"), in: a).vorlauf != nil)
        #expect(Verlaufsgliederung.aussen(in: a)?.geraet == "fbh_1")
    }

    @Test func nurBeiLaufenderPumpe() throws {
        let a = auszug()
        let r = Verlaufsgliederung.heizkreis(kreis(1), in: a)
        let aussen = try #require(Verlaufsgliederung.aussen(in: a))
        let p = Heizkurve.punkte(a, wert: r.vorlauf!, aussen: aussen, pumpe: r.pumpe)
        #expect(p.count == 36)
        let g = try #require(Heizkurve.gerade(p))
        #expect(abs(g.steigung + 0.7) < 0.001)
        #expect(abs(g.beiNull - 36) < 0.001)
        #expect(g.bestimmtheit > 0.999)
        // Mit den Plätzen ohne Pumpe verfälscht der ausgekühlte Kreis die Kurve.
        let alle = Heizkurve.punkte(a, wert: r.vorlauf!, aussen: aussen, pumpe: nil)
        #expect(alle.count == 48)
        #expect(abs(Heizkurve.gerade(alle)!.steigung + 0.7) > 0.05)
    }

    @Test func ohneSchwankungKeineGerade() {
        let t = Date.now
        let p = (0..<30).map { Heizkurve.Punkt(zeit: t.addingTimeInterval(Double($0)), aussen: 5 + Double($0 % 3) * 0.5, wert: 33) }
        #expect(Heizkurve.gerade(p) == nil, "1 K Schwankung reicht nicht für eine Steigung")
        #expect(Heizkurve.gerade(Array(p.prefix(5))) == nil)
    }
}
