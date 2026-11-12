import SwiftUI
import Anlage
import Assistent
import Diagnose

// MARK: - Karte im Gespräch

/// Die Erklärung, die der Assistent sich merken möchte; sie wirkt erst nach „Merken“.
struct ErklaerungKarte: View {
    @Environment(AppModell.self) private var modell
    let erklaerungID: String

    var body: some View {
        if let e = modell.erklaerungen.erklaerung(erklaerungID) {
            VStack(alignment: .leading, spacing: 12) {
                HStack(alignment: .firstTextBaseline) {
                    Label(e.status == .offen ? "Erklärung merken" : "Erklärung", systemImage: "text.badge.checkmark")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.tint)
                    Spacer()
                    status(e)
                }
                Text(e.titel)
                    .font(.headline)
                Text(e.orte.joined(separator: ", "))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                ErklaerungsZitat(text: e.text)
                Label(e.geltung, systemImage: "calendar")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                if e.status == .offen {
                    HStack {
                        Button {
                            withAnimation(.snappy) { modell.erklaerungMerken(e.id) }
                        } label: {
                            Label("Merken", systemImage: "checkmark")
                        }
                        .buttonStyle(.rahmenBetont)
                        Button("Verwerfen") {
                            withAnimation(.snappy) { modell.erklaerungVerwerfen(e.id) }
                        }
                        .buttonStyle(.rahmen)
                    }
                    Text("Danach erscheint der Befund nicht mehr im Lagebericht und löst keine Mitteilung aus, solange er nur an diesen Orten und nicht schwerer ansteht. Kommt ein Ort hinzu oder steigt die Schwere, meldet die App ihn wieder.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .padding(16)
            .karte(betont: e.status == .offen)
        }
    }

    @ViewBuilder private func status(_ e: Erklaerung) -> some View {
        switch e.status {
        case .offen: EmptyView()
        case .gemerkt: Zustandsmarke(text: "Gemerkt", symbol: "checkmark.circle.fill", farbe: Farbe.gut)
        case .verworfen: Zustandsmarke(text: "Verworfen", symbol: "xmark.circle", farbe: .secondary)
        }
    }
}

/// Die Erklärung im Wortlaut, als Zitat abgesetzt
struct ErklaerungsZitat: View {
    let text: String

    var body: some View {
        Text("„\(text)“")
            .font(.callout)
            .fixedSize(horizontal: false, vertical: true)
            .padding(.leading, 10)
            .overlay(alignment: .leading) {
                Rectangle().fill(Color.accentColor.opacity(0.5)).frame(width: 3)
            }
    }
}

// MARK: - Liste

/// Was der Assistent sich gemerkt hat: die Erklärungen zu Befunden, mit Löschen.
struct ErklaerungenAnsicht: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        let liste = modell.erklaerungen.gemerkte
        Form {
            if liste.isEmpty {
                ContentUnavailableView("Keine Erklärung gemerkt", systemImage: "text.badge.checkmark",
                                       description: Text("Erklären Sie dem Assistenten einen Befund, der an Ihrer Anlage bekannt oder gewollt ist, oder tippen Sie in der Ansicht des Befunds auf „Erklären“."))
            }
            ForEach(liste) { e in
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack(alignment: .firstTextBaseline) {
                            Text(e.titel).font(.subheadline.weight(.medium))
                            Spacer()
                            zustand(e)
                        }
                        Text(e.orte.joined(separator: ", "))
                            .font(Schrift.datenKlein)
                            .foregroundStyle(Farbe.gedaempft)
                        ErklaerungsZitat(text: e.text)
                        Text("\(e.geltung) · gemerkt \(Format.tagMitZeit(e.gemerkt ?? e.erstellt))")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        if let grund = modell.erklaerungsgrund(e) {
                            Label(grund, systemImage: "exclamationmark.triangle")
                                .font(.caption)
                                .foregroundStyle(Farbe.warnung)
                        }
                    }
                    .padding(.vertical, 2)
                    Button("Erklärung löschen", systemImage: "trash", role: .destructive) {
                        withAnimation { modell.erklaerungLoeschen(e.id) }
                    }
                }
            }
            Section {
                Text("Eine Erklärung gilt nur für denselben Befund an denselben Orten und bis zu der Schwere, die er beim Erklären hatte. Kommt ein Ort hinzu, steigt die Schwere, läuft die Geltung ab oder steht der Befund länger als die Höchstdauer an, erscheint er wieder und löst eine Mitteilung aus.")
                    .font(.footnote)
                    .foregroundStyle(Farbe.gedaempft)
            }
        }
        .formularStil()
        .seitentitel("Erklärungen")
    }

    @ViewBuilder private func zustand(_ e: Erklaerung) -> some View {
        if modell.befundIstErklaert(e.befund) {
            Zustandsmarke(text: "Wirkt", symbol: "checkmark.circle.fill", farbe: Farbe.gut)
        } else if modell.erklaerungsgrund(e) != nil {
            Zustandsmarke(text: "Greift nicht", symbol: "exclamationmark.triangle.fill", farbe: Farbe.warnung)
        } else {
            Zustandsmarke(text: "Steht nicht an", symbol: "moon.zzz", farbe: .secondary)
        }
    }
}

// MARK: - In der Ansicht des Befunds

/// Die gemerkte Erklärung zum Befund oder der Knopf, ihn zu erklären
struct BefundErklaerungsabschnitt: View {
    @Environment(AppModell.self) private var modell
    let befund: Befund
    @State private var zeigeBlatt = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let e = modell.befundErklaerung(befund.id) {
                Kicker("Ihre Erklärung")
                ErklaerungsZitat(text: e.text)
                Text("\(e.geltung) · gemerkt \(Format.tagMitZeit(e.gemerkt ?? e.erstellt))")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                if let grund = modell.erklaerungsgrund(e) {
                    Label("\(grund) Der Befund wird deshalb wieder gemeldet.", systemImage: "exclamationmark.triangle")
                        .font(.callout)
                        .foregroundStyle(Farbe.warnung)
                        .fixedSize(horizontal: false, vertical: true)
                }
                HStack {
                    Button {
                        zeigeBlatt = true
                    } label: {
                        Label("Neu erklären", systemImage: "square.and.pencil")
                    }
                    .buttonStyle(.rahmen)
                    Button(role: .destructive) {
                        withAnimation { modell.erklaerungLoeschen(e.id) }
                    } label: {
                        Label("Löschen", systemImage: "trash")
                    }
                    .buttonStyle(.rahmen)
                }
            } else {
                Button {
                    zeigeBlatt = true
                } label: {
                    Label("Erklären …", systemImage: "text.badge.checkmark")
                }
                .buttonStyle(.rahmen)
                Text("Ist der Befund an Ihrer Anlage bekannt oder gewollt, erklären Sie ihn. Er erscheint dann nicht mehr im Lagebericht, und der Assistent berücksichtigt Ihre Erklärung.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .sheet(isPresented: $zeigeBlatt) {
            ErklaerenBlatt(befund: befund, vorgabe: modell.befundErklaerung(befund.id)) { text, tage, stunden in
                modell.befundErklaeren(befund, text: text, tage: tage, stunden: stunden)
            }
        }
    }
}

/// Erklärung ohne Gespräch: Wortlaut, Geltung und Höchstdauer
struct ErklaerenBlatt: View {
    @Environment(\.dismiss) private var schliessen
    let befund: Befund
    var vorgabe: Erklaerung?
    let merken: (String, Int, Int) -> Void
    @State private var text = ""
    @State private var tage = 0
    @State private var stunden = 0

    static let geltungen = [(0, "bis auf Widerruf"), (7, "7 Tage"), (30, "30 Tage"), (90, "90 Tage"), (365, "ein Jahr")]
    static let hoechstdauern = [(0, "ohne Grenze"), (6, "6 Stunden"), (12, "12 Stunden"), (14, "14 Stunden"), (24, "24 Stunden"), (48, "48 Stunden")]

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Text(befund.titel).font(.subheadline.weight(.medium))
                    Text(befund.ort).font(Schrift.datenKlein).foregroundStyle(Farbe.gedaempft)
                }
                Section {
                    TextField("Warum ist der Befund bekannt oder gewollt?", text: $text, axis: .vertical)
                        .lineLimit(3...8)
                } header: {
                    Text("Erklärung")
                } footer: {
                    Text("Etwa: Die Relais hängen hinter der Kesselregelung und sind nachts stromlos.")
                }
                Section {
                    Picker("Gilt", selection: $tage) {
                        ForEach(Self.geltungen, id: \.0) { Text($0.1).tag($0.0) }
                    }
                    Picker("Höchstens am Stück", selection: $stunden) {
                        ForEach(Self.hoechstdauern, id: \.0) { Text($0.1).tag($0.0) }
                    }
                } footer: {
                    Text("Die Erklärung gilt für diesen Befund an \(Erklaerung.orte(befund.ort).count == 1 ? "diesem Ort" : "diesen Orten") bis zur Schwere „\(befund.schwere.bezeichnung)“. Steht er länger als die Höchstdauer am Stück an, meldet die App ihn wieder; so fällt etwa ein Relais auf, das nicht nur nachts stromlos, sondern ausgefallen ist.")
                }
            }
            .formularStil()
            .seitentitel("Befund erklären")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Abbrechen") { schliessen() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Merken") {
                        merken(text, tage, stunden)
                        schliessen()
                    }
                    .disabled(text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
            }
            .onAppear {
                guard let vorgabe else { return }
                text = vorgabe.text
                stunden = vorgabe.hoechstdauer.map { Int(($0 / 3600).rounded()) } ?? 0
                if !Self.hoechstdauern.contains(where: { $0.0 == stunden }) { stunden = 0 }
            }
        }
    }
}
