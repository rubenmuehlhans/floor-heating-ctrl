import Foundation
import Anlage
import Verlauf

/// Kennzahlen der Anlage über einen Zeitraum: Brennerlauf und Verbrauch aus dem Tagesprotokoll,
/// Ladungen aus dem Ladungsprotokoll, der Abgas-Vorlauf-Abstand des Kessels und die
/// Abweichung der Räume vom Sollwert aus dem gespeicherten Verlauf.
///
/// Sie dienen der KI als Beleg und der Wirkungskontrolle eines übernommenen Vorschlags: dieselben
/// Zahlen vor und nach der Änderung.
public struct Kennzahlen: Sendable, Hashable, Codable {
    public struct Raumkennzahl: Sendable, Hashable, Codable {
        public var raum: String
        public var name: String
        public var etage: String
        /// Mittlere Abweichung Ist − Soll in Kelvin, solange geregelt wurde
        public var mittlereAbweichungK: Double?
        /// Anteil der Zeit mehr als 0,5 K unter dem Sollwert
        public var anteilZuKalt: Double?
        /// Anteil der Zeit mehr als 1 K über dem Sollwert
        public var anteilZuWarm: Double?
        /// Mittlere Ventilstellung, 0 zu, 1 offen
        public var mittlereStellung: Double?
        /// Zahl der Raster, in die die Werte eingingen, je fünf Minuten
        public var messpunkte: Int
    }

    public var von: Date
    public var bis: Date
    /// Tage im Tagesprotokoll innerhalb des Zeitraums
    public var tage: Int
    public var brennerstartsJeTag: Double?
    public var laufzeitJeTagStunden: Double?
    public var laufzeitJeStartMinuten: Double?
    public var literJeTag: Double?
    public var heizgradtage: Double?
    /// Aus der Verbrauchslinie des Heizungsgeräts, sofern es sie schon bilden kann
    public var stundenJeHeizgradtag: Double?
    public var grundlastStundenJeTag: Double?
    public var ladungen: Int
    public var ladungsdauerMinuten: Double?
    public var ladungsendeC: Double?
    public var abgasAbstandK: Double?
    public var abgasAbstandAenderungK: Double?
    public var taktbetrieb: Bool?
    public var raeume: [Raumkennzahl]

    /// Die Wirkungskontrolle vergleicht diese Werte; Räume nach ihrer Kennung.
    public var vergleichswerte: [String: Double] {
        var w: [String: Double] = [:]
        w["Brennerstarts je Tag"] = brennerstartsJeTag
        w["Laufzeit je Tag (h)"] = laufzeitJeTagStunden
        w["Laufzeit je Start (min)"] = laufzeitJeStartMinuten
        w["Öl je Tag (l)"] = literJeTag
        w["Ladungsende (°C)"] = ladungsendeC
        w["Abgas-Vorlauf-Abstand (K)"] = abgasAbstandK
        for r in raeume {
            w["\(r.name) (\(r.etage)): Abweichung (K)"] = r.mittlereAbweichungK
            w["\(r.name) (\(r.etage)): zu kalt (Anteil)"] = r.anteilZuKalt
        }
        return w
    }
}

extension Kennzahlen {
    /// Berechnet die Kennzahlen aus dem Bild der Anlage und einem Verlaufsauszug über denselben
    /// Zeitraum. Ohne Auszug fehlen die Raumwerte.
    public static func berechnen(bild: Anlagenbild, auszug: Verlaufsauszug?, von: Date, bis: Date) -> Kennzahlen {
        let tage = bild.tage.filter { $0.datum >= Calendar.current.startOfDay(for: von) && $0.datum < bis }
        let starts = tage.map(\.starts).reduce(0, +)
        let laufzeit = tage.map(\.laufzeit).reduce(0, +)
        let n = Double(tage.count)
        let ladungen = bild.ladungen.filter { $0.beginn >= von && $0.beginn < bis }
        let linie = bild.verbrauchslinie.flatMap { $0.gueltig ? $0 : nil }

        return Kennzahlen(
            von: von, bis: bis, tage: tage.count,
            brennerstartsJeTag: tage.isEmpty ? nil : gerundet(Double(starts) / n, 1),
            laufzeitJeTagStunden: tage.isEmpty ? nil : gerundet(Double(laufzeit) / 3600 / n, 2),
            laufzeitJeStartMinuten: starts > 0 ? gerundet(Double(laufzeit) / Double(starts) / 60, 1) : nil,
            literJeTag: tage.isEmpty ? nil : gerundet(tage.map(\.liter).reduce(0, +) / n, 2),
            heizgradtage: tage.isEmpty ? nil : gerundet(tage.map(\.heizgradtage).reduce(0, +), 1),
            stundenJeHeizgradtag: linie.map { gerundet($0.steigung, 3) },
            grundlastStundenJeTag: linie.map { gerundet($0.grundlast, 2) },
            ladungen: ladungen.count,
            ladungsdauerMinuten: ladungen.isEmpty ? nil : gerundet(Double(ladungen.map(\.dauer).reduce(0, +)) / Double(ladungen.count) / 60, 0),
            ladungsendeC: ladungen.isEmpty ? nil : gerundet(ladungen.map(\.speicherNachher).reduce(0, +) / Double(ladungen.count), 1),
            abgasAbstandK: bild.kessel?.abgasAbstand.jetzt.map { gerundet($0, 1) },
            abgasAbstandAenderungK: bild.kessel?.abgasAbstand.veraenderung.map { gerundet($0, 1) },
            taktbetrieb: bild.kessel?.taktbetrieb,
            raeume: auszug.map { raumkennzahlen(bild, $0) } ?? [])
    }

    static func raumkennzahlen(_ bild: Anlagenbild, _ auszug: Verlaufsauszug) -> [Raumkennzahl] {
        bild.etagen.flatMap { etage in
            etage.raeume.map { raum -> Raumkennzahl in
                let ist = auszug.reihe(etage.id, Messgroesse.raum(raum.nummer, "ist"))?.werte ?? []
                let soll = auszug.reihe(etage.id, Messgroesse.raum(raum.nummer, "soll"))?.werte ?? []
                let stellung = auszug.reihe(etage.id, Messgroesse.raum(raum.nummer, "stellung"))?.werte ?? []
                var abweichungen: [Double] = []
                for (i, wert) in ist.enumerated() {
                    guard let wert, i < soll.count, let s = soll[i] else { continue }
                    abweichungen.append(wert - s)
                }
                let stellungen = stellung.compactMap { $0 }
                let anzahl = Double(abweichungen.count)
                return Raumkennzahl(
                    raum: raum.id, name: raum.name, etage: etage.name,
                    mittlereAbweichungK: abweichungen.isEmpty ? nil : gerundet(abweichungen.reduce(0, +) / anzahl, 2),
                    anteilZuKalt: abweichungen.isEmpty ? nil : gerundet(Double(abweichungen.filter { $0 < -0.5 }.count) / anzahl, 2),
                    anteilZuWarm: abweichungen.isEmpty ? nil : gerundet(Double(abweichungen.filter { $0 > 1 }.count) / anzahl, 2),
                    mittlereStellung: stellungen.isEmpty ? nil : gerundet(stellungen.reduce(0, +) / Double(stellungen.count), 2),
                    messpunkte: abweichungen.count)
            }
        }
    }

    static func gerundet(_ x: Double, _ stellen: Int) -> Double {
        let f = pow(10, Double(stellen))
        return (x * f).rounded() / f
    }
}
