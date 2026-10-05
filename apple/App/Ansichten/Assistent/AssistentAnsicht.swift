import SwiftUI
import Anlage
import Assistent

struct AssistentAnsicht: View {
    /// Als Seitenbereich neben einer anderen Seite: schmaler, ohne Kopf.
    var kompakt = false

    @Environment(AppModell.self) private var modell
    @State private var eingabe = ""
    @State private var zeigeZustimmung = false
    @FocusState private var eingabeAktiv: Bool

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 18) {
                    if !kompakt {
                        kopf
                    }
                    ForEach(modell.gespraech.beitraege) { beitrag in
                        BeitragAnsicht(beitrag: beitrag)
                            .id(beitrag.id)
                    }
                    if modell.antwortLaeuft {
                        VStack(alignment: .leading, spacing: 10) {
                            if !modell.laufendeBausteine.isEmpty {
                                BeitragAnsicht(beitrag: Beitrag(id: "laufend", rolle: .assistent, bausteine: modell.laufendeBausteine, zeit: .now))
                            }
                            HStack(spacing: 8) {
                                ProgressView().controlSize(.small)
                                Text(modell.laufendeBausteine.isEmpty ? "Liest die Anlage …" : "Antwortet …")
                                    .font(.callout)
                                    .foregroundStyle(.secondary)
                            }
                        }
                        .id("laeuft")
                    }
                    if modell.gespraech.beitraege.isEmpty {
                        vorschlagsfragen
                    }
                }
                .padding()
                .frame(maxWidth: 820)
                .frame(maxWidth: .infinity)
            }
            .background(Farbe.grund)
            .scrollDismissesKeyboard(.interactively)
            .onChange(of: modell.gespraech.beitraege.count) {
                withAnimation { proxy.scrollTo(modell.gespraech.beitraege.last?.id, anchor: .bottom) }
            }
            .onChange(of: modell.laufendeBausteine.count) {
                withAnimation { proxy.scrollTo("laeuft", anchor: .bottom) }
            }
            .safeAreaInset(edge: .bottom) {
                Eingabeleiste(text: $eingabe, bezug: modell.fragebezug, aktiv: $eingabeAktiv, laeuft: modell.antwortLaeuft) {
                    modell.fragebezug = nil
                } senden: {
                    guard !modell.antwortLaeuft else { return }
                    // Vor der ersten Frage an Claude die Zustimmung; danach geht die Frage hinaus.
                    if modell.kiBrauchtZustimmung {
                        zeigeZustimmung = true
                        return
                    }
                    modell.senden(eingabe)
                    eingabe = ""
                } anhalten: {
                    modell.antwortAnhalten()
                }
            }
        }
        .claudeZustimmung($zeigeZustimmung) {
            modell.senden(eingabe)
            eingabe = ""
        }
        .navigationTitle(kompakt ? "Assistent" : modell.gespraech.titel)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button("Neues Gespräch", systemImage: "square.and.pencil") { modell.neuesGespraech() }
                        .disabled(modell.antwortLaeuft)
                    if !modell.istBeispiel, modell.ablage.gespraeche.contains(where: { $0.id != modell.gespraech.id }) {
                        Menu("Frühere Gespräche", systemImage: "text.bubble") {
                            ForEach(modell.ablage.gespraeche.filter { $0.id != modell.gespraech.id }.prefix(15)) { g in
                                Button {
                                    modell.gespraechOeffnen(g.id)
                                } label: {
                                    Text(g.titel)
                                    Text(Format.tagMitZeit(g.zuletzt))
                                }
                            }
                        }
                        .disabled(modell.antwortLaeuft)
                    }
                    NavigationLink(value: Ziel.aenderungen) {
                        Label("Änderungsprotokoll", systemImage: "clock.arrow.circlepath")
                    }
                    if !modell.istBeispiel {
                        NavigationLink(value: Ziel.lageberichte) {
                            Label("Frühere Lageberichte", systemImage: "doc.text.magnifyingglass")
                        }
                    }
                    Divider()
                    Picker("Modell", selection: Bindable(modell).kiModell) {
                        ForEach(AppModell.KIModell.allCases) { m in
                            Text(modell.kiName(m)).tag(m)
                        }
                    }
                } label: {
                    Label("Mehr", systemImage: "ellipsis")
                }
            }
        }
    }

    private var kopf: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                Image(systemName: "sparkles")
                    .font(.title2)
                    .foregroundStyle(.tint)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Assistent für Ihre Heizung")
                        .font(.headline)
                    Text("\(modell.modellwahl.name) · liest Messwerte, Einstellungen und Befunde, ändert nichts ohne Ihre Bestätigung")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    if !modell.istBeispiel, let hindernis = modell.kiHindernis {
                        Label(hindernis, systemImage: "exclamationmark.circle")
                            .font(.caption)
                            .foregroundStyle(Farbe.warnung)
                            .padding(.top, 2)
                        if modell.kiBrauchtZustimmung {
                            Button("Zustimmen …") { zeigeZustimmung = true }
                                .font(.caption)
                                .buttonStyle(.rahmen)
                        }
                    }
                }
            }
            if !modell.offeneVorschlaege.isEmpty {
                Text("Offene Vorschläge")
                    .font(.subheadline.weight(.semibold))
                    .padding(.top, 4)
                VStack(spacing: 8) {
                    ForEach(modell.offeneVorschlaege) { v in
                        VorschlagKompakt(vorschlag: v)
                    }
                }
            }
            Divider().padding(.top, 4)
        }
    }

    private var vorschlagsfragen: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Zum Beispiel")
                .font(.subheadline.weight(.semibold))
            ForEach(["Wie läuft die Anlage heute?", "Warum wird das Bad nicht warm?", "Lässt sich der Ölverbrauch senken?", "Was sollte vor dem Winter geprüft werden?"], id: \.self) { frage in
                Button(frage) { eingabe = frage; eingabeAktiv = true }
                    .buttonStyle(.rahmen)
            }
        }
    }
}

// MARK: - Beiträge

struct BeitragAnsicht: View {
    let beitrag: Beitrag

    var body: some View {
        switch beitrag.rolle {
        case .nutzer:
            HStack {
                Spacer(minLength: 48)
                VStack(alignment: .trailing, spacing: 8) {
                    ForEach(Array(beitrag.bausteine.enumerated()), id: \.offset) { _, baustein in
                        switch baustein {
                        case .bild(let beschreibung):
                            Bildanhang(beschreibung: beschreibung)
                        case .text(let text):
                            Text(text)
                                .padding(.horizontal, 14)
                                .padding(.vertical, 10)
                                .background(Farbe.akzentWeich, in: .rect(cornerRadius: 14))
                                .overlay { RoundedRectangle(cornerRadius: 14).strokeBorder(Color.accentColor.opacity(0.25), lineWidth: 1) }
                        default:
                            EmptyView()
                        }
                    }
                }
            }
        case .assistent:
            VStack(alignment: .leading, spacing: 10) {
                ForEach(Array(beitrag.bausteine.enumerated()), id: \.offset) { _, baustein in
                    switch baustein {
                    case .werkzeug(let aufruf):
                        WerkzeugZeile(aufruf: aufruf)
                    case .ueberlegung(let text):
                        Ueberlegung(text: text)
                    case .text(let text):
                        Text(Self.formatiert(text))
                            .textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                    case .vorschlag(let id):
                        VorschlagKarte(vorschlagID: id)
                    case .bild(let beschreibung):
                        Bildanhang(beschreibung: beschreibung)
                    case .fehler(let text):
                        Label(text, systemImage: "exclamationmark.triangle")
                            .font(.callout)
                            .foregroundStyle(Farbe.warnung)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
        }
    }
}

extension BeitragAnsicht {
    /// Fettdruck und Hervorhebungen der KI; Absätze bleiben, wie sie sind.
    static func formatiert(_ text: String) -> AttributedString {
        let fest = Format.einheitenFest(text)
        return (try? AttributedString(markdown: fest, options: .init(interpretedSyntax: .inlineOnlyPreservingWhitespace)))
            ?? AttributedString(fest)
    }
}

private struct WerkzeugZeile: View {
    let aufruf: Werkzeugaufruf
    @State private var offen = false

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Button {
                withAnimation(.snappy) { offen.toggle() }
            } label: {
                HStack(spacing: 6) {
                    if aufruf.fertig {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(Farbe.gut)
                    } else {
                        ProgressView().controlSize(.mini)
                    }
                    Text(aufruf.beschreibung)
                        .font(Schrift.datenKlein)
                        .multilineTextAlignment(.leading)
                    Image(systemName: offen ? "chevron.up" : "chevron.down")
                        .font(.caption2)
                }
                .font(.caption)
                .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            if offen {
                Text("\(aufruf.name): \(aufruf.ergebnis.isEmpty ? "läuft …" : aufruf.ergebnis)")
                    .font(.caption.monospaced())
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
                    .padding(8)
                    .background(.quaternary.opacity(0.5), in: .rect(cornerRadius: 8))
            }
        }
    }
}

private struct Ueberlegung: View {
    let text: String
    @State private var offen = false

    var body: some View {
        DisclosureGroup(isExpanded: $offen) {
            Text(Format.einheitenFest(text))
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.top, 4)
        } label: {
            Label("Überlegung", systemImage: "brain")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}

private struct Bildanhang: View {
    let beschreibung: String

    var body: some View {
        VStack(spacing: 6) {
            RoundedRectangle(cornerRadius: 12)
                .fill(LinearGradient(colors: [Farbe.kaelte.opacity(0.35), Farbe.waerme.opacity(0.25)], startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: 180, height: 120)
                .overlay {
                    Image(systemName: "gauge.with.dots.needle.67percent")
                        .font(.system(size: 44))
                        .foregroundStyle(.white.opacity(0.85))
                }
            Text(beschreibung)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Foto: \(beschreibung)")
    }
}

// MARK: - Vorschlag

struct VorschlagKarte: View {
    @Environment(AppModell.self) private var modell
    let vorschlagID: String
    @State private var zeigeWirkung = false

    var body: some View {
        if let v = modell.vorschlag(vorschlagID) {
            VStack(alignment: .leading, spacing: 12) {
                HStack(alignment: .firstTextBaseline) {
                    Label(v.istAktion ? "Aktion" : "Vorschlag", systemImage: v.istAktion ? "play.circle" : "wand.and.stars")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.tint)
                    Spacer()
                    status(v)
                }
                Text(v.titel)
                    .font(.headline)
                Text(v.geraete.joined(separator: " und "))
                    .font(.caption)
                    .foregroundStyle(.secondary)

                werte(v)

                Text(Format.einheitenFest(v.begruendung))
                    .font(.callout)
                    .fixedSize(horizontal: false, vertical: true)

                if !v.wirkung.isEmpty {
                    DisclosureGroup(v.istAktion ? "Was geschieht" : "Erwartete Wirkung", isExpanded: $zeigeWirkung) {
                        Text(Format.einheitenFest(v.wirkung))
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                            .padding(.top, 4)
                    }
                    .font(.callout)
                }

                if case .gescheitert(let meldung) = v.status {
                    Label(meldung, systemImage: "exclamationmark.triangle")
                        .font(.callout)
                        .foregroundStyle(Farbe.warnung)
                        .fixedSize(horizontal: false, vertical: true)
                } else if let ergebnis = v.ergebnis {
                    Label(ergebnis, systemImage: v.istAktion ? "info.circle" : "exclamationmark.triangle")
                        .font(.callout)
                        .foregroundStyle(v.istAktion ? Color.secondary : Farbe.warnung)
                        .fixedSize(horizontal: false, vertical: true)
                }

                if let k = v.kontrolle {
                    Wirkungskontrollansicht(kontrolle: k)
                }

                knoepfe(v)
            }
            .padding(16)
            .karte(betont: v.status == .offen)
        }
    }

    /// Ein Wert: bisher → neu. Mehrere Werte als Liste.
    @ViewBuilder private func werte(_ v: Vorschlag) -> some View {
        if v.istAktion {
            Kennzahl(titel: v.parameter, wert: v.neu, farbe: Color.accentColor)
        } else if v.bisher.contains("\n") {
            VStack(alignment: .leading, spacing: 6) {
                ForEach(Array(v.ziele.enumerated()), id: \.offset) { _, z in
                    VStack(alignment: .leading, spacing: 2) {
                        Text("\(z.bezeichnung) · \(z.ort)").font(.caption).foregroundStyle(.secondary)
                        HStack(spacing: 6) {
                            Text(Zielwerte.anzeige(z.parameter, z.bisher)).monospacedDigit()
                            Image(systemName: "arrow.right").font(.caption).foregroundStyle(Farbe.blass)
                            Text(Zielwerte.anzeige(z.parameter, z.neu)).monospacedDigit().foregroundStyle(Color.accentColor)
                        }
                        .font(.callout.weight(.medium))
                    }
                }
            }
        } else {
            HStack(spacing: 10) {
                Kennzahl(titel: "Bisher", wert: v.bisher)
                Image(systemName: "arrow.right")
                    .foregroundStyle(Farbe.blass)
                Kennzahl(titel: "Neu", wert: v.neu, farbe: Color.accentColor)
            }
        }
    }

    @ViewBuilder private func status(_ v: Vorschlag) -> some View {
        if modell.vorschlagLaeuft.contains(v.id) {
            ProgressView().controlSize(.small)
        } else {
            switch v.status {
            case .offen:
                EmptyView()
            case .uebernommen(let zeit):
                Zustandsmarke(text: "\(v.istAktion ? "Ausgelöst" : "Übernommen") \(Format.uhrzeit(zeit))", symbol: "checkmark.circle.fill", farbe: Farbe.gut)
            case .verworfen:
                Zustandsmarke(text: "Verworfen", symbol: "xmark.circle", farbe: .secondary)
            case .zurueckgenommen:
                Zustandsmarke(text: "Zurückgenommen", symbol: "arrow.uturn.backward.circle", farbe: .secondary)
            case .gescheitert:
                Zustandsmarke(text: "Nicht übernommen", symbol: "exclamationmark.triangle.fill", farbe: Farbe.warnung)
            }
        }
    }

    @ViewBuilder private func knoepfe(_ v: Vorschlag) -> some View {
        let laeuft = modell.vorschlagLaeuft.contains(v.id)
        switch v.status {
        case .offen:
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Button {
                        withAnimation(.snappy) { modell.uebernehmen(v.id) }
                    } label: {
                        Label(v.istAktion ? "Ausführen" : "Übernehmen", systemImage: v.istAktion ? "play" : "checkmark")
                    }
                    .buttonStyle(.rahmenBetont)
                    Button("Verwerfen") {
                        withAnimation(.snappy) { modell.verwerfen(v.id) }
                    }
                    .buttonStyle(.rahmen)
                }
                .disabled(laeuft)
                Text(v.istAktion
                     ? "Die Aktion ändert keine Einstellung. Die App löst sie am Gerät aus und zeigt das Ergebnis hier."
                     : "Vor der Übernahme sichert die App die Einstellungen der betroffenen Geräte. Danach liest sie den Wert zurück und vergleicht nach sieben Tagen die Kennzahlen.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        case .uebernommen where !v.istAktion:
            Button {
                withAnimation(.snappy) { modell.zuruecknehmen(v.id) }
            } label: {
                Label("Rückgängig", systemImage: "arrow.uturn.backward")
            }
            .buttonStyle(.rahmen)
            .disabled(laeuft)
        case .gescheitert:
            Button("Schließen") {
                withAnimation(.snappy) { modell.verwerfen(v.id) }
            }
            .buttonStyle(.rahmen)
        default:
            EmptyView()
        }
    }
}

/// Kennzahlen der sieben Tage vor und nach einer Übernahme
private struct Wirkungskontrollansicht: View {
    let kontrolle: Wirkungskontrolle
    @State private var offen = false

    var body: some View {
        DisclosureGroup(isExpanded: $offen) {
            if kontrolle.ausgewertet == nil {
                Text("Die Werte danach stehen ab \(Format.tagMitZeit(kontrolle.faellig)) fest. Verglichen werden die sieben Tage vor und nach der Übernahme.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.top, 4)
            }
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
                GridRow {
                    Text("Kennzahl").foregroundStyle(.secondary)
                    Text("vorher").foregroundStyle(.secondary)
                    Text("nachher").foregroundStyle(.secondary)
                }
                ForEach(kontrolle.werte.filter { $0.vorher != nil || $0.nachher != nil }) { w in
                    GridRow {
                        Text(w.name)
                        Text(zahl(w.vorher)).monospacedDigit()
                        Text(zahl(w.nachher)).monospacedDigit()
                    }
                }
            }
            .font(.caption)
            .padding(.top, 4)
        } label: {
            Label(kontrolle.ausgewertet == nil ? "Wirkungskontrolle läuft" : "Wirkungskontrolle", systemImage: "chart.bar.xaxis")
                .font(.callout)
        }
    }

    private func zahl(_ x: Double?) -> String {
        x.map { Format.zahl($0, stellen: abs($0) < 10 ? 2 : 1) } ?? "–"
    }
}

/// Offener Vorschlag als Zeile; aufgeklappt zeigt er die volle Karte.
private struct VorschlagKompakt: View {
    let vorschlag: Vorschlag
    @State private var offen = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Button {
                withAnimation(.snappy) { offen.toggle() }
            } label: {
                HStack(spacing: 10) {
                    Image(systemName: "wand.and.stars")
                        .foregroundStyle(.tint)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(vorschlag.titel)
                            .font(.subheadline.weight(.semibold))
                        Text(vorschlag.geraete.joined(separator: " und "))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()
                    Image(systemName: offen ? "chevron.up" : "chevron.down")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(12)
                .contentShape(.rect)
            }
            .buttonStyle(.plain)
            .karte()
            if offen {
                VorschlagKarte(vorschlagID: vorschlag.id)
            }
        }
    }
}

// MARK: - Eingabe

struct Eingabeleiste: View {
    @Binding var text: String
    let bezug: String?
    var aktiv: FocusState<Bool>.Binding
    /// Solange eine Antwort entsteht, hält der Knopf sie an, statt zu senden.
    var laeuft = false
    let bezugEntfernen: () -> Void
    let senden: () -> Void
    var anhalten: () -> Void = {}

    @State private var hinweisAnhang = false

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if let bezug {
                HStack(spacing: 6) {
                    Image(systemName: "scope")
                    Text("Bezug: \(bezug)").lineLimit(1)
                    Button {
                        bezugEntfernen()
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Bezug entfernen")
                }
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.horizontal, 10)
                .padding(.vertical, 4)
                .glassEffect(.regular, in: .capsule)
            }
            HStack(alignment: .bottom, spacing: 8) {
                Menu {
                    Button("Foto aufnehmen", systemImage: "camera") { hinweisAnhang = true }
                    Button("Foto auswählen", systemImage: "photo") { hinweisAnhang = true }
                } label: {
                    Image(systemName: "plus")
                        .font(.title3)
                        .frame(width: 36, height: 36)
                }
                .menuStyle(.button)
                .buttonStyle(.glass)
                .buttonBorderShape(.circle)
                .accessibilityLabel("Anhang")

                TextField("Frage zur Anlage …", text: $text, axis: .vertical)
                    .lineLimit(1...5)
                    .focused(aktiv)
                    .textFieldStyle(.plain)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .glassEffect(.regular, in: .rect(cornerRadius: 20))
                    .onSubmit(senden)

                if laeuft {
                    Button(action: anhalten) {
                        Image(systemName: "stop.fill")
                            .font(.title3.weight(.semibold))
                            .frame(width: 36, height: 36)
                    }
                    .buttonStyle(.glassProminent)
                    .buttonBorderShape(.circle)
                    .keyboardShortcut(".", modifiers: .command)
                    .accessibilityLabel("Antwort anhalten")
                } else {
                    Button(action: senden) {
                        Image(systemName: "arrow.up")
                            .font(.title3.weight(.semibold))
                            .frame(width: 36, height: 36)
                    }
                    .buttonStyle(.glassProminent)
                    .buttonBorderShape(.circle)
                    .disabled(text.trimmingCharacters(in: .whitespaces).isEmpty)
                    .keyboardShortcut(.return, modifiers: .command)
                    .accessibilityLabel("Senden")
                }
            }
        }
        .padding(.horizontal)
        .padding(.vertical, 8)
        .alert("Bildanhang", isPresented: $hinweisAnhang) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("Im Klickmodell ohne Funktion. In der App geht ein Foto, etwa der Ölstandsanzeige, an das gewählte Modell; die Apple-Modelle lesen es über die Texterkennung.")
        }
    }
}

// MARK: - Änderungsprotokoll

struct AenderungenAnsicht: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        Form {
            if modell.aenderungen.isEmpty {
                ContentUnavailableView("Noch keine Änderung", systemImage: "clock.arrow.circlepath",
                                       description: Text("Sobald Sie eine Einstellung über die App ändern oder einen Vorschlag übernehmen, steht er hier."))
            }
            Section {
                ForEach(modell.aenderungen) { a in
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text(a.parameter).font(.subheadline.weight(.medium))
                            Spacer()
                            Text(Format.tagMitZeit(a.zeit)).font(.caption).foregroundStyle(.secondary)
                        }
                        Text("\(a.geraet): \(a.bisher) → \(a.neu)")
                            .font(.callout)
                            .monospacedDigit()
                        Text(a.ausloeser)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 2)
                }
            } footer: {
                Text("Hier stehen die Einstellungen, die über die App an ein Gerät gingen, auch die bestätigten Vorschläge der KI und ihre Rücknahme. Sollwerte und Betriebsarten, die Sie am Raum stellen, erscheinen nicht.")
            }
        }
        .formularStil()
        .navigationTitle("Änderungsprotokoll")
    }
}

// MARK: - Frühere Lageberichte

/// Die Lageberichte der KI, jüngste zuerst; die App behält die letzten 30.
struct LageberichteAnsicht: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        Form {
            if modell.ablage.lageberichte.isEmpty {
                ContentUnavailableView("Noch kein Lagebericht der KI", systemImage: "doc.text",
                                       description: Text("Bis zum ersten Bericht der KI zeigt die Übersicht den Bericht aus den Prüfungen der App."))
            }
            ForEach(Array(modell.ablage.lageberichte.enumerated()), id: \.offset) { _, b in
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Zustandsmarke(text: b.zustand.rawValue, symbol: b.zustand.symbol, farbe: b.zustand.farbe)
                            Spacer()
                            Text(Format.tagMitZeit(b.erstellt)).font(.caption).foregroundStyle(.secondary)
                        }
                        Text(b.kurztext).font(.callout.weight(.medium)).fixedSize(horizontal: false, vertical: true)
                        ForEach(b.hinweise) { h in
                            HStack(alignment: .firstTextBaseline, spacing: 8) {
                                SchwereSymbol(schwere: h.schwere)
                                Text(h.text).font(.callout).fixedSize(horizontal: false, vertical: true)
                            }
                        }
                        Text(b.modell).font(.caption).foregroundStyle(.secondary)
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .formularStil()
        .navigationTitle("Lageberichte")
    }
}
