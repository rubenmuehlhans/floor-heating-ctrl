import SwiftUI
import UniformTypeIdentifiers
import Anlage
import Verlauf
import Geraeteschnittstelle

// MARK: - Einstellungsformular

/// Formular aus dem Parameterkatalog. Werte kommen aus der Konfiguration des Geräts; geändert
/// wird ein Entwurf, der erst beim Speichern geprüft und geschrieben wird.
struct EinstellungsFormular<Zusatz: View>: View {
    @Environment(AppModell.self) private var modell
    let ziel: Einstellungsziel
    /// Abschnitt über den Einstellungen, etwa die Netzsuche; er kann den Entwurf füllen.
    let zusatz: (Binding<[Parameter.ID: JSONWert]>) -> Zusatz
    @State private var entwurf: [Parameter.ID: JSONWert] = [:]
    @State private var erweitert = false
    @State private var speichert = false
    @State private var fehler: String?
    @State private var neustartNoetig = false
    @State private var frageNeustart = false

    var body: some View {
        if let kontext = modell.einstellungskontext(ziel) {
            let pruefung = pruefung(kontext)
            Form {
                if let erklaerung = kontext.erklaerung {
                    Section {
                        Text(Format.einheitenFest(erklaerung))
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                zusatz($entwurf)
                ForEach(abschnitte(kontext.parameter.filter { !$0.erweitert }), id: \.titel) { a in
                    Section(a.titel ?? "") {
                        ForEach(a.parameter) { p in zeile(p, kontext) }
                    }
                }
                let weitere = kontext.parameter.filter(\.erweitert)
                if !weitere.isEmpty {
                    Section {
                        DisclosureGroup("Erweitert", isExpanded: $erweitert) {
                            ForEach(weitere) { p in zeile(p, kontext) }
                        }
                    }
                }
                if !pruefung.isEmpty || fehler != nil {
                    Section {
                        ForEach(pruefung, id: \.self) { f in
                            Label(f, systemImage: "exclamationmark.triangle.fill").foregroundStyle(Farbe.warnung)
                        }
                        if let fehler {
                            Label(fehler, systemImage: "xmark.octagon.fill").foregroundStyle(Farbe.stoerung)
                        }
                    }
                }
                if neustartNoetig {
                    Section {
                        Button("Jetzt neu starten", systemImage: "restart") { frageNeustart = true }
                    } footer: {
                        Text("Einige Änderungen wirken erst nach einem Neustart des Geräts.")
                    }
                }
                Section {
                    if kontext.parameter.contains(where: \.kiFreigegeben) {
                        Label("Für Vorschläge der KI freigegeben", systemImage: "sparkles")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                } footer: {
                    VStack(alignment: .leading, spacing: 4) {
                        if !kontext.weitere.isEmpty {
                            Text("Gespeichert wird auf allen Heizungsgeräten.")
                        }
                        if kontext.art == .heizung {
                            Text("Jedes Speichern setzt den 24-Stunden-Verlauf des Heizungsgeräts zurück; die App sichert das Gerät vorher.")
                        } else if !modell.istBeispiel {
                            Text("Vor dem Schreiben sichert die App die Konfiguration des Geräts.")
                        }
                    }
                }
            }
            .formularStil()
            .navigationTitle(kontext.titel)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    if speichert {
                        ProgressView().controlSize(.small)
                    } else {
                        Button("Speichern") { speichern(kontext) }
                            .disabled(entwurf.isEmpty || !pruefung.isEmpty)
                    }
                }
            }
            .confirmationDialog("Gerät neu starten?", isPresented: $frageNeustart, titleVisibility: .visible) {
                Button("Neu starten") {
                    modell.neustart(kontext.geraet)
                    neustartNoetig = false
                }
            } message: {
                Text(kontext.art == .verteiler
                     ? "Fährt gerade ein Ventil, wird die Fahrt abgebrochen und die Stellung beim nächsten Start neu ermittelt."
                     : "Die Pumpen bleiben während des Neustarts im letzten Zustand.")
            }
            .fragenKnopf(kontext.titel)
        } else {
            ContentUnavailableView("Einstellungen nicht verfügbar", systemImage: "slider.horizontal.3",
                                   description: Text("Das Gerät ist nicht eingebunden oder seine Konfiguration noch nicht gelesen."))
        }
    }

    init(ziel: Einstellungsziel, @ViewBuilder zusatz: @escaping (Binding<[Parameter.ID: JSONWert]>) -> Zusatz) {
        self.ziel = ziel
        self.zusatz = zusatz
    }

    private struct Abschnitt {
        var titel: String?
        var parameter: [Parameter]
    }

    private func abschnitte(_ parameter: [Parameter]) -> [Abschnitt] {
        var liste: [Abschnitt] = []
        for p in parameter {
            if let i = liste.firstIndex(where: { $0.titel == p.abschnitt }) {
                liste[i].parameter.append(p)
            } else {
                liste.append(Abschnitt(titel: p.abschnitt, parameter: [p]))
            }
        }
        return liste
    }

    private func istwert(_ p: Parameter, _ k: AppModell.Einstellungskontext) -> JSONWert? {
        let quelle: JSONWert?
        if let liste = p.ort.liste {
            quelle = k.konfiguration[liste]?.alsListe?.first { $0[p.ort.schluessel] == k.eintrag }
        } else {
            quelle = k.konfiguration
        }
        return p.pfad.reduce(quelle) { acc, t in Int(t).map { acc?[$0] } ?? acc?[t] }
    }

    private func binding(_ p: Parameter, _ k: AppModell.Einstellungskontext) -> Binding<JSONWert> {
        Binding(
            get: { entwurf[p.id] ?? istwert(p, k) ?? (p.istKennwort ? "" : p.vorgabe) },
            set: { neu in
                fehler = nil
                if neu == istwert(p, k) || (p.istKennwort && neu == "") {
                    entwurf[p.id] = nil
                } else {
                    entwurf[p.id] = neu
                }
            })
    }

    private func zeile(_ p: Parameter, _ k: AppModell.Einstellungskontext) -> some View {
        ParameterZeile(parameter: p, wert: binding(p, k), geaendert: entwurf[p.id] != nil,
                       gesetzt: kennwortGesetzt(p, k))
    }

    private func kennwortGesetzt(_ p: Parameter, _ k: AppModell.Einstellungskontext) -> Bool? {
        guard p.istKennwort, let feld = p.pfad.last else { return nil }
        let gruppe = p.pfad.dropLast().reduce(Optional(k.konfiguration)) { $0?[$1] }
        let eintrag = p.ort.liste.flatMap { l in k.konfiguration[l]?.alsListe?.first { $0[p.ort.schluessel] == k.eintrag } }
        let ort = p.ort == .geraet ? gruppe : p.pfad.dropLast().reduce(eintrag) { $0?[$1] }
        return ort?[feld == "ap_pass" ? "ap_pass_set" : "pass_set"]?.alsBool
    }

    /// Dieselben Prüfungen wie die Firmware, auf dem Entwurf
    private func pruefung(_ k: AppModell.Einstellungskontext) -> [String] {
        guard !entwurf.isEmpty else { return [] }
        let aenderungen = entwurf.compactMap { id, wert in
            Parameterkatalog.parameter(id).map { Wertaenderung($0, eintrag: k.eintrag, wert: wert) }
        }
        let neu = Schreibweg.zusammengefuehrt(k.konfiguration, Schreibweg.teil(aenderungen, konfiguration: k.konfiguration))
        let vorher = Set(Schreibweg.pruefen(k.konfiguration, k.art))
        // Nur, was die Änderung neu verursacht; alte Fehler meldet das Gerät beim Speichern selbst.
        return Schreibweg.pruefen(neu, k.art).filter { !vorher.contains($0) }
    }

    private func speichern(_ k: AppModell.Einstellungskontext) {
        speichert = true
        let werte = entwurf
        Task {
            do {
                let wirkungen = try await modell.einstellungenSpeichern(k, werte)
                entwurf = [:]
                neustartNoetig = wirkungen.contains(.neustart)
                modell.melden(modell.istBeispiel ? "Im Beispiel gespeichert." : "Gespeichert und zurückgelesen.")
            } catch {
                fehler = error.localizedDescription
            }
            speichert = false
        }
    }
}

extension EinstellungsFormular where Zusatz == EmptyView {
    init(ziel: Einstellungsziel) {
        self.init(ziel: ziel) { _ in EmptyView() }
    }
}

/// Eine Zeile je Einstellung, nach ihrer Art
struct ParameterZeile: View {
    let parameter: Parameter
    @Binding var wert: JSONWert
    let geaendert: Bool
    /// Bei Kennwörtern: ist eines gespeichert?
    let gesetzt: Bool?
    @State private var zeigeHilfe = false

    var body: some View {
        let p = parameter
        switch p.art {
        case .schalter:
            Toggle(isOn: Binding(get: { wert.alsBool ?? false }, set: { wert = .bool($0) })) { kopf }
        case .auswahl(let wahl):
            Picker(selection: Binding(get: { wert }, set: { wert = $0 })) {
                ForEach(wahl, id: \.wert) { w in Text(w.bezeichnung).tag(w.wert) }
                if !wahl.contains(where: { $0.wert == wert }) { Text(wert.kompakt).tag(wert) }
            } label: { kopf }
        case .text(let laenge):
            textzeile { ausrichtung in
                TextField("nicht gesetzt", text: Binding(get: { wert.alsText ?? "" }, set: { wert = .text(String($0.prefix(laenge))) }))
                    .multilineTextAlignment(ausrichtung)
                    .autocorrectionDisabled()
                    #if os(iOS)
                    .textInputAutocapitalization(.never)
                    #endif
            }
        case .kennwort:
            textzeile { ausrichtung in
                SecureField(gesetzt == true ? "gespeichert" : "nicht gesetzt",
                            text: Binding(get: { wert.alsText ?? "" }, set: { wert = .text($0) }))
                    .multilineTextAlignment(ausrichtung)
            }
        case .zahl(let bereich, let schritt, let stellen):
            zahlzeile(wert: wert.alsZahl ?? 0, bereich: bereich, schritt: schritt, stellen: stellen) { wert = .zahl($0) }
        case .ganzzahl(let bereich, let schritt):
            zahlzeile(wert: wert.alsZahl ?? 0, bereich: Double(bereich.lowerBound)...Double(bereich.upperBound),
                      schritt: Double(schritt), stellen: 0) { wert = .zahl($0.rounded()) }
        case .millisekunden(let bereich, let schritt):
            zahlzeile(wert: (wert.alsZahl ?? 0) / 1000, bereich: bereich, schritt: schritt, stellen: 1) {
                wert = .zahl(($0 * 1000).rounded())
            }
        }
    }

    private var kopf: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                // Geändert: in der Akzentfarbe statt fett, damit die Zeile ihre Breite behält.
                Text(parameter.bezeichnung)
                    .foregroundStyle(geaendert ? AnyShapeStyle(.tint) : AnyShapeStyle(.primary))
                if parameter.kiFreigegeben {
                    Image(systemName: "sparkles")
                        .font(.caption2)
                        .foregroundStyle(.tint)
                        .accessibilityLabel("für Vorschläge der KI freigegeben")
                }
                Button {
                    zeigeHilfe.toggle()
                } label: {
                    Image(systemName: "info.circle").foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Erklärung")
                .popover(isPresented: $zeigeHilfe) {
                    Text(Format.einheitenFest(parameter.hilfe))
                        .padding()
                        .frame(idealWidth: 320)
                        .fixedSize(horizontal: false, vertical: true)
                        .presentationCompactAdaptation(.popover)
                }
            }
            if parameter.wirkung == .neustart {
                Text("wirkt nach einem Neustart")
                    .font(.caption)
                    .foregroundStyle(Farbe.warnung)
            } else if parameter.wirkung == .neuverbindung {
                Text("das Gerät verbindet sich neu")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    /// Bezeichnung und Feld nebeneinander, wenn die Bezeichnung ungekürzt passt, sonst
    /// untereinander. Die feste Idealbreite hält die Wahl stabil, während getippt wird; sonst
    /// wechselte die Zeile mitten im Wort die Form, und das Feld verlöre den Fokus.
    private func textzeile<Feld: View>(@ViewBuilder _ feld: @escaping (TextAlignment) -> Feld) -> some View {
        ViewThatFits(in: .horizontal) {
            HStack {
                kopf.layoutPriority(1)
                Spacer(minLength: 8)
                feld(.trailing).frame(minWidth: 80, idealWidth: 130, maxWidth: .infinity)
            }
            VStack(alignment: .leading, spacing: 6) {
                kopf
                feld(.leading)
            }
        }
    }

    private func zahlzeile(wert: Double, bereich: ClosedRange<Double>, schritt: Double, stellen: Int,
                           setzen: @escaping (Double) -> Void) -> some View {
        // Auf die Stellen des Parameters gerundet: Der Stepper summiert sonst Gleitkommafehler auf,
        // und aus 1 + 0,1 + 0,1 würde 1,2000000000000002.
        let faktor = pow(10, Double(max(stellen, 0)))
        let zahl = Binding<Double>(get: { wert }, set: { neu in
            setzen((min(max(neu, bereich.lowerBound), bereich.upperBound) * faktor).rounded() / faktor)
        })
        // Die Eingabe wird nie gestaucht; passt sie nicht neben die Bezeichnung, rückt sie darunter.
        let eingabe = HStack(spacing: 8) {
            TextField(parameter.bezeichnung, value: zahl, format: .number.precision(.fractionLength(0...max(stellen, 0))).locale(Format.deutsch))
                .multilineTextAlignment(.trailing)
                .font(.body.monospacedDigit().weight(.semibold))
                .foregroundStyle(.tint)
                .frame(width: 80)
                #if os(iOS)
                .keyboardType(stellen > 0 || bereich.lowerBound < 0 ? .numbersAndPunctuation : .numberPad)
                #endif
            if !parameter.einheit.isEmpty {
                Text(parameter.einheit).foregroundStyle(.secondary)
            }
            Stepper("", value: zahl, in: bereich, step: schritt)
                .labelsHidden()
        }
        .fixedSize()
        return VStack(alignment: .leading, spacing: 6) {
            ViewThatFits(in: .horizontal) {
                HStack {
                    kopf.layoutPriority(1)
                    Spacer(minLength: 8)
                    eingabe
                }
                VStack(alignment: .leading, spacing: 6) {
                    kopf
                    HStack {
                        Spacer(minLength: 0)
                        eingabe
                    }
                }
            }
            Text("Vorgabe \(Format.zahl(vorgabe, stellen: stellen)) · \(Format.zahl(bereich.lowerBound, stellen: stellen)) bis \(Format.zahl(bereich.upperBound, stellen: stellen))")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    private var vorgabe: Double {
        if case .millisekunden = parameter.art { return (parameter.vorgabe.alsZahl ?? 0) / 1000 }
        return parameter.vorgabe.alsZahl ?? 0
    }
}

// MARK: - Systemseiten

/// Seiten unter „System“ eines Geräts. Die meisten sind Formulare aus dem Katalog; dazu kommen
/// Netzsuche, Schutzfahrt, Busdurchsuchung, Firmware und Sicherung.
struct SystemAnsicht: View {
    @Environment(AppModell.self) private var modell
    let seite: Systemseite
    let geraetID: Geraet.ID

    var body: some View {
        let g = modell.anlage.geraet(geraetID)
        switch seite {
        case .netzwerk:
            EinstellungsFormular(ziel: .init(gruppe: "netz", geraet: geraetID)) { entwurf in
                Section {
                    Kennwert(titel: "Adresse", wert: g?.adresse ?? "–")
                    Kennwert(titel: "Signal", wert: Format.signal(g?.signal))
                    if let name = g?.hostname, !name.isEmpty {
                        Kennwert(titel: "Im Netz als", wert: "\(name).local")
                    }
                }
                NetzsucheAbschnitt(geraetID: geraetID, entwurf: entwurf)
            }
        case .mqtt:
            EinstellungsFormular(ziel: .init(gruppe: "mqtt", geraet: geraetID))
        case .betrieb:
            EinstellungsFormular(ziel: .init(gruppe: "betrieb", geraet: geraetID))
        case .tasten:
            EinstellungsFormular(ziel: .init(gruppe: "tasten", geraet: geraetID)) { _ in
                if let tasten = g?.tasten, !tasten.isEmpty {
                    Section("Rohwerte jetzt") {
                        ForEach(tasten) { t in
                            LabeledContent(t.name) {
                                Text("\(t.rohwert)\(t.rohwert > 0 && t.rohwert < t.schwelle ? " · berührt" : "")")
                                    .monospacedDigit()
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }
        case .geraet:
            EinstellungsFormular(ziel: .init(gruppe: "geraet", geraet: geraetID))
        case .zeit:
            EinstellungsFormular(ziel: .init(gruppe: "zeit", geraet: geraetID)) { _ in
                if g?.art == .verteiler {
                    SchutzfahrtAbschnitt(etageID: geraetID)
                }
            }
        case .bus:
            EinstellungsFormular(ziel: .init(gruppe: "bus", geraet: geraetID)) { _ in
                if let bus = g?.bus {
                    Section {
                        Kennwert(titel: "Gefunden / zugeordnet", wert: "\(bus.gefunden) / \(bus.zugeordnet)")
                        Kennwert(titel: "Messrunde", wert: "\(Format.mitEinheit("\(bus.rundeMillisekunden)", "ms")) alle \(Format.mitEinheit("\(bus.abfrageSekunden)", "s"))")
                        Button("Bus neu durchsuchen", systemImage: "arrow.clockwise") {
                            modell.fuehlerNeuSuchen(geraetID)
                        }
                    }
                }
            }
        case .bedarfsabfrage:
            EinstellungsFormular(ziel: .init(gruppe: "bedarf", geraet: geraetID)) { _ in
                BedarfsquellenAbschnitt(geraetID: geraetID)
            }
        case .firmware:
            FirmwareAnsicht(geraetID: geraetID)
        case .sicherung:
            SicherungAnsicht(geraetID: geraetID)
        }
    }
}

/// Netze in Reichweite des Geräts; die Auswahl trägt den Namen ins Formular ein.
struct NetzsucheAbschnitt: View {
    @Environment(AppModell.self) private var modell
    let geraetID: String
    @Binding var entwurf: [Parameter.ID: JSONWert]
    @State private var netze: [Netzsuche.Netz] = []
    @State private var sucht = false

    var body: some View {
        let art = modell.geraeteart(geraetID) ?? .verteiler
        let ssid = "\(art == .verteiler ? "verteiler" : "heizung")/wifi.ssid"
        Section {
            Button {
                sucht = true
                Task {
                    netze = await modell.netzeSuchen(geraetID)
                    sucht = false
                }
            } label: {
                HStack {
                    Label(sucht ? "sucht …" : "Netze suchen", systemImage: "wifi")
                    if sucht { Spacer(); ProgressView().controlSize(.small) }
                }
            }
            .disabled(sucht)
            ForEach(netze, id: \.self) { n in
                Button {
                    entwurf[ssid] = .text(n.name ?? "")
                } label: {
                    HStack {
                        Text(n.name ?? "")
                        Spacer()
                        Text("\(n.signal.map { Format.mitEinheit("\($0)", "dBm") } ?? "") · \(n.verschluesselt == false ? "offen" : "gesichert")")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                .buttonStyle(.plain)
            }
        } footer: {
            Text("Während der Suche ist das Gerät kurz nicht erreichbar. Ein Tipp auf ein Netz trägt es unten ein.")
        }
    }
}

/// Stand und Befehle der Schutzfahrt eines Verteilers
struct SchutzfahrtAbschnitt: View {
    @Environment(AppModell.self) private var modell
    let etageID: String
    @State private var frageAlle = false

    var body: some View {
        if let e = modell.anlage.etage(etageID) {
            let sf = e.schutzfahrt
            Section {
                Kennwert(titel: "Stand", wert: sf.laeuft ? "läuft" : terminText(sf))
                Button("Jetzt fahren", systemImage: "play") { modell.schutzfahrt(etageID, .jetzt) }
                    .disabled(sf.laeuft)
                Button("Alle Kreise fahren", systemImage: "play.square.stack") { frageAlle = true }
                    .disabled(sf.laeuft)
                Button("Abbrechen", systemImage: "stop", role: .destructive) { modell.schutzfahrt(etageID, .abbrechen) }
            } header: {
                Text("Schutzfahrt")
            } footer: {
                Text("„Jetzt fahren“ übergeht Kreise, die sich seit der letzten Fahrt bewegt haben. Beim Abbrechen laufen angefangene Fahrten zu Ende.")
            }
            .confirmationDialog("Alle Kreise fahren?", isPresented: $frageAlle, titleVisibility: .visible) {
                Button("Alle fahren") { modell.schutzfahrt(etageID, .alle) }
            } message: {
                Text("Alle elf Kreise fahren einmal auf Anschlag auf und zu. Das dauert rund zwanzig Minuten und stört die Regelung so lange.")
            }
        }
    }

    private func terminText(_ sf: Schutzfahrt) -> String {
        guard let tag = sf.wochentag else { return "kein Termin" }
        switch sf.tageBis {
        case .none, .some(-1): return "\(Format.wochentag(tag)) \(sf.stunde) Uhr, Uhr des Geräts noch nicht gestellt"
        case .some(0): return "heute \(sf.stunde) Uhr"
        case .some(7): return "heute erledigt, nächste am \(Format.wochentag(tag))"
        case .some(let n): return "in \(n) \(n == 1 ? "Tag" : "Tagen"), \(Format.wochentag(tag)) \(sf.stunde) Uhr"
        }
    }
}

/// Die Verteiler, deren Bedarf ein Heizungsgerät abfragt
struct BedarfsquellenAbschnitt: View {
    @Environment(AppModell.self) private var modell
    let geraetID: String

    var body: some View {
        let quellen = modell.betrieb.staende[geraetID]?.heizgeraet?.bedarfsquellen ?? []
        if !quellen.isEmpty {
            Section("Verteiler") {
                ForEach(Array(quellen.enumerated()), id: \.offset) { _, q in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(q.ort ?? q.id ?? "")
                            Text("\(q.adresse ?? "–") · \(q.offeneKanaele ?? 0) Kreise offen\(q.gesehen == true ? " · vor \(Format.dauer(q.alterS ?? 0))" : " · nie erreicht")")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        Zustandsmarke(text: q.bedarf == true ? "Bedarf" : "kein Bedarf", symbol: q.bedarf == true ? "flame" : "minus",
                                      farbe: q.bedarf == true ? Farbe.waerme : Farbe.gedaempft)
                    }
                }
            }
        }
    }
}

// MARK: - Einstellungen der App

struct AppEinstellungenAnsicht: View {
    @Environment(AppModell.self) private var modell
    @State private var zeigeImport = false
    @State private var frageLoeschen = false
    @State private var umfang: Verlaufsumfang?
    @State private var ereignisse: (tage: Int, ereignisse: Int, groesse: Int)?

    var body: some View {
        @Bindable var modell = modell
        Form {
            KIAbschnitt()

            VerbrauchAbschnitt()

            Section {
                Toggle("Störungen", isOn: $modell.mitteilungStoerungen)
                Toggle("Warnungen", isOn: $modell.mitteilungWarnungen)
                Toggle("Hinweise", isOn: $modell.mitteilungHinweise)
                switch modell.mitteilungsstatus {
                case .notDetermined:
                    Button("Mitteilungen erlauben …", systemImage: "bell.badge") {
                        Task { await modell.mitteilungenErlauben() }
                    }
                case .denied:
                    Label("Mitteilungen sind in den Systemeinstellungen ausgeschaltet.", systemImage: "bell.slash")
                        .foregroundStyle(Farbe.warnung)
                    Button("Systemeinstellungen öffnen", systemImage: "gear") { Systemeinstellungen.mitteilungenOeffnen() }
                default:
                    EmptyView()
                }
            } header: {
                Text("Mitteilungen")
            } footer: {
                Text("Die App meldet einen Befund einmal, sobald er ansteht, und nimmt die Mitteilung zurück, wenn er erledigt ist. Einzelne Befunde lassen sich in der Liste der Befunde stumm schalten.\(Systemeinstellungen.hintergrundhinweis)")
            }

            Section {
                #if os(macOS)
                Toggle("Anlage dauerhaft aufzeichnen", isOn: $modell.aufzeichnenAufDemMac)
                AnmeldeobjektZeile()
                #endif
                Kennwert(titel: "Gespeicherter Verlauf", wert: umfangstext)
                if let e = ereignisse, e.ereignisse > 0 {
                    Kennwert(titel: "Ereignisse vom Leitstand", wert: "\(e.ereignisse) an \(e.tage) \(e.tage == 1 ? "Tag" : "Tagen")")
                }
                if let anteil = modell.importFortschritt {
                    ProgressView(value: anteil) {
                        Text("Mitschnitt wird gelesen …")
                    } currentValueLabel: {
                        Text(Format.prozent(anteil))
                    }
                } else {
                    Button {
                        zeigeImport = true
                    } label: {
                        Label("Mitschnitt importieren …", systemImage: "square.and.arrow.down")
                    }
                    .disabled(modell.verlaufsspeicher == nil)
                }
                NavigationLink(value: Ziel.analysepaket) {
                    Label("Analysepaket erstellen …", systemImage: "shippingbox")
                }
                .disabled((umfang?.bloecke ?? 0) == 0)
                Button(role: .destructive) {
                    frageLoeschen = true
                } label: {
                    Label("Verlauf löschen …", systemImage: "trash")
                }
                .disabled((umfang?.bloecke ?? 0) == 0 || modell.importFortschritt != nil)
            } header: {
                Text("Daten")
            } footer: {
                Text("Den Verlauf speichert die App nur auf diesem Gerät, im Raster von fünf Minuten. Ist ein Leitstand eingerichtet, übernimmt sie dessen Verlauf und Ereignisse von der Karte. Ein Mitschnitt im Format von messungen/verlauf.jsonl füllt Lücken; vorhandene Werte überschreibt er nicht.")
            }

            Section {
                Kennwert(titel: "Fassung", wert: Self.fassung)
                Link(destination: AppModell.datenschutzseite) {
                    Label("Datenschutzerklärung", systemImage: "hand.raised")
                }
            }
        }
        .formularStil()
        .navigationTitle("Einstellungen")
        .task(id: modell.verlaufsrevision) {
            umfang = await modell.verlaufsumfang()
            ereignisse = await modell.ereignisumfang()
        }
        .task { await modell.mitteilungsstatusLesen() }
        .fileImporter(isPresented: $zeigeImport, allowedContentTypes: [.json, .plainText, .data]) { ergebnis in
            guard case .success(let url) = ergebnis else { return }
            Task {
                await modell.mitschnittImportieren(url)
                umfang = await modell.verlaufsumfang()
            }
        }
        .confirmationDialog("Gespeicherten Verlauf löschen?", isPresented: $frageLoeschen, titleVisibility: .visible) {
            Button("Löschen", role: .destructive) {
                modell.verlaufLoeschen()
                umfang = nil
                ereignisse = nil
            }
        } message: {
            Text("Der Verlauf aller Geräte auf diesem Gerät geht verloren, auch importierte Mitschnitte und Ereignisse. Die Heizungsgeräte behalten ihren eigenen 24-Stunden-Verlauf; was auf der Karte des Leitstands liegt, übernimmt die App danach erneut.")
        }
    }

    static let fassung: String = {
        let info = Bundle.main.infoDictionary
        let version = info?["CFBundleShortVersionString"] as? String ?? "–"
        let bau = info?["CFBundleVersion"] as? String ?? ""
        return bau.isEmpty ? version : "\(version) (\(bau))"
    }()

    private var umfangstext: String {
        guard let u = umfang, u.tage > 0, let von = u.von, let bis = u.bis else { return "noch keiner" }
        let groesse = u.groesse < 1_048_576
            ? Format.mitEinheit("\(max(1, u.groesse / 1024))", "kB")
            : Format.mitEinheit(Format.zahl(Double(u.groesse) / 1_048_576, stellen: 1), "MB")
        return "\(Format.tag(von)) bis \(Format.tag(bis.addingTimeInterval(-1))), \(u.tage) \(u.tage == 1 ? "Tag" : "Tage") mit Werten · \(groesse)"
    }
}

/// Modellwahl und API-Schlüssel; in den Einstellungen und im Einrichtungsassistenten.
struct KIAbschnitt: View {
    @Environment(AppModell.self) private var modell
    @State private var eingabe = ""
    @State private var zeigeZustimmung = false

    var body: some View {
        @Bindable var modell = modell
        Section {
            Picker("Modell", selection: $modell.kiModell) {
                ForEach(AppModell.KIModell.allCases) { m in
                    Text(modell.kiName(m)).tag(m)
                }
            }
            // Am Picker, weil die Zeilen darunter wechseln: Mit dem gesicherten Schlüssel ist
            // Claude bereit, und vor der ersten Anfrage mit Daten kommt die Zustimmung.
            .claudeZustimmung($zeigeZustimmung)
            .onChange(of: modell.schluesselEnde) { alt, neu in
                if alt == nil, neu != nil, modell.kiModell.istClaude, modell.claudeZustimmung == nil {
                    zeigeZustimmung = true
                }
            }
            Text(modell.kiModell.erklaerung)
                .font(.caption)
                .foregroundStyle(.secondary)
            if !modell.kiModell.istClaude, let hindernis = modell.kiHindernis {
                Label(hindernis, systemImage: "exclamationmark.circle")
                    .font(.callout)
                    .foregroundStyle(Farbe.warnung)
            }
            Toggle("Lagebericht der KI von selbst", isOn: $modell.lageberichtAutomatisch)
            if modell.kiModell.istClaude {
                if let ende = modell.schluesselEnde {
                    LabeledContent("API-Schlüssel", value: "hinterlegt, endet auf …\(ende)")
                    verbindungsstand
                    Button("Verbindung prüfen", systemImage: "checkmark.shield") {
                        modell.verbindungPruefen()
                    }
                    .disabled(modell.verbindung == .pruefe)
                    Button("Schlüssel entfernen", systemImage: "trash", role: .destructive) {
                        modell.schluesselEntfernen()
                    }
                    if let seit = modell.claudeZustimmung {
                        LabeledContent("Übertragung an Anthropic", value: "zugestimmt am \(Format.tag(seit))")
                        Button("Zustimmung widerrufen", systemImage: "hand.raised.slash", role: .destructive) {
                            modell.claudeZustimmungWiderrufen()
                        }
                    } else {
                        Button("Übertragung an Anthropic zustimmen …", systemImage: "hand.raised") {
                            zeigeZustimmung = true
                        }
                    }
                } else {
                    SecureField("API-Schlüssel von Anthropic", text: $eingabe)
                        .autocorrectionDisabled()
                        .onSubmit(sichern)
                    Button("Schlüssel sichern", systemImage: "key", action: sichern)
                        .disabled(eingabe.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
            }
        } header: {
            Text("KI")
        } footer: {
            Text(fusszeile(modell.kiModell))
        }
    }

    private func sichern() {
        modell.schluesselSpeichern(eingabe)
        eingabe = ""
    }

    private func fusszeile(_ m: AppModell.KIModell) -> String {
        let daten = switch m {
        case .claude, .claudeSonnet, .claudeHaiku:
            "Der Schlüssel liegt im Schlüsselbund dieses Geräts. An Anthropic gehen Messwerte, Einstellungen, Raumnamen und das Handbuch, nie Kennwörter, WLAN-Zugangsdaten oder Relaisadressen."
        case .privateCloud:
            "An Private Cloud Compute gehen Messwerte, Einstellungen und Raumnamen, nie Kennwörter, WLAN-Zugangsdaten oder Relaisadressen; Apple verwirft die Daten nach der Anfrage."
        case .geraet:
            "Das Modell rechnet auf diesem Gerät; keine Daten verlassen es. Es hat wenig Kontext, liest Abschnitte des Handbuchs einzeln nach und eignet sich für kurze Auskünfte."
        }
        return daten + " Ändern kann die KI nichts selbst: Sie legt Vorschläge an, die Sie übernehmen oder verwerfen. Von selbst erstellt sie den Lagebericht bei geöffneter App höchstens alle sechs Stunden und nach neuen Befunden."
    }

    @ViewBuilder
    private var verbindungsstand: some View {
        switch modell.verbindung {
        case .ungeprueft:
            EmptyView()
        case .pruefe:
            HStack(spacing: 8) {
                ProgressView().controlSize(.small)
                Text("Verbindung wird geprüft …").foregroundStyle(.secondary)
            }
        case .verbunden:
            Label("Verbunden mit \(modell.modellwahl.name)", systemImage: "checkmark.circle.fill")
                .foregroundStyle(Farbe.gut)
        case .fehler(let meldung):
            Label(meldung, systemImage: "xmark.octagon.fill")
                .foregroundStyle(Farbe.stoerung)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

/// Wege in die Systemeinstellungen; ändern kann dort nur, wer das Gerät bedient.
enum Systemeinstellungen {
    static func mitteilungenOeffnen() {
        #if os(iOS)
        if let url = URL(string: UIApplication.openNotificationSettingsURLString) {
            UIApplication.shared.open(url)
        }
        #else
        if let url = URL(string: "x-apple.systempreferences:com.apple.Notifications-Settings.extension") {
            NSWorkspace.shared.open(url)
        }
        #endif
    }

    static var hintergrundhinweis: String {
        #if os(iOS)
        " Im Hintergrund fragt iOS die Geräte nur gelegentlich ab, wann genau, entscheidet das System."
        #else
        " Auf dem Mac fragt die App ab, solange sie läuft, auch ohne Fenster in der Menüleiste."
        #endif
    }
}

#if os(macOS)
import ServiceManagement

/// „Beim Anmelden öffnen“, damit der Mac die Anlage ohne Zutun aufzeichnet
struct AnmeldeobjektZeile: View {
    @State private var status = SMAppService.mainApp.status
    @State private var fehler: String?

    var body: some View {
        Toggle("Beim Anmelden öffnen", isOn: Binding(
            get: { status == .enabled || status == .requiresApproval },
            set: { an in
                do {
                    if an { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
                    fehler = nil
                } catch {
                    fehler = error.localizedDescription
                }
                status = SMAppService.mainApp.status
            }))
        if status == .requiresApproval {
            Text("Noch zu bestätigen in den Systemeinstellungen unter Allgemein › Anmeldeobjekte.")
                .font(.caption)
                .foregroundStyle(Farbe.warnung)
        }
        if let fehler {
            Text(fehler).font(.caption).foregroundStyle(Farbe.stoerung)
        }
    }
}
#endif

/// Token je Modell im laufenden Monat; bei Claude mit dem Anteil aus dem Zwischenspeicher, der
/// einen Bruchteil kostet.
struct VerbrauchAbschnitt: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        Section {
            let monat = modell.ablage.verbrauch(monat: .now)
            if monat.isEmpty {
                Text("Noch keine Anfrage in diesem Monat.").foregroundStyle(.secondary)
            }
            ForEach(monat) { v in
                VStack(alignment: .leading, spacing: 4) {
                    Text(v.modell).font(.subheadline.weight(.medium))
                    Kennwert(titel: "Anfragen", wert: Format.zahl(Double(v.anfragen), stellen: 0))
                    Kennwert(titel: "Gelesen", wert: Self.token(v.eingabe) + (v.trefferquote.map { ", \(Format.prozent($0)) aus dem Zwischenspeicher" } ?? ""))
                    Kennwert(titel: "Geschrieben", wert: Self.token(v.ausgabe) + (v.denken > 0 ? ", davon \(Self.token(v.denken)) Überlegung" : ""))
                }
                .padding(.vertical, 2)
            }
            Kennwert(titel: "Private Cloud Compute", wert: modell.assistenzdienst.kontingent)
        } header: {
            Text("Verbrauch in diesem Monat")
        }
    }

    static func token(_ n: Int) -> String {
        if n >= 1_000_000 { return "\(Format.zahl(Double(n) / 1_000_000, stellen: 2)) Mio. Token" }
        if n >= 10_000 { return "\(Format.zahl(Double(n) / 1000, stellen: 0)) Tsd. Token" }
        return "\(Format.zahl(Double(n), stellen: 0)) Token"
    }
}
