import SwiftUI
import Charts
import Anlage
import Verlauf

// MARK: - Anlagenverlauf

/// Temperaturen der Anlage über einen wählbaren Zeitraum, darunter Brenner und Füllstand. Die
/// Reihen lassen sich nach Gruppen ein- und ausblenden.
struct VerlaufAnsicht: View {
    @Environment(AppModell.self) private var modell
    @State private var zeitraum: Verlaufszeitraum = .tag
    @State private var auszug: Verlaufsauszug?
    @State private var ereignisse: [Ereignis] = []
    /// Gemeinsame Auswahl aller Tafeln
    @State private var auswahl: Date?
    /// Heizkreise, deren Tafel die Außentemperatur zeigt
    @State private var aussenAus: Set<Int> = []

    var body: some View {
        let a = auszug
        let kreise = modell.anlage.heizkreise
        Form {
            Section {
                Picker("Zeitraum", selection: $zeitraum) {
                    ForEach(Verlaufszeitraum.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                Auswahlzeile(auswahl: $auswahl)
            } footer: {
                Text("Jede Gruppe hat ihre eigene Skala. Tippen oder ziehen wählt eine Zeit und zeigt ihre Werte in allen Diagrammen; die Legende blendet Reihen aus und ein.")
            }
            if let a {
                if a.reihen.contains(where: { Verlaufsgliederung.gruppe($0) != nil }) {
                    tafeln(a, kreise: kreise)
                } else {
                    Section {
                        Text("Für diesen Zeitraum liegt kein Verlauf vor.").foregroundStyle(.secondary)
                    }
                }
            } else {
                Section { ProgressView().frame(maxWidth: .infinity, minHeight: 300) }
            }
        }
        .formularStil()
        .navigationTitle("Verlauf")
        .fragenKnopf("Verlauf, \(zeitraum.rawValue)")
        .task(id: Ladeschluessel(zeitraum: zeitraum, revision: modell.verlaufsrevision)) {
            auszug = await modell.verlaufsauszug(zeitraum)
            ereignisse = await modell.ereignisse(zeitraum, arten: Ereignistext.markiert)
        }
    }

    @ViewBuilder
    private func tafeln(_ a: Verlaufsauszug, kreise: [Heizkreis]) -> some View {
        let geraete = Set(a.reihen.map(\.geraet))
        let eigene = ereignisse.filter { geraete.contains($0.geraet) }
        let brenner = zeitraum.brennerHinterlegen ? Verlaufsfarben.brennerlaeufe(a) : []
        let erzeugung = Verlaufsgliederung.reihen(.waermeerzeugung, in: a).filter { $0.schluessel != "fuehler.abgas" }
        let abgas = a.reihen.filter { $0.schluessel == "fuehler.abgas" }

        if !erzeugung.isEmpty {
            Section {
                Temperaturtafel(auszug: a, zeitraum: zeitraum, reihen: erzeugung.map { tafelreihe($0, in: a) },
                                bereich: tafelbereich(erzeugung), hinterlegt: brenner, hinterlegtName: "Brenner läuft",
                                ereignisse: eigene, auswahl: $auswahl)
            } header: {
                Text("Kessel und Speicher")
            } footer: {
                Text("Die App zeichnet auf, solange sie läuft, und übernimmt den Verlauf des Leitstands; verbleibende Lücken füllt sie aus dem 24-Stunden-Verlauf der Heizungsgeräte.")
            }
        }
        if !abgas.isEmpty {
            Section("Abgas") {
                Temperaturtafel(auszug: a, zeitraum: zeitraum, reihen: abgas.map { tafelreihe($0, in: a) },
                                bereich: tafelbereich(abgas, mindestens: 40), hinterlegt: brenner, hinterlegtName: "Brenner läuft",
                                ereignisse: [], auswahl: $auswahl, hoehe: 120)
            }
        }
        if Betriebsdiagramm.hatDaten(a) {
            Section {
                Betriebsdiagramm(auszug: a, zeitraum: zeitraum, auswahl: $auswahl)
                    .frame(height: 150)
                    .padding(.vertical, 8)
            } header: {
                Text("Brenner und Speicher")
            } footer: {
                Text("Balken: Anteil der Zeit, in der der Brenner lief. Linie: geschätzter Füllstand des Pufferspeichers.")
            }
        }
        let mitVerlauf = kreise.filter { !Verlaufsgliederung.heizkreis($0, in: a).leer }
        let kreisbereich = Heizkreistafel.bereich(mitVerlauf, in: a)
        ForEach(mitVerlauf) { kreis in
            Section {
                Heizkreistafel(kreis: kreis, auszug: a, zeitraum: zeitraum, bereich: kreisbereich, ereignisse: eigene,
                               auswahl: $auswahl, aussenAn: Binding(
                                   get: { !aussenAus.contains(kreis.nummer) },
                                   set: { an in if an { aussenAus.remove(kreis.nummer) } else { aussenAus.insert(kreis.nummer) } }))
                NavigationLink(value: Ziel.heizkreisverlauf(kreis.nummer)) {
                    Label("Heizkurve", systemImage: "chart.dots.scatter")
                }
            } header: {
                Text(kreis.name)
            } footer: {
                if kreis.id == mitVerlauf.last?.id {
                    Text("Alle Heizkreise haben dieselbe Skala. Grau hinterlegt: Die Pumpe läuft. Die Außentemperatur steht auf der rechten Achse, fest von −10 bis 20 °C.")
                }
            }
        }
        // Fühler eines Heizkreises, den die App nicht kennt, etwa nach einer Umbenennung der Rollen
        let bekannt = Set(mitVerlauf.flatMap { k in
            let r = Verlaufsgliederung.heizkreis(k, in: a)
            return [r.vorlauf?.id, r.ruecklauf?.id].compactMap { $0 }
        })
        let uebrige = Verlaufsgliederung.reihen(.heizkreise, in: a).filter { !bekannt.contains($0.id) }
        if !uebrige.isEmpty {
            Section("Weitere Heizkreisfühler") {
                Temperaturtafel(auszug: a, zeitraum: zeitraum, reihen: uebrige.map { tafelreihe($0, in: a) },
                                bereich: tafelbereich(uebrige), aussen: Verlaufsgliederung.aussen(in: a),
                                ereignisse: eigene, auswahl: $auswahl)
            }
        }
        let raeume = Verlaufsgliederung.reihen(.raeume, in: a)
        if !raeume.isEmpty {
            Section("Räume") {
                Temperaturtafel(auszug: a, zeitraum: zeitraum, reihen: raeume.map { tafelreihe($0, in: a) },
                                bereich: tafelbereich(raeume, mindestens: 6), ereignisse: [], auswahl: $auswahl)
            }
        }
        let vorlauf = Verlaufsgliederung.reihen(.verteilervorlauf, in: a)
        if !vorlauf.isEmpty {
            Section("Vorlauf an den Verteilern") {
                Temperaturtafel(auszug: a, zeitraum: zeitraum, reihen: vorlauf.map { tafelreihe($0, in: a) },
                                bereich: tafelbereich(vorlauf), ereignisse: [], auswahl: $auswahl)
            }
        }
        if !eigene.isEmpty {
            Section {
                Ereignisliste(ereignisse: eigene, orte: Dictionary(a.reihen.map { ($0.geraet, $0.ort) }, uniquingKeysWith: { a, _ in a }))
            } header: {
                Text("Ereignisse")
            } footer: {
                Text("Aus dem Protokoll des Leitstands. Im Diagramm: Neustart \(Image(systemName: "arrow.clockwise")), neue Firmware \(Image(systemName: "shippingbox")), neuer Befund \(Image(systemName: "exclamationmark.triangle")); ein grauer Streifen heißt, das Gerät war nicht erreichbar. Rot markiert sind Neustarts nach Absturz, Wächter oder Unterspannung.")
            }
        }
    }

    private func tafelreihe(_ r: Verlaufsauszug.Reihe, in a: Verlaufsauszug) -> Tafelreihe {
        Tafelreihe(reihe: r, name: Verlaufsgliederung.bezeichnung(r, in: a), farbe: Verlaufsfarben.farbe(r, in: a),
                   gestrichelt: r.schluessel == "fuehler.puffer_unten")
    }
}

struct Ladeschluessel: Hashable {
    var zeitraum: Verlaufszeitraum
    var revision: Int
}

/// Brenneranteil als Balken, Füllstand als Linie, beides in Prozent
struct Betriebsdiagramm: View {
    let auszug: Verlaufsauszug
    let zeitraum: Verlaufszeitraum
    var auswahl: Binding<Date?> = .constant(nil)

    static func hatDaten(_ a: Verlaufsauszug) -> Bool {
        a.reihen.contains { $0.schluessel == "brenner" || $0.schluessel == "fuellstand" }
    }

    private func werte(_ i: Int) -> [(String, String, Color)] {
        auszug.reihen.compactMap { r -> (String, String, Color)? in
            guard r.schluessel == "brenner" || r.schluessel == "fuellstand", i < r.werte.count, let w = r.werte[i] else { return nil }
            let brenner = r.schluessel == "brenner"
            return (brenner ? "Brenner" : "Füllstand", Format.prozent(w), brenner ? Farbe.waerme : Color.orange)
        }
    }

    var body: some View {
        Chart {
            ForEach(auszug.reihen.filter { $0.schluessel == "brenner" }) { r in
                // Je Raster ein Rechteck in voller Breite; so bleibt der Anteil auch bei 30 Tagen lesbar.
                ForEach(auszug.punkte(r).filter { $0.wert > 0 }) { p in
                    RectangleMark(xStart: .value("Beginn", p.zeit.addingTimeInterval(-auszug.schritt / 2)),
                                  xEnd: .value("Ende", p.zeit.addingTimeInterval(auszug.schritt / 2)),
                                  yStart: .value("Anteil", 0), yEnd: .value("Anteil", p.wert * 100))
                        .foregroundStyle(Farbe.waerme.opacity(0.55))
                }
            }
            ForEach(auszug.reihen.filter { $0.schluessel == "fuellstand" }) { r in
                ForEach(Array(auszug.abschnitte(r).enumerated()), id: \.offset) { i, stueck in
                    Linienstueck(stueck: stueck.map { .init(zeit: $0.zeit, wert: $0.wert * 100) }, serie: "\(r.id)#\(i)",
                                 groesse: "Anteil", farbe: .orange)
                }
            }
            if let (mitte, i) = auszug.platz(auswahl.wrappedValue) {
                Auswahlmarke(zeit: mitte, schritt: auszug.schritt, zeilen: werte(i))
            }
        }
        .chartXSelection(value: Binding.haltend(auswahl, raster: auszug))
        .chartLegend(.hidden)
        .chartYScale(domain: 0...100)
        .chartYAxisLabel("%")
        .chartXScale(domain: auszug.beginn...auszug.ende)
        .chartXAxis { verlaufsachse(zeitraum) }
        .accessibilityLabel("Brennerlauf und Füllstand, \(zeitraum.rawValue)")
    }
}

/// Ein zusammenhängendes Stück einer Reihe. Ein einzelner Wert zwischen zwei Lücken erscheint
/// als Punkt, weil eine Linie mindestens zwei braucht.
struct Linienstueck: ChartContent {
    let stueck: [Verlaufsauszug.Punkt]
    let serie: String
    let groesse: String
    let farbe: Color
    var gestrichelt = false
    var stufen = false

    @ChartContentBuilder
    var body: some ChartContent {
        if stueck.count == 1, let p = stueck.first {
            PointMark(x: .value("Zeit", p.zeit), y: .value(groesse, p.wert))
                .symbolSize(18)
                .foregroundStyle(farbe)
        } else {
            ForEach(stueck) { p in
                LineMark(x: .value("Zeit", p.zeit), y: .value(groesse, p.wert), series: .value("Reihe", serie))
                    .foregroundStyle(farbe)
                    .lineStyle(StrokeStyle(lineWidth: gestrichelt ? 1.5 : 2, dash: gestrichelt ? [4, 3] : []))
                    .interpolationMethod(stufen ? .stepCenter : .monotone)
            }
        }
    }
}

/// Zeitachse passend zum Zeitraum: Uhrzeiten bis zu einem Tag, darüber Datum
func verlaufsachse(_ zeitraum: Verlaufszeitraum) -> some AxisContent {
    AxisMarks(values: .stride(by: zeitraum.achse.0, count: zeitraum.achse.1)) { _ in
        AxisGridLine()
        AxisValueLabel(format: zeitraum.achse.0 == .hour ? Date.FormatStyle.dateTime.hour().minute() : .dateTime.day().month(.twoDigits))
    }
}

/// Feste Farben für die wichtigsten Reihen, die übrigen aus einer Palette
enum Verlaufsfarben {
    static let palette: [Color] = [.blue, .purple, .mint, .cyan, .pink, .indigo, .green, .yellow, .gray, .brown]

    static func farbe(_ r: Verlaufsauszug.Reihe, in auszug: Verlaufsauszug) -> Color {
        switch r.schluessel {
        case "fuehler.abgas": return .brown
        case "fuehler.kessel_vl": return Farbe.waerme
        case "fuehler.kessel_rl": return Farbe.kaelte
        case "fuehler.puffer": return .orange
        case "fuehler.puffer_unten": return .yellow
        case "fuehler.hk1_vl": return .pink
        case "fuehler.hk1_rl": return .indigo
        case "fuehler.hk2_vl": return .red
        case "fuehler.hk2_rl": return .teal
        case "aussen", "fuehler.aussen": return Farbe.gut
        default:
            let uebrige = auszug.reihen.filter { Verlaufsgliederung.gruppe($0) == .raeume || Verlaufsgliederung.gruppe($0) == .verteilervorlauf }
            let i = uebrige.firstIndex { $0.id == r.id } ?? 0
            return palette[i % palette.count]
        }
    }

    struct Lauf { let beginn: Date; let ende: Date }

    /// Zusammenhängende Raster, in denen der Brenner mindestens die Hälfte der Zeit lief
    static func brennerlaeufe(_ a: Verlaufsauszug) -> [Lauf] {
        guard let r = a.reihen.first(where: { $0.schluessel == "brenner" }) else { return [] }
        var laeufe: [Lauf] = []
        var beginn: Int?
        for (i, w) in r.werte.enumerated() {
            let an = (w ?? 0) >= 0.5
            if an, beginn == nil { beginn = i }
            if !an, let b = beginn {
                laeufe.append(Lauf(beginn: a.beginn.addingTimeInterval(Double(b) * a.schritt), ende: a.beginn.addingTimeInterval(Double(i) * a.schritt)))
                beginn = nil
            }
        }
        if let b = beginn {
            laeufe.append(Lauf(beginn: a.beginn.addingTimeInterval(Double(b) * a.schritt), ende: a.ende))
        }
        return laeufe
    }
}

// MARK: - Raumverlauf

/// Ist, Soll und Ventilstellung eines Raums. Der Verteiler speichert selbst keinen Verlauf; die
/// Werte stammen aus der Aufzeichnung der App oder einem importierten Mitschnitt.
struct RaumVerlaufKarte: View {
    @Environment(AppModell.self) private var modell
    let etage: Etage
    let raum: Raum
    @State private var zeitraum: Verlaufszeitraum = .tag
    @State private var auszug: Verlaufsauszug?
    @State private var ereignisse: [Ereignis] = []

    private var schluessel: (ist: String, soll: String, stellung: String) {
        (Messgroesse.raum(raum.nummer, "ist"), Messgroesse.raum(raum.nummer, "soll"), Messgroesse.raum(raum.nummer, "stellung"))
    }

    var body: some View {
        let k = schluessel
        VStack(alignment: .leading, spacing: 10) {
            Kartenkopf(titel: "Verlauf", erklaerung: "Raumtemperatur, Sollwert und Ventilstellung.")
            Picker("Zeitraum", selection: $zeitraum) {
                ForEach([Verlaufszeitraum.tag, .woche]) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            if let a = auszug, let ist = a.reihe(etage.id, k.ist) {
                Chart {
                    Ereignismarken(ereignisse: ereignisse, bis: a.ende)
                    ForEach(Array(a.abschnitte(ist).enumerated()), id: \.offset) { i, stueck in
                        Linienstueck(stueck: stueck, serie: "ist#\(i)", groesse: "Temperatur", farbe: Farbe.waerme)
                    }
                    if let soll = a.reihe(etage.id, k.soll) {
                        ForEach(Array(a.abschnitte(soll).enumerated()), id: \.offset) { i, stueck in
                            Linienstueck(stueck: stueck, serie: "soll#\(i)", groesse: "Temperatur", farbe: .accentColor,
                                         gestrichelt: true, stufen: true)
                        }
                    }
                }
                .chartLegend(.hidden)
                .chartYScale(domain: .automatic(includesZero: false))
                .chartYAxisLabel("°C")
                .chartXScale(domain: a.beginn...a.ende)
                .chartXAxis { verlaufsachse(zeitraum) }
                .frame(height: 170)
                .accessibilityLabel("Raumtemperatur und Sollwert, \(zeitraum.rawValue)")
                if let stellung = a.reihe(etage.id, k.stellung) {
                    Chart {
                        ForEach(Array(a.abschnitte(stellung).enumerated()), id: \.offset) { i, stueck in
                            // Die Stellung steht als Fläche; ein einzelner Wert als Balken seiner Rasterbreite.
                            if stueck.count == 1, let p = stueck.first {
                                RectangleMark(xStart: .value("Beginn", p.zeit.addingTimeInterval(-a.schritt / 2)),
                                              xEnd: .value("Ende", p.zeit.addingTimeInterval(a.schritt / 2)),
                                              yStart: .value("Stellung", 0), yEnd: .value("Stellung", p.wert * 100))
                                    .foregroundStyle(Farbe.waerme.opacity(0.25))
                            } else {
                                ForEach(stueck) { p in
                                    AreaMark(x: .value("Zeit", p.zeit), y: .value("Stellung", p.wert * 100), series: .value("Reihe", "stellung#\(i)"))
                                        .foregroundStyle(Farbe.waerme.opacity(0.25))
                                        .interpolationMethod(.stepCenter)
                                }
                            }
                        }
                    }
                    .chartYScale(domain: 0...100)
                    .chartYAxis { AxisMarks(values: [0, 50, 100]) }
                    .chartYAxisLabel("Ventile %")
                    .chartXScale(domain: a.beginn...a.ende)
                    .chartXAxis(.hidden)
                    .frame(height: 64)
                    .accessibilityLabel("Ventilstellung, \(zeitraum.rawValue)")
                }
                HStack(spacing: 14) {
                    Legendeneintrag(farbe: Farbe.waerme, text: "Ist")
                    Legendeneintrag(farbe: .accentColor, text: "Soll", gestrichelt: true)
                    Legendeneintrag(farbe: Farbe.waerme.opacity(0.4), text: "Ventilstellung")
                }
                .font(.caption)
                .foregroundStyle(.secondary)
            } else if auszug != nil {
                Text("Noch kein Verlauf. Der Verteiler speichert keinen; die App zeichnet die Werte auf, solange sie geöffnet ist.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            } else {
                ProgressView().frame(maxWidth: .infinity, minHeight: 170)
            }
        }
        .padding(16)
        .karte()
        .task(id: Ladeschluessel(zeitraum: zeitraum, revision: modell.verlaufsrevision)) {
            auszug = await modell.verlaufsauszug(zeitraum, geraet: etage.id, schluessel: [k.ist, k.soll, k.stellung])
            // Ein- und Ausschalten dieses Raums; Neustarts und Ausfälle des Verteilers
            let nummer = raum.nummer
            ereignisse = await modell.ereignisse(zeitraum, geraet: etage.id,
                                                 arten: ["betriebsart", "neustart", "nicht_erreichbar", "erreichbar"])
                .filter { $0.art != "betriebsart" || $0.felder["raum"]?.alsGanzzahl == nummer }
        }
    }
}

struct Legendeneintrag: View {
    let farbe: Color
    let text: String
    var gestrichelt = false

    var body: some View {
        HStack(spacing: 5) {
            Capsule()
                .stroke(farbe, style: StrokeStyle(lineWidth: 2, dash: gestrichelt ? [3, 2] : []))
                .frame(width: 16, height: 2)
            Text(text)
        }
    }
}
