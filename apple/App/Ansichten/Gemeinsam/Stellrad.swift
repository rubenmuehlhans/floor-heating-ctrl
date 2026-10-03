import SwiftUI

/// Sollwert einstellen wie an einem Stellrad: Die Skala wird waagerecht verschoben und rastet
/// in Schritten ein; die Nadel in der Mitte zeigt den Wert.
///
/// Zum Sollwert wird nur, was die Hand am Rad bewegt. Die Scroll-Ansicht verschiebt sich auch
/// beim Aufbau der Seite und beim Nachführen auf einen neuen Wert; diese Bewegungen schreiben
/// nichts, sonst ginge ein Wert an den Verteiler, den niemand eingestellt hat.
///
/// Auf dem Mac verschiebt eine Scroll-Ansicht nur das Trackpad. Dort kommen Ziehen mit der Maus,
/// Klick auf einen Wert und die Pfeiltasten hinzu.
struct Stellrad: View {
    @Binding var wert: Double
    var bereich: ClosedRange<Double> = 5...35
    var schritt: Double = 0.5
    /// Abstand zweier Striche.
    var teilung: CGFloat = 14
    var hoehe: CGFloat = 62
    /// Farbe hinter dem Rad; an den Rändern blendet die Skala in sie über. Eine Maske leistete
    /// dasselbe, braucht beim Scrollen der Seite aber in jedem Bild einen eigenen Grafikdurchgang.
    var hintergrund: Color = Farbe.flaeche
    var gedimmt = false

    @State private var position = ScrollPosition(idType: Int.self)
    @State private var breite: CGFloat = 0
    /// Die Hand dreht gerade am Rad.
    @State private var bedient = false
    #if os(macOS)
    @State private var ziehBeginn: Int?
    @FocusState private var fokussiert: Bool
    #endif

    private var anzahl: Int {
        Int(((bereich.upperBound - bereich.lowerBound) / schritt).rounded()) + 1
    }

    private func index(_ w: Double) -> Int {
        min(max(Int(((w - bereich.lowerBound) / schritt).rounded()), 0), anzahl - 1)
    }

    private func wert(bei i: Int) -> Double {
        bereich.lowerBound + Double(i) * schritt
    }

    /// Der Strich unter der Nadel
    private var angezeigt: Int? {
        position.viewID(type: Int.self)
    }

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            LazyHStack(spacing: 0) {
                ForEach(0..<anzahl, id: \.self) { i in
                    strich(i)
                        .frame(width: teilung, height: hoehe, alignment: .top)
                }
            }
            .scrollTargetLayout()
            .opacity(gedimmt ? 0.45 : 1)
        }
        #if os(macOS)
        .gesture(ziehen)
        #endif
        .contentMargins(.horizontal, max(0, breite / 2 - teilung / 2), for: .scrollContent)
        .scrollTargetBehavior(.viewAligned)
        .scrollPosition($position, anchor: .center)
        // Die Ränder hängen an der Breite. Erst wenn sie feststeht, trifft die Nadel den Wert;
        // vorher angesteuert, lag sie um einen Rand daneben, auf dem iPhone sechs Grad zu tief.
        .onGeometryChange(for: CGFloat.self, of: { $0.size.width }) { neu in
            breite = neu
            position.scrollTo(id: index(wert), anchor: .center)
        }
        .onScrollPhaseChange { _, phase in
            switch phase {
            case .interacting:
                bedient = true
            case .idle:
                if bedient, let i = angezeigt { uebernehmen(i) }
                bedient = false
            default:
                break
            }
        }
        .onChange(of: angezeigt) { _, i in
            if bedient, let i { uebernehmen(i) }
        }
        .onChange(of: wert) { _, neu in
            let i = index(neu)
            if !bedient, angezeigt != i {
                withAnimation(.snappy) { position.scrollTo(id: i, anchor: .center) }
            }
        }
        .overlay {
            LinearGradient(
                stops: [.init(color: hintergrund, location: 0), .init(color: hintergrund.opacity(0), location: 0.18),
                        .init(color: hintergrund.opacity(0), location: 0.82), .init(color: hintergrund, location: 1)],
                startPoint: .leading, endPoint: .trailing
            )
            .allowsHitTesting(false)
        }
        .overlay(alignment: .top) {
            Capsule()
                .fill(Color.accentColor)
                .frame(width: 3, height: 34)
                .opacity(gedimmt ? 0.45 : 1)
                .allowsHitTesting(false)
        }
        .frame(height: hoehe)
        .sensoryFeedback(.selection, trigger: angezeigt) { _, _ in bedient }
        #if os(macOS)
        // Wie ein Schieberegler: erreichbar über die Tastaturnavigation, nicht per Klick. Den
        // Systemrahmen setzt AppKit bei einer Scroll-Ansicht falsch, daher eine eigene Markierung.
        .focusable(interactions: .activate)
        .focusEffectDisabled()
        .focused($fokussiert)
        .background {
            RoundedRectangle(cornerRadius: 8)
                .strokeBorder(Color(nsColor: .keyboardFocusIndicatorColor), lineWidth: 3)
                .opacity(fokussiert ? 1 : 0)
        }
        .onMoveCommand { richtung in
            switch richtung {
            case .left, .down: rasten(um: -1)
            case .right, .up: rasten(um: 1)
            @unknown default: break
            }
        }
        #endif
        .accessibilityElement()
        .accessibilityLabel("Sollwert")
        .accessibilityValue(Format.temperatur(wert))
        .accessibilityAdjustableAction { richtung in
            switch richtung {
            case .increment: wert = min(bereich.upperBound, wert + schritt)
            case .decrement: wert = max(bereich.lowerBound, wert - schritt)
            @unknown default: break
            }
        }
    }

    private func uebernehmen(_ i: Int) {
        let neu = wert(bei: i)
        if abs(neu - wert) > schritt / 4 { wert = neu }
    }

    private func strich(_ i: Int) -> some View {
        let w = wert(bei: i)
        let ganz = abs(w.rounded() - w) < 0.001
        return VStack(spacing: 6) {
            Capsule()
                .fill(ganz ? Farbe.gedaempft : Farbe.linieStark)
                .frame(width: ganz ? 2 : 1.5, height: ganz ? 26 : 14)
            if ganz {
                Text("\(Int(w))")
                    .font(Schrift.datenKlein)
                    .foregroundStyle(Farbe.gedaempft)
                    .fixedSize()
            }
        }
    }

    #if os(macOS)
    /// Die Skala folgt dem Mauszeiger wie beim Wischen auf dem iPhone: nach links ziehen erhöht.
    /// Ein Klick ohne Bewegung springt auf den angeklickten Wert. Tipp-Gesten auf den einzelnen
    /// Strichen erreicht der Klick in der Scroll-Ansicht des Mac nicht, daher rechnet die Geste
    /// den Wert aus dem Abstand zur Nadel.
    private var ziehen: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { zug in
                let beginn = ziehBeginn ?? angezeigt ?? index(wert)
                ziehBeginn = beginn
                let neu = begrenzt(beginn - Int((zug.translation.width / teilung).rounded()))
                if neu != angezeigt { einstellen(neu, animation: .linear(duration: 0.06)) }
            }
            .onEnded { zug in
                defer { ziehBeginn = nil }
                guard abs(zug.translation.width) < 3, abs(zug.translation.height) < 3 else { return }
                let beginn = ziehBeginn ?? angezeigt ?? index(wert)
                einstellen(begrenzt(beginn + Int(((zug.startLocation.x - breite / 2) / teilung).rounded())))
            }
    }

    private func rasten(um schritte: Int) {
        einstellen(begrenzt((angezeigt ?? index(wert)) + schritte))
    }

    /// Maus und Tastatur bewegen das Rad selbst; der Wert folgt unmittelbar.
    private func einstellen(_ i: Int, animation: Animation = .snappy) {
        uebernehmen(i)
        withAnimation(animation) { position.scrollTo(id: i, anchor: .center) }
    }

    private func begrenzt(_ i: Int) -> Int {
        min(max(i, 0), anzahl - 1)
    }
    #endif
}

/// Sollwert groß mit Stellrad darunter.
struct SollwertStellrad: View {
    @Binding var wert: Double
    var hinweis: String? = nil
    /// Für einen ausgeschalteten Raum
    var gedimmt = false

    var body: some View {
        VStack(spacing: 10) {
            VStack(spacing: 2) {
                Messzahl(zahl: Format.zahl(wert), einheit: "°C", font: Schrift.messwert, farbe: Color.accentColor)
                Kicker("Sollwert")
            }
            .opacity(gedimmt ? 0.45 : 1)
            Stellrad(wert: $wert, gedimmt: gedimmt)
            if let hinweis {
                Text(hinweis)
                    .font(Schrift.datenKlein)
                    .foregroundStyle(Farbe.gedaempft)
            }
        }
    }
}
