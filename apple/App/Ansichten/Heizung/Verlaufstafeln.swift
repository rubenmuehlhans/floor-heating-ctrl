import SwiftUI
import Charts
import Anlage
import Verlauf

/// Eine Linie einer Tafel
struct Tafelreihe: Identifiable {
    let reihe: Verlaufsauszug.Reihe
    let name: String
    let farbe: Color
    var gestrichelt = false
    var id: String { reihe.id }
}

/// Die Außentemperatur auf fester rechter Achse. Fest statt nach den Werten, damit der
/// Zusammenhang mit dem Vorlauf von Tag zu Tag gleich aussieht und nicht vom Maßstab abhängt.
enum Aussenachse {
    static let bereich: ClosedRange<Double> = -10...20
    static let marken: [Double] = [-10, 0, 10, 20]

    /// Außentemperatur in Werte der linken Achse; außerhalb des Bereichs am Rand
    static func abbilden(_ t: Double, auf y: ClosedRange<Double>) -> Double {
        let t = min(max(t, bereich.lowerBound), bereich.upperBound)
        return y.lowerBound + (t - bereich.lowerBound) / (bereich.upperBound - bereich.lowerBound) * (y.upperBound - y.lowerBound)
    }

    static func zurueck(_ y: Double, von bereichY: ClosedRange<Double>) -> Double {
        bereich.lowerBound + (y - bereichY.lowerBound) / (bereichY.upperBound - bereichY.lowerBound) * (bereich.upperBound - bereich.lowerBound)
    }
}

/// Wertebereich einer Tafel: Tiefst- und Höchstwert mit Rand, auf 5 K gerundet, mindestens
/// `mindestens` K breit, damit Rauschen nicht zur Welle wird.
func tafelbereich(_ reihen: [Verlaufsauszug.Reihe], mindestens: Double = 10) -> ClosedRange<Double> {
    let w = reihen.flatMap { $0.werte.compactMap { $0 } }
    guard let lo = w.min(), let hi = w.max() else { return 0...50 }
    var a = (lo - 1) / 5, b = (hi + 1) / 5
    a.round(.down)
    b.round(.up)
    var unten = a * 5, oben = b * 5
    if oben - unten < mindestens {
        let mitte = (unten + oben) / 2
        unten = ((mitte - mindestens / 2) / 5).rounded(.down) * 5
        oben = unten + (mindestens / 5).rounded(.up) * 5
    }
    return unten...oben
}

/// Temperaturen einer Gruppe mit eigener Skala. Alle Tafeln einer Ansicht teilen die Auswahl:
/// Wer in einer Tafel tippt oder zieht, sieht dieselbe Zeit in allen.
struct Temperaturtafel: View {
    let auszug: Verlaufsauszug
    let zeitraum: Verlaufszeitraum
    let reihen: [Tafelreihe]
    let bereich: ClosedRange<Double>
    var hinterlegt: [Verlaufsfarben.Lauf] = []
    var hinterlegtFarbe: Color = Farbe.waerme.opacity(0.12)
    var hinterlegtName: String?
    /// Fläche zwischen zwei Reihen, etwa die Spreizung zwischen Vor- und Rücklauf
    var band: (oben: Verlaufsauszug.Reihe, unten: Verlaufsauszug.Reihe)?
    /// Außentemperatur, zuschaltbar über `aussenAn`
    var aussen: Verlaufsauszug.Reihe?
    var aussenAn: Binding<Bool>?
    var ereignisse: [Ereignis] = []
    @Binding var auswahl: Date?
    var hoehe: CGFloat = 180

    @State private var ausgeblendet: Set<String> = []

    private var aussenSichtbar: Bool { aussen != nil && (aussenAn?.wrappedValue ?? false) }

    var body: some View {
        let sichtbar = reihen.filter { !ausgeblendet.contains($0.id) }
        let geraete = Set(reihen.map(\.reihe.geraet))
        VStack(alignment: .leading, spacing: 8) {
            Chart {
                ForEach(Array(hinterlegt.enumerated()), id: \.offset) { _, lauf in
                    RectangleMark(xStart: .value("Beginn", lauf.beginn), xEnd: .value("Ende", lauf.ende))
                        .foregroundStyle(hinterlegtFarbe)
                }
                if let band, !ausgeblendet.contains(band.oben.id), !ausgeblendet.contains(band.unten.id) {
                    ForEach(bandplaetze(band), id: \.zeit) { p in
                        RectangleMark(xStart: .value("Beginn", p.zeit), xEnd: .value("Ende", p.zeit.addingTimeInterval(auszug.schritt)),
                                      yStart: .value("Temperatur", p.unten), yEnd: .value("Temperatur", p.oben))
                            .foregroundStyle(Farbe.waerme.opacity(0.10))
                    }
                }
                Ereignismarken(ereignisse: ereignisse.filter { geraete.contains($0.geraet) }, bis: auszug.ende)
                ForEach(sichtbar) { t in
                    ForEach(Array(auszug.abschnitte(t.reihe).enumerated()), id: \.offset) { i, stueck in
                        Linienstueck(stueck: stueck, serie: "\(t.id)#\(i)", groesse: "Temperatur", farbe: t.farbe, gestrichelt: t.gestrichelt)
                    }
                }
                if aussenSichtbar, let aussen {
                    ForEach(Array(auszug.abschnitte(aussen).enumerated()), id: \.offset) { i, stueck in
                        Linienstueck(stueck: stueck.map { .init(zeit: $0.zeit, wert: Aussenachse.abbilden($0.wert, auf: bereich)) },
                                     serie: "aussen#\(i)", groesse: "Temperatur", farbe: Farbe.gut)
                    }
                }
                if let (zeit, platz) = auszug.platz(auswahl) {
                    Auswahlmarke(zeit: zeit, schritt: auszug.schritt, zeilen: werte(platz, sichtbar))
                }
            }
            .chartLegend(.hidden)
            .chartYScale(domain: bereich)
            .chartYAxis {
                AxisMarks(position: .leading)
                AxisMarks(position: .trailing, values: aussenSichtbar ? Aussenachse.marken.map { Aussenachse.abbilden($0, auf: bereich) } : []) { v in
                    AxisValueLabel {
                        if let y = v.as(Double.self) {
                            // + 0 macht aus −0 eine 0
                            Text(Format.zahl(Aussenachse.zurueck(y, von: bereich).rounded() + 0, stellen: 0)).foregroundStyle(Farbe.gut)
                        }
                    }
                }
            }
            .chartXScale(domain: auszug.beginn...auszug.ende)
            .chartXAxis { verlaufsachse(zeitraum) }
            .chartXSelection(value: Binding.haltend($auswahl, raster: auszug))
            .frame(height: hoehe)
            .padding(.top, 6)
            .accessibilityLabel(reihen.map(\.name).joined(separator: ", ") + ", \(zeitraum.rawValue)")
            legende
        }
        .padding(.vertical, 6)
    }

    /// Die Legende schaltet zugleich: Antippen blendet eine Reihe aus und wieder ein.
    private var legende: some View {
        FlussLayout(abstand: 12, zeilenabstand: 6) {
            ForEach(reihen) { t in
                let aus = ausgeblendet.contains(t.id)
                Button {
                    if aus { ausgeblendet.remove(t.id) } else { ausgeblendet.insert(t.id) }
                } label: {
                    Legendeneintrag(farbe: aus ? Color.secondary.opacity(0.4) : t.farbe, text: t.name, gestrichelt: t.gestrichelt)
                        .opacity(aus ? 0.5 : 1)
                }
                .buttonStyle(.plain)
            }
            if band != nil {
                HStack(spacing: 5) {
                    RoundedRectangle(cornerRadius: 2).fill(Farbe.waerme.opacity(0.25)).frame(width: 14, height: 8)
                    Text("Spreizung")
                }
            }
            if let hinterlegtName, !hinterlegt.isEmpty {
                HStack(spacing: 5) {
                    RoundedRectangle(cornerRadius: 2).fill(hinterlegtFarbe.opacity(2.5)).frame(width: 14, height: 8)
                    Text(hinterlegtName)
                }
            }
            if aussen != nil, let aussenAn {
                Button {
                    aussenAn.wrappedValue.toggle()
                } label: {
                    // Kein Label: In Formularzeilen setzt SwiftUI dessen Symbol in eine eigene Spalte.
                    HStack(spacing: 4) {
                        Image(systemName: aussenAn.wrappedValue ? "checkmark.circle.fill" : "plus.circle")
                        Text(aussenAn.wrappedValue ? "Außen, rechte Achse" : "Außen zeigen")
                    }
                    .foregroundStyle(Farbe.gut)
                }
                .buttonStyle(.plain)
            }
        }
        .font(.caption)
        .foregroundStyle(.secondary)
    }

    private func werte(_ platz: Int, _ sichtbar: [Tafelreihe]) -> [(String, String, Color)] {
        var z = sichtbar.compactMap { t -> (String, String, Color)? in
            guard platz < t.reihe.werte.count, let w = t.reihe.werte[platz] else { return nil }
            return (t.name, Format.temperatur(w), t.farbe)
        }
        if aussenSichtbar, let aussen, platz < aussen.werte.count, let w = aussen.werte[platz] {
            z.append(("Außen", Format.temperatur(w), Farbe.gut))
        }
        return z
    }

    private func bandplaetze(_ b: (oben: Verlaufsauszug.Reihe, unten: Verlaufsauszug.Reihe)) -> [(zeit: Date, oben: Double, unten: Double)] {
        (0..<auszug.anzahl).compactMap { i in
            guard i < b.oben.werte.count, i < b.unten.werte.count,
                  let o = b.oben.werte[i], let u = b.unten.werte[i] else { return nil }
            return (auszug.beginn.addingTimeInterval(Double(i) * auszug.schritt), max(o, u), min(o, u))
        }
    }
}

extension Verlaufsauszug {
    /// Mitte und Nummer des Platzes, in den eine Zeit fällt
    func platz(_ zeit: Date?) -> (Date, Int)? {
        guard let zeit else { return nil }
        let i = Int((zeit.timeIntervalSince(beginn) / schritt).rounded(.down))
        guard i >= 0, i < anzahl else { return nil }
        return (beginn.addingTimeInterval((Double(i) + 0.5) * schritt), i)
    }
}

/// Senkrechte Linie der gewählten Zeit mit den Werten daneben
struct Auswahlmarke: ChartContent {
    /// Mitte des Platzes; das Schild nennt dessen Beginn
    let zeit: Date
    let schritt: TimeInterval
    let zeilen: [(String, String, Color)]

    var body: some ChartContent {
        RuleMark(x: .value("Auswahl", zeit))
            .foregroundStyle(Color.secondary.opacity(0.6))
            .lineStyle(StrokeStyle(lineWidth: 1))
            .annotation(position: .trailing, alignment: .top, spacing: 6,
                        overflowResolution: .init(x: .fit(to: .chart), y: .fit(to: .chart))) {
                Auswahlschild(zeit: zeit.addingTimeInterval(-schritt / 2), zeilen: zeilen)
            }
    }
}

/// Werte zur gewählten Zeit neben der Linie
struct Auswahlschild: View {
    let zeit: Date
    let zeilen: [(String, String, Color)]

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(zeit, format: .dateTime.weekday(.abbreviated).hour().minute())
                .foregroundStyle(.secondary)
            ForEach(zeilen, id: \.0) { name, wert, farbe in
                HStack(spacing: 4) {
                    Circle().fill(farbe).frame(width: 6, height: 6)
                    Text(name)
                    Spacer(minLength: 6)
                    Text(wert).monospacedDigit()
                }
            }
        }
        .font(.caption2)
        .padding(6)
        .frame(minWidth: 130)
        .background(.regularMaterial, in: .rect(cornerRadius: 6))
    }
}

/// Einfacher Zeilenumbruch für Legenden
struct FlussLayout: Layout {
    var abstand: CGFloat = 8
    var zeilenabstand: CGFloat = 4

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let breite = proposal.width ?? .infinity
        var x: CGFloat = 0, y: CGFloat = 0, zeile: CGFloat = 0, weitest: CGFloat = 0
        for s in subviews {
            let g = s.sizeThatFits(.unspecified)
            if x > 0, x + g.width > breite {
                y += zeile + zeilenabstand
                x = 0
                zeile = 0
            }
            x += g.width + abstand
            zeile = max(zeile, g.height)
            weitest = max(weitest, x - abstand)
        }
        return CGSize(width: proposal.width ?? weitest, height: y + zeile)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX, y = bounds.minY, zeile: CGFloat = 0
        for s in subviews {
            let g = s.sizeThatFits(.unspecified)
            if x > bounds.minX, x + g.width > bounds.maxX {
                y += zeile + zeilenabstand
                x = bounds.minX
                zeile = 0
            }
            s.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(g))
            x += g.width + abstand
            zeile = max(zeile, g.height)
        }
    }
}

extension Verlaufsfarben {
    /// Zusammenhängende Raster, in denen eine Anteilsreihe, etwa eine Pumpe, mindestens die
    /// Hälfte der Zeit an war
    static func laeufe(_ r: Verlaufsauszug.Reihe?, in a: Verlaufsauszug) -> [Lauf] {
        guard let r else { return [] }
        var laeufe: [Lauf] = []
        var beginn: Int?
        for i in 0...r.werte.count {
            let an = i < r.werte.count && (r.werte[i] ?? 0) >= 0.5
            if an, beginn == nil { beginn = i }
            if !an, let b = beginn {
                laeufe.append(Lauf(beginn: a.beginn.addingTimeInterval(Double(b) * a.schritt),
                                   ende: a.beginn.addingTimeInterval(Double(i) * a.schritt)))
                beginn = nil
            }
        }
        return laeufe
    }
}

// MARK: - Heizkreis

/// Tafel eines Heizkreises: Vor- und Rücklauf mit Spreizung, Pumpe hinterlegt, Außen zuschaltbar
struct Heizkreistafel: View {
    let kreis: Heizkreis
    let auszug: Verlaufsauszug
    let zeitraum: Verlaufszeitraum
    let bereich: ClosedRange<Double>
    var ereignisse: [Ereignis] = []
    @Binding var auswahl: Date?
    @Binding var aussenAn: Bool
    var hoehe: CGFloat = 180

    var body: some View {
        let r = Verlaufsgliederung.heizkreis(kreis, in: auszug)
        let reihen = [r.vorlauf.map { Tafelreihe(reihe: $0, name: "Vorlauf", farbe: Farbe.waerme) },
                      r.ruecklauf.map { Tafelreihe(reihe: $0, name: "Rücklauf", farbe: Farbe.kaelte) }].compactMap { $0 }
        Temperaturtafel(
            auszug: auszug, zeitraum: zeitraum, reihen: reihen, bereich: bereich,
            hinterlegt: Verlaufsfarben.laeufe(r.pumpe, in: auszug), hinterlegtFarbe: Color.secondary.opacity(0.10),
            hinterlegtName: "Pumpe läuft",
            band: r.vorlauf.flatMap { v in r.ruecklauf.map { (v, $0) } },
            aussen: Verlaufsgliederung.aussen(in: auszug), aussenAn: $aussenAn,
            ereignisse: ereignisse, auswahl: $auswahl, hoehe: hoehe)
    }

    /// Gemeinsamer Bereich aller Kreise, damit sie sich vergleichen lassen
    static func bereich(_ kreise: [Heizkreis], in a: Verlaufsauszug) -> ClosedRange<Double> {
        tafelbereich(kreise.flatMap { k in
            let r = Verlaufsgliederung.heizkreis(k, in: a)
            return [r.vorlauf, r.ruecklauf].compactMap { $0 }
        })
    }
}

/// Verlauf und Heizkurve eines Heizkreises, aus der Karte des Kreises
struct HeizkreisVerlaufAnsicht: View {
    @Environment(AppModell.self) private var modell
    let nummer: Int
    @State private var zeitraum: Verlaufszeitraum = .woche
    @State private var auszug: Verlaufsauszug?
    @State private var ereignisse: [Ereignis] = []
    @State private var auswahl: Date?
    @State private var aussenAn = true

    var body: some View {
        let kreis = modell.anlage.heizkreise.first { $0.nummer == nummer }
        Form {
            Section {
                Picker("Zeitraum", selection: $zeitraum) {
                    ForEach(Verlaufszeitraum.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                Auswahlzeile(auswahl: $auswahl)
            }
            if let kreis, let a = auszug {
                let r = Verlaufsgliederung.heizkreis(kreis, in: a)
                if r.leer {
                    Section {
                        Text("Für diesen Zeitraum liegt kein Verlauf von Vor- und Rücklauf vor.").foregroundStyle(.secondary)
                    }
                } else {
                    Section {
                        Heizkreistafel(kreis: kreis, auszug: a, zeitraum: zeitraum, bereich: Heizkreistafel.bereich([kreis], in: a),
                                       ereignisse: ereignisse, auswahl: $auswahl, aussenAn: $aussenAn, hoehe: 220)
                    } header: {
                        Text("Verlauf")
                    } footer: {
                        Text("Tippen oder ziehen wählt eine Zeit und zeigt ihre Werte. Grau hinterlegt: Die Pumpe läuft. Die Außentemperatur steht auf der rechten Achse, fest von −10 bis 20 °C.")
                    }
                    HeizkurveAbschnitt(auszug: a, reihen: r)
                }
            } else if kreis == nil {
                Section { Text("Diesen Heizkreis gibt es nicht mehr.").foregroundStyle(.secondary) }
            } else {
                Section { ProgressView().frame(maxWidth: .infinity, minHeight: 220) }
            }
        }
        .formularStil()
        .navigationTitle(kreis?.name ?? "Heizkreis")
        .fragenKnopf("Heizkreis \(nummer), Verlauf und Heizkurve, \(zeitraum.rawValue)")
        .task(id: Ladeschluessel(zeitraum: zeitraum, revision: modell.verlaufsrevision)) {
            auszug = await modell.verlaufsauszug(zeitraum)
            ereignisse = await modell.ereignisse(zeitraum, arten: Ereignistext.markiert)
        }
    }
}

/// Vor- und Rücklauf über der Außentemperatur, nur bei laufender Pumpe, mit Ausgleichsgeraden
struct HeizkurveAbschnitt: View {
    let auszug: Verlaufsauszug
    let reihen: Heizkreisreihen

    var body: some View {
        let aussen = Verlaufsgliederung.aussen(in: auszug)
        let vl = aussen.flatMap { t in reihen.vorlauf.map { Heizkurve.punkte(auszug, wert: $0, aussen: t, pumpe: reihen.pumpe) } } ?? []
        let rl = aussen.flatMap { t in reihen.ruecklauf.map { Heizkurve.punkte(auszug, wert: $0, aussen: t, pumpe: reihen.pumpe) } } ?? []
        let gv = Heizkurve.gerade(vl)
        let gr = Heizkurve.gerade(rl)
        Section {
            if aussen == nil {
                Text("Ohne Außentemperatur im Verlauf lässt sich keine Heizkurve zeigen. Sie kommt vom Außenfühler des Leitstands oder eines Verteilers.")
                    .foregroundStyle(.secondary)
            } else if vl.isEmpty {
                Text("Im Zeitraum lief die Pumpe nicht, oder es fehlen Werte.").foregroundStyle(.secondary)
            } else {
                Chart {
                    ForEach(rl) { p in
                        PointMark(x: .value("Außen", p.aussen), y: .value("Temperatur", p.wert))
                            .foregroundStyle(Farbe.kaelte.opacity(0.35))
                            .symbolSize(14)
                    }
                    ForEach(vl) { p in
                        PointMark(x: .value("Außen", p.aussen), y: .value("Temperatur", p.wert))
                            .foregroundStyle(Farbe.waerme.opacity(0.45))
                            .symbolSize(14)
                    }
                    ForEach([(gv, "vl", Farbe.waerme), (gr, "rl", Farbe.kaelte)], id: \.1) { g, name, farbe in
                        if let g {
                            ForEach([Aussenachse.bereich.lowerBound, Aussenachse.bereich.upperBound], id: \.self) { t in
                                LineMark(x: .value("Außen", t), y: .value("Temperatur", g.wert(t)), series: .value("Gerade", name))
                                    .foregroundStyle(farbe)
                                    .lineStyle(StrokeStyle(lineWidth: 2))
                            }
                        }
                    }
                }
                .chartXScale(domain: Aussenachse.bereich)
                .chartYScale(domain: tafelbereich([reihen.vorlauf, reihen.ruecklauf].compactMap { $0 }))
                .chartPlotStyle { $0.clipped() }
                .frame(height: 240)
                .padding(.vertical, 8)
                .accessibilityLabel("Heizkurve: Vor- und Rücklauf über der Außentemperatur")
                HStack(spacing: 14) {
                    Legendeneintrag(farbe: Farbe.waerme, text: "Vorlauf")
                    Legendeneintrag(farbe: Farbe.kaelte, text: "Rücklauf")
                    Spacer(minLength: 0)
                    Text("waagerecht: Außen °C")
                }
                .font(.caption)
                .foregroundStyle(.secondary)
                if let gv {
                    LabeledContent("Steigung Vorlauf", value: steigung(gv))
                    LabeledContent("Vorlauf bei 0 °C außen", value: Format.temperatur(gv.beiNull))
                }
                if let gr {
                    LabeledContent("Steigung Rücklauf", value: steigung(gr))
                }
                if gv == nil {
                    Text("Für eine Gerade schwankt die Außentemperatur im Zeitraum zu wenig. Ein längerer Zeitraum hilft.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
        } header: {
            Text("Heizkurve")
        } footer: {
            Text("Ein Punkt je \(auszug.schritt >= 3600 ? "\(Int(auszug.schritt / 3600)) Stunden" : "\(Int(auszug.schritt / 60)) Minuten"), nur solange die Pumpe lief. Die Gerade zeigt, wie stark der Vorlauf der Außentemperatur folgt, so wie es sich einstellt; eine Steigung von −0,7 heißt: 1 K kälter draußen, 0,7 K wärmer im Vorlauf. Eine enge Punktwolke um die Gerade (hohe Bestimmtheit) heißt, der Kreis folgt vor allem dem Wetter.")
        }
    }

    private func steigung(_ g: Heizkurve.Gerade) -> String {
        "\(Format.zahl(g.steigung, stellen: 2)) K je K · Bestimmtheit \(Format.zahl(g.bestimmtheit, stellen: 2))"
    }
}

extension Binding where Value == Date? {
    /// Auswahl, die nach dem Loslassen stehen bleibt. `chartXSelection` setzt sie am Ende der
    /// Berührung auf nil; zum Vergleich mehrerer Tafeln muss sie beim Blättern bleiben.
    /// Die Zeit wird auf den Beginn ihres Platzes gerundet; so zeigen alle Tafeln und die
    /// Zeile darüber dieselbe Zeit wie die Dateien des Leitstands.
    static func haltend(_ b: Binding<Date?>, raster a: Verlaufsauszug) -> Binding<Date?> {
        Binding(get: { b.wrappedValue }, set: { neu in
            guard let neu, let (mitte, _) = a.platz(neu) else { return }
            b.wrappedValue = mitte.addingTimeInterval(-a.schritt / 2)
        })
    }
}

/// Zeigt die gewählte Zeit und hebt die Auswahl auf
struct Auswahlzeile: View {
    @Binding var auswahl: Date?

    var body: some View {
        if let zeit = auswahl {
            HStack {
                Label {
                    Text(zeit, format: .dateTime.weekday(.wide).hour().minute())
                } icon: {
                    Image(systemName: "line.diagonal").rotationEffect(.degrees(45))
                }
                Spacer()
                Button("Aufheben") { auswahl = nil }
            }
        }
    }
}
