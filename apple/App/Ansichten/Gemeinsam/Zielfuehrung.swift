import SwiftUI

// Zweispaltig steht rechts neben einer Seite, was aus ihr geöffnet wurde, sofern es nur eine Spalte
// füllt: ein Befund, ein Gerät, ein Formular. Einspaltig liegen beide im Stapel übereinander wie
// bisher. Beim Auf- und Zuklappen wandert die rechte Seite zwischen Spalte und Stapel.

/// Ein geöffnetes Ziel und die Spalte, aus der es geöffnet wurde.
struct Weg: Hashable {
    var ziel: Ziel
    var aus: Spalte?
}

extension Ziel {
    /// Seiten, die eine Spalte füllen: Formulare, Einträge einer Liste, Systemseiten.
    var fuelltEineSpalte: Bool {
        switch self {
        case .befund, .geraet, .leitstand, .kanal, .messfahrt, .sensoren, .raumverwaltung, .fuehler,
             .heizkreis, .einstellung, .system, .appEinstellungen, .analysepaket:
            true
        default:
            false
        }
    }

    /// Seiten mit rechter Spalte für das, was aus ihnen geöffnet wird.
    var hatNebenspalte: Bool {
        switch self {
        case .raum, .befunde: true
        default: false
        }
    }

    /// Seiten, die zweispaltig selbst beide Spalten belegen; alle anderen bleiben links der
    /// Teilung, damit nichts über der Faltlinie liegt.
    var belegtBeideSpalten: Bool {
        switch self {
        case .raum, .befunde, .verlauf: true
        default: false
        }
    }
}

extension AppModell.Bereich {
    /// Bereiche, deren erste Seite eine rechte Spalte für Geöffnetes hat
    var hatNebenspalte: Bool {
        switch self {
        case .uebersicht, .heizung, .geraete: true
        case .raeume, .assistent: false
        }
    }
}

/// Die geöffneten Seiten eines Bereichs. `haupt` belegt das Fenster; `neben` steht zweispaltig in der
/// rechten Spalte der obersten Hauptseite und einspaltig darüber im Stapel.
struct Stapel: Equatable {
    var haupt: [Weg] = []
    var neben: [Weg] = []

    func pfad(zweispaltig: Bool) -> [Weg] {
        zweispaltig ? haupt : haupt + neben
    }

    /// Übernimmt den Pfad, den der `NavigationStack` nach einem Öffnen oder Zurück meldet.
    mutating func setzen(_ neu: [Weg], zweispaltig: Bool, wurzelHatNebenspalte: Bool) {
        if zweispaltig {
            if neu.count == haupt.count + 1, Array(neu.dropLast()) == haupt, let weg = neu.last,
               weg.ziel.fuelltEineSpalte, (haupt.last?.ziel.hatNebenspalte ?? wurzelHatNebenspalte) {
                // Aus der rechten Spalte geöffnet: darüber; aus der linken: an ihrer Stelle
                if weg.aus == .rechts { neben.append(weg) } else { neben = [weg] }
            } else if neu != haupt {
                haupt = neu
                neben = []
            }
        } else {
            // Seiten für eine Spalte am Ende des Stapels gehören zur Seite davor, wenn sie eine
            // rechte Spalte hat; so stehen sie nach dem Aufklappen dort.
            var i = neu.count
            while i > 0, neu[i - 1].ziel.fuelltEineSpalte { i -= 1 }
            let traeger = i > 0 ? neu[i - 1].ziel.hatNebenspalte : wurzelHatNebenspalte
            if i < neu.count, traeger {
                haupt = Array(neu[..<i])
                neben = Array(neu[i...])
            } else {
                haupt = neu
                neben = []
            }
        }
    }
}

// MARK: - Umgebung

extension EnvironmentValues {
    /// Bereich des `NavigationStack`, in dem die Seite steht
    @Entry var bereich: AppModell.Bereich? = nil
    /// Spalte, in der ein Verweis steht; bestimmt, ob er die rechte Spalte ersetzt oder darüber öffnet
    @Entry var zielspalte: Spalte? = nil
    /// Was die rechte Spalte gerade zeigt; der Verweis darauf ist links hervorgehoben
    @Entry var angezeigtesZiel: Ziel? = nil
    /// Die Seiten der rechten Spalte, nur für die oberste Seite des Stapels
    @Entry var nebenspalte: [Weg]? = nil
    /// Die Seite steht in der rechten Spalte einer anderen; Titel und Werkzeugleiste gehören dann
    /// der Seite, die sie trägt.
    @Entry var eingebettet = false
}

/// Verweis auf ein Ziel; ersetzt `NavigationLink(value:)` und merkt sich die Spalte.
struct ZielLink<Beschriftung: View>: View {
    @Environment(\.zielspalte) private var spalte
    @Environment(\.angezeigtesZiel) private var angezeigt
    let ziel: Ziel
    @ViewBuilder var beschriftung: Beschriftung

    init(_ ziel: Ziel, @ViewBuilder beschriftung: () -> Beschriftung) {
        self.ziel = ziel
        self.beschriftung = beschriftung()
    }

    var body: some View {
        NavigationLink(value: Weg(ziel: ziel, aus: spalte)) {
            beschriftung
        }
        .listRowBackground(spalte == .links && angezeigt == ziel ? Farbe.akzentWeich : nil)
    }
}

/// Gibt einer Seite ihren Platz im Stapel bekannt: Die oberste erhält die Seiten ihrer rechten
/// Spalte. Seiten, die nicht selbst zwei Spalten belegen, bleiben zweispaltig links der Teilung.
struct Seitenrahmen<Inhalt: View>: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.bereich) private var bereich
    @Environment(\.spaltenmass) private var mass
    let weg: Weg?
    @ViewBuilder var inhalt: Inhalt

    var body: some View {
        let stapel = bereich.map { modell.stapel[$0, default: Stapel()] }
        let oben = stapel.map { $0.haupt.last == weg } ?? false
        Group {
            if let weg, mass.spalten == 2, !weg.ziel.belegtBeideSpalten {
                inhalt
                    .padding(.trailing, mass.fuge)
                    .frame(width: mass.teilung)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
                    .background(Farbe.grund)
            } else {
                inhalt
            }
        }
        .environment(\.nebenspalte, oben ? stapel?.neben : nil)
    }
}

extension View {
    /// Alle Ziele eines Bereichs; einmal je `NavigationStack` angebracht.
    func zielnavigation() -> some View {
        navigationDestination(for: Weg.self) { weg in
            Seitenrahmen(weg: weg) { ZielAnsicht(ziel: weg.ziel) }
        }
    }

    /// Titel der Seite; in der rechten Spalte zeigt ihn deren Kopf statt der Navigationsleiste.
    func seitentitel(_ titel: String) -> some View {
        modifier(SeitentitelSetzen(titel: titel))
    }
}

private struct SeitentitelSchluessel: PreferenceKey {
    static let defaultValue = ""
    static func reduce(value: inout String, nextValue: () -> String) {
        if value.isEmpty { value = nextValue() }
    }
}

private struct SeitentitelSetzen: ViewModifier {
    @Environment(\.eingebettet) private var eingebettet
    let titel: String

    func body(content: Content) -> some View {
        if eingebettet {
            content.preference(key: SeitentitelSchluessel.self, value: titel)
        } else {
            content.navigationTitle(titel)
        }
    }
}

// MARK: - Rechte Spalte

/// Eine Seite in der rechten Spalte, mit Kopf aus Zurück und Titel.
struct Nebenseite: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.bereich) private var bereich
    let ziel: Ziel
    /// Zurück zur vorigen Seite der Spalte; ohne, wenn die Seite dort von selbst steht
    var zurueck = true
    @State private var titel = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 10) {
                if zurueck, let bereich {
                    Button {
                        withAnimation(.snappy) { _ = modell.stapel[bereich]?.neben.popLast() }
                    } label: {
                        Label("Zurück", systemImage: "chevron.left")
                            .labelStyle(.iconOnly)
                            .fontWeight(.semibold)
                            .frame(width: 32, height: 32)
                            .contentShape(.circle)
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(Color.accentColor)
                    .background(Farbe.flaeche, in: .circle)
                    .overlay { Circle().strokeBorder(Farbe.linie) }
                }
                Text(titel)
                    .font(.headline)
                    .lineLimit(2)
            }
            .padding(.horizontal, 16)
            .padding(.top, 10)
            .padding(.bottom, 4)
            ZielAnsicht(ziel: ziel)
                .environment(\.eingebettet, true)
                .environment(\.zielspalte, .rechts)
                .environment(\.angezeigtesZiel, nil)
                .onPreferenceChange(SeitentitelSchluessel.self) { titel = $0 }
        }
        .background(Farbe.grund)
        .id(ziel)
    }
}

/// Listenseiten: zweispaltig links die Liste, rechts der gewählte Eintrag, ohne Auswahl der erste.
/// Einspaltig nur die Liste; ein Eintrag öffnet sich darüber.
struct ListeUndEintrag<Liste: View>: View {
    @Environment(\.spaltenmass) private var mass
    @Environment(\.nebenspalte) private var nebenspalte
    /// Eintrag, der ohne Auswahl rechts steht
    let vorgabe: Ziel?
    @ViewBuilder var liste: Liste

    var body: some View {
        if mass.spalten == 2 {
            let gewaehlt = nebenspalte?.last
            let angezeigt = nebenspalte?.first?.ziel ?? vorgabe
            HStack(spacing: 0) {
                liste
                    .environment(\.zielspalte, .links)
                    .environment(\.angezeigtesZiel, angezeigt)
                    .padding(.trailing, mass.fuge)
                    .frame(width: mass.teilung)
                Group {
                    if let gewaehlt {
                        // Zurück erst ab der zweiten Seite; zur ersten führt die Liste
                        Nebenseite(ziel: gewaehlt.ziel, zurueck: (nebenspalte?.count ?? 0) > 1)
                    } else if let vorgabe {
                        Nebenseite(ziel: vorgabe, zurueck: false)
                    } else {
                        Color.clear
                    }
                }
                .padding(.leading, mass.fuge)
                .padding(.trailing, max(mass.rand - 16, 0))
                .frame(maxWidth: .infinity)
            }
            .background(Farbe.grund)
        } else {
            liste
        }
    }
}
