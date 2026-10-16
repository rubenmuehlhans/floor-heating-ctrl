import SwiftUI

// Seiten aus Karten stehen in einer Spalte oder in zwei, je nach Platz. Die Regel knüpft an
// Größenklasse, Breite und Faltlinie an, nicht an das Gerät; iPad und Mac teilen in der Mitte.

// MARK: - Spaltenmaß

/// Wie eine Seite ihre Breite aufteilt. Zwei Spalten gibt es ab Größenklasse regular, wenn links
/// und rechts der Teilung je mindestens 350 pt bleiben, so breit wie das Außendisplay des
/// iPhone Duo. Die Teilung liegt auf der Faltlinie, ohne Faltlinie in der Mitte der Inhaltsfläche.
struct Spaltenmass: Equatable {
    /// Abstand der Spalten zum Rand der Inhaltsfläche und, ohne Faltlinie, zur Teilung
    static let abstand: CGFloat = 16
    static let mindestbreite: CGFloat = 350
    /// Höchstbreite beider Spalten zusammen, wie bisher die der einen Spalte
    static let hoechstbreite: CGFloat = 1100

    /// Senkrechte Faltlinie: Mitte und der Freiraum beiderseits, den das System für Bedienelemente
    /// frei hält
    struct Faltlinie: Equatable {
        var mitte: CGFloat
        var freiraum: CGFloat
    }

    var spalten = 1
    /// Breite der Inhaltsfläche
    var breite: CGFloat = 0
    /// Spaltengrenze, von der linken Kante der Inhaltsfläche aus gemessen
    var teilung: CGFloat = 0
    /// Abstand der Spalten zur Teilung
    var fuge: CGFloat = abstand
    /// Außenrand der beiden Spalten; ohne Faltlinie hält er sie in breiten Fenstern zusammen
    var rand: CGFloat = abstand

    /// Breite der linken Spalte
    var links: CGFloat { teilung - fuge - rand }
    /// Breite der rechten Spalte
    var rechts: CGFloat { breite - rand - teilung - fuge }

    init() {}

    init(breite: CGFloat, faltlinie: Faltlinie?, regular: Bool) {
        self.breite = breite
        if let faltlinie {
            teilung = faltlinie.mitte
            fuge = max(faltlinie.freiraum, Self.abstand)
        } else {
            let block = min(breite, Self.hoechstbreite)
            rand = (breite - block) / 2 + Self.abstand
            teilung = breite / 2
        }
        spalten = regular && links >= Self.mindestbreite && rechts >= Self.mindestbreite ? 2 : 1
    }
}

extension EnvironmentValues {
    @Entry var spaltenmass = Spaltenmass()
}

extension View {
    /// Ermittelt das Spaltenmaß aus der Fläche dieser Ansicht und gibt es an alle Seiten darin
    /// weiter; einmal je Bereich in `Hauptansicht`.
    func spaltenmass() -> some View {
        modifier(SpaltenmassErmitteln())
    }
}

private struct SpaltenmassErmitteln: ViewModifier {
    @Environment(\.horizontalSizeClass) private var klasse

    func body(content: Content) -> some View {
        // Ein GeometryReader statt onGeometryChange: Das Maß steht schon beim ersten Zeichnen
        // fest, die Seite springt nicht von einer Spalte in zwei. Er liegt in der sicheren
        // Fläche; `size` ist schon ohne Randspalte und Kamera, die `safeAreaInsets` nennen nur,
        // was außerhalb liegt.
        GeometryReader { geo in
            content.environment(\.spaltenmass, Spaltenmass(
                breite: geo.size.width, faltlinie: Self.faltlinie(geo), regular: klasse != .compact))
        }
    }

    /// Die senkrechte Faltlinie, auch im flachen Zustand, damit sich die Teilung beim Anwinkeln
    /// nicht verschiebt. Eine waagerechte Faltlinie, Innendisplay hochkant, teilt keine Spalten.
    private static func faltlinie(_ geo: GeometryProxy) -> Spaltenmass.Faltlinie? {
        #if os(iOS)
        if #available(iOS 27.1, *) {
            return geo.reservedRegions(kind: .division, options: .includeInactive)
                .first { $0.frame.height > $0.frame.width }
                .map { Spaltenmass.Faltlinie(mitte: $0.frame.midX, freiraum: max($0.margins.leading, $0.margins.trailing)) }
        }
        #endif
        return nil
    }
}

// MARK: - Doppelseite

/// Spalte eines Abschnitts in einer `Doppelseite`.
enum Spalte {
    case links, rechts
}

extension ContainerValues {
    @Entry var spalte = Spalte.links
}

extension View {
    /// Stellt den Abschnitt in einer Doppelseite in die angegebene Spalte; ohne Angabe links.
    func spalte(_ spalte: Spalte) -> some View {
        containerValue(\.spalte, spalte)
    }
}

/// Die Abschnitte einer Seite, einspaltig untereinander in der Reihenfolge, in der die Seite sie
/// aufzählt, zweispaltig links und rechts der Teilung mit je einer eigenen Scroll-Ansicht.
struct Doppelseite<Inhalt: View>: View {
    @Environment(\.spaltenmass) private var mass
    /// Höchstbreite der einen Spalte
    var breite: CGFloat = Spaltenmass.hoechstbreite
    @ViewBuilder var inhalt: Inhalt

    var body: some View {
        Group(subviews: inhalt) { abschnitte in
            if mass.spalten == 2 {
                HStack(spacing: 0) {
                    spalte(abschnitte.filter { $0.containerValues.spalte == .links },
                           innen: .trailing)
                        .frame(width: mass.teilung)
                    spalte(abschnitte.filter { $0.containerValues.spalte == .rechts },
                           innen: .leading)
                }
            } else {
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        abschnitte
                    }
                    .padding()
                    .frame(maxWidth: breite)
                    .frame(maxWidth: .infinity)
                }
            }
        }
    }

    /// Eine Spalte; `innen` ist die Seite zur Teilung hin.
    private func spalte(_ abschnitte: [Subview], innen: Edge.Set) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                ForEach(abschnitte) { $0 }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.vertical)
            .padding(innen, mass.fuge)
            .padding(innen == .leading ? .trailing : .leading, mass.rand)
        }
    }
}
