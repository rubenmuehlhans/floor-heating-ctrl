import SwiftUI
import Anlage
import Assistent

struct UebersichtAnsicht: View {
    @Environment(AppModell.self) private var modell

    private let raumspalten = [GridItem(.adaptive(minimum: 160, maximum: 260), spacing: 12)]
    private let kennzahlspalten = [GridItem(.adaptive(minimum: 150), spacing: 10)]

    var body: some View {
        let anlage = modell.anlage
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Geraeteleiste(anlage: anlage)

                if modell.istBeispiel {
                    HStack(alignment: .firstTextBaseline, spacing: 10) {
                        Image(systemName: "theatermasks").foregroundStyle(Farbe.gedaempft)
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Beispielanlage").font(.subheadline.weight(.semibold))
                            Text("Die App zeigt einen Mitschnitt, bis Sie ein Gerät einbinden. Befehle verändern hier nur die Beispieldaten.")
                                .font(.callout)
                                .foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                        Spacer(minLength: 0)
                        Button("Geräte einbinden") { modell.zeigeEinrichtung = true }
                            .buttonStyle(.rahmenBetont)
                            .fixedSize()
                    }
                    .padding(14)
                    .karte()
                } else if modell.betrieb.staende.values.allSatisfy({ $0.letzterKontakt == nil }) {
                    HStack(spacing: 10) {
                        ProgressView().controlSize(.small)
                        Text("Die Geräte werden abgefragt …").foregroundStyle(.secondary)
                    }
                    .padding(.horizontal, 4)
                }

                LageberichtKarte(bericht: modell.aktuellerLagebericht)

                Abschnitt(titel: "Räume", zusatz: raeumeZusatz(anlage))
                // Eine Platine ohne Räume, die nur ein Funkthermometer empfängt, hätte hier
                // nur eine leere Überschrift.
                ForEach(anlage.etagen.filter { !$0.raeume.isEmpty }) { etage in
                    VStack(alignment: .leading, spacing: 8) {
                        HStack(spacing: 8) {
                            Text(etage.name).font(.subheadline.weight(.semibold))
                            if etage.bedarf {
                                Zustandsmarke(text: "Wärmebedarf", symbol: "flame", farbe: Farbe.waerme)
                            }
                        }
                        .padding(.horizontal, 4)
                        LazyVGrid(columns: raumspalten, spacing: 12) {
                            ForEach(etage.raeume) { raum in
                                NavigationLink(value: Ziel.raum(raum.id)) {
                                    RaumKachel(raum: raum)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                }

                if anlage.speicher != nil || anlage.kessel != nil || anlage.aussen != nil || pumpenzahl(anlage) > 0 {
                    Abschnitt(titel: "Wärmeerzeugung", zusatz: "Stand \(Format.uhrzeit(anlage.stand))")
                    Button {
                        modell.bereich = .heizung
                    } label: {
                        LazyVGrid(columns: kennzahlspalten, spacing: 10) {
                            if let speicher = anlage.speicher {
                                Kennzahl(titel: "Speicher", wert: speicher.temperatur.map { Format.zahl($0) } ?? "–", einheit: "°C",
                                         fuss: "Ladung \(Format.prozent(speicher.ladung)) · \(speicher.phase)", farbe: Farbe.waerme)
                            }
                            if let kessel = anlage.kessel {
                                Kennzahl(titel: "Brenner", wert: kessel.brennerLaeuft ? "läuft" : "aus",
                                         fuss: "gestern \(Format.dauer(kessel.laufzeitGestern)) · \(Format.starts(kessel.startsGestern))",
                                         farbe: kessel.brennerLaeuft ? Farbe.waerme : Farbe.tinte)
                            }
                            if let aussen = anlage.aussen {
                                Kennzahl(titel: "Außen", wert: Format.zahl(aussen.temperatur), einheit: "°C",
                                         fuss: [aussen.quelle, aussen.feuchte.map { "Feuchte \(Format.mitEinheit(Format.zahl($0, stellen: 0), "%"))" }]
                                            .compactMap { $0 }.joined(separator: " · "),
                                         farbe: Farbe.kaelte)
                            }
                            if pumpenzahl(anlage) > 0 {
                                let gestoert = pumpenGestoert(anlage)
                                Kennzahl(titel: "Pumpen", wert: gestoert > 0 ? "\(gestoert) gestört" : "in Ordnung",
                                         fuss: gestoert > 0 ? "Relais nicht erreichbar" : "\(pumpenzahl(anlage)) Relais erreichbar",
                                         farbe: gestoert > 0 ? Farbe.stoerung : Farbe.gut)
                            }
                        }
                        .padding(14)
                        .karte()
                    }
                    .buttonStyle(.plain)
                }

                Abschnitt(titel: "Befunde", zusatz: "\(anlage.befunde.count)")
                VStack(spacing: 0) {
                    ForEach(Array(anlage.befundeNachSchwere.prefix(3).enumerated()), id: \.element.id) { i, befund in
                        if i > 0 { Divider().overlay(Farbe.linie).padding(.leading, 46) }
                        NavigationLink(value: Ziel.befund(befund.id)) {
                            BefundZeile(befund: befund)
                                .padding(.horizontal, 14)
                                .padding(.vertical, 12)
                        }
                        .buttonStyle(.plain)
                    }
                    Divider().overlay(Farbe.linie)
                    NavigationLink(value: Ziel.befunde) {
                        HStack {
                            Text("Alle \(anlage.befunde.count) Befunde").font(.subheadline.weight(.medium))
                            Spacer()
                            Image(systemName: "chevron.right").font(.caption).foregroundStyle(Farbe.blass)
                        }
                        .foregroundStyle(Color.accentColor)
                        .padding(14)
                        .contentShape(.rect)
                    }
                    .buttonStyle(.plain)
                }
                .karte()
            }
            .padding()
            .frame(maxWidth: 1100)
            .frame(maxWidth: .infinity)
        }
        .background(Farbe.grund)
        .navigationTitle("Übersicht")
        .fragenKnopf("Übersicht")
    }

    private func raeumeZusatz(_ anlage: Anlagenbild) -> String {
        let heizen = anlage.raeume.filter { $0.zustand == .heizt }.count
        return "\(heizen) von \(anlage.raeume.count) heizen"
    }

    private func pumpenGestoert(_ anlage: Anlagenbild) -> Int {
        anlage.heizkreise.filter { !$0.relais.erreichbar }.count + (anlage.kesselkreispumpe.map { $0.relais.erreichbar ? 0 : 1 } ?? 0)
    }

    private func pumpenzahl(_ anlage: Anlagenbild) -> Int {
        anlage.heizkreise.count + (anlage.kesselkreispumpe == nil ? 0 : 1)
    }
}

/// Kopfzeile wie in der Weboberfläche: die Geräte der Anlage als Marken, mit Erreichbarkeit.
struct Geraeteleiste: View {
    @Environment(AppModell.self) private var modell
    let anlage: Anlagenbild

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                Kicker("Heizung")
                ForEach(anlage.geraete.filter { $0.art == .kessel || $0.art == .speicher }) { g in marke(g) }
                Kicker("Verteiler").padding(.leading, 6)
                ForEach(anlage.geraete.filter { $0.art == .verteiler }) { g in marke(g) }
            }
            .padding(.horizontal, 4)
        }
        .scrollClipDisabled()
    }

    private func marke(_ g: Geraet) -> some View {
        NavigationLink(value: Ziel.geraet(g.id)) {
            HStack(spacing: 5) {
                Circle().fill(g.erreichbar ? Farbe.gut : Farbe.stoerung).frame(width: 6, height: 6)
                Text(g.ort)
            }
            .font(.caption.weight(.medium))
            .foregroundStyle(Color.accentColor)
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(Farbe.flaeche, in: .capsule)
            .overlay { Capsule().strokeBorder(Farbe.linieStark, lineWidth: 1) }
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Lagebericht

struct LageberichtKarte: View {
    @Environment(AppModell.self) private var modell
    let bericht: Lagebericht

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Label("Lagebericht", systemImage: "sparkles")
                    .font(Schrift.kartentitel)
                Spacer()
                Zustandsmarke(text: bericht.zustand.rawValue, symbol: symbol, farbe: farbe)
            }

            Text(bericht.kurztext)
                .font(.title3.weight(.medium))
                .fixedSize(horizontal: false, vertical: true)

            if !bericht.hinweise.isEmpty {
            VStack(spacing: 0) {
                ForEach(Array(bericht.hinweise.enumerated()), id: \.element.id) { i, hinweis in
                    if i > 0 { Divider().overlay(Farbe.linie).padding(.leading, 34) }
                    Button {
                        modell.fragen(hinweis.text)
                    } label: {
                        HStack(alignment: .firstTextBaseline, spacing: 12) {
                            SchwereSymbol(schwere: hinweis.schwere)
                                .frame(width: 20)
                            Text(hinweis.text)
                                .font(.subheadline)
                                .multilineTextAlignment(.leading)
                                .fixedSize(horizontal: false, vertical: true)
                            Spacer(minLength: 4)
                            Image(systemName: "chevron.right")
                                .font(.caption)
                                .foregroundStyle(Farbe.blass)
                        }
                        .padding(.vertical, 10)
                        .padding(.horizontal, 12)
                        .contentShape(.rect)
                    }
                    .buttonStyle(.plain)
                }
            }
            .background(Farbe.vertieft, in: .rect(cornerRadius: 8))
            .overlay { RoundedRectangle(cornerRadius: 8).strokeBorder(Farbe.linie, lineWidth: 1) }
            }

            HStack(spacing: 6) {
                Text("\(bericht.modell) · \(Format.relativ(bericht.erstellt, bezug: modell.anlage.stand))")
                if modell.lageberichtLaeuft {
                    ProgressView().controlSize(.mini)
                    Text("neuer Bericht entsteht")
                }
            }
            .font(Schrift.datenKlein)
            .foregroundStyle(Farbe.gedaempft)
            HStack(spacing: 8) {
                if !modell.offeneVorschlaege.isEmpty {
                    Button("\(modell.offeneVorschlaege.count) Vorschläge") {
                        modell.bereich = .assistent
                    }
                    .buttonStyle(.rahmen)
                    .fixedSize()
                }
                Button("Nachfragen") {
                    modell.fragen("Lagebericht")
                }
                .buttonStyle(.rahmenBetont)
                .fixedSize()
                Spacer(minLength: 0)
                if !modell.istBeispiel, modell.lageberichtMoeglich {
                    Button {
                        modell.lageberichtErstellen()
                    } label: {
                        Label("Neu erstellen", systemImage: "arrow.clockwise")
                            .labelStyle(.iconOnly)
                    }
                    .buttonStyle(.rahmen)
                    .disabled(modell.lageberichtLaeuft)
                    .help("Lagebericht der KI neu erstellen")
                    .accessibilityLabel("Lagebericht neu erstellen")
                }
            }
        }
        .padding(16)
        .karte(betont: true)
    }

    private var farbe: Color {
        switch bericht.zustand {
        case .inOrdnung: Farbe.gut
        case .beobachten: Farbe.warnung
        case .handeln: Farbe.stoerung
        }
    }

    private var symbol: String {
        switch bericht.zustand {
        case .inOrdnung: "checkmark"
        case .beobachten: "eye"
        case .handeln: "exclamationmark.triangle"
        }
    }
}

// MARK: - Raumkachel

struct RaumKachel: View {
    let raum: Raum

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline) {
                Text(raum.name)
                    .font(.subheadline.weight(.semibold))
                    .lineLimit(1)
                Spacer(minLength: 4)
                Image(systemName: raum.zustand.symbol)
                    .font(.caption.weight(.bold))
                    .foregroundStyle(raum.zustand.farbe)
                    .frame(width: 22, height: 22)
                    .background(raum.zustand.farbe.opacity(0.12), in: .circle)
                    .overlay { Circle().strokeBorder(raum.zustand.farbe.opacity(0.4), lineWidth: 1) }
                    .accessibilityLabel(raum.zustand.text)
            }
            Messzahl(zahl: raum.ist.map { Format.zahl($0) } ?? "–,–", einheit: "°C", font: Schrift.messwert,
                     farbe: raum.ist == nil ? Farbe.blass : Farbe.tinte)
            Text(untertitel)
                .font(Schrift.datenKlein)
                .foregroundStyle(raum.abstandFarbe)
                .lineLimit(1)
            Fortschritt(anteil: raum.zielstellung, farbe: raum.betriebsart == .aus ? Farbe.blass : Farbe.waerme, hoehe: 4)
                .padding(.top, 2)
        }
        .padding(14)
        .karte()
        .contentShape(.rect)
        .accessibilityElement(children: .combine)
    }

    private var untertitel: String {
        switch raum.zustand {
        case .ausgeschaltet, .keinThermometer, .messwertVeraltet: raum.abstandText
        default:
            // Kurz, weil die Kachel schmal ist: „Soll 20,0 · +3,1 K“
            if let ist = raum.ist {
                "Soll \(Format.zahl(raum.soll)) · \(Format.kelvin(ist - raum.soll, vorzeichen: true))"
            } else {
                "Soll \(Format.zahl(raum.soll))"
            }
        }
    }
}

// MARK: - Befunde

struct BefundZeile: View {
    @Environment(AppModell.self) private var modell
    let befund: Befund

    var body: some View {
        let seit = modell.befundSeit(befund.id)
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            SchwereSymbol(schwere: befund.schwere)
                .frame(width: 20)
            VStack(alignment: .leading, spacing: 3) {
                Text(befund.titel)
                    .font(.subheadline.weight(.medium))
                    .multilineTextAlignment(.leading)
                Text(([befund.ort, befund.quelle] + (seit.map { ["seit \(Format.seit($0))"] } ?? [])).joined(separator: " · "))
                    .font(Schrift.datenKlein)
                    .foregroundStyle(Farbe.gedaempft)
            }
            if modell.befundIstStumm(befund.id) {
                Image(systemName: "bell.slash")
                    .font(.caption)
                    .foregroundStyle(Farbe.gedaempft)
                    .accessibilityLabel("ohne Mitteilung")
            }
            Spacer(minLength: 4)
            Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundStyle(Farbe.blass)
        }
        .contentShape(.rect)
    }
}

struct BefundeAnsicht: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        Form {
            ForEach([Befund.Schwere.stoerung, .warnung, .hinweis], id: \.self) { schwere in
                let liste = modell.anlage.befunde.filter { $0.schwere == schwere }
                if !liste.isEmpty {
                    Section(schwere == .hinweis ? "Hinweise" : schwere == .warnung ? "Warnungen" : "Störungen") {
                        ForEach(liste) { befund in
                            NavigationLink(value: Ziel.befund(befund.id)) {
                                BefundZeile(befund: befund)
                            }
                            .contextMenu {
                                let stumm = modell.befundIstStumm(befund.id)
                                Button(stumm ? "Wieder melden" : "Nicht mehr melden", systemImage: stumm ? "bell" : "bell.slash") {
                                    modell.befundStumm(befund.id, !stumm)
                                }
                            }
                        }
                    }
                }
            }
            if modell.anlage.befunde.isEmpty {
                Section {
                    Label("Zurzeit liegt kein Befund vor.", systemImage: "checkmark.circle")
                        .foregroundStyle(Farbe.gut)
                }
            }
            let erledigt = modell.istBeispiel ? [] : modell.befundgedaechtnis.erledigte
            if !erledigt.isEmpty {
                Section("Erledigt in den letzten 30 Tagen") {
                    ForEach(erledigt.prefix(30)) { e in
                        HStack(alignment: .firstTextBaseline, spacing: 12) {
                            Image(systemName: "checkmark.circle")
                                .foregroundStyle(Farbe.gut)
                                .frame(width: 20)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(e.titel).font(.subheadline)
                                Text("\(e.ort) · \(Format.tagMitZeit(e.erstmals)) bis \(e.erledigt.map(Format.tagMitZeit) ?? "")")
                                    .font(Schrift.datenKlein)
                                    .foregroundStyle(Farbe.gedaempft)
                            }
                        }
                    }
                }
            }
            Section {
                Text("Befunde der Geräte erscheinen erst, wenn sie 30 Minuten anstehen, und verschwinden ebenso langsam. Die Prüfungen der App ergänzen, was kein einzelnes Gerät sehen kann; manche erscheinen erst, wenn sie eine Weile anstehen, etwa ein Kanal im Handbetrieb nach zwölf Stunden.")
                    .font(.footnote)
                    .foregroundStyle(Farbe.gedaempft)
            }
        }
        .formularStil()
        .navigationTitle("Befunde")
        .fragenKnopf("Befunde")
    }
}

struct BefundAnsicht: View {
    @Environment(AppModell.self) private var modell
    let befundID: Befund.ID

    var body: some View {
        if let befund = modell.anlage.befunde.first(where: { $0.id == befundID }) {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    VStack(alignment: .leading, spacing: 12) {
                        Zustandsmarke(text: befund.schwere.bezeichnung, symbol: befund.schwere.symbol, farbe: befund.schwere.farbe)
                        Text(befund.titel).font(.title3.weight(.semibold))
                        Text(befund.text).fixedSize(horizontal: false, vertical: true)
                        HStack(alignment: .top, spacing: 24) {
                            VStack(alignment: .leading, spacing: 3) {
                                Kicker("Ort")
                                Text(befund.ort).font(Schrift.daten)
                            }
                            VStack(alignment: .leading, spacing: 3) {
                                Kicker("Gemeldet von")
                                Text(befund.quelle).font(Schrift.daten)
                            }
                            if let seit = modell.befundSeit(befund.id) {
                                VStack(alignment: .leading, spacing: 3) {
                                    Kicker("Seit")
                                    Text(Format.tagMitZeit(seit)).font(Schrift.daten)
                                }
                            }
                        }
                        if !modell.istBeispiel {
                            let stumm = modell.befundIstStumm(befund.id)
                            Toggle(isOn: Binding(get: { !stumm }, set: { modell.befundStumm(befund.id, !$0) })) {
                                Label("Mitteilung zu diesem Befund", systemImage: stumm ? "bell.slash" : "bell")
                            }
                        }
                        Button {
                            modell.fragen(befund.titel)
                        } label: {
                            Label("Assistenten fragen", systemImage: "sparkles")
                        }
                        .buttonStyle(.rahmenBetont)
                    }
                    .padding(16)
                    .karte()
                }
                .padding()
                .frame(maxWidth: 820)
                .frame(maxWidth: .infinity)
            }
            .background(Farbe.grund)
            .navigationTitle(befund.schwere.bezeichnung)
        } else {
            ContentUnavailableView("Befund erledigt", systemImage: "checkmark.circle", description: Text("Der Befund steht nicht mehr an."))
        }
    }
}
