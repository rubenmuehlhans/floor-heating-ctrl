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
    /// Innendisplay hochkant und angewinkelt: oben Anzeige, unten Bedienung
    var aufgestellt = false
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

    init(breite: CGFloat, faltlinie: Faltlinie?, regular: Bool, aufgestellt: Bool = false) {
        self.breite = breite
        self.aufgestellt = aufgestellt
        if let faltlinie {
            teilung = faltlinie.mitte
            fuge = max(faltlinie.freiraum, Self.abstand)
        } else {
            let block = min(breite, Self.hoechstbreite)
            rand = (breite - block) / 2 + Self.abstand
            teilung = breite / 2
        }
        spalten = !aufgestellt && regular && links >= Self.mindestbreite && rechts >= Self.mindestbreite ? 2 : 1
    }
}

extension EnvironmentValues {
    @Entry var spaltenmass = Spaltenmass()
}

extension View {
    /// Ermittelt das Spaltenmaß aus der Fläche dieser Ansicht und gibt es an alle Seiten darin
    /// weiter; einmal je Bereich in `Hauptansicht`.
    func spaltenmass(_ bereich: AppModell.Bereich) -> some View {
        modifier(SpaltenmassErmitteln(bereich: bereich))
    }
}

private struct SpaltenmassErmitteln: ViewModifier {
    @Environment(AppModell.self) private var modell
    @Environment(\.horizontalSizeClass) private var klasse
    let bereich: AppModell.Bereich

    func body(content: Content) -> some View {
        // Ein GeometryReader statt onGeometryChange: Das Maß steht schon beim ersten Zeichnen
        // fest, die Seite springt nicht von einer Spalte in zwei. Er liegt in der sicheren
        // Fläche; `size` ist schon ohne Randspalte und Kamera, die `safeAreaInsets` nennen nur,
        // was außerhalb liegt.
        GeometryReader { geo in
            let mass = Spaltenmass(breite: geo.size.width, faltlinie: Self.faltlinie(geo), regular: klasse != .compact,
                                   aufgestellt: Aufstellung<EmptyView, EmptyView>.falz(geo) != nil)
            content
                .environment(\.spaltenmass, mass)
                // Der Stapel teilt sich danach in Seite und rechte Spalte
                .onChange(of: mass.spalten, initial: true) { _, spalten in
                    modell.zweispaltig[bereich] = spalten == 2
                }
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

/// Hälfte eines Abschnitts, wenn das Gerät aufgestellt ist: oben Anzeige, unten Bedienung.
enum Haelfte {
    case oben, unten
}

extension ContainerValues {
    @Entry var spalte = Spalte.links
    @Entry var haelfte = Haelfte.oben
}

extension View {
    /// Stellt den Abschnitt in einer Doppelseite in die angegebene Spalte; ohne Angabe links.
    func spalte(_ spalte: Spalte) -> some View {
        containerValue(\.spalte, spalte)
    }

    /// Stellt den Abschnitt aufgestellt in die angegebene Hälfte; ohne Angabe oben.
    func haelfte(_ haelfte: Haelfte) -> some View {
        containerValue(\.haelfte, haelfte)
    }
}

/// Die Abschnitte einer Seite, einspaltig untereinander in der Reihenfolge, in der die Seite sie
/// aufzählt, zweispaltig links und rechts der Teilung mit je einer eigenen Scroll-Ansicht.
struct Doppelseite<Inhalt: View>: View {
    @Environment(\.spaltenmass) private var mass
    @Environment(\.nebenspalte) private var nebenspalte
    /// Höchstbreite der einen Spalte
    var breite: CGFloat = Spaltenmass.hoechstbreite
    /// Aufgestellt oben Anzeige, unten Bedienung; sonst bleibt die Seite einspaltig
    var aufstellbar = false
    @ViewBuilder var inhalt: Inhalt

    var body: some View {
        Group(subviews: inhalt) { abschnitte in
            if aufstellbar && mass.aufgestellt {
                Aufstellung {
                    ForEach(abschnitte.filter { $0.containerValues.haelfte == .oben }) { $0 }
                } unten: {
                    ForEach(abschnitte.filter { $0.containerValues.haelfte == .unten }) { $0 }
                }
            } else if mass.spalten == 2 {
                HStack(spacing: 0) {
                    spalte(abschnitte.filter { $0.containerValues.spalte == .links },
                           innen: .trailing)
                        .environment(\.zielspalte, .links)
                        .environment(\.angezeigtesZiel, nebenspalte?.first?.ziel)
                        .frame(width: mass.teilung)
                    // Was aus der Seite geöffnet wurde, steht an Stelle ihrer rechten Abschnitte
                    if let oben = nebenspalte?.last {
                        Nebenseite(ziel: oben.ziel)
                            .padding(.leading, mass.fuge)
                            .padding(.trailing, max(mass.rand - 16, 0))
                            .frame(maxWidth: .infinity)
                    } else {
                        spalte(abschnitte.filter { $0.containerValues.spalte == .rechts },
                               innen: .leading)
                            .environment(\.zielspalte, .rechts)
                    }
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

// MARK: - Aufgestellt

/// Innendisplay hochkant, angewinkelt auf dem Tisch: oben, was aus Abstand zu lesen ist, unten
/// die Bedienung auf der aufliegenden Hälfte. Beide scrollen für sich; zwischen ihnen bleibt die
/// Faltlinie frei.
struct Aufstellung<Oben: View, Unten: View>: View {
    /// Abschnitte in je eine Scroll-Ansicht legen; aus, wenn die Hälften selbst scrollen, etwa als Form
    var scrollen = true
    @ViewBuilder var oben: Oben
    @ViewBuilder var unten: Unten

    var body: some View {
        GeometryReader { geo in
            let falz = Self.falz(geo) ?? (geo.size.height / 2 ... geo.size.height / 2)
            VStack(spacing: 0) {
                haelfte { oben }
                    .frame(height: max(falz.lowerBound, 0))
                Color.clear.frame(height: falz.upperBound - falz.lowerBound)
                haelfte { unten }
                    .frame(maxHeight: .infinity)
            }
        }
        .background(Farbe.grund)
    }

    @ViewBuilder
    private func haelfte<V: View>(@ViewBuilder _ inhalt: () -> V) -> some View {
        if scrollen {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) { inhalt() }
                    .padding()
            }
        } else {
            inhalt()
        }
    }

    /// Senkrechter Bereich der aktiven waagerechten Faltlinie samt Freiraum, in eigenen Koordinaten;
    /// `nil`, wenn das Gerät nicht aufgestellt ist.
    static func falz(_ geo: GeometryProxy) -> ClosedRange<CGFloat>? {
        #if os(iOS)
        if #available(iOS 27.1, *) {
            return geo.reservedRegions(kind: .division)
                .first { $0.isActive && $0.frame.width > $0.frame.height }
                .map { (max($0.frame.minY - $0.margins.top, 0)) ... ($0.frame.maxY + $0.margins.bottom) }
        }
        #endif
        return nil
    }
}
