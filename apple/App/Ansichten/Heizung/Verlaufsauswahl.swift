import SwiftUI
import Charts
import Verlauf

// Auswahl einer Zeit in den Verlaufstafeln. Tippen oder waagerecht Ziehen wählt die Zeit; das
// Schild mit den Werten steht neben dem Finger und folgt ihm. Die Linie erscheint in allen
// Tafeln der Seite, das Schild nur in der berührten. Tippen außerhalb der Tafeln hebt die
// Auswahl auf. Senkrechtes Ziehen bleibt der Seite: Es scrollt.

/// Gewählte Zeit, die Tafel, in der sie gewählt wurde, und wo der Finger zuletzt war
struct Verlaufsauswahl: Equatable {
    /// Beginn des Platzes im Raster des Auszugs
    var zeit: Date
    var tafel: UUID
    /// Höhe des Fingers in der Tafel
    var hoehe: CGFloat
    /// Wann zuletzt gesetzt; unterscheidet ein Tippen in eine Tafel von einem daneben
    var gesetzt = Date.now
}

extension View {
    /// Wählt mit Tippen und waagerechtem Ziehen eine Zeit und zeigt `zeilen` als Schild beim Finger.
    func verlaufsauswahl(_ auswahl: Binding<Verlaufsauswahl?>, auszug: Verlaufsauszug, tafel: UUID,
                         zeilen: @escaping (Int) -> [(String, String, Color)]) -> some View {
        modifier(Auswahlgeste(auswahl: auswahl, auszug: auszug, tafel: tafel, zeilen: zeilen))
    }

    /// Hebt die Auswahl auf, wenn außerhalb der Tafeln getippt wird.
    func verlaufsauswahlAufheben(_ auswahl: Binding<Verlaufsauswahl?>) -> some View {
        simultaneousGesture(TapGesture().onEnded {
            // Eine Tafel setzt die Auswahl im selben Tippen neu; dann bleibt sie.
            if let a = auswahl.wrappedValue, Date.now.timeIntervalSince(a.gesetzt) > 0.3 {
                auswahl.wrappedValue = nil
            }
        })
    }
}

private struct Auswahlgeste: ViewModifier {
    @Binding var auswahl: Verlaufsauswahl?
    let auszug: Verlaufsauszug
    let tafel: UUID
    let zeilen: (Int) -> [(String, String, Color)]
    @State private var schild: CGSize = .zero

    func body(content: Content) -> some View {
        content.chartOverlay { proxy in
            GeometryReader { geo in
                let flaeche = proxy.plotFrame.map { geo[$0] } ?? CGRect(origin: .zero, size: geo.size)
                ZStack(alignment: .topLeading) {
                    beruehrflaeche(proxy, flaeche)
                    if let a = auswahl, a.tafel == tafel, let (mitte, platz) = auszug.platz(a.zeit),
                       let x = proxy.position(forX: mitte) {
                        let z = zeilen(platz)
                        if !z.isEmpty {
                            Auswahlschild(zeit: a.zeit, zeilen: z)
                                .fixedSize()
                                .onGeometryChange(for: CGSize.self) { $0.size } action: { schild = $0 }
                                .offset(lage(linie: flaeche.minX + x, finger: a.hoehe, in: flaeche))
                                .allowsHitTesting(false)
                        }
                    }
                }
            }
        }
    }

    /// Neben der Linie auf der Seite mit mehr Platz, etwas über dem Finger, innerhalb der Tafel
    private func lage(linie: CGFloat, finger: CGFloat, in f: CGRect) -> CGSize {
        let abstand: CGFloat = 14
        let rechts = linie < f.midX
        var x = rechts ? linie + abstand : linie - abstand - schild.width
        x = min(max(x, f.minX), max(f.minX, f.maxX - schild.width))
        var y = finger - schild.height - 24
        y = min(max(y, f.minY), max(f.minY, f.maxY - schild.height))
        return CGSize(width: x, height: y)
    }

    @ViewBuilder
    private func beruehrflaeche(_ proxy: ChartProxy, _ f: CGRect) -> some View {
        let waehlen = { (p: CGPoint) in
            guard let zeit: Date = proxy.value(atX: p.x - f.minX), let (mitte, _) = auszug.platz(zeit) else { return }
            auswahl = Verlaufsauswahl(zeit: mitte.addingTimeInterval(-auszug.schritt / 2), tafel: tafel, hoehe: p.y)
        }
        let flaeche = Rectangle().fill(.clear).contentShape(.rect)
            .gesture(SpatialTapGesture().onEnded { waehlen($0.location) })
        #if os(iOS)
        flaeche.gesture(WaagerechtesZiehen(bewegt: waehlen))
        #else
        flaeche.gesture(DragGesture(minimumDistance: 2).onChanged { waehlen($0.location) })
        #endif
    }
}

#if os(iOS)
/// Ziehen, das nur waagerecht beginnt. Ein senkrechter Zug gehört der Scroll-Ansicht der Seite;
/// ein SwiftUI-`DragGesture` nähme ihn ihr weg, `chartXSelection` verlangt dort erst ein Halten.
private struct WaagerechtesZiehen: UIGestureRecognizerRepresentable {
    let bewegt: (CGPoint) -> Void

    func makeUIGestureRecognizer(context: Context) -> UIPanGestureRecognizer {
        let g = UIPanGestureRecognizer()
        g.delegate = context.coordinator
        g.maximumNumberOfTouches = 1
        return g
    }

    func makeCoordinator(converter: CoordinateSpaceConverter) -> Richtung { Richtung() }

    func handleUIGestureRecognizerAction(_ g: UIPanGestureRecognizer, context: Context) {
        switch g.state {
        case .began, .changed: bewegt(context.converter.localLocation)
        default: break
        }
    }

    final class Richtung: NSObject, UIGestureRecognizerDelegate {
        func gestureRecognizerShouldBegin(_ g: UIGestureRecognizer) -> Bool {
            guard let p = g as? UIPanGestureRecognizer else { return true }
            let v = p.velocity(in: p.view)
            return abs(v.x) > abs(v.y)
        }
    }
}
#endif
