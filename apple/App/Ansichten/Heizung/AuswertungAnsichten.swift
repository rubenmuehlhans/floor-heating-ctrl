import SwiftUI
import Charts
import Anlage
import Geraeteschnittstelle

// MARK: - Verbrauchslinie

struct VerbrauchslinieAnsicht: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        let linie = modell.anlage.verbrauchslinie
        Form {
            Section {
                if let linie, linie.gueltig {
                    Verbrauchsdiagramm(linie: linie).frame(height: 260)
                } else {
                    VStack(alignment: .leading, spacing: 8) {
                        Label("Noch keine Linie", systemImage: "hourglass")
                            .font(.headline)
                        Text(linie.map { "\($0.erfassteTage) Tage erfasst. \($0.grund ?? "")" } ?? "Die Linie bildet das Heizungsgerät am Kessel aus seinem Tagesprotokoll.")
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    .padding(.vertical, 4)
                }
            } header: {
                Text("Diese Anlage")
            }

            Section {
                Verbrauchsdiagramm(linie: .winterbeispiel)
                    .frame(height: 260)
                    .padding(.vertical, 8)
            } header: {
                Text("So sieht sie in der Heizperiode aus")
            } footer: {
                Text("Beispielwerte, nicht gemessen. Die Steigung ist der Wärmebedarf des Hauses je Heizgradtag, der Achsenabschnitt der Grundverbrauch für Warmwasser. Tage deutlich über der Linie meldet das Gerät als Befund – etwa ein offenes Fenster oder eine durchlaufende Pumpe.")
            }
        }
        .formularStil()
        .navigationTitle("Verbrauchslinie")
        .fragenKnopf("Verbrauchslinie")
    }
}

private struct Verbrauchsdiagramm: View {
    let linie: Verbrauchslinie

    var body: some View {
        let xs = linie.tage.map(\.heizgradtage)
        let von = (xs.min() ?? 0) - 1
        let bis = (xs.max() ?? 20) + 1
        Chart {
            ForEach(linie.tage) { tag in
                PointMark(x: .value("Heizgradtage", tag.heizgradtage), y: .value("Laufzeit", tag.laufzeitStunden))
                    .foregroundStyle(tag.auffaellig ? Farbe.warnung : Farbe.waerme)
                    .symbolSize(tag.auffaellig ? 90 : 45)
                    .annotation(position: .top) {
                        if tag.auffaellig {
                            Text("über der Linie").font(.caption2).foregroundStyle(Farbe.warnung)
                        }
                    }
            }
            ForEach([von, bis], id: \.self) { x in
                LineMark(x: .value("Heizgradtage", x), y: .value("Laufzeit", linie.grundlast + linie.steigung * x))
                    .foregroundStyle(.secondary)
                    .lineStyle(StrokeStyle(lineWidth: 1.5, dash: [5, 4]))
            }
        }
        .chartXAxisLabel("Heizgradtage je Tag")
        .chartYAxisLabel("Brennerlaufzeit in h")
    }
}

// MARK: - Protokolle

struct ProtokolleAnsicht: View {
    @Environment(AppModell.self) private var modell
    @State private var frageLoeschen = false
    @State private var export: Textdokument?
    @State private var exportName = "Protokoll.csv"

    /// „Tagesprotokoll 2026-09-22.csv“
    static func dateiname(_ art: String) -> String {
        "\(art) \(Date.now.formatted(.iso8601.year().month().day())).csv"
    }

    var body: some View {
        let a = modell.anlage
        Form {
            Section {
                if a.tage.isEmpty {
                    Text("Keine Einträge.").foregroundStyle(.secondary)
                }
                ForEach(a.tage) { tag in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(Format.tag(tag.datum)).font(.subheadline.weight(.medium))
                            Text("außen \(Format.temperatur(tag.aussenMin)) bis \(Format.temperatur(tag.aussenMax))")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        VStack(alignment: .trailing, spacing: 2) {
                            Text("\(Format.dauer(tag.laufzeit)) · \(Format.starts(tag.starts))")
                            Text("\(Format.liter(tag.liter, stellen: 2)) · \(Format.zahl(tag.heizgradtage)) Gradtage")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        .monospacedDigit()
                    }
                }
            } header: {
                Text("Tagesprotokoll")
            } footer: {
                Text("Das Gerät hält 365 Tage vor; der erste Satz ist der laufende Tag.")
            }

            Section {
                if a.ladungen.isEmpty {
                    Text("Keine Einträge.").foregroundStyle(.secondary)
                }
                ForEach(a.ladungen) { l in
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text(Format.tagMitZeit(l.beginn)).font(.subheadline.weight(.medium))
                            Spacer()
                            Text(Format.dauer(l.dauer)).foregroundStyle(.secondary)
                        }
                        Text("Speicher \(Format.temperatur(l.speicherVorher)) → \(Format.temperatur(l.speicherNachher)) · Brenner \(Format.dauer(l.brenner)), \(Format.starts(l.starts))")
                            .font(.callout)
                        Text("Kesselvorlauf bis \(Format.temperatur(l.kesselVorlaufMax)) · Abgas bis \(Format.temperatur(l.abgasMax)) · \(Format.liter(l.liter, stellen: 2))")
                            .font(.caption).foregroundStyle(.secondary)
                    }
                    .monospacedDigit()
                    .padding(.vertical, 2)
                }
            } header: {
                Text("Ladungsprotokoll")
            } footer: {
                Text("Das Gerät hält die letzten 64 Ladungen vor. Öl ist geschätzt aus Laufzeit und Düsendurchsatz.")
            }

            Section {
                Button {
                    Task { if let text = await modell.protokollHolen(tage: true) { export = Textdokument(text: text); exportName = Self.dateiname("Tagesprotokoll") } }
                } label: {
                    Label("Tagesprotokoll als CSV sichern", systemImage: "square.and.arrow.up")
                }
                Button {
                    Task { if let text = await modell.protokollHolen(tage: false) { export = Textdokument(text: text); exportName = Self.dateiname("Ladungsprotokoll") } }
                } label: {
                    Label("Ladungsprotokoll als CSV sichern", systemImage: "square.and.arrow.up")
                }
                Button(role: .destructive) {
                    frageLoeschen = true
                } label: {
                    Label("Protokolle löschen …", systemImage: "trash")
                }
            } footer: {
                Text("Anders als der Verlauf bleiben die Protokolle nach einem Neustart erhalten. Aus ihnen ergeben sich der Verbrauch je Außentemperatur und die Entwicklung des Abgas-Vorlauf-Abstands über die Zeit.")
            }
        }
        .formularStil()
        .navigationTitle("Protokolle")
        .fragenKnopf("Protokolle")
        .confirmationDialog("Protokolle löschen?", isPresented: $frageLoeschen, titleVisibility: .visible) {
            Button("Unwiderruflich löschen", role: .destructive) { modell.protokolleLoeschen() }
        } message: {
            Text("Ladungs- und Tagesprotokoll des Heizungsgeräts am Kessel gehen verloren, ebenso Laufzeit und Starts des laufenden Tags. Sichern Sie sie vorher als CSV, wenn Sie sie behalten wollen.")
        }
        .fileExporter(isPresented: Binding(get: { export != nil }, set: { if !$0 { export = nil } }), document: export,
                      contentType: .commaSeparatedText, defaultFilename: exportName) { _ in export = nil }
    }
}

// MARK: - Ladungsaufzeichnung

struct AufzeichnungAnsicht: View {
    @Environment(AppModell.self) private var modell
    @State private var frageVerwerfen: Aufzeichnungsbefehl?
    @State private var export: Textdokument?
    @State private var gewaehlt: String?

    var body: some View {
        let geraete = modell.aufzeichnungsgeraete
        let geraet = gewaehlt.flatMap { geraete.contains($0) ? $0 : nil } ?? geraete.first
        let r = modell.aufzeichnungsstand(geraet)
        let zustand = modell.istBeispiel ? modell.aufzeichnung.rawValue : r?.zustand ?? "aus"
        // Eine scharf geschaltete Aufzeichnung reserviert schon Speicher; Daten hat sie erst mit Messpunkten.
        let vorhanden = (r?.messpunkte ?? 0) > 0
        Form {
            if geraete.count > 1 {
                Section {
                    Picker("Gerät", selection: Binding(get: { geraet ?? "" }, set: { gewaehlt = $0 })) {
                        ForEach(geraete, id: \.self) { id in
                            Text(modell.anlage.geraet(id)?.ort ?? id).tag(id)
                        }
                    }
                    .pickerStyle(.segmented)
                } footer: {
                    Text("Jedes Heizungsgerät zeichnet seine eigenen Fühler auf: der Speicher die Pufferfühler und Heizkreise, der Kessel Abgas, Vor- und Rücklauf.")
                }
            }
            Section {
                Kennwert(titel: "Zustand", wert: zustandstext(zustand, r))
                if let r {
                    if (r.messpunkte ?? 0) > 0 {
                        Kennwert(titel: "Umfang", wert: "\(r.messpunkte ?? 0) Zeilen aus \(r.spalten ?? 0) Messstellen, \(Format.dauer((r.messpunkte ?? 0) * (r.rasterS ?? 5)))")
                    }
                    Kennwert(titel: "Beginn", wert: r.quelle == "brenner" ? "beim Brennerstart" : r.quelle == "speicher" ? "wenn die Speichertemperatur steigt" : "kein Zeichen für eine Ladung")
                    Kennwert(titel: "Raster", wert: Format.mitEinheit("\(r.rasterS ?? 5)", "s"))
                }
            } footer: {
                Text("Scharf geschaltet, beginnt die Aufzeichnung mit der nächsten Ladung und endet von selbst. Sie ist die Grundlage, um „voll“ und „leer“ aus Messungen statt aus Annahmen zu setzen.")
            }
            Section {
                switch zustand {
                case "laeuft", "läuft":
                    Button("Beenden", systemImage: "stop.circle") { modell.aufzeichnung(.beenden, geraet: geraet) }
                case "scharf":
                    Button("Abbrechen", systemImage: "xmark.circle") { modell.aufzeichnung(.beenden, geraet: geraet) }
                    Button("Jetzt starten", systemImage: "play.circle") { frage(.starten, vorhanden, geraet) }
                default:
                    Button("Bei der nächsten Ladung aufzeichnen", systemImage: "record.circle") { frage(.scharfSchalten, vorhanden, geraet) }
                        .disabled(r?.quelle == "keiner")
                    Button("Sofort aufzeichnen", systemImage: "play.circle") { frage(.starten, vorhanden, geraet) }
                }
                if (r?.messpunkte ?? 0) > 0 {
                    Button("Als CSV sichern", systemImage: "square.and.arrow.up") {
                        Task { if let text = await modell.aufzeichnungHolen(geraet: geraet) { export = Textdokument(text: text) } }
                    }
                }
                if vorhanden && !["laeuft", "läuft", "scharf"].contains(zustand) {
                    Button("Verwerfen", systemImage: "trash", role: .destructive) { frageVerwerfen = .verwerfen }
                }
            }
        }
        .formularStil()
        .navigationTitle("Ladung aufzeichnen")
        .confirmationDialog("Vorhandene Aufzeichnung verwerfen?", isPresented: Binding(get: { frageVerwerfen != nil }, set: { if !$0 { frageVerwerfen = nil } }),
                            titleVisibility: .visible) {
            Button(frageVerwerfen == .verwerfen ? "Verwerfen" : "Verwerfen und neu beginnen", role: .destructive) {
                if let b = frageVerwerfen { modell.aufzeichnung(b, geraet: geraet) }
                frageVerwerfen = nil
            }
        } message: {
            Text("Das Gerät hält nur eine Aufzeichnung. Sichern Sie sie vorher als CSV, wenn Sie sie behalten wollen.")
        }
        .fileExporter(isPresented: Binding(get: { export != nil }, set: { if !$0 { export = nil } }), document: export,
                      contentType: .commaSeparatedText, defaultFilename: ProtokolleAnsicht.dateiname("Ladungsaufzeichnung")) { _ in export = nil }
    }

    /// Scharf schalten und Starten verwerfen eine vorhandene Aufzeichnung ohne Rückfrage des Geräts.
    private func frage(_ befehl: Aufzeichnungsbefehl, _ vorhanden: Bool, _ geraet: String?) {
        if vorhanden { frageVerwerfen = befehl } else { modell.aufzeichnung(befehl, geraet: geraet) }
    }

    private func zustandstext(_ zustand: String, _ r: Heizgeraetezustand.Aufzeichnungsstand?) -> String {
        switch zustand {
        case "laeuft", "läuft":
            r?.nachlauf == true ? "läuft, Brenner aus, Nachlauf noch \(Format.dauer(r?.nachlaufRestS ?? 0))" : "läuft"
        case "scharf":
            r?.wartetAufAus == true ? "scharf, der Brenner läuft noch; aufgezeichnet wird der nächste Start" : "scharf, wartet auf die nächste Ladung"
        case "fertig": "fertig"
        default: "keine Aufzeichnung"
        }
    }
}
