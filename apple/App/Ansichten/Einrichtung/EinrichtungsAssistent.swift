import SwiftUI
import Anlage
import Geraeteschnittstelle
import Geraetesuche

/// Einrichtung: Geräte im Heimnetz aufnehmen, neue Geräte über ihren Zugangspunkt einbinden, den
/// Assistenten einrichten und zum Schluss alle Geräte sichern.
struct EinrichtungsAssistent: View {
    enum Schritt: Hashable {
        case neuesGeraet, ki, abschluss
    }

    @Environment(\.dismiss) private var schliessen
    @Environment(AppModell.self) private var modell
    @State private var pfad: [Schritt] = []

    var body: some View {
        let ablauf = modell.einrichtung()
        NavigationStack(path: $pfad) {
            SucheSchritt(ablauf: ablauf, pfad: $pfad)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Schließen") { schliessen() }
                    }
                }
                .navigationDestination(for: Schritt.self) { schritt in
                    switch schritt {
                    case .neuesGeraet: NeuesGeraetSchritt(ablauf: ablauf)
                    case .ki: KISchritt(pfad: $pfad)
                    case .abschluss: AbschlussSchritt(ablauf: ablauf) { schliessen() }
                    }
                }
        }
        #if os(macOS)
        .frame(minWidth: 480, minHeight: 640)
        #endif
        .onAppear { ablauf.starten() }
        .onDisappear { modell.einrichtungBeendet() }
    }
}

// MARK: - Geräte finden

private struct SucheSchritt: View {
    let ablauf: Einrichtungsablauf
    @Binding var pfad: [EinrichtungsAssistent.Schritt]
    @State private var sucheDauertLange = false

    var body: some View {
        @Bindable var ablauf = ablauf
        Form {
            Section {
                Kopf(symbol: "dot.radiowaves.left.and.right", titel: "Geräte finden",
                     text: "Die App sucht im Heimnetz nach Verteilern, Heizungsgeräten und dem Leitstand. Neue Geräte ohne WLAN öffnen ein eigenes Netz, über das die App sie einbindet.",
                     aktiv: ablauf.gefunden.isEmpty)
            }

            Section {
                if ablauf.suche.zustand == .keinZugriff {
                    Label("Die App darf das lokale Netzwerk nicht nutzen. Erlauben Sie es in den Einstellungen des Systems unter Datenschutz, Lokales Netzwerk.", systemImage: "hand.raised.fill")
                        .foregroundStyle(Farbe.warnung)
                }
                ForEach(ablauf.gefunden) { g in
                    GefundenZeile(geraet: g, bekannt: ablauf.kennt(g.id), vorgang: ablauf.aufnahme[g.id]) {
                        Task { await ablauf.aufnehmen(g) }
                    }
                }
                if ablauf.gefunden.isEmpty {
                    HStack(spacing: 10) {
                        ProgressView().controlSize(.small)
                        Text("Suche läuft …").foregroundStyle(.secondary)
                    }
                    if sucheDauertLange, ablauf.suche.zustand != .keinZugriff {
                        // Ohne Freigabe verwirft das System die Antworten stillschweigend.
                        Text("Noch kein Gerät gefunden. Prüfen Sie, ob die App das lokale Netzwerk nutzen darf (Einstellungen, Datenschutz, Lokales Netzwerk), oder geben Sie die Adresse unten von Hand ein.")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                if ablauf.offene.count > 1 {
                    Button("Alle \(ablauf.offene.count) aufnehmen", systemImage: "plus.circle") {
                        Task { await ablauf.alleAufnehmen() }
                    }
                }
            } header: {
                Text("Im Heimnetz")
            } footer: {
                if ablauf.gefunden.contains(where: \.istAttrappe) {
                    Text("„Attrappe“ kennzeichnet die Übungsgeräte auf diesem Mac; im Heimnetz sieht sie niemand.")
                }
            }

            Section {
                NavigationLink(value: EinrichtungsAssistent.Schritt.neuesGeraet) {
                    Label {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Neues Gerät einbinden")
                            Text("Gerät noch ohne WLAN oder auf Werkseinstellung zurückgesetzt")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    } icon: {
                        Image(systemName: "wifi.router").foregroundStyle(Farbe.waerme)
                    }
                }
            } header: {
                Text("Neue Geräte")
            } footer: {
                Text("Neue Verteiler öffnen das WLAN floor-heating-XXXX, neue Heizungsgeräte heizung-XXXX.")
            }

            Section {
                HStack {
                    TextField("z. B. 192.168.1.77", text: $ablauf.adresseEingabe)
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .keyboardType(.URL)
                        .textInputAutocapitalization(.never)
                        #endif
                        .onSubmit { Task { await ablauf.adresseAufnehmen() } }
                    Button("Aufnehmen") {
                        Task { await ablauf.adresseAufnehmen() }
                    }
                    .disabled(ablauf.adresseEingabe.trimmingCharacters(in: .whitespaces).isEmpty || ablauf.handeingabe == .laeuft)
                }
                Vorgangszeile(vorgang: ablauf.handeingabe, erledigt: "Gerät aufgenommen")
            } header: {
                Text("Adresse von Hand")
            } footer: {
                Text("Falls die Suche ein Gerät nicht findet, etwa weil es in einem anderen Teilnetz hängt.")
            }

            if !ablauf.verzeichnis.geraete.isEmpty || !ablauf.verzeichnis.leitstaende.isEmpty {
                Section("Eingebunden") {
                    ForEach(ablauf.verzeichnis.geraete) { g in
                        EingebundenZeile(geraet: g)
                    }
                    ForEach(ablauf.verzeichnis.leitstaende) { l in
                        LeitstandEingebundenZeile(leitstand: l)
                    }
                }
            }

            Section {
                Button {
                    pfad.append(.ki)
                } label: {
                    Label("Weiter", systemImage: "arrow.right.circle.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.glassProminent)
            }
        }
        .formularStil()
        .navigationTitle("Einrichtung")
        .task {
            try? await Task.sleep(for: .seconds(8))
            sucheDauertLange = true
        }
    }
}

private struct GefundenZeile: View {
    let geraet: GefundenesGeraet
    let bekannt: Bool
    let vorgang: Einrichtungsablauf.Vorgang?
    let aufnehmen: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: geraet.istLeitstand ? Geraetesymbol.leitstand : Geraetesymbol.symbol(art: geraet.art, ort: geraet.ort))
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 2) {
                if geraet.istAttrappe {
                    // Neben der Schaltfläche reicht der Platz nicht immer für Titel und Marke;
                    // dann rückt die Marke unter den Titel, statt ihn zu zerdrücken.
                    ViewThatFits(in: .horizontal) {
                        HStack(spacing: 6) {
                            Text(titel).fixedSize()
                            attrappenmarke
                        }
                        VStack(alignment: .leading, spacing: 4) {
                            Text(titel)
                            attrappenmarke
                        }
                    }
                } else {
                    Text(titel)
                }
                Text("\(geraet.id) · \(geraet.adresse?.host() ?? "")")
                    .font(Schrift.datenKlein)
                    .foregroundStyle(.secondary)
                if case .fehler(let meldung) = vorgang {
                    Text(meldung)
                        .font(.caption)
                        .foregroundStyle(Farbe.stoerung)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Spacer()
            if bekannt {
                Label("aufgenommen", systemImage: "checkmark.circle.fill")
                    .labelStyle(.iconOnly)
                    .foregroundStyle(Farbe.gut)
            } else if vorgang == .laeuft {
                ProgressView().controlSize(.small)
            } else {
                Button("Aufnehmen", action: aufnehmen)
                    .buttonStyle(.rahmen)
            }
        }
    }

    private var attrappenmarke: some View {
        Zustandsmarke(text: "Attrappe", symbol: "theatermasks", farbe: Farbe.gedaempft)
    }

    private var titel: String {
        let ort = geraet.ort ?? geraet.name
        if geraet.istLeitstand { return ort == "Leitstand" ? ort : "Leitstand \(ort)" }
        return geraet.art == .verteiler ? "Verteiler \(ort)" : ort
    }
}

enum Geraetesymbol {
    static let leitstand = "display"

    /// Kessel und Speicher sind dasselbe Gerät; das Symbol folgt dem Ort.
    static func symbol(art: Geraeteart?, ort: String?) -> String {
        let o = (ort ?? "").lowercased()
        return switch art {
        case .verteiler: "square.split.2x2"
        case .heizung where o.contains("speicher") || o.contains("puffer"): "cylinder.split.1x2"
        case .heizung: "flame"
        case nil: "questionmark.circle"
        }
    }
}

// MARK: - Neues Gerät

private struct NeuesGeraetSchritt: View {
    let ablauf: Einrichtungsablauf
    @Environment(\.openURL) private var oeffnen
    @Environment(\.dismiss) private var zurueck

    var body: some View {
        @Bindable var ablauf = ablauf
        Form {
            switch ablauf.phase {
            case .bereit:
                vorbereitung
            case .verbinden, .netzeSuchen:
                Section {
                    Schrittfolge(schritte: ["Mit dem Gerät verbinden", "Gerät erkennen", "Netze in Reichweite suchen"],
                                erledigt: ablauf.phase == .verbinden ? (ablauf.erkannt == nil ? 0 : 1) : 2)
                } footer: {
                    Text("Während der Suche nach Netzen ist das Gerät kurz nicht erreichbar.")
                }
            case .eingabe:
                eingabe
            case .schreiben, .warten, .verlassen:
                Section {
                    Schrittfolge(schritte: [
                        "Zugangsdaten an das Gerät übertragen",
                        "Gerät verbindet sich mit „\(ablauf.netz)“",
                        ablauf.neueAdresse.map { "Neue Adresse gemeldet: \($0)" } ?? "Neue Adresse abwarten",
                        "Einrichtungs-WLAN verlassen",
                    ], erledigt: ablauf.phase == .schreiben ? 0 : ablauf.phase == .warten ? 1 : 3)
                } footer: {
                    Text("Das dauert meist unter einer Minute.")
                }
            case .fertig:
                fertig
            case .fehler(let meldung):
                Section {
                    Label(meldung, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(Farbe.stoerung)
                        .fixedSize(horizontal: false, vertical: true)
                    Button("Erneut versuchen", systemImage: "arrow.clockwise") {
                        ablauf.erneutVersuchen()
                    }
                }
            }
        }
        .formularStil()
        .navigationTitle("Neues Gerät")
        .navigationBarBackButtonHidden(ablauf.laeuft)
        .animation(.snappy, value: ablauf.phase)
    }

    @ViewBuilder
    private var vorbereitung: some View {
        @Bindable var ablauf = ablauf
        Section {
            Kopf(symbol: "wifi.router", titel: "Gerät ohne WLAN",
                 text: "Ein Gerät ohne WLAN-Zugang öffnet ein eigenes Netz. Darüber bekommt es den Zugang zu Ihrem Heimnetz und einen Ort.",
                 aktiv: false)
        }
        Section {
            Picker("Gerät", selection: $ablauf.art) {
                ForEach(Einbindungsart.allCases, id: \.self) { art in
                    Text(art.bezeichnung).tag(art)
                }
            }
            .pickerStyle(.segmented)
            LabeledContent("WLAN des Geräts", value: "\(Einbindung.praefix(ablauf.art))XXXX")
            LabeledContent("Kennwort") {
                TextField("Kennwort des Geräte-WLANs", text: $ablauf.zugangspunktKennwort)
                    .multilineTextAlignment(.trailing)
                    .autocorrectionDisabled()
                    #if os(iOS)
                    .textInputAutocapitalization(.never)
                    #endif
            }
        } footer: {
            Text(ablauf.art == .leitstand
                 ? "Ab Werk ist das Netz des Leitstands offen; das Feld bleibt dann leer. Der Leitstand muss eingeschaltet und in der Nähe sein."
                 : "Ab Werk lautet das Kennwort „\(Einbindung.werkskennwort)“. Das Gerät muss eingeschaltet und in der Nähe sein.")
        }
        if Zugangspunkt.beitrittMoeglich {
            Section {
                startknopf("Mit dem Gerät verbinden")
            } footer: {
                if ablauf.einrichtungsadresse != Einbindung.zugangspunkt {
                    Text("Entwicklung: Einrichtung gegen \(ablauf.einrichtungsadresse.absoluteString)")
                }
            }
        } else {
            Section {
                Text("1. Wählen Sie in der Menüleiste das WLAN „\(Einbindung.praefix(ablauf.art))XXXX“.")
                Text(ablauf.zugangspunktKennwort.isEmpty
                     ? "2. Das Netz ist offen; ein Kennwort ist nicht nötig."
                     : "2. Geben Sie das Kennwort „\(ablauf.zugangspunktKennwort)“ ein.")
                Text("3. Sobald der Mac verbunden ist, erkennt die App das Gerät.")
                startknopf("Gerät erkennen")
            } header: {
                Text("Am Mac")
            } footer: {
                Text("Nach der Einrichtung wechseln Sie das WLAN wieder zurück in Ihr Heimnetz.")
            }
        }
    }

    private func startknopf(_ titel: String) -> some View {
        Button {
            Task { await ablauf.verbinden() }
        } label: {
            Label(titel, systemImage: "antenna.radiowaves.left.and.right")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.glassProminent)
        .disabled(ablauf.zugangspunktKennwort.count < 8 && !ablauf.zugangspunktKennwort.isEmpty)
    }

    @ViewBuilder
    private var eingabe: some View {
        @Bindable var ablauf = ablauf
        Section {
            Label("\(ablauf.art.bezeichnung) verbunden", systemImage: "checkmark.circle.fill")
                .foregroundStyle(Farbe.gut)
            if let id = ablauf.erkannt?.id {
                LabeledContent("Kennung", value: id)
            }
        }
        Section {
            Picker("WLAN", selection: $ablauf.netz) {
                ForEach(ablauf.netze, id: \.name) { n in
                    Text("\(n.name ?? "")  \(n.signal.map { Format.mitEinheit("\($0)", "dBm") } ?? "")").tag(n.name ?? "")
                }
            }
            SecureField("WLAN-Kennwort", text: $ablauf.kennwort)
            Button("Erneut suchen", systemImage: "arrow.clockwise") {
                Task { await ablauf.netzeSuchen() }
            }
        } header: {
            Text("Heimnetz")
        } footer: {
            Text("Das Kennwort geht nur an das Gerät; die App speichert es nicht.")
        }
        Section {
            TextField(ortsvorschlag, text: $ablauf.ort)
            if let name = ablauf.geraetename {
                LabeledContent("Gerätename im Netz", value: name)
            }
        } header: {
            Text("Ort")
        } footer: {
            Text(ablauf.art == .leitstand
                 ? "Der Leitstand behält seinen Gerätenamen; unter diesem Namen finden ihn Verteiler und Heizungsgeräte."
                 : "Der Gerätename wird wie in der Weboberfläche aus dem Ort gebildet, solange noch die Werksvorgabe gilt.")
        }
        Section {
            Button {
                Task { await ablauf.einrichten() }
            } label: {
                Label("Einrichten", systemImage: "checkmark.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.glassProminent)
            .disabled(!ablauf.eingabeVollstaendig)
        }
    }

    @ViewBuilder
    private var fertig: some View {
        Section {
            Kopf(symbol: "checkmark.seal.fill", titel: "Gerät eingebunden",
                 text: "„\(ablauf.eingebunden?.bezeichnung ?? ablauf.ort)“ ist im Heimnetz unter \(ablauf.neueAdresse ?? "") erreichbar.\(ablauf.vonHandVerbunden ? " Wechseln Sie das WLAN jetzt zurück in Ihr Heimnetz." : "")",
                 aktiv: false, farbe: Farbe.gut)
        }
        Section {
            if let url = ablauf.eingebunden?.adresse {
                Button("In der Weboberfläche weiter einrichten", systemImage: "safari") {
                    oeffnen(url)
                }
            }
            Button("Weiteres Gerät einbinden", systemImage: "plus.circle") {
                ablauf.neuBeginnen()
            }
            Button("Zurück zur Suche", systemImage: "arrow.uturn.backward") {
                ablauf.neuBeginnen()
                zurueck()
            }
        } footer: {
            switch ablauf.art {
            case .verteiler:
                Text("Räume, Thermometer und Messfahrten richten Sie in dieser Fassung noch in der Weboberfläche des Geräts ein.")
            case .heizung:
                Text("Den 1-Wire-Anschluss und die Fühler richten Sie in dieser Fassung noch in der Weboberfläche des Geräts ein.")
            case .leitstand:
                Text("Außenfühler, Funkthermometer, Anzeige und HomeKit richten Sie in der App unter Geräte › Leitstand ein.")
            }
        }
    }

    private var ortsvorschlag: String {
        switch ablauf.art {
        case .verteiler: "Etage, z. B. Erdgeschoss"
        case .heizung: "Ort, z. B. Kessel oder Pufferspeicher"
        case .leitstand: "Ort, z. B. Heizungsraum"
        }
    }
}

// MARK: - KI

private struct KISchritt: View {
    @Binding var pfad: [EinrichtungsAssistent.Schritt]

    var body: some View {
        Form {
            Section {
                Kopf(symbol: "sparkles", titel: "Assistent einrichten",
                     text: "Der Assistent liest die Anlage, erkennt Auffälligkeiten und schlägt Einstellungen vor. Ändern kann er nichts ohne Ihre Bestätigung.",
                     aktiv: false)
            }
            KIAbschnitt()
            Section {
                Button {
                    pfad.append(.abschluss)
                } label: {
                    Label("Weiter", systemImage: "arrow.right.circle.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.glassProminent)
            }
        }
        .formularStil()
        .navigationTitle("KI")
    }
}

// MARK: - Abschluss

private struct AbschlussSchritt: View {
    let ablauf: Einrichtungsablauf
    let fertig: () -> Void

    var body: some View {
        Form {
            Section {
                if ablauf.verzeichnis.geraete.isEmpty {
                    Kopf(symbol: "questionmark.circle", titel: "Noch keine Geräte",
                         text: "Ohne eingebundene Geräte zeigt die App weiter die Beispielanlage. Sie können die Einrichtung jederzeit unter Geräte fortsetzen.",
                         aktiv: false)
                } else {
                    Kopf(symbol: "checkmark.seal.fill", titel: "Einrichtung abgeschlossen",
                         text: "\(ablauf.verzeichnis.geraete.count) Geräte gehören zu dieser Anlage.",
                         aktiv: false, farbe: Farbe.gut)
                }
            }
            if !ablauf.verzeichnis.geraete.isEmpty {
                Section {
                    ForEach(ablauf.verzeichnis.geraete) { g in
                        HStack {
                            EingebundenZeile(geraet: g)
                            Spacer()
                            Vorgangssymbol(vorgang: ablauf.sicherung[g.id])
                        }
                        if case .fehler(let meldung)? = ablauf.sicherung[g.id] {
                            Text(meldung)
                                .font(.caption)
                                .foregroundStyle(Farbe.stoerung)
                        }
                    }
                } header: {
                    Text("Sicherung")
                } footer: {
                    Text("Bevor die App etwas ändert, sichert sie die Konfiguration jedes Geräts, verschlüsselt auf diesem Gerät. Die Sicherungen enthalten die WLAN-Zugangsdaten und gehen nie an die KI.")
                }
            }
            Section {
                Button(action: fertig) {
                    Label("Fertig", systemImage: "checkmark")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.glassProminent)
            }
        }
        .formularStil()
        .navigationTitle("Abschluss")
        .task { await ablauf.allesSichern() }
    }
}

// MARK: - Bausteine

private struct Kopf: View {
    let symbol: String
    let titel: String
    let text: String
    let aktiv: Bool
    var farbe: Color = .accentColor

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Image(systemName: symbol)
                .font(.largeTitle)
                .foregroundStyle(farbe)
                .symbolEffect(.variableColor.iterative, isActive: aktiv)
            Text(titel)
                .font(.title2.weight(.semibold))
            Text(text)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(.vertical, 6)
    }
}

private struct Schrittfolge: View {
    let schritte: [String]
    let erledigt: Int

    var body: some View {
        ForEach(Array(schritte.enumerated()), id: \.offset) { i, text in
            HStack(spacing: 10) {
                if i < erledigt {
                    Image(systemName: "checkmark.circle.fill").foregroundStyle(Farbe.gut)
                } else if i == erledigt {
                    ProgressView().controlSize(.small)
                } else {
                    Image(systemName: "circle").foregroundStyle(.tertiary)
                }
                Text(text).foregroundStyle(i <= erledigt ? .primary : .secondary)
            }
        }
    }
}

private struct Vorgangszeile: View {
    let vorgang: Einrichtungsablauf.Vorgang
    let erledigt: String

    var body: some View {
        switch vorgang {
        case .ruht:
            EmptyView()
        case .laeuft:
            HStack(spacing: 8) {
                ProgressView().controlSize(.small)
                Text("wird geprüft …").foregroundStyle(.secondary)
            }
        case .erledigt:
            Label(erledigt, systemImage: "checkmark.circle.fill").foregroundStyle(Farbe.gut)
        case .fehler(let meldung):
            Label(meldung, systemImage: "xmark.octagon.fill")
                .foregroundStyle(Farbe.stoerung)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

private struct Vorgangssymbol: View {
    let vorgang: Einrichtungsablauf.Vorgang?

    var body: some View {
        switch vorgang {
        case .laeuft?:
            ProgressView().controlSize(.small)
        case .erledigt?:
            Image(systemName: "lock.fill").foregroundStyle(Farbe.gut).accessibilityLabel("gesichert")
        case .fehler?:
            Image(systemName: "xmark.octagon.fill").foregroundStyle(Farbe.stoerung).accessibilityLabel("nicht gesichert")
        default:
            Image(systemName: "circle").foregroundStyle(.tertiary)
        }
    }
}
