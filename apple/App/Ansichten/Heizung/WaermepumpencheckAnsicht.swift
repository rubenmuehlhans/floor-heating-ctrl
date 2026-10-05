import SwiftUI
import Anlage
import Verlauf

/// Annahmen und Tankablesungen des Wärmepumpen-Checks, gespeichert neben dem Verlauf
struct Waermepumpenstand: Codable, Equatable {
    var annahmen = Waermepumpencheck.Annahmen()
    var ablesungen: [Waermepumpencheck.Tankablesung] = []

    static var datei: URL { URL.applicationSupportDirectory.appending(path: "Heizung/waermepumpencheck.json") }

    static func laden() -> Waermepumpenstand {
        (try? Data(contentsOf: datei)).flatMap { try? JSONDecoder().decode(Self.self, from: $0) } ?? .init()
    }

    func sichern() {
        try? FileManager.default.createDirectory(at: Self.datei.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? JSONEncoder().encode(self).write(to: Self.datei, options: .atomic)
    }
}

/// Grundlagen für die Planung einer Wärmepumpe: Heizlast, Warmwasser, nötiger Vorlauf, kritische Räume
struct WaermepumpencheckAnsicht: View {
    @Environment(AppModell.self) private var modell
    @State private var stand = Waermepumpenstand.laden()
    @State private var auszug: Verlaufsauszug?
    @State private var kalibrierung: Waermepumpencheck.Kalibrierung?
    @State private var neueAblesung = false

    /// Stundenmittel über diesen Zeitraum für Vorlauf und Räume
    private let tage = 90

    var body: some View {
        let bild = modell.anlage
        let gemessen = kalibrierung?.durchsatz
        let heizlast = bild.verbrauchslinie.flatMap {
            Waermepumpencheck.heizlast($0, duese: bild.kessel?.duese, gemessen: gemessen, annahmen: stand.annahmen)
        }
        Form {
            Section {
                Stepper(value: $stand.annahmen.normaussen, in: -20...0, step: 1) {
                    LabeledContent("Normaußentemperatur", value: Format.temperatur(stand.annahmen.normaussen, stellen: 0))
                }
                Stepper(value: $stand.annahmen.wirkungsgrad, in: 0.7...0.98, step: 0.01) {
                    LabeledContent("Kesselwirkungsgrad", value: Format.prozent(stand.annahmen.wirkungsgrad))
                }
                LabeledContent("Heizwert Heizöl", value: "\(Format.zahl(stand.annahmen.heizwert)) kWh/l")
            } header: {
                Text("Annahmen")
            } footer: {
                Text("Die Normaußentemperatur Ihres Orts steht in DIN/TS 12831-1; in Deutschland liegt sie meist zwischen −10 und −16 °C. Den Wirkungsgrad schätzt der Abgas-Vorlauf-Abstand; ein Ölkessel ohne Brennwert liegt meist bei 85 bis 90 %.")
            }

            waermeAbschnitt(Waermepumpencheck.waermelast(
                ladungen: bild.ladungen, tage: bild.tage, volumen: bild.speicher?.volumen, annahmen: stand.annahmen))
            heizlastAbschnitt(heizlast, linie: bild.verbrauchslinie)
            tankAbschnitt(gemessen: gemessen)
            vorlaufAbschnitt(bild)
            raeumeAbschnitt(bild)

            Section {
                Text("Die Werte stammen aus dem Betrieb der Ölheizung und sind ein Gegencheck, keine Auslegung. Für Auslegung und Förderung verlangen Installateur und Förderstelle in der Regel eine raumweise Heizlastberechnung und einen hydraulischen Abgleich; das Analysepaket unter Einstellungen › Daten gibt ihnen die Messwerte dazu.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
        .formularStil()
        .navigationTitle("Wärmepumpen-Check")
        .fragenKnopf("Wärmepumpen-Check: Heizlast, Vorlauf und Räume")
        .onChange(of: stand) { stand.sichern() }
        .task(id: modell.verlaufsrevision) { auszug = await modell.stundenauszug(tage: tage) }
        .task(id: stand.ablesungen) { await kalibrieren() }
        .sheet(isPresented: $neueAblesung) {
            TankablesungBlatt { stand.ablesungen.append($0) }
        }
    }

    // MARK: Abschnitte

    @ViewBuilder
    private func heizlastAbschnitt(_ h: Waermepumpencheck.Heizlast?, linie: Verbrauchslinie?) -> some View {
        Section {
            if let h {
                Kennwert(titel: "Heizlast bei \(Format.temperatur(stand.annahmen.normaussen, stellen: 0))",
                         wert: "\(Format.zahl(h.kilowatt, stellen: 1)) kW", farbe: Farbe.tinte)
                Kennwert(titel: "Wärmeverlust des Hauses", wert: "\(Format.zahl(h.wattJeKelvin, stellen: 0)) W/K")
                Kennwert(titel: "Warmwasser", wert: "\(Format.zahl(h.warmwasserKWhJeTag, stellen: 1)) kWh je Tag")
                Kennwert(titel: "Heizwärme je Heizgradtag", wert: "\(Format.zahl(h.kwhJeHeizgradtag, stellen: 1)) kWh")
                Kennwert(titel: "Kesselleistung", wert: "\(Format.zahl(h.kesselleistung, stellen: 1)) kW bei \(Format.zahl(h.durchsatz, stellen: 2)) l/h \(h.gemessen ? "(gemessen)" : "(angenommen)")")
                Kennwert(titel: "Brennerstunden am Auslegungstag", wert: Format.zahl(h.stundenAmAuslegungstag, stellen: 1))
                if let t = h.kaeltesterTag {
                    Kennwert(titel: "Kältester erfasster Tag", wert: "Mittel \(Format.temperatur(t))")
                }
                ForEach(h.hinweise, id: \.self) { t in
                    Label(t, systemImage: "exclamationmark.triangle")
                        .font(.callout)
                        .foregroundStyle(Farbe.warnung)
                }
            } else {
                Text(linie?.grund ?? "Für die Heizlast braucht die App die Verbrauchslinie des Heizungsgeräts mit dem Tagesprotokoll und einen Düsendurchsatz.")
                    .foregroundStyle(.secondary)
            }
        } header: {
            Text("Heizlast")
        } footer: {
            Text("Aus der Verbrauchslinie: Brennerstunden je Heizgradtag mal Kesselleistung, verlängert bis zur Normaußentemperatur. Der Warmwasseranteil ist die Brennerlaufzeit an Tagen ohne Heizbedarf; er kommt zur Heizlast hinzu, wenn die Wärmepumpe auch das Warmwasser bereitet. Jahreswärme: Heizwärme je Heizgradtag mal der Heizgradtage Ihres Orts im Jahr.")
        }
    }

    @ViewBuilder
    private func waermeAbschnitt(_ w: Waermepumpencheck.Waermelast) -> some View {
        Section {
            if let kw = w.kilowatt {
                Kennwert(titel: "Heizlast bei \(Format.temperatur(stand.annahmen.normaussen, stellen: 0))",
                         wert: "\(Format.zahl(kw, stellen: 1)) kW", farbe: Farbe.tinte)
                if let wk = w.wattJeKelvin {
                    Kennwert(titel: "Wärmeverlust des Hauses", wert: "\(Format.zahl(wk, stellen: 0)) W/K")
                }
                if let s = w.sockelKWhJeTag {
                    Kennwert(titel: "Sockel ohne Heizbedarf", wert: "\(Format.zahl(s, stellen: 1)) kWh je Tag")
                }
                if let h = w.kwhJeHeizgradtag {
                    Kennwert(titel: "Heizwärme je Heizgradtag", wert: "\(Format.zahl(h, stellen: 2)) kWh")
                }
                if let t = w.kaeltesterTag {
                    Kennwert(titel: "Kälteste erfasste Entladung", wert: "Mittel \(Format.temperatur(t))")
                }
                Kennwert(titel: "Entladungen", wert: "\(w.entladungen.count) über \(Format.zahl(w.entladungen.map(\.tage).reduce(0, +), stellen: 0)) Tage")
            } else if let g = w.grund {
                Text(g).foregroundStyle(.secondary)
            }
            if let l = w.ladeleistung, let d = w.durchsatz {
                Kennwert(titel: "Ladeleistung in den Speicher",
                         wert: "\(Format.zahl(l, stellen: 1)) kW, passend zu \(Format.zahl(d, stellen: 2)) l/h")
            }
            ForEach(w.hinweise, id: \.self) { t in
                Label(t, systemImage: "exclamationmark.triangle")
                    .font(.callout)
                    .foregroundStyle(Farbe.warnung)
            }
        } header: {
            Text("Heizlast aus der Speicherwärme")
        } footer: {
            Text("Gemessen wird die Wärme selbst: Zwischen zwei Ladungen gibt der Speicher Wärme an das Haus ab, Temperaturabfall mal Inhalt. Jede Entladung erhält die Heizgradtage ihres Zeitraums; eine Gerade durch diese Paare ergibt Wärme je Heizgradtag und den Sockel bei null Heizgradtagen. Düse und Wirkungsgrad gehen nicht ein. Der Sockel umfasst Warmwasser und die Verluste von Speicher und Leitungen. Die Ladeleistung ergibt mit dem Wirkungsgrad oben den Düsendurchsatz, der zu den Messungen passt.")
        }
    }

    @ViewBuilder
    private func tankAbschnitt(gemessen: Double?) -> some View {
        Section {
            ForEach(stand.ablesungen.sorted { $0.datum < $1.datum }) { a in
                LabeledContent {
                    Text("\(Format.zahl(a.liter, stellen: 0)) l" + (a.nachgetankt > 0 ? ", \(Format.zahl(a.nachgetankt, stellen: 0)) l nachgetankt" : ""))
                } label: {
                    Text(a.datum, format: .dateTime.day().month().year())
                }
                .swipeActions {
                    Button("Löschen", role: .destructive) { stand.ablesungen.removeAll { $0.id == a.id } }
                }
                .contextMenu {
                    Button("Löschen", systemImage: "trash", role: .destructive) { stand.ablesungen.removeAll { $0.id == a.id } }
                }
            }
            Button("Ablesung hinzufügen …", systemImage: "plus") { neueAblesung = true }
            if let k = kalibrierung {
                Kennwert(titel: "Verbrauch", wert: "\(Format.zahl(k.liter, stellen: 0)) l in \(Format.zahl(k.stunden, stellen: 1)) Brennerstunden")
                if let d = k.durchsatz {
                    Kennwert(titel: "Gemessener Durchsatz", wert: "\(Format.zahl(d, stellen: 2)) l/h", farbe: Farbe.tinte)
                } else if let g = k.grund {
                    Label(g, systemImage: "info.circle").font(.callout).foregroundStyle(.secondary)
                }
            }
        } header: {
            Text("Tankablesungen")
        } footer: {
            Text("Lesen Sie den Tankinhalt in Litern ab, heute und wieder nach einigen Wochen Heizbetrieb. Aus dem Verbrauch und den aufgezeichneten Brennerstunden ergibt sich der tatsächliche Düsendurchsatz; er ersetzt dann die Annahme\(modell.anlage.kessel?.duese.map { " von \(Format.zahl($0, stellen: 2)) l/h" } ?? ""). Nachgetankte Liter tragen Sie bei der ersten Ablesung danach ein.")
        }
    }

    @ViewBuilder
    private func vorlaufAbschnitt(_ bild: Anlagenbild) -> some View {
        let bedarf = auszug.map { a in bild.heizkreise.compactMap { Waermepumpencheck.vorlauf($0, auszug: a, annahmen: stand.annahmen) } } ?? []
        Section {
            if auszug == nil {
                ProgressView()
            } else if bedarf.isEmpty {
                Text("Noch kein Verlauf von Vorlauf und Außentemperatur. Er entsteht mit dem Leitstand oder einem Außenfühler am Verteiler.")
                    .foregroundStyle(.secondary)
            }
            ForEach(bedarf, id: \.kreis) { v in
                VStack(alignment: .leading, spacing: 6) {
                    Text(v.name).font(.headline)
                    if let n = v.beiNormaussen {
                        Kennwert(titel: "Bei \(Format.temperatur(stand.annahmen.normaussen, stellen: 0)) außen, verlängert",
                                 wert: v.belastbar ? "\(Format.temperatur(n)) · \(Waermepumpencheck.bewertung(n))"
                                                   : "\(Format.temperatur(n)), noch nicht belastbar",
                                 farbe: v.belastbar ? Farbe.tinte : nil)
                    }
                    if let h = v.hoechsterBeiKaelte, let bis = v.kaelteBis {
                        Kennwert(titel: "Höchster Vorlauf bis \(Format.temperatur(bis, stellen: 0)) außen", wert: Format.temperatur(h))
                    }
                    if let t = v.tiefsteAussentemperatur {
                        Kennwert(titel: "Tiefste Außentemperatur im Zeitraum", wert: Format.temperatur(t))
                    }
                    if v.beiNormaussen == nil {
                        Text("Für eine Heizkurve schwankt die Außentemperatur noch zu wenig.").font(.callout).foregroundStyle(.secondary)
                    } else if let grund = v.vorbehalt {
                        Text(grund).font(.callout).foregroundStyle(Farbe.warnung)
                    }
                }
                .padding(.vertical, 4)
            }
        } header: {
            Text("Vorlauftemperatur")
        } footer: {
            Text("Aus den letzten \(tage) Tagen, nur solange die Pumpe lief. Richtwerte für eine Wärmepumpe: bis 35 °C sehr günstig, bis 45 °C günstig, bis 55 °C möglich. Aussagekräftig wird der Wert, wenn der Zeitraum kalte Tage enthält.")
        }
    }

    @ViewBuilder
    private func raeumeAbschnitt(_ bild: Anlagenbild) -> some View {
        let raeume = auszug.map { Waermepumpencheck.raeume(bild, auszug: $0) } ?? []
        Section {
            if auszug != nil, raeume.isEmpty {
                Text("Noch zu wenige kalte Stunden mit Raumwerten und Außentemperatur.").foregroundStyle(.secondary)
            }
            ForEach(raeume.prefix(8)) { r in
                LabeledContent {
                    VStack(alignment: .trailing, spacing: 2) {
                        Text("Ventile \(Format.prozent(r.mittlereStellung))")
                        Text("\(Format.kelvin(r.mittlereAbweichung, vorzeichen: true)) zum Soll")
                            .foregroundStyle(r.mittlereAbweichung < -0.5 ? Farbe.warnung : .secondary)
                    }
                    .font(.callout.monospacedDigit())
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(r.name)
                        Text(r.anteilUnterversorgt > 0
                             ? "\(r.etage) · \(Format.prozent(r.anteilUnterversorgt)) der kalten Stunden zu kühl bei offenen Ventilen"
                             : "\(r.etage) · \(r.stunden) kalte Stunden")
                            .font(.caption)
                            .foregroundStyle(r.anteilUnterversorgt > 0.1 ? Farbe.warnung : .secondary)
                    }
                }
            }
        } header: {
            Text("Räume bei Kälte")
        } footer: {
            Text("Das kälteste Viertel der Stunden aus den letzten \(tage) Tagen. Oben stehen die Räume, die bei offenen Ventilen unter dem Sollwert blieben, danach die mit den am weitesten geöffneten Ventilen. Sie begrenzen, wie weit sich der Vorlauf senken lässt; für sie lohnt ein Blick auf Heizflächen und Abgleich.")
        }
    }

    private func kalibrieren() async {
        guard let v = Waermepumpencheck.verbrauch(stand.ablesungen) else {
            kalibrierung = nil
            return
        }
        let b = await modell.brennerstunden(von: v.von, bis: v.bis)
        kalibrierung = Waermepumpencheck.kalibrierung(liter: v.liter, stunden: b.stunden, abdeckung: b.abdeckung)
    }
}

/// Eingabe einer Tankablesung
struct TankablesungBlatt: View {
    @Environment(\.dismiss) private var schliessen
    let uebernehmen: (Waermepumpencheck.Tankablesung) -> Void
    @State private var datum = Date.now
    @State private var liter: Double?
    @State private var nachgetankt: Double?

    private func literfeld(_ wert: Binding<Double?>) -> some View {
        HStack(spacing: 4) {
            TextField("0", value: wert, format: .number)
                .multilineTextAlignment(.trailing)
                #if os(iOS)
                .keyboardType(.decimalPad)
                #endif
            Text("l").foregroundStyle(.secondary)
        }
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    DatePicker("Datum", selection: $datum, in: ...Date.now)
                    LabeledContent("Im Tank") { literfeld($liter) }
                    LabeledContent("Nachgetankt") { literfeld($nachgetankt) }
                } footer: {
                    Text("Nachgetankt: die Liter, die seit der vorigen Ablesung in den Tank kamen; sonst leer lassen.")
                }
            }
            .formularStil()
            .navigationTitle("Tankablesung")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Abbrechen") { schliessen() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Sichern") {
                        if let liter { uebernehmen(.init(datum: datum, liter: liter, nachgetankt: nachgetankt ?? 0)) }
                        schliessen()
                    }
                    .disabled(liter == nil)
                }
            }
        }
    }
}
