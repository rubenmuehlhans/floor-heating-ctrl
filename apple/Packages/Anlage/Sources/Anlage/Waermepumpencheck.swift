import Foundation
import Verlauf

/// Grundlagen für die Planung einer Wärmepumpe aus dem Betrieb der Ölheizung: Heizlast bei der
/// Normaußentemperatur, Warmwasseranteil, nötige Vorlauftemperatur je Heizkreis und die Räume,
/// die eine niedrigere Vorlauftemperatur begrenzen.
///
/// Die Heizlast folgt aus der Verbrauchslinie: Brennerstunden je Heizgradtag mal Kesselleistung.
/// Die Kesselleistung ist Düsendurchsatz mal Heizwert mal Wirkungsgrad; der Durchsatz ist
/// angenommen, bis Tankablesungen ihn messen. Das ersetzt keine raumweise Heizlastberechnung,
/// ist aber ein Gegencheck aus dem tatsächlichen Verbrauch.
public enum Waermepumpencheck {
    /// Heizgradtage zählen jede Stunde unter 20 °C (siehe Handbuch, Heizgradtag).
    public static let bezug = 20.0

    public struct Annahmen: Codable, Sendable, Equatable {
        /// Normaußentemperatur des Orts nach DIN/TS 12831-1
        public var normaussen = -12.0
        /// Heizwert Heizöl EL, kWh je Liter
        public var heizwert = 10.0
        /// Anteil der Brennstoffenergie, der im Wasser ankommt
        public var wirkungsgrad = 0.88

        public init() {}
    }

    /// Ablesung des Tanks. `nachgetankt` sind die Liter, die seit der vorigen Ablesung
    /// hinzukamen.
    public struct Tankablesung: Codable, Sendable, Equatable, Identifiable {
        public var id = UUID()
        public var datum: Date
        public var liter: Double
        public var nachgetankt: Double = 0

        public init(datum: Date, liter: Double, nachgetankt: Double = 0) {
            self.datum = datum
            self.liter = liter
            self.nachgetankt = nachgetankt
        }
    }

    /// Verbrauch zwischen erster und letzter Ablesung
    public static func verbrauch(_ a: [Tankablesung]) -> (liter: Double, von: Date, bis: Date)? {
        let s = a.sorted { $0.datum < $1.datum }
        guard s.count >= 2, let erste = s.first, let letzte = s.last else { return nil }
        var liter = 0.0
        for (vorige, jetzt) in zip(s, s.dropFirst()) {
            liter += vorige.liter + jetzt.nachgetankt - jetzt.liter
        }
        return (liter, erste.datum, letzte.datum)
    }

    public struct Kalibrierung: Sendable, Equatable {
        public var liter: Double
        public var stunden: Double
        /// Anteil der Zeit zwischen den Ablesungen, für den der Brennerlauf aufgezeichnet ist
        public var abdeckung: Double
        /// Liter je Stunde Brennerlauf; nil, wenn die Grundlage zu schmal ist
        public var durchsatz: Double?
        public var grund: String?
    }

    /// Mindestens 20 Brennerstunden und 90 % aufgezeichnete Zeit, sonst wirkt ein Ablesefehler
    /// von wenigen Litern zu stark.
    public static func kalibrierung(liter: Double, stunden: Double, abdeckung: Double) -> Kalibrierung {
        var k = Kalibrierung(liter: liter, stunden: stunden, abdeckung: abdeckung)
        if liter <= 0 {
            k.grund = "Zwischen den Ablesungen ist kein Verbrauch zu erkennen; nachgetankte Liter prüfen."
        } else if abdeckung < 0.9 {
            k.grund = "Der Brennerlauf ist nur für \(Int((abdeckung * 100).rounded())) % der Zeit zwischen den Ablesungen aufgezeichnet."
        } else if stunden < 20 {
            k.grund = "Zwischen den Ablesungen lief der Brenner erst \(Int(stunden.rounded())) Stunden; nötig sind mindestens 20."
        } else {
            k.durchsatz = liter / stunden
        }
        return k
    }

    public struct Heizlast: Sendable, Equatable {
        /// Liter je Stunde und ob gemessen
        public var durchsatz: Double
        public var gemessen: Bool
        /// Wärmeleistung des Kessels, während der Brenner läuft
        public var kesselleistung: Double
        public var kwhJeHeizgradtag: Double
        /// Wärmeverlust des Hauses in W je K Temperaturunterschied
        public var wattJeKelvin: Double
        /// Heizlast bei der Normaußentemperatur, ohne Warmwasser
        public var kilowatt: Double
        public var warmwasserKWhJeTag: Double
        /// Brennerstunden, die der Auslegungstag bräuchte, samt Warmwasser
        public var stundenAmAuslegungstag: Double
        /// Kältester erfasster Tag als Tagesmittel, aus seinen Heizgradtagen
        public var kaeltesterTag: Double?
        public var bestimmtheit: Double?
        public var hinweise: [String]
    }

    public static func heizlast(_ linie: Verbrauchslinie, duese: Double?, gemessen: Double?, annahmen a: Annahmen) -> Heizlast? {
        guard linie.gueltig, linie.steigung > 0, let durchsatz = gemessen ?? duese, durchsatz > 0 else { return nil }
        let leistung = durchsatz * a.heizwert * a.wirkungsgrad
        let jeGradtag = linie.steigung * leistung
        let gradtage = max(0, bezug - a.normaussen)
        let stunden = linie.grundlast + linie.steigung * gradtage
        let kaeltester = linie.tage.map(\.heizgradtage).max().map { bezug - $0 }
        var hinweise: [String] = []
        if gemessen == nil {
            hinweise.append("Der Düsendurchsatz von \(zahl(durchsatz, 2)) l/h ist angenommen, nicht gemessen. Die Heizlast ist ihm proportional; zwei Tankablesungen machen daraus eine Messung.")
        }
        if let kaeltester, kaeltester - a.normaussen > 10 {
            hinweise.append("Der kälteste erfasste Tag lag im Mittel bei \(zahl(kaeltester, 1)) °C, \(zahl(kaeltester - a.normaussen, 0)) K über der Normaußentemperatur. Die Gerade wird weit über das Gemessene hinaus verlängert; nach einer kalten Periode ist der Wert belastbarer.")
        }
        if let b = linie.bestimmtheit, b < 0.6 {
            hinweise.append("Die Verbrauchslinie erklärt nur \(Int((b * 100).rounded())) % der Unterschiede zwischen den Tagen.")
        }
        if stunden > 24 {
            hinweise.append("Am Auslegungstag bräuchte der Brenner \(zahl(stunden, 1)) Stunden, mehr als ein Tag hat. Entweder ist der Kessel knapp bemessen, oder die verlängerte Gerade überschätzt den Bedarf.")
        }
        return Heizlast(
            durchsatz: durchsatz, gemessen: gemessen != nil, kesselleistung: leistung,
            kwhJeHeizgradtag: jeGradtag, wattJeKelvin: jeGradtag / 24 * 1000,
            kilowatt: jeGradtag * gradtage / 24, warmwasserKWhJeTag: linie.grundlast * leistung,
            stundenAmAuslegungstag: stunden, kaeltesterTag: kaeltester, bestimmtheit: linie.bestimmtheit, hinweise: hinweise)
    }

    public struct Vorlaufbedarf: Sendable, Equatable {
        public var kreis: Int
        public var name: String
        /// Aus der Heizkurve verlängert; nil ohne Gerade
        public var beiNormaussen: Double?
        public var bestimmtheit: Double?
        /// Höchster Vorlauf in der kältesten Zeit des Zeitraums, als 95-%-Wert
        public var hoechsterBeiKaelte: Double?
        /// Obere Grenze dieser kältesten Zeit
        public var kaelteBis: Double?
        public var tiefsteAussentemperatur: Double?
        /// Warum `beiNormaussen` noch keine Bewertung trägt; nil, wenn der Wert belastbar ist
        public var vorbehalt: String?

        public var belastbar: Bool { beiNormaussen != nil && vorbehalt == nil }
    }

    public static func vorlauf(_ kreis: Heizkreis, auszug a: Verlaufsauszug, annahmen: Annahmen) -> Vorlaufbedarf? {
        let r = Verlaufsgliederung.heizkreis(kreis, in: a)
        guard let vl = r.vorlauf, let aussen = Verlaufsgliederung.aussen(in: a) else { return nil }
        let punkte = Heizkurve.punkte(a, wert: vl, aussen: aussen, pumpe: r.pumpe)
        guard !punkte.isEmpty else { return nil }
        let g = Heizkurve.gerade(punkte)
        // Die kälteste Zeit: das kälteste Zehntel der Plätze, mindestens alles bis 2 K über dem Tiefstwert
        let temps = punkte.map(\.aussen).sorted()
        let grenze = max(temps[min(temps.count - 1, temps.count / 10)], temps[0] + 2)
        let kalt = punkte.filter { $0.aussen <= grenze }.map(\.wert).sorted()
        let p95 = kalt.isEmpty ? nil : kalt[min(kalt.count - 1, Int(Double(kalt.count) * 0.95))]
        // Belastbar erst, wenn die Kurve den Vorlauf erklärt und nicht zu weit verlängert wird
        var vorbehalt: String?
        if let g {
            if g.bestimmtheit < 0.5 {
                vorbehalt = "Die Heizkurve erklärt nur \(Int((g.bestimmtheit * 100).rounded())) % der Unterschiede; der Vorlauf folgt vor allem etwas anderem als dem Wetter, etwa der Speichertemperatur."
            } else if temps[0] - annahmen.normaussen > 15 {
                vorbehalt = "Gemessen wurde nur bis \(zahl(temps[0], 1)) °C außen; bis zur Normaußentemperatur ist die Kurve zu weit verlängert."
            }
        }
        return Vorlaufbedarf(kreis: kreis.nummer, name: kreis.name, beiNormaussen: g?.wert(annahmen.normaussen),
                             bestimmtheit: g?.bestimmtheit, hoechsterBeiKaelte: p95, kaelteBis: grenze,
                             tiefsteAussentemperatur: temps.first, vorbehalt: vorbehalt)
    }

    /// Richtwerte: Je niedriger der Vorlauf, desto höher die Jahresarbeitszahl.
    public static func bewertung(_ vorlauf: Double) -> String {
        switch vorlauf {
        case ..<35.5: "sehr günstig für eine Wärmepumpe"
        case ..<45.5: "günstig"
        case ..<55.5: "möglich, mit geringerer Effizienz"
        default: "ungünstig; vorher Heizflächen prüfen"
        }
    }

    public struct Raumbefund: Sendable, Equatable, Identifiable {
        public var id: String { "\(verteiler)/\(nummer)" }
        public var verteiler: String
        public var nummer: Int
        public var name: String
        public var etage: String
        /// Anteil der kalten Stunden mit Heizbetrieb, in denen der Raum mehr als 0,5 K unter dem
        /// Sollwert lag, obwohl seine Ventile zu mindestens 90 % offen standen
        public var anteilUnterversorgt: Double
        public var mittlereStellung: Double
        public var mittlereAbweichung: Double
        public var stunden: Int
    }

    /// Räume in der kältesten Zeit des Zeitraums (Außen höchstens `kaelteBis`), sortiert nach
    /// dem Anteil unterversorgter Stunden, dann nach mittlerer Ventilstellung
    public static func raeume(_ bild: Anlagenbild, auszug a: Verlaufsauszug, kaelteBis: Double? = nil) -> [Raumbefund] {
        guard let aussen = Verlaufsgliederung.aussen(in: a) else { return [] }
        let temps = aussen.werte.compactMap { $0 }.sorted()
        guard !temps.isEmpty else { return [] }
        let grenze = kaelteBis ?? max(temps[min(temps.count - 1, temps.count / 4)], temps[0] + 2)
        var liste: [Raumbefund] = []
        for e in bild.etagen {
            for raum in e.raeume {
                guard let ist = a.reihe(e.id, Messgroesse.raum(raum.nummer, "ist")),
                      let soll = a.reihe(e.id, Messgroesse.raum(raum.nummer, "soll")) else { continue }
                let stellung = a.reihe(e.id, Messgroesse.raum(raum.nummer, "stellung"))
                var n = 0, knapp = 0
                var summeStellung = 0.0, nStellung = 0, summeAbweichung = 0.0
                for i in 0..<a.anzahl {
                    guard i < aussen.werte.count, let t = aussen.werte[i], t <= grenze,
                          i < ist.werte.count, let x = ist.werte[i], i < soll.werte.count, let s = soll.werte[i] else { continue }
                    n += 1
                    summeAbweichung += x - s
                    let st = stellung.flatMap { i < $0.werte.count ? $0.werte[i] : nil }
                    if let st { summeStellung += st; nStellung += 1 }
                    if x < s - 0.5, (st ?? 0) >= 0.9 { knapp += 1 }
                }
                guard n >= 12 else { continue }
                liste.append(Raumbefund(verteiler: e.id, nummer: raum.nummer, name: raum.name, etage: e.name,
                                        anteilUnterversorgt: Double(knapp) / Double(n),
                                        mittlereStellung: nStellung > 0 ? summeStellung / Double(nStellung) : 0,
                                        mittlereAbweichung: summeAbweichung / Double(n), stunden: n))
            }
        }
        return liste.sorted { ($0.anteilUnterversorgt, $0.mittlereStellung) > ($1.anteilUnterversorgt, $1.mittlereStellung) }
    }

    /// Brennerstunden zwischen zwei Zeitpunkten aus einem Auszug mit der Reihe `brenner`, samt
    /// dem Anteil der Zeit, für den Werte vorliegen
    // MARK: Heizlast aus der Speicherwärme

    /// Wärmekapazität von Wasser in kWh je Liter und Kelvin
    static let wasser = 0.001163

    /// Zeitraum zwischen zwei Ladungen: Was der Speicher darin abgibt, hat das Haus verbraucht.
    public struct Entladung: Sendable, Equatable {
        public var von: Date
        public var bis: Date
        public var kwh: Double
        /// Heizgradtage des Zeitraums, anteilig aus den Tagen
        public var heizgradtage: Double
        public var tage: Double { bis.timeIntervalSince(von) / 86_400 }
    }

    public struct Waermelast: Sendable, Equatable {
        public var entladungen: [Entladung]
        /// Nil, solange die Grundlage nicht reicht; dann nennt `grund` warum.
        public var kwhJeHeizgradtag: Double?
        /// Abgabe an Tagen ohne Heizbedarf: Warmwasser und Verluste von Speicher und Leitungen
        public var sockelKWhJeTag: Double?
        public var wattJeKelvin: Double?
        /// Heizlast bei der Normaußentemperatur, ohne Sockel
        public var kilowatt: Double?
        public var bestimmtheit: Double?
        /// Wärme in den Speicher je Brennerstunde, aus den Ladungen mit mindestens 20 Minuten Brenner
        public var ladeleistung: Double?
        /// Düsendurchsatz, der zur Ladeleistung passt
        public var durchsatz: Double?
        public var kaeltesterTag: Double?
        public var hinweise: [String]
        public var grund: String?
    }

    /// Zwischen zwei Ladungen müssen mindestens so viele Stunden liegen, sonst gehören sie
    /// zusammen -- etwa ein Kessel, der nach der Ladung mehrmals nachzündet.
    static let ladungsabstandStunden = 6.0

    /// Heizlast aus der Wärme, die der Speicher zwischen den Ladungen abgibt.
    ///
    /// Anders als die Verbrauchslinie braucht das weder Düsendurchsatz noch Wirkungsgrad: Gemessen
    /// wird die Wärme selbst, als Temperaturabfall des Speichers mal Inhalt. Jede Entladung --
    /// vom Ende einer Ladung bis zum Beginn der nächsten -- ergibt eine mittlere Leistung und
    /// erhält die Heizgradtage ihres Zeitraums. Eine Gerade durch diese Paare, gewichtet nach
    /// Dauer, liefert Wärme je Heizgradtag und den Sockel bei null Heizgradtagen.
    ///
    /// Tage mit unvollständiger Außentemperatur gehen nicht ein: Ihre Heizgradtage sind zu klein.
    public static func waermelast(
        ladungen: [Ladungssatz], tage: [Tagessatz], volumen: Double?, annahmen a: Annahmen,
        kalender: Calendar = .current
    ) -> Waermelast {
        var w = Waermelast(entladungen: [], hinweise: [])
        guard let volumen, volumen > 0 else {
            w.grund = "Der Inhalt des Speichers ist nicht bekannt; er steht in der Konfiguration des Heizungsgeräts."
            return w
        }
        let kapazitaet = volumen * wasser

        // Tage mit vollständiger Außentemperatur, nach Beginn. Der jüngste läuft noch.
        let juengster = tage.map(\.datum).max()
        var gradtage: [Date: Double] = [:]
        for t in tage where t.datum != juengster {
            guard let max = t.aussenMax, t.aussenMin != nil else { continue }
            if max < bezug, t.heizgradtage < 0.5 * (bezug - max) { continue }
            gradtage[kalender.startOfDay(for: t.datum)] = t.heizgradtage
        }

        // Ladungen, die kurz nacheinander folgen, gelten als eine.
        let sortiert = ladungen.sorted { $0.beginn < $1.beginn }
        var gruppen: [(vorher: Double, nachher: Double, beginn: Date, ende: Date)] = []
        for l in sortiert {
            let ende = l.beginn.addingTimeInterval(TimeInterval(l.dauer))
            if var g = gruppen.last, l.beginn.timeIntervalSince(g.ende) < ladungsabstandStunden * 3600 {
                g.nachher = l.speicherNachher
                g.ende = ende
                gruppen[gruppen.count - 1] = g
            } else {
                gruppen.append((l.speicherVorher, l.speicherNachher, l.beginn, ende))
            }
        }

        for (vorige, naechste) in zip(gruppen, gruppen.dropFirst()) {
            let von = vorige.ende, bis = naechste.beginn
            // Länger als vier Tage ohne Ladung: Vermutlich fehlt dazwischen eine im Protokoll,
            // und ihre Wärme fehlte der Rechnung.
            guard bis > von, bis.timeIntervalSince(von) <= 4 * 86_400,
                  let hgt = anteiligeGradtage(von: von, bis: bis, gradtage: gradtage, kalender: kalender)
            else { continue }
            let kwh = (vorige.nachher - naechste.vorher) * kapazitaet
            w.entladungen.append(Entladung(von: von, bis: bis, kwh: kwh, heizgradtage: hgt))
        }

        // Ladeleistung: nur Ladungen mit nennenswertem Brennerlauf, sonst wiegt die Trägheit
        // des Speicherfühlers zu viel.
        let lange = ladungen.filter { $0.brenner >= 1200 && $0.speicherNachher > $0.speicherVorher }
        let brennerstunden = Double(lange.map(\.brenner).reduce(0, +)) / 3600
        if brennerstunden > 0 {
            let kwh = lange.map { ($0.speicherNachher - $0.speicherVorher) * kapazitaet }.reduce(0, +)
            w.ladeleistung = kwh / brennerstunden
            w.durchsatz = kwh / brennerstunden / (a.heizwert * a.wirkungsgrad)
        }

        let summeTage = w.entladungen.map(\.tage).reduce(0, +)
        let raten = w.entladungen.map { $0.heizgradtage / $0.tage }
        let spreizung = (raten.max() ?? 0) - (raten.min() ?? 0)
        guard w.entladungen.count >= 8, summeTage >= 10 else {
            w.grund = "Erfasst sind \(w.entladungen.count) Entladungen über \(zahl(summeTage, 0)) Tage; nötig sind mindestens 8 über 10 Tage mit vollständiger Außentemperatur."
            return w
        }
        guard spreizung >= 3 else {
            w.grund = "Die Tage unterscheiden sich um höchstens \(zahl(spreizung, 1)) Heizgradtage; für eine Gerade sind mindestens 3 nötig. Nach einer kälteren Periode reicht es."
            return w
        }

        // Gewichtete Gerade: Leistung in kWh je Tag gegen Heizgradtage je Tag
        let g = w.entladungen.map(\.tage)
        let x = raten
        let y = w.entladungen.map { $0.kwh / $0.tage }
        let sg = g.reduce(0, +)
        let mx = zip(g, x).map(*).reduce(0, +) / sg
        let my = zip(g, y).map(*).reduce(0, +) / sg
        var sxx = 0.0, sxy = 0.0, syy = 0.0
        for i in g.indices {
            sxx += g[i] * (x[i] - mx) * (x[i] - mx)
            sxy += g[i] * (x[i] - mx) * (y[i] - my)
            syy += g[i] * (y[i] - my) * (y[i] - my)
        }
        let steigung = sxy / sxx
        guard steigung > 0 else {
            w.grund = "Die Wärmeabgabe steigt nicht mit der Kälte; der Heizbetrieb ist im erfassten Zeitraum noch nicht zu erkennen."
            return w
        }
        let sockel = my - steigung * mx
        w.kwhJeHeizgradtag = steigung
        w.sockelKWhJeTag = sockel
        w.wattJeKelvin = steigung / 24 * 1000
        w.kilowatt = steigung * max(0, bezug - a.normaussen) / 24
        w.bestimmtheit = syy > 0 ? sxy * sxy / (sxx * syy) : nil
        w.kaeltesterTag = x.max().map { bezug - $0 }

        if let k = w.kaeltesterTag, k - a.normaussen > 10 {
            w.hinweise.append("Die kälteste erfasste Entladung lag im Mittel bei \(zahl(k, 1)) °C, \(zahl(k - a.normaussen, 0)) K über der Normaußentemperatur. Die Gerade wird weit verlängert; nach einer kalten Periode ist der Wert belastbarer.")
        }
        if let b = w.bestimmtheit, b < 0.6 {
            w.hinweise.append("Die Gerade erklärt nur \(Int((b * 100).rounded())) % der Unterschiede zwischen den Entladungen.")
        }
        w.hinweise.append("Gemessen wird an einem einzigen Speicherfühler. Ist der Speicher geschichtet, folgt er der mittleren Temperatur nur ungefähr; der Wert kann deshalb um einige Prozent abweichen.")
        return w
    }

    /// Heizgradtage eines Zeitraums, anteilig aus den Tagen. Nil, wenn ein berührter Tag fehlt.
    static func anteiligeGradtage(von: Date, bis: Date, gradtage: [Date: Double], kalender: Calendar) -> Double? {
        var summe = 0.0
        var tag = kalender.startOfDay(for: von)
        while tag < bis {
            guard let naechster = kalender.date(byAdding: .day, value: 1, to: tag) else { return nil }
            guard let hgt = gradtage[tag] else { return nil }
            let anfang = max(von, tag), ende = min(bis, naechster)
            summe += hgt * ende.timeIntervalSince(anfang) / naechster.timeIntervalSince(tag)
            tag = naechster
        }
        return summe
    }

    public static func brennerstunden(_ a: Verlaufsauszug) -> (stunden: Double, abdeckung: Double) {
        guard let b = a.reihen.first(where: { $0.schluessel == "brenner" }), a.anzahl > 0 else { return (0, 0) }
        let werte = b.werte.compactMap { $0 }
        return (werte.reduce(0, +) * a.schritt / 3600, Double(werte.count) / Double(a.anzahl))
    }

    static func zahl(_ x: Double, _ stellen: Int) -> String {
        x.formatted(.number.precision(.fractionLength(stellen)).locale(Locale(identifier: "de_DE")))
    }
}
