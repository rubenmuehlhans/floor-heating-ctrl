import SwiftUI

/// Tab-Leiste auf dem iPhone, Seitenleiste auf iPad und Mac. Der Assistent steht in der Mitte und
/// lässt sich auf jeder Seite als Seitenbereich einblenden.
struct Hauptansicht: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.horizontalSizeClass) private var breite
    @Environment(\.scenePhase) private var phase
    /// Breite des Assistenten als Seitenbereich; mit Faltlinie genau die rechte Hälfte
    @State private var assistentbreite: CGFloat = 400

    /// Zeigt der Seitenbereich den Assistenten, entfällt der Tab auf dem iPad; sonst reicht die
    /// Tab-Leiste im Hochformat nicht für alle fünf Bereiche.
    private var assistentImSeitenbereich: Bool {
        #if os(iOS)
        modell.zeigeAssistent && breite == .regular
        #else
        false
        #endif
    }

    /// Ein Bereich: sein Stapel mit dem Pfad aus dem Modell, das Spaltenmaß und die Ziele.
    private func stapel<Inhalt: View>(_ b: AppModell.Bereich, @ViewBuilder inhalt: () -> Inhalt) -> some View {
        NavigationStack(path: pfad(b)) {
            Seitenrahmen(weg: nil) { inhalt() }
                .zielnavigation()
        }
        .environment(\.bereich, b)
        .spaltenmass(b)
    }

    /// Der Pfad des Stapels: zweispaltig nur die Seiten, die das Fenster belegen, einspaltig auch die
    /// der rechten Spalte darüber.
    private func pfad(_ b: AppModell.Bereich) -> Binding<[Weg]> {
        Binding(
            get: { modell.stapel[b, default: Stapel()].pfad(zweispaltig: modell.zweispaltig[b] ?? false) },
            set: { neu in
                modell.stapel[b, default: Stapel()].setzen(
                    neu, zweispaltig: modell.zweispaltig[b] ?? false, wurzelHatNebenspalte: b.hatNebenspalte)
            }
        )
    }

    var body: some View {
        @Bindable var modell = modell
        // Das Spaltenmaß je Bereich, nicht für das ganze Fenster: Randspalte, Seitenleiste und
        // Assistent nehmen der Seite Breite weg.
        TabView(selection: $modell.bereich) {
            Tab("Übersicht", systemImage: "house", value: AppModell.Bereich.uebersicht) {
                stapel(.uebersicht) { UebersichtAnsicht() }
            }
            Tab("Räume", systemImage: "square.grid.2x2", value: AppModell.Bereich.raeume) {
                stapel(.raeume) { RaeumeAnsicht() }
            }
            Tab("Assistent", systemImage: "sparkles", value: AppModell.Bereich.assistent) {
                stapel(.assistent) { AssistentAnsicht() }
            }
            .hidden(assistentImSeitenbereich)
            Tab("Heizung", systemImage: "flame", value: AppModell.Bereich.heizung) {
                stapel(.heizung) { HeizungAnsicht() }
            }
            Tab("Geräte", systemImage: "cpu", value: AppModell.Bereich.geraete) {
                stapel(.geraete) { GeraeteAnsicht() }
            }
        }
        .tabViewStyle(.sidebarAdaptable)
        .inspector(isPresented: $modell.zeigeAssistent) {
            NavigationStack {
                AssistentAnsicht(kompakt: true)
                    .zielnavigation()
            }
            .inspectorColumnWidth(min: 340, ideal: assistentbreite, max: max(560, assistentbreite))
        }
        .onChange(of: modell.bereich) { _, neu in
            if neu == .assistent { modell.zeigeAssistent = false }
        }
        .sheet(isPresented: $modell.zeigeEinrichtung) {
            EinrichtungsAssistent()
        }
        .overlay(alignment: .top) {
            if let meldung = modell.rueckmeldung {
                RueckmeldungsLeiste(meldung: meldung) { modell.rueckmeldungSchliessen() }
                    .padding(.top, 8)
                    .padding(.horizontal)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
        .animation(.snappy, value: modell.rueckmeldung)
        .task { modell.betriebAbgleichen() }
        // Angefangene Plätze des Verlaufs sichern, bevor das System die App anhält
        // Auf dem Mac läuft die App ohne Fenster in der Menüleiste weiter; von selbst erstellt
        // die KI Lageberichte nur, solange ein Fenster offen ist.
        .onAppear {
            modell.vordergrund = phase == .active
            modell.adressenNachfuehren(.vordergrund)
        }
        .onDisappear { modell.vordergrund = false }
        .onChange(of: phase) { _, neu in
            modell.vordergrund = neu == .active
            if neu != .active {
                Task { await modell.verlaufSichern() }
            } else {
                modell.adressenNachfuehren(.vordergrund)
                modell.lageberichtPruefen()
                modell.wirkungskontrollenPruefen()
            }
            #if os(iOS)
            if neu == .background { modell.hintergrundabrufPlanen() }
            #endif
        }
        // Nach dem Antippen einer Mitteilung
        .sheet(item: Binding(get: { modell.geoeffneterBefund.map(GeoeffneterBefund.init) },
                             set: { modell.geoeffneterBefund = $0?.id })) { b in
            NavigationStack {
                BefundAnsicht(befundID: b.id)
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) {
                            Button("Fertig") { modell.geoeffneterBefund = nil }
                        }
                    }
            }
            .environment(modell)
        }
        .tint(Color.accentColor)
        .background {
            GeometryReader { geo in
                Color.clear.onChange(of: Self.rechteHaelfte(geo), initial: true) { _, breite in
                    assistentbreite = breite ?? 400
                }
            }
        }
    }
}

extension Hauptansicht {
    /// Von der senkrechten Faltlinie bis zum rechten Rand; ohne Faltlinie `nil`.
    static func rechteHaelfte(_ geo: GeometryProxy) -> CGFloat? {
        #if os(iOS)
        if #available(iOS 27.1, *) {
            return geo.reservedRegions(kind: .division, options: .includeInactive)
                .first { $0.frame.height > $0.frame.width }
                .map { (geo.size.width - $0.frame.midX).rounded() }
        }
        #endif
        return nil
    }
}

/// Kurze Meldung nach einem Befehl; Fehler bleiben länger stehen und lassen sich wegtippen.
struct RueckmeldungsLeiste: View {
    let meldung: AppModell.Rueckmeldung
    let schliessen: () -> Void

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 10) {
            Image(systemName: meldung.fehler ? "xmark.octagon.fill" : "checkmark.circle.fill")
                .foregroundStyle(meldung.fehler ? Farbe.stoerung : Farbe.gut)
            Text(Format.einheitenFest(meldung.text))
                .font(.callout)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
            Button("Schließen", systemImage: "xmark", action: schliessen)
                .labelStyle(.iconOnly)
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .frame(maxWidth: 560)
        .glassEffect(in: .rect(cornerRadius: 14))
        .accessibilityElement(children: .combine)
    }
}

struct GeoeffneterBefund: Identifiable {
    let id: String
}
