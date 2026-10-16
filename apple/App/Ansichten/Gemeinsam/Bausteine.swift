import SwiftUI
import Anlage
import Assistent

// MARK: - Zustände

extension Lagebericht.Zustand {
    var farbe: Color {
        switch self {
        case .inOrdnung: Farbe.gut
        case .beobachten: Farbe.warnung
        case .handeln: Farbe.stoerung
        }
    }

    var symbol: String {
        switch self {
        case .inOrdnung: "checkmark"
        case .beobachten: "eye"
        case .handeln: "exclamationmark.triangle"
        }
    }
}

extension Befund.Schwere {
    var farbe: Color {
        switch self {
        case .hinweis: Farbe.gedaempft
        case .warnung: Farbe.warnung
        case .stoerung: Farbe.stoerung
        }
    }

    var symbol: String {
        switch self {
        case .hinweis: "info.circle"
        case .warnung: "exclamationmark.triangle"
        case .stoerung: "xmark.octagon"
        }
    }

    var bezeichnung: String {
        switch self {
        case .hinweis: "Hinweis"
        case .warnung: "Warnung"
        case .stoerung: "Störung"
        }
    }
}

struct SchwereSymbol: View {
    let schwere: Befund.Schwere

    var body: some View {
        Image(systemName: schwere.symbol)
            .fontWeight(.semibold)
            .foregroundStyle(schwere.farbe)
            .accessibilityLabel(schwere.bezeichnung)
    }
}

extension Raumzustand {
    var text: String {
        switch self {
        case .ausgeschaltet: "ausgeschaltet"
        case .keinThermometer: "kein Thermometer"
        case .messwertVeraltet: "Messwert veraltet"
        case .heizt: "heizt"
        case .sollErreicht: "Soll erreicht"
        }
    }

    var symbol: String {
        switch self {
        case .ausgeschaltet: "power"
        case .keinThermometer: "exclamationmark.triangle"
        case .messwertVeraltet: "clock.badge.exclamationmark"
        case .heizt: "flame"
        case .sollErreicht: "checkmark"
        }
    }

    var farbe: Color {
        switch self {
        case .ausgeschaltet: Farbe.gedaempft
        case .keinThermometer: Farbe.stoerung
        case .messwertVeraltet: Farbe.warnung
        case .heizt: Farbe.waerme
        case .sollErreicht: Farbe.gut
        }
    }
}

extension Raum {
    /// „0,5 K über Soll“, wie in der Weboberfläche.
    var abstandText: String {
        switch zustand {
        case .ausgeschaltet: return "ausgeschaltet"
        case .keinThermometer: return "ohne Thermometer"
        case .messwertVeraltet: return "Messwert veraltet"
        case .heizt, .sollErreicht:
            guard let ist else { return "" }
            let abstand = ist - soll
            if abs(abstand) < 0.05 { return "am Soll" }
            return "\(Format.zahl(abs(abstand))) K \(abstand > 0 ? "über" : "unter") Soll"
        }
    }

    var abstandFarbe: Color {
        guard let ist, betriebsart == .heizen else { return Farbe.gedaempft }
        return ist < soll - 0.05 ? Farbe.waerme : Farbe.gedaempft
    }
}

// MARK: - Ventilstellung

/// Stellung eines Ventils oder die Zielstellung eines Raums, 0 = zu, 1 = auf.
struct Stellungsbalken: View {
    let wert: Double
    var farbe: Color = Farbe.waerme
    var hoehe: CGFloat = 6

    var body: some View {
        Fortschritt(anteil: wert, farbe: farbe, hoehe: hoehe)
            .accessibilityLabel("Ventilstellung")
    }
}

// MARK: - Zeilen

/// Bezeichnung links, Wert rechts in Festbreitenschrift.
struct Kennwert: View {
    let titel: String
    let wert: String
    var symbol: String? = nil
    var farbe: Color? = nil

    var body: some View {
        LabeledContent {
            Text(wert)
                .font(Schrift.daten)
                .foregroundStyle(farbe ?? Farbe.gedaempft)
                .multilineTextAlignment(.trailing)
        } label: {
            if let symbol {
                Label(titel, systemImage: symbol)
            } else {
                Text(titel)
            }
        }
    }
}

struct Abschnittstitel: View {
    let titel: String
    var zusatz: String? = nil

    var body: some View {
        Abschnitt(titel: titel, zusatz: zusatz)
    }
}

/// Großer Messwert in Festbreitenschrift.
struct Messwert: View {
    let text: String
    var groesse: Font = Schrift.messwertGross

    var body: some View {
        Text(text)
            .font(groesse)
            .monospacedDigit()
            .contentTransition(.numericText())
    }
}

// MARK: - Umbrechende Reihe

/// Legt Marken nebeneinander und bricht in die nächste Zeile um, wenn der Platz nicht reicht.
/// Die Elemente einer Zeile stehen mittig zueinander. Ein Element, das allein breiter ist als die
/// Zeile, erhält die Zeilenbreite und bricht selbst um; so bleibt eine Gruppe, die ein inneres
/// Fliesslayout zusammenhält, beisammen und rückt als Ganzes in die nächste Zeile.
struct Fliesslayout: Layout {
    var abstand: CGFloat = 6
    /// Abstand zwischen den Zeilen; ohne Angabe derselbe wie zwischen den Elementen.
    var zeilenabstand: CGFloat? = nil

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        anordnung(subviews, breite: proposal.width ?? .infinity).groesse
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        // Dieselbe Breite wie beim Messen, nicht bounds.width: Sonst kann ein Rundungsfehler das
        // letzte Element einer genau gefüllten Zeile in die nächste schieben.
        let anordnung = anordnung(subviews, breite: proposal.width ?? .infinity)
        for (sub, platz) in zip(subviews, anordnung.plaetze) {
            sub.place(at: CGPoint(x: bounds.minX + platz.ort.x, y: bounds.minY + platz.ort.y), proposal: platz.vorschlag)
        }
    }

    private struct Platz {
        var ort: CGPoint
        var groesse: CGSize
        var vorschlag: ProposedViewSize
    }

    private func anordnung(_ subviews: Subviews, breite: CGFloat) -> (plaetze: [Platz], groesse: CGSize) {
        var plaetze: [Platz] = []
        var x: CGFloat = 0, y: CGFloat = 0, zeile: CGFloat = 0, rechts: CGFloat = 0
        var zeilenanfang = 0

        func zeileAbschliessen() {
            for i in zeilenanfang..<plaetze.count {
                plaetze[i].ort.y = y + (zeile - plaetze[i].groesse.height) / 2
            }
        }

        for sub in subviews {
            var vorschlag = ProposedViewSize(sub.sizeThatFits(.unspecified))
            if let ideal = vorschlag.width, ideal > breite {
                vorschlag = ProposedViewSize(width: breite, height: nil)
            }
            let groesse = sub.sizeThatFits(vorschlag)
            if x > 0, x + groesse.width > breite {
                zeileAbschliessen()
                x = 0
                y += zeile + (zeilenabstand ?? abstand)
                zeile = 0
                zeilenanfang = plaetze.count
            }
            plaetze.append(Platz(ort: CGPoint(x: x, y: y), groesse: groesse, vorschlag: vorschlag))
            rechts = max(rechts, x + groesse.width)
            x += groesse.width + abstand
            zeile = max(zeile, groesse.height)
        }
        zeileAbschliessen()
        return (plaetze, CGSize(width: min(rechts, breite), height: y + zeile))
    }
}
