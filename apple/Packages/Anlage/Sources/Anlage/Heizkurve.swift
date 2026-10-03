import Foundation
import Verlauf

/// Die Reihen eines Heizkreises im Verlauf
public struct Heizkreisreihen: Sendable {
    public var vorlauf: Verlaufsauszug.Reihe?
    public var ruecklauf: Verlaufsauszug.Reihe?
    /// Anteil der Zeit, in der die Pumpe lief
    public var pumpe: Verlaufsauszug.Reihe?

    public var leer: Bool { vorlauf == nil && ruecklauf == nil }
}

extension Verlaufsgliederung {
    /// Vorlauf und Rücklauf nach den Rollen des Kreises, gleich welches Gerät den Fühler trägt;
    /// die Pumpe als `pumpe.<nummer>`.
    public static func heizkreis(_ kreis: Heizkreis, in a: Verlaufsauszug) -> Heizkreisreihen {
        Heizkreisreihen(
            vorlauf: a.reihen.first { $0.schluessel == Messgroesse.fuehler(kreis.vorlaufRolle) },
            ruecklauf: a.reihen.first { $0.schluessel == Messgroesse.fuehler(kreis.ruecklaufRolle) },
            pumpe: a.reihen.first { $0.schluessel == "pumpe.\(kreis.nummer)" })
    }

    /// Die Außentemperatur, vom Verteiler oder Leitstand (`aussen`) vor einem Fühler mit dieser Rolle
    public static func aussen(in a: Verlaufsauszug) -> Verlaufsauszug.Reihe? {
        a.reihen.first { $0.schluessel == "aussen" } ?? a.reihen.first { $0.schluessel == "fuehler.aussen" }
    }
}

/// Vorlauf über Außentemperatur: je Platz ein Punkt, solange die Pumpe läuft. Die Steigung der
/// Ausgleichsgeraden ist die wirksame Heizkurve, so wie sie sich einstellt, nicht wie sie
/// eingestellt ist.
public enum Heizkurve {
    public struct Punkt: Sendable, Equatable, Identifiable {
        public var id: Date { zeit }
        public var zeit: Date
        public var aussen: Double
        public var wert: Double
    }

    public struct Gerade: Sendable, Equatable {
        /// K Vorlauf je K Außentemperatur, bei einer Heizkurve negativ
        public var steigung: Double
        /// Vorlauf bei 0 °C außen
        public var beiNull: Double
        /// Bestimmtheitsmaß, 0 bis 1
        public var bestimmtheit: Double

        public func wert(_ aussen: Double) -> Double { beiNull + steigung * aussen }
    }

    /// Punkte, an denen Außentemperatur und Wert vorliegen und die Pumpe mindestens
    /// `pumpeAb` der Zeit lief. Ohne Pumpenreihe zählt jeder Platz.
    public static func punkte(_ a: Verlaufsauszug, wert: Verlaufsauszug.Reihe, aussen: Verlaufsauszug.Reihe,
                              pumpe: Verlaufsauszug.Reihe?, pumpeAb: Double = 0.5) -> [Punkt] {
        (0..<a.anzahl).compactMap { i in
            guard i < wert.werte.count, i < aussen.werte.count,
                  let w = wert.werte[i], let t = aussen.werte[i] else { return nil }
            if let pumpe, i < pumpe.werte.count, (pumpe.werte[i] ?? 0) < pumpeAb { return nil }
            return Punkt(zeit: a.beginn.addingTimeInterval((Double(i) + 0.5) * a.schritt), aussen: t, wert: w)
        }
    }

    /// Ausgleichsgerade nach der Methode der kleinsten Quadrate. Nil bei weniger als zwölf Punkten
    /// oder wenn die Außentemperatur um weniger als 2 K schwankt: Dann gibt es keine Steigung zu
    /// sehen.
    public static func gerade(_ p: [Punkt]) -> Gerade? {
        guard p.count >= 12 else { return nil }
        let n = Double(p.count)
        let mx = p.map(\.aussen).reduce(0, +) / n
        let my = p.map(\.wert).reduce(0, +) / n
        var sxy = 0.0, sxx = 0.0, syy = 0.0
        for q in p {
            sxy += (q.aussen - mx) * (q.wert - my)
            sxx += (q.aussen - mx) * (q.aussen - mx)
            syy += (q.wert - my) * (q.wert - my)
        }
        guard let lo = p.map(\.aussen).min(), let hi = p.map(\.aussen).max(), hi - lo >= 2, sxx > 0 else { return nil }
        let m = sxy / sxx
        let r2 = syy > 0 ? (sxy * sxy) / (sxx * syy) : 0
        return Gerade(steigung: m, beiNull: my - m * mx, bestimmtheit: r2)
    }
}
