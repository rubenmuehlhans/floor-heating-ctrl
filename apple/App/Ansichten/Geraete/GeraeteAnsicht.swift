import SwiftUI
import Charts
import Anlage
import Geraeteschnittstelle

struct GeraeteAnsicht: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        let geraete = modell.anlage.geraete
        Form {
            Section("Heizungsgeräte") {
                ForEach(geraete.filter { $0.art == .kessel || $0.art == .speicher }) { g in
                    NavigationLink(value: Ziel.geraet(g.id)) { GeraetZeile(geraet: g) }
                }
            }
            Section("Verteiler") {
                ForEach(geraete.filter { $0.art == .verteiler }) { g in
                    NavigationLink(value: Ziel.geraet(g.id)) { GeraetZeile(geraet: g) }
                }
            }
            if !modell.verzeichnis.leitstaende.isEmpty {
                Section {
                    ForEach(modell.verzeichnis.leitstaende) { l in
                        NavigationLink(value: Ziel.leitstand(l.id)) {
                            LeitstandZeile(eintrag: l, stand: modell.leitstandbetrieb.staende[l.id])
                        }
                    }
                } header: {
                    Text("Leitstand")
                } footer: {
                    Text("Empfängt Funkthermometer in Kesselnähe und zeigt den Zustand der Anlage an; er regelt nichts.")
                }
            }
            Section {
                ForEach(geraete.filter { $0.art == .relais }) { g in
                    GeraetZeile(geraet: g)
                }
            } header: {
                Text("Pumpenrelais")
            } footer: {
                Text("Die Tasmota-Relais schaltet das Heizungsgerät; die App fragt sie nur zur Fehlersuche unmittelbar ab.")
            }
            if !modell.verzeichnis.geraete.isEmpty || !modell.verzeichnis.leitstaende.isEmpty {
                Section {
                    ForEach(modell.verzeichnis.leitstaende) { l in
                        LeitstandEingebundenZeile(leitstand: l)
                            .swipeActions {
                                Button("Entfernen", systemImage: "trash", role: .destructive) {
                                    modell.leitstandEntfernen(l.id)
                                }
                            }
                            .contextMenu {
                                Button("Aus der App entfernen", systemImage: "trash", role: .destructive) {
                                    modell.leitstandEntfernen(l.id)
                                }
                            }
                    }
                    ForEach(modell.verzeichnis.geraete) { g in
                        EingebundenZeile(geraet: g)
                            .swipeActions {
                                Button("Entfernen", systemImage: "trash", role: .destructive) {
                                    modell.geraetEntfernen(g.id)
                                }
                            }
                            .contextMenu {
                                Button("Aus der App entfernen", systemImage: "trash", role: .destructive) {
                                    modell.geraetEntfernen(g.id)
                                }
                            }
                    }
                } header: {
                    Text("In der App eingebunden")
                } footer: {
                    Text("Die Einrichtung nimmt Geräte auf, die Suche hält ihre Adressen aktuell. Entfernen betrifft nur die App, nicht das Gerät.")
                }
            }
            Section {
                Button {
                    modell.zeigeEinrichtung = true
                } label: {
                    Label("Gerät hinzufügen", systemImage: "plus.circle")
                }
                #if os(iOS)
                NavigationLink(value: Ziel.appEinstellungen) {
                    Label("Einstellungen der App", systemImage: "gearshape")
                }
                #endif
            }
        }
        .formularStil()
        .navigationTitle("Geräte")
        .fragenKnopf("Geräte")
    }
}

extension Geraet.Art {
    var symbol: String {
        switch self {
        case .verteiler: "square.split.2x2"
        case .kessel: "flame"
        case .speicher: "cylinder.split.1x2"
        case .relais: "switch.2"
        }
    }

    var bezeichnung: String {
        switch self {
        case .verteiler: "Verteiler"
        case .kessel: "Heizungsgerät am Kessel"
        case .speicher: "Heizungsgerät am Speicher"
        case .relais: "Tasmota-Relais"
        }
    }
}

struct GeraetZeile: View {
    let geraet: Geraet

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: geraet.art.symbol)
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(geraet.ort)
                if geraet.erreichbar {
                    Text("\(geraet.adresse) · \(geraet.firmware)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                } else {
                    Text("\(geraet.adresse) · nicht erreichbar")
                        .font(.caption)
                        .foregroundStyle(Farbe.stoerung)
                }
            }
            Spacer()
            Image(systemName: geraet.erreichbar ? "circle.fill" : "xmark.octagon.fill")
                .font(geraet.erreichbar ? .caption2 : .body)
                .foregroundStyle(geraet.erreichbar ? Farbe.gut : Farbe.stoerung)
                .accessibilityLabel(geraet.erreichbar ? "erreichbar" : "nicht erreichbar")
        }
    }
}

/// Ein Gerät aus dem Verzeichnis, wie es die Einrichtung aufgenommen hat.
struct EingebundenZeile: View {
    let geraet: BekanntesGeraet

    /// Adresse ohne „http://“; der Port nur, wenn er vom üblichen abweicht.
    private var anschrift: String {
        let host = geraet.adresse.host() ?? ""
        return geraet.adresse.port.map { "\(host):\($0)" } ?? host
    }

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: Geraetesymbol.symbol(art: geraet.art, ort: geraet.ort))
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(geraet.bezeichnung)
                Text([anschrift, geraet.firmware ?? "", geraet.id].filter { !$0.isEmpty }.joined(separator: " · "))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }
}

/// Wählt die Seite passend zur Geräteart.
struct GeraetAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraetID: Geraet.ID

    var body: some View {
        if let g = modell.anlage.geraet(geraetID) {
            switch g.art {
            case .verteiler: VerteilerAnsicht(geraet: g)
            case .kessel, .speicher: HeizgeraetAnsicht(geraet: g)
            case .relais: ContentUnavailableView("Relais", systemImage: "switch.2")
            }
        }
    }
}

/// Erreichbarkeit, Adresse, Signal – oben auf jeder Geräteseite.
struct GeraetZustand: View {
    let geraet: Geraet

    var body: some View {
        Section {
            Kennwert(titel: "Zustand", wert: geraet.erreichbar ? "erreichbar" : "nicht erreichbar", farbe: geraet.erreichbar ? Farbe.gut : Farbe.stoerung)
            Kennwert(titel: "Art", wert: geraet.art.bezeichnung)
            Kennwert(titel: "Adresse", wert: "\(geraet.adresse) · \(geraet.hostname).local")
            Kennwert(titel: "WLAN", wert: Format.signal(geraet.signal))
            Kennwert(titel: "Laufzeit seit Start", wert: Format.dauer(geraet.laufzeit))
            if let frei = geraet.freierSpeicher {
                Kennwert(titel: "Freier Speicher", wert: Format.mitEinheit("\(frei / 1024)", "kB"))
            }
            Kennwert(titel: "Firmware", wert: geraet.firmware)
            Kennwert(titel: "Kennung", wert: geraet.id)
        }
    }
}

struct SystemAbschnitt: View {
    @Environment(AppModell.self) private var modell
    let geraet: Geraet
    let seiten: [Systemseite]
    @State private var frageWerksvorgabe = false
    @State private var frageNeustart = false

    var body: some View {
        Section("System") {
            ForEach(seiten, id: \.self) { seite in
                NavigationLink(value: Ziel.system(seite, geraet: geraet.id)) {
                    Label(seite.rawValue, systemImage: seite.symbol)
                }
            }
        }
        Section {
            Button {
                frageNeustart = true
            } label: {
                Label("Neu starten", systemImage: "restart")
            }
            Button(role: .destructive) {
                frageWerksvorgabe = true
            } label: {
                Label("Auf Werksvorgabe zurücksetzen …", systemImage: "arrow.counterclockwise")
            }
        }
        .confirmationDialog("\(geraet.ort) neu starten?", isPresented: $frageNeustart, titleVisibility: .visible) {
            Button("Neu starten") { modell.neustart(geraet.id) }
        } message: {
            Text("Fährt gerade ein Ventil, wird die Fahrt abgebrochen und die Stellung beim nächsten Start neu ermittelt.")
        }
        .confirmationDialog("\(geraet.ort) auf Werksvorgabe zurücksetzen?", isPresented: $frageWerksvorgabe, titleVisibility: .visible) {
            Button("Zurücksetzen", role: .destructive) { modell.werksvorgabe(geraet.id) }
        } message: {
            Text("Alle Einstellungen gehen verloren, auch WLAN-Zugang und MQTT. Die App legt vorher eine Sicherung an und startet das Gerät danach neu; es öffnet dann seinen Einrichtungs-Zugangspunkt.")
        }
    }
}

// MARK: - Verteiler

struct VerteilerAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraet: Geraet
    @State private var frageAlle: AppModell.Kanalbefehl?

    var body: some View {
        let etage = modell.anlage.etage(geraet.id)
        Form {
            GeraetZustand(geraet: geraet)

            if let etage {
                Section {
                    ForEach(etage.kanaele) { kanal in
                        NavigationLink(value: Ziel.kanal(etage: etage.id, nummer: kanal.nummer)) {
                            KanalZeile(kanal: kanal, zeigeRaum: true)
                        }
                    }
                } header: {
                    Text("Kanäle")
                } footer: {
                    Text("Je Messgruppe (1+2, 3+4, 5+6, 7+8, 9+10, 11) fährt immer nur ein Kanal.")
                }

                Section {
                    Menu {
                        Button("Alle auf") { frageAlle = .auf }
                        Button("Alle zu") { frageAlle = .zu }
                        Button("Alle anhalten") { frageAlle = .stopp }
                        Button("Alle an die Regelung") { frageAlle = .regelung }
                    } label: {
                        Label("Alle Kanäle", systemImage: "slider.vertical.3")
                    }
                    NavigationLink(value: Ziel.messfahrt(etage: etage.id)) {
                        Label("Messfahrt", systemImage: "waveform.path.ecg")
                    }
                    NavigationLink(value: Ziel.raumverwaltung(etage: etage.id)) {
                        Label("Räume und Kanäle", systemImage: "square.grid.2x2")
                    }
                    NavigationLink(value: Ziel.sensoren(geraet.id)) {
                        Label("Sensoren", systemImage: "sensor")
                    }
                }
            }

            SystemAbschnitt(geraet: geraet, seiten: [.netzwerk, .mqtt, .betrieb, .tasten, .zeit, .firmware, .sicherung])
        }
        .formularStil()
        .navigationTitle(geraet.ort)
        .fragenKnopf("Verteiler \(geraet.ort)")
        .confirmationDialog("Befehl an alle Kanäle?", isPresented: Binding(get: { frageAlle != nil }, set: { if !$0 { frageAlle = nil } }), titleVisibility: .visible) {
            Button("Ausführen") {
                if let befehl = frageAlle, let etage { modell.alleKanaele(befehl, etage: etage.id) }
                frageAlle = nil
            }
        } message: {
            Text("Auf, Zu und Anhalten setzen alle Kanäle in Handbetrieb; die Regelung greift erst nach „An die Regelung“ wieder.")
        }
    }
}

struct KanalZeile: View {
    let kanal: Kanal
    let zeigeRaum: Bool

    var body: some View {
        HStack(spacing: 12) {
            Text("\(kanal.nummer)")
                .font(.callout.monospacedDigit().weight(.semibold))
                .frame(width: 24)
                .foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 5) {
                Text(zeigeRaum ? (kanal.raum ?? "frei") : "Kanal \(kanal.nummer)")
                    .foregroundStyle(kanal.raum == nil && zeigeRaum ? .secondary : .primary)
                    .lineLimit(1)
                Stellungsbalken(wert: kanal.stellung, farbe: kanal.raum == nil ? .secondary : Farbe.waerme)
                if kanal.handbetrieb || (kanal.raum != nil && !kanal.kalibriert) || kanal.bewegung != .steht {
                    Fliesslayout {
                        if kanal.handbetrieb {
                            Zustandsmarke(text: "Handbetrieb", symbol: "hand.raised.fill", farbe: Farbe.warnung)
                        }
                        if kanal.raum != nil && !kanal.kalibriert {
                            Zustandsmarke(text: "ohne Messfahrt", symbol: "ruler", farbe: .secondary)
                        }
                        if kanal.bewegung != .steht {
                            Zustandsmarke(text: kanal.bewegung == .oeffnet ? "öffnet" : "schließt", symbol: "arrow.left.and.right", farbe: Farbe.waerme)
                        }
                    }
                }
            }
            Text(Format.prozent(kanal.stellung))
                .font(Schrift.daten)
                .foregroundStyle(Farbe.gedaempft)
                .frame(width: 48, alignment: .trailing)
        }
        .accessibilityElement(children: .combine)
    }
}

struct KanalAnsicht: View {
    @Environment(AppModell.self) private var modell
    let etageID: Etage.ID
    let nummer: Int

    var body: some View {
        if let etage = modell.anlage.etage(etageID), let kanal = etage.kanaele.first(where: { $0.nummer == nummer }) {
            Form {
                Section {
                    KanalZeile(kanal: kanal, zeigeRaum: true)
                    Kennwert(titel: "Messgruppe", wert: "\(kanal.gruppe)")
                    Kennwert(titel: "Gegenspannung", wert: "\(kanal.gegenspannung) mV")
                }
                Section {
                    HStack(spacing: 10) {
                        Button { modell.kanal(.auf, etage: etageID, nummer: nummer) } label: {
                            Label("Auf", systemImage: "arrow.up.to.line").frame(maxWidth: .infinity)
                        }
                        Button { modell.kanal(.stopp, etage: etageID, nummer: nummer) } label: {
                            Label("Stopp", systemImage: "stop.fill").frame(maxWidth: .infinity)
                        }
                        Button { modell.kanal(.zu, etage: etageID, nummer: nummer) } label: {
                            Label("Zu", systemImage: "arrow.down.to.line").frame(maxWidth: .infinity)
                        }
                    }
                    // Auf schmalen Bildschirmen passt Symbol neben Text nicht in ein Drittel der Breite.
                    .labelStyle(SymbolUeberText())
                    .buttonStyle(.glass)
                    .disabled(kanal.belegt)
                    if kanal.belegt {
                        Label("Durch eine Messfahrt belegt; bis zu ihrem Ende nimmt der Kanal keine Befehle an.", systemImage: "waveform.path.ecg")
                            .font(.callout)
                            .foregroundStyle(Farbe.warnung)
                    } else if kanal.befehlWartet {
                        Label("Wartet: In der Messgruppe \(kanal.gruppe) fährt gerade ein anderer Kanal.", systemImage: "hourglass")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                    }
                    if kanal.handbetrieb {
                        Button {
                            modell.kanal(.regelung, etage: etageID, nummer: nummer)
                        } label: {
                            Label("Wieder an die Regelung übergeben", systemImage: "arrow.uturn.backward")
                        }
                    }
                } header: {
                    Text("Handsteuerung")
                } footer: {
                    Text("Auf und Zu fahren bis zur Endlage, unabhängig von der geschätzten Stellung. Der Kanal bleibt danach in Handbetrieb.")
                }
                Section {
                    StellungWahl(stellung: kanal.stellung) { ziel in
                        modell.kanal(.stellung(ziel), etage: etageID, nummer: nummer)
                    }
                    .disabled(kanal.belegt)
                } header: {
                    Text("Zwischenstellung")
                } footer: {
                    Text("Fährt auf die gewählte Stellung, ohne den Handbetrieb zu setzen. Die Regelung kann sie beim nächsten Prüfintervall wieder ändern; für eine feste Stellung den Raum ausschalten.")
                }
                Section("Fahrzeiten und Schwellen") {
                    Kennwert(titel: "Öffnen", wert: "\(Format.zahl(kanal.fahrzeitAuf)) s")
                    Kennwert(titel: "Schließen", wert: "\(Format.zahl(kanal.fahrzeitZu)) s")
                    Kennwert(titel: "Maximal", wert: "\(Format.zahl(kanal.maximal)) s")
                    Kennwert(titel: "Sperrzeit", wert: "\(Format.zahl(kanal.sperrzeit)) s")
                    Kennwert(titel: "Auslöseschwelle", wert: "\(kanal.schwelle) mV")
                    Kennwert(titel: "Hysterese", wert: "\(kanal.hysterese) mV")
                    Kennwert(titel: "Herkunft", wert: kanal.kalibriert ? "Messfahrt" : "Vorgabe")
                    NavigationLink(value: Ziel.einstellungen("kanal", geraet: etageID, eintrag: "\(nummer)")) {
                        Label("Fahrzeiten und Schwellen bearbeiten", systemImage: "slider.horizontal.3")
                    }
                    NavigationLink(value: Ziel.messfahrt(etage: etageID)) {
                        Label("Messfahrt starten", systemImage: "waveform.path.ecg")
                    }
                }
            }
            .formularStil()
            .navigationTitle("Kanal \(nummer)")
            .fragenKnopf("\(etage.name), Kanal \(nummer)")
        }
    }
}

/// Symbol über dem Text, für Knöpfe in schmalen Spalten
struct SymbolUeberText: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        VStack(spacing: 4) {
            configuration.icon
            configuration.title.font(.callout).lineLimit(1)
        }
        .padding(.vertical, 2)
    }
}

/// Zielstellung in Schritten von 5 %, angefahren erst auf Knopfdruck
struct StellungWahl: View {
    let stellung: Double
    let anfahren: (Double) -> Void
    @State private var ziel: Double?

    var body: some View {
        let wert = ziel ?? (stellung * 20).rounded() / 20
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Ziel")
                Spacer()
                Text(Format.prozent(wert)).monospacedDigit().foregroundStyle(.tint)
            }
            Slider(value: Binding(get: { wert }, set: { ziel = ($0 * 20).rounded() / 20 }), in: 0...1)
                .accessibilityValue(Format.prozent(wert))
            Button("Stellung anfahren", systemImage: "arrow.left.and.right") {
                anfahren(wert)
                ziel = nil
            }
            .disabled(ziel == nil)
        }
    }
}

struct MessfahrtAnsicht: View {
    @Environment(AppModell.self) private var modell
    let etageID: Etage.ID
    @State private var kanalwahl: Int?

    var body: some View {
        let etage = modell.anlage.etage(etageID)
        let m = etage?.messfahrt
        Form {
            if let m {
                Section {
                    if m.werte.isEmpty {
                        HStack(spacing: 10) {
                            ProgressView().controlSize(.small)
                            Text("Die Fahrt beginnt …").foregroundStyle(.secondary)
                        }
                    } else {
                        Chart {
                            ForEach(Array(m.werte.enumerated()), id: \.offset) { i, wert in
                                LineMark(x: .value("Zeit", Double(i) * m.abtastung), y: .value("mV", wert))
                                    .foregroundStyle(by: .value("Phase", i < (m.oeffnenAb ?? Int.max) ? "Schließen" : "Öffnen"))
                            }
                            if let schwelle = m.vorschlag?.schwelle {
                                RuleMark(y: .value("Schwelle", schwelle))
                                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 3]))
                                    .foregroundStyle(Farbe.stoerung)
                                    .annotation(position: .top, alignment: .trailing) {
                                        Text("Schwelle \(Format.mitEinheit("\(schwelle)", "mV"))").font(.caption2).foregroundStyle(Farbe.stoerung)
                                    }
                            }
                        }
                        .chartForegroundStyleScale(["Schließen": Color.brown, "Öffnen": Farbe.kaelte])
                        .chartXAxisLabel("Sekunden")
                        .chartYAxisLabel("Gegenspannung in mV")
                        .frame(height: 240)
                        .padding(.vertical, 8)
                    }
                    switch m.zustand {
                    case .laeuft:
                        HStack(spacing: 10) {
                            ProgressView().controlSize(.small)
                            Text("Messfahrt läuft, \(m.werte.count) Messpunkte").foregroundStyle(.secondary)
                        }
                        Button("Abbrechen", systemImage: "stop.fill", role: .destructive) {
                            modell.messfahrtAbbrechen(etage: etageID)
                        }
                    case .fehlgeschlagen:
                        Label(m.meldung ?? "Die Messfahrt ist fehlgeschlagen.", systemImage: "xmark.octagon.fill")
                            .foregroundStyle(Farbe.stoerung)
                    case .fertig:
                        EmptyView()
                    }
                } header: {
                    Text("Kanal \(m.kanal) · \(m.raum)")
                } footer: {
                    Text("Schließen, drei Sekunden Pause, Öffnen. An jeder Endlage steigt die Gegenspannung des blockierenden Motors; daraus ergeben sich Fahrzeiten und Auslöseschwelle.")
                }

                if m.zustand == .fertig, let v = m.vorschlag {
                    Section("Ergebnis") {
                        Kennwert(titel: "Fahrzeit zu", wert: Format.mitEinheit(Format.zahl(v.fahrzeitZu), "s"))
                        Kennwert(titel: "Fahrzeit auf", wert: Format.mitEinheit(Format.zahl(v.fahrzeitAuf), "s"))
                        Kennwert(titel: "Maximallaufzeit", wert: Format.mitEinheit(Format.zahl(v.maximal), "s"))
                        Kennwert(titel: "Auslöseschwelle", wert: Format.mitEinheit("\(v.schwelle)", "mV"))
                        Kennwert(titel: "Hysterese", wert: Format.mitEinheit("\(v.hysterese)", "mV"))
                    }
                    Section {
                        Button("Werte übernehmen", systemImage: "checkmark") {
                            modell.messfahrtUebernehmen(etage: etageID)
                        }
                        Button("Verwerfen", systemImage: "trash", role: .destructive) {
                            modell.messfahrtVerwerfen(etage: etageID)
                        }
                    } footer: {
                        Text("Übernommen werden die Werte für diesen Kanal; bis dahin gelten die bisherigen.")
                    }
                }
            }

            if m?.zustand != .laeuft, let etage {
                Section {
                    Picker("Kanal", selection: $kanalwahl) {
                        Text("wählen").tag(Int?.none)
                        ForEach(etage.belegteKanaele) { k in
                            Text("\(k.nummer) · \(k.raum ?? "")\(k.kalibriert ? "" : " · ohne Messfahrt")").tag(Int?.some(k.nummer))
                        }
                    }
                    Button("Messfahrt starten", systemImage: "waveform.path.ecg") {
                        if let kanalwahl { modell.messfahrtStarten(etage: etageID, kanal: kanalwahl) }
                    }
                    .disabled(kanalwahl == nil)
                } header: {
                    Text(m == nil ? "Messfahrt" : "Neue Messfahrt")
                } footer: {
                    Text("Der Kanal fährt ganz zu und ganz auf; das dauert etwa zwei Minuten. Solange ist er für die Regelung gesperrt.")
                }
            }
        }
        .formularStil()
        .navigationTitle("Messfahrt")
        .onAppear {
            if kanalwahl == nil { kanalwahl = etage.flatMap { e in e.belegteKanaele.first { !$0.kalibriert }?.nummer } }
        }
    }
}

struct RaumverwaltungAnsicht: View {
    @Environment(AppModell.self) private var modell
    let etageID: Etage.ID
    @State private var raeume: [JSONWert] = []
    @State private var geladen = false
    @State private var entfernen: Int?

    var body: some View {
        let thermometer = (modell.anlage.geraet(etageID)?.funkthermometer ?? []).sorted { $0.signal > $1.signal }
        Form {
            if raeume.isEmpty && geladen {
                Section { Text("Noch kein Raum eingerichtet.").foregroundStyle(.secondary) }
            }
            ForEach(raeume.indices, id: \.self) { i in
                raumabschnitt(i, thermometer: thermometer)
            }
            Section {
                Button("Raum hinzufügen", systemImage: "plus") { hinzufuegen() }
                    .disabled(raeume.count >= 11)
            } footer: {
                Text("Jeder Kanal gehört zu höchstens einem Raum. Kanäle ohne Raum schließt der Verteiler einmal in der Minute, außer sie stehen in Handbetrieb.")
            }
            let doppelt = doppelteThermometer()
            if !doppelt.isEmpty {
                Section {
                    Label("Dasselbe Thermometer steht in mehreren Räumen: \(doppelt.joined(separator: ", ")). Die Firmware verhindert das nicht, regelt dann aber beide Räume nach einem Messwert.",
                          systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(Farbe.warnung)
                }
            }
        }
        .formularStil()
        .navigationTitle("Räume \(modell.anlage.etage(etageID)?.name ?? "")")
        .toolbar {
            ToolbarItem(placement: .confirmationAction) {
                Button("Speichern") { modell.raeumeSchreiben(etageID, raeume) }
                    .disabled(raeume == (modell.konfiguration(etageID)?["rooms"]?.alsListe ?? []))
            }
        }
        .onAppear {
            if !geladen {
                raeume = modell.konfiguration(etageID)?["rooms"]?.alsListe ?? []
                geladen = true
            }
        }
        .confirmationDialog("Raum entfernen?", isPresented: Binding(get: { entfernen != nil }, set: { if !$0 { entfernen = nil } }),
                            titleVisibility: .visible) {
            Button("Entfernen", role: .destructive) {
                if let i = entfernen, raeume.indices.contains(i) { raeume.remove(at: i) }
                entfernen = nil
            }
        } message: {
            Text("Seine Kanäle schließt der Verteiler danach. Wirksam wird das erst mit „Speichern“.")
        }
    }

    @ViewBuilder
    private func raumabschnitt(_ i: Int, thermometer: [Funkthermometer]) -> some View {
        let r = raeume[i]
        let name = Binding<String>(get: { raeume[i]["name"]?.alsText ?? "" }, set: { raeume[i]["name"] = .text(String($0.prefix(31))) })
        let kanaele = Set(r["channels"]?.alsListe?.compactMap(\.alsGanzzahl) ?? [])
        let mac = r["sensor_mac"]?.alsText
        Section {
            TextField("Name", text: name)
            VStack(alignment: .leading, spacing: 8) {
                Text("Kanäle").font(.subheadline)
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 6), spacing: 6) {
                    ForEach(1...11, id: \.self) { n in
                        let anderer = besitzer(n, ausser: i)
                        Toggle("\(n)", isOn: Binding(
                            get: { kanaele.contains(n) },
                            set: { an in
                                var neu = kanaele
                                if an { neu.insert(n) } else { neu.remove(n) }
                                raeume[i]["channels"] = .liste(neu.sorted().map { .zahl(Double($0)) })
                            }))
                            .toggleStyle(.button)
                            .disabled(anderer != nil)
                            .help(anderer.map { "belegt durch \($0)" } ?? "Kanal \(n)")
                    }
                }
            }
            Picker("Thermometer", selection: Binding<String>(
                get: { mac ?? "" },
                set: { raeume[i]["sensor_mac"] = $0.isEmpty ? .null : .text($0) })) {
                Text("kein Thermometer").tag("")
                ForEach(thermometer) { t in
                    Text("\(t.name) · \(Format.temperatur(t.temperatur)) · \(Format.mitEinheit("\(t.signal)", "dBm"))\(t.brauchtSchluessel ? " · \((t.schluessel ?? .fehlt).text)" : "")").tag(t.mac)
                }
                if let mac, !mac.isEmpty, !thermometer.contains(where: { Zusammenfuehrung.normalisiert($0.mac) == Zusammenfuehrung.normalisiert(mac) }) {
                    Text("\(mac) · nicht in Reichweite").tag(mac)
                }
            }
            Button("Raum entfernen", systemImage: "trash", role: .destructive) { entfernen = i }
        } header: {
            Text(r["name"]?.alsText.flatMap { $0.isEmpty ? nil : $0 } ?? "Raum \(r["id"]?.alsGanzzahl ?? i + 1)")
        } footer: {
            if mac == nil {
                Text("Ohne Thermometer wird der Raum nicht geregelt.")
            }
        }
    }

    private func besitzer(_ kanal: Int, ausser: Int) -> String? {
        for (j, r) in raeume.enumerated() where j != ausser {
            if (r["channels"]?.alsListe?.compactMap(\.alsGanzzahl) ?? []).contains(kanal) {
                return r["name"]?.alsText ?? "Raum \(j + 1)"
            }
        }
        return nil
    }

    private func doppelteThermometer() -> [String] {
        var zaehler: [String: Int] = [:]
        for r in raeume {
            guard let mac = r["sensor_mac"]?.alsText, !mac.isEmpty else { continue }
            zaehler[Zusammenfuehrung.normalisiert(mac), default: 0] += 1
        }
        return zaehler.filter { $0.value > 1 }.keys.sorted()
    }

    /// Neuer Raum mit den Vorgaben der Firmware und der kleinsten freien Kennung
    private func hinzufuegen() {
        let belegt = Set(raeume.compactMap { $0["id"]?.alsGanzzahl })
        let id = (1...11).first { !belegt.contains($0) } ?? raeume.count + 1
        var raum: JSONWert = ["id": .zahl(Double(id)), "name": "Neuer Raum", "channels": [], "sensor_mac": .null]
        for p in Parameterkatalog.gruppe("raum", .verteiler) {
            raum[p.pfad[0]] = p.vorgabe
        }
        raeume.append(raum)
    }
}

struct SensorenAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraetID: Geraet.ID
    @State private var aussenWahl: String?
    @State private var schluesselFuer: Schluesselziel?

    var body: some View {
        if let g = modell.anlage.geraet(geraetID) {
            let aussenMac = modell.konfiguration(geraetID)?["outdoor_mac"]?.alsText
            let empfangen = Set(g.funkthermometer.map { Zusammenfuehrung.normalisiert($0.mac) })
            let ohneGeraet = g.funkschluessel.filter { !empfangen.contains(Zusammenfuehrung.normalisiert($0)) }
            Form {
                Section {
                    ForEach(g.funkthermometer.sorted { $0.signal > $1.signal }) { t in
                        if t.verschluesselt {
                            // Verschlüsselte Thermometer öffnen die Eingabe des Schlüssels.
                            Button {
                                schluesselFuer = Schluesselziel(
                                    mac: t.mac, name: t.name, zustand: t.schluessel,
                                    hinterlegt: g.funkschluessel.contains { Zusammenfuehrung.normalisiert($0) == Zusammenfuehrung.normalisiert(t.mac) })
                            } label: {
                                ThermometerZeile(t: t)
                                    .contentShape(.rect)
                            }
                            .buttonStyle(.plain)
                        } else {
                            ThermometerZeile(t: t)
                        }
                    }
                    if g.funkthermometer.isEmpty {
                        Text("Noch kein Gerät empfangen.").foregroundStyle(.secondary)
                    }
                    Button("Schlüssel zu einer Adresse hinterlegen …", systemImage: "key") {
                        schluesselFuer = Schluesselziel(mac: "", name: "", zustand: nil, hinterlegt: false)
                    }
                } header: {
                    Text("Bluetooth-Thermometer")
                } footer: {
                    Text("Empfangen werden Xiaomi-Thermometer mit ATC- oder pvvx-Firmware, RuuviTags und Thermometer mit BTHome wie der Climate-Sat von camperSense. Ein verschlüsselt sendendes Thermometer liefert erst Werte, wenn sein Schlüssel hinterlegt ist; tippen Sie dazu auf das Thermometer. Raumthermometer ordnen Sie unter „Räume und Kanäle“ zu.")
                }

                if !ohneGeraet.isEmpty {
                    Section {
                        ForEach(ohneGeraet, id: \.self) { mac in
                            Button {
                                schluesselFuer = Schluesselziel(mac: mac, name: mac, zustand: nil, hinterlegt: true)
                            } label: {
                                LabeledContent(mac) {
                                    Text("nicht in Reichweite")
                                }
                                .font(.callout.monospaced())
                                .contentShape(.rect)
                            }
                            .buttonStyle(.plain)
                        }
                    } header: {
                        Text("Schlüssel ohne empfangenes Thermometer")
                    } footer: {
                        Text("Für diese Adressen liegt am Verteiler ein Schlüssel, das Thermometer ist aber gerade nicht zu empfangen. Einen nicht mehr gebrauchten Schlüssel entfernen Sie durch Antippen.")
                    }
                }

                Section {
                    let frei = g.funkthermometer.filter { $0.zuordnung == nil || $0.zuordnung == "Außenfühler" }
                    Picker("Außenfühler", selection: Binding<String>(
                        get: { aussenWahl ?? aussenMac ?? "" },
                        set: { aussenWahl = $0 })) {
                        Text("keiner").tag("")
                        ForEach(frei) { t in Text("\(t.name) (\(t.mac))").tag(t.mac) }
                        if let aussenMac, !frei.contains(where: { Zusammenfuehrung.normalisiert($0.mac) == Zusammenfuehrung.normalisiert(aussenMac) }) {
                            Text("\(aussenMac) · nicht in Reichweite").tag(aussenMac)
                        }
                    }
                    if let wahl = aussenWahl, wahl != (aussenMac ?? "") {
                        Button("Außenfühler speichern", systemImage: "checkmark") {
                            modell.aussenfuehlerSetzen(geraetID, mac: wahl.isEmpty ? nil : wahl)
                            aussenWahl = nil
                        }
                    }
                } header: {
                    Text("Außenfühler")
                } footer: {
                    Text("Seine Temperatur geht in keine Ventilstellung ein; sie wird an die Heizungsgeräte weitergereicht, damit sich Verbrauch und Wetterlage gegenüberstellen lassen. Nur ein Verteiler der Anlage sollte einen Außenfühler tragen.")
                }

                if let b = g.bordfuehler {
                    Section("Fühler im Gehäuse") {
                        Kennwert(titel: "Temperatur und Feuchte", wert: b.gueltig ? "\(Format.temperatur(b.temperatur)) · \(Format.mitEinheit(Format.zahl(b.feuchte, stellen: 0), "%"))" : "ungültig", farbe: b.gueltig ? nil : Farbe.warnung)
                        ForEach(Array(b.vorlauffuehler.enumerated()), id: \.offset) { i, wert in
                            Kennwert(titel: "Vorlauffühler \(i + 1)", wert: Format.temperatur(wert))
                        }
                    }
                }

                Section {
                    ForEach(g.tasten) { t in
                        LabeledContent(t.name) {
                            Text("Rohwert \(t.rohwert) · Schwelle \(t.schwelle)")
                                .monospacedDigit()
                                .foregroundStyle(.secondary)
                        }
                    }
                    NavigationLink(value: Ziel.einstellungen("tasten", geraet: geraetID)) {
                        Label("Schwellen einstellen", systemImage: "slider.horizontal.3")
                    }
                } header: {
                    Text("Tasten am Gehäuse")
                } footer: {
                    Text("Rohwert in Ruhe und bei Berührung ablesen und die Schwelle etwa mittig setzen. Kleinere Werte bedeuten Berührung.")
                }
            }
            .formularStil()
            .navigationTitle("Sensoren")
            .sheet(item: $schluesselFuer) { ziel in
                FunkschluesselBlatt(ort: .verteiler(geraetID), ziel: ziel)
            }
        }
    }
}

/// Ein empfangenes Bluetooth-Thermometer mit Messwerten, Empfang und Zuordnung
struct ThermometerZeile: View {
    let t: Funkthermometer

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(t.name)
                Text(t.name == t.mac ? t.formatbezeichnung : "\(t.mac) · \(t.formatbezeichnung)")
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
                if t.verschluesselt {
                    Label(t.schluessel?.text ?? "verschlüsselt",
                          systemImage: t.brauchtSchluessel ? "key.slash" : "key")
                        .font(.caption)
                        .foregroundStyle(t.schluessel == .falsch ? Farbe.stoerung : t.brauchtSchluessel ? Farbe.warnung : Farbe.gut)
                }
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 2) {
                Text("\(Format.temperatur(t.temperatur)) · \(t.feuchte.map { Format.mitEinheit(Format.zahl($0, stellen: 0), "%") } ?? "–")")
                    .monospacedDigit()
                Text("\(t.batterie.map { Format.mitEinheit("\($0)", "%") + " · " } ?? "")\(t.batterieMillivolt > 0 ? Format.mitEinheit("\(t.batterieMillivolt)", "mV") + " · " : "")\(Format.mitEinheit("\(t.signal)", "dBm"))")
                    .font(.caption)
                    .foregroundStyle((t.batterie ?? 100) < 10 ? Farbe.stoerung : .secondary)
                Text(t.zuordnung ?? "nicht zugeordnet")
                    .font(.caption)
                    .foregroundStyle(t.zuordnung == nil ? .secondary : Color.accentColor)
            }
        }
        // Die Trennlinie beginnt sonst hinter dem Symbol des Schlüsselzustands.
        .alignmentGuide(.listRowSeparatorLeading) { _ in 0 }
        .accessibilityElement(children: .combine)
        .accessibilityHint(t.verschluesselt ? "Öffnet die Eingabe des Schlüssels" : "")
    }
}

// MARK: - Heizungsgerät

struct HeizgeraetAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraet: Geraet
    @State private var frageReinigung = false

    var body: some View {
        Form {
            GeraetZustand(geraet: geraet)

            Section("Fühler") {
                NavigationLink(value: Ziel.fuehler(geraet.id)) {
                    LabeledContent {
                        Text("\(geraet.bus?.zugeordnet ?? 0) von \(geraet.bus?.gefunden ?? 0) zugeordnet")
                    } label: {
                        Label("Fühler am Bus", systemImage: "thermometer.medium")
                    }
                }
            }

            if geraet.art == .kessel {
                Section("Kessel") {
                    NavigationLink(value: Ziel.einstellungen("brenner", geraet: geraet.id)) {
                        Label("Brennererkennung", systemImage: "flame")
                    }
                    NavigationLink(value: Ziel.einstellungen("kkp", geraet: geraet.id)) {
                        Label("Kesselkreispumpe", systemImage: "fanblades")
                    }
                    NavigationLink(value: Ziel.einstellungen("speicher", geraet: geraet.id)) {
                        Label("Pufferspeicher", systemImage: "cylinder.split.1x2")
                    }
                    Button {
                        frageReinigung = true
                    } label: {
                        Label("Kessel gereinigt – ab jetzt neu messen", systemImage: "sparkles.rectangle.stack")
                    }
                }
            } else {
                Section("Speicher und Heizkreise") {
                    NavigationLink(value: Ziel.einstellungen("speicher", geraet: geraet.id)) {
                        Label("Pufferspeicher", systemImage: "cylinder.split.1x2")
                    }
                    ForEach(modell.anlage.heizkreise) { kreis in
                        NavigationLink(value: Ziel.heizkreis(kreis.nummer)) {
                            Label(kreis.name, systemImage: "arrow.triangle.2.circlepath")
                        }
                    }
                    Button {
                        modell.heizkreisAnlegen()
                    } label: {
                        Label("Heizkreis anlegen", systemImage: "plus")
                    }
                    .disabled(modell.anlage.heizkreise.count >= 4)
                }
            }

            SystemAbschnitt(
                geraet: geraet,
                seiten: geraet.art == .speicher
                    ? [.netzwerk, .mqtt, .geraet, .zeit, .bedarfsabfrage, .bus, .firmware, .sicherung]
                    : [.netzwerk, .mqtt, .geraet, .zeit, .bus, .firmware, .sicherung]
            )
        }
        .formularStil()
        .navigationTitle(geraet.ort)
        .fragenKnopf("Heizungsgerät \(geraet.ort)")
        .confirmationDialog("Reinigung auf heute setzen?", isPresented: $frageReinigung, titleVisibility: .visible) {
            Button("Reinigung vermerken") { modell.kesselGereinigt() }
        } message: {
            Text("Der bisherige Bezugspunkt der Abgasauswertung wird verworfen; die folgenden Ladungen bilden den sauberen Kessel neu ab.")
        }
    }
}

struct FuehlerAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraetID: Geraet.ID

    var body: some View {
        if let g = modell.anlage.geraet(geraetID) {
            Form {
                Section {
                    ForEach(g.fuehler) { f in
                        NavigationLink(value: Ziel.einstellungen("fuehler", geraet: geraetID, eintrag: f.rom)) {
                            VStack(alignment: .leading, spacing: 4) {
                                HStack {
                                    Text(f.rolle.isEmpty ? "nicht zugeordnet" : Parameterkatalog.rollenname(f.rolle))
                                        .font(.body.weight(.medium))
                                        .foregroundStyle(f.rolle.isEmpty ? .secondary : .primary)
                                    Spacer()
                                    Text(Format.temperatur(f.wert, stellen: 2)).monospacedDigit()
                                }
                                HStack(spacing: 10) {
                                    Text(f.rom).font(.caption.monospaced())
                                    Text("30 s: \(Format.kelvin(f.aenderung30s, stellen: 2, vorzeichen: true))")
                                        .foregroundStyle(abs(f.aenderung30s) >= 0.4 ? Farbe.waerme : .secondary)
                                    if f.korrektur != 0 { Text("Korrektur \(Format.kelvin(f.korrektur, vorzeichen: true))") }
                                    Text("\(f.fehler) Fehler")
                                        .foregroundStyle(f.fehler > 0 ? Farbe.warnung : .secondary)
                                }
                                .font(.caption)
                                .foregroundStyle(.secondary)
                            }
                            .padding(.vertical, 2)
                        }
                    }
                    if g.fuehler.isEmpty {
                        Text("Am Bus meldet sich kein Fühler. Prüfen Sie unter „1-Wire-Bus“ die Belegung und den Anschlusswiderstand.")
                            .foregroundStyle(.secondary)
                    }
                } header: {
                    Text("Fühler am Bus")
                } footer: {
                    Text("Einen Fühler findet man, indem man ihn mit der Hand erwärmt: Die Spalte „30 s“ zeigt die Änderung der letzten halben Minute. Fühler, die gerade nicht am Bus hängen, behält die App in der Zuordnung.")
                }
                Section {
                    Button("Bus neu durchsuchen", systemImage: "arrow.clockwise") { modell.fuehlerNeuSuchen(geraetID) }
                    if let bus = g.bus {
                        Kennwert(titel: "Anschluss", wert: bus.anschluesse.map { "GPIO \($0)" }.joined(separator: ", "))
                        Kennwert(titel: "Gefunden / zugeordnet", wert: "\(bus.gefunden) / \(bus.zugeordnet)")
                        Kennwert(titel: "Messrunde", wert: "\(Format.mitEinheit("\(bus.rundeMillisekunden)", "ms")) alle \(Format.mitEinheit("\(bus.abfrageSekunden)", "s"))")
                    }
                }
            }
            .formularStil()
            .navigationTitle("Fühler am Bus")
        }
    }
}
