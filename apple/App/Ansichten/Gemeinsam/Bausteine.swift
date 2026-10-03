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
struct Fliesslayout: Layout {
    var abstand: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let breite = proposal.width ?? .infinity
        var x: CGFloat = 0, y: CGFloat = 0, zeile: CGFloat = 0, rechts: CGFloat = 0
        for sub in subviews {
            let groesse = sub.sizeThatFits(.unspecified)
            if x > 0, x + groesse.width > breite {
                x = 0
                y += zeile + abstand
                zeile = 0
            }
            x += groesse.width + abstand
            zeile = max(zeile, groesse.height)
            rechts = max(rechts, x - abstand)
        }
        return CGSize(width: min(rechts, breite), height: y + zeile)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX, y = bounds.minY, zeile: CGFloat = 0
        for sub in subviews {
            let groesse = sub.sizeThatFits(.unspecified)
            if x > bounds.minX, x + groesse.width > bounds.maxX {
                x = bounds.minX
                y += zeile + abstand
                zeile = 0
            }
            sub.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(groesse))
            x += groesse.width + abstand
            zeile = max(zeile, groesse.height)
        }
    }
}
