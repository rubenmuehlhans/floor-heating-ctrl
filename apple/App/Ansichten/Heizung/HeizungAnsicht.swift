import SwiftUI
import Anlage
import Geraeteschnittstelle

struct HeizungAnsicht: View {
    @Environment(AppModell.self) private var modell

    private let kennzahlen = [GridItem(.adaptive(minimum: 150), spacing: 10, alignment: .top)]

    var body: some View {
        let a = modell.anlage
        Group {
            if a.kessel == nil && a.speicher == nil && a.heizkreise.isEmpty {
                ContentUnavailableView {
                    Label("Kein Heizungsgerät eingebunden", systemImage: "flame")
                } description: {
                    Text("Kessel, Pufferspeicher und Heizkreise erscheinen hier, sobald ein Heizungsgerät eingebunden ist.")
                } actions: {
                    Button("Gerät hinzufügen") { modell.zeigeEinrichtung = true }
                }
            } else {
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        VStack(alignment: .leading, spacing: 12) {
                            Kartenkopf(titel: "Anlage", erklaerung: "Kessel, Pufferspeicher und die angelegten Heizkreise. Messwerte stehen dort, wo der Fühler sitzt.")
                            Anlagenschema(anlage: a)
                            Divider().overlay(Farbe.linie)
                            AussenZeile(aussen: a.aussen)
                        }
                        .padding(16)
                        .karte()

                        if let speicher = a.speicher {
                            SpeicherKarte(speicher: speicher)
                        }
                        if let kessel = a.kessel {
                            BrennerKarte(kessel: kessel)
                        }

                        if !a.heizkreise.isEmpty || a.kesselkreispumpe != nil {
                            Abschnitt(titel: "Pumpen")
                        }
                        ForEach(a.heizkreise) { kreis in
                            HeizkreisKarte(kreis: kreis)
                        }
                        if let pumpe = a.kesselkreispumpe {
                            KesselkreispumpeKarte(pumpe: pumpe)
                        }

                        Abschnitt(titel: "Auswertung")
                        VStack(alignment: .leading, spacing: 0) {
                            Kartenlink(titel: "Verlauf", symbol: "chart.xyaxis.line", ziel: .verlauf, trenner: false)
                            Kartenlink(titel: "Verbrauchslinie", symbol: "chart.dots.scatter", ziel: .verbrauchslinie)
                            Kartenlink(titel: "Wärmepumpen-Check", symbol: "heat.waves", ziel: .waermepumpencheck)
                            Kartenlink(titel: "Ladungs- und Tagesprotokoll", symbol: "list.bullet.rectangle", ziel: .protokolle)
                            Kartenlink(titel: "Ladung aufzeichnen", symbol: "record.circle", ziel: .aufzeichnung)
                            if let abstand = a.kessel?.abgasAbstand {
                                HStack {
                                    Text("Abgas-Vorlauf-Abstand")
                                    Spacer()
                                    Text(abstand.jetzt.map { Format.kelvin($0) } ?? "ab 10 Ladungen (\(abstand.ladungen) erfasst)")
                                        .font(Schrift.datenKlein)
                                        .foregroundStyle(Farbe.gedaempft)
                                }
                                .padding(.top, 12)
                            }
                        }
                        .padding(16)
                        .karte()
                    }
                    .padding()
                    .frame(maxWidth: 1100)
                    .frame(maxWidth: .infinity)
                }
            }
        }
        .background(Farbe.grund)
        .navigationTitle("Heizung")
        .fragenKnopf("Heizung")
    }
}

/// Die Außentemperatur gehört zur Anlage, nicht zu einem Heizkreis: Sie geht in keine
/// Ventilstellung ein, sondern in Heizgradtage, Verbrauchslinie und Ladungsprotokoll.
struct AussenZeile: View {
    let aussen: Aussenwerte?

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Label("Außen", systemImage: "thermometer.snowflake")
                .font(.subheadline.weight(.medium))
                .foregroundStyle(aussen == nil ? Farbe.gedaempft : Farbe.kaelte)
            if let aussen {
                Messzahl(zahl: Format.zahl(aussen.temperatur), einheit: "°C", font: Schrift.wert,
                         farbe: Farbe.kaelte)
                Spacer(minLength: 8)
                VStack(alignment: .trailing, spacing: 2) {
                    Text(aussen.quelle)
                    Text(zusatz(aussen))
                }
                .font(Schrift.datenKlein)
                .foregroundStyle(Farbe.gedaempft)
                .multilineTextAlignment(.trailing)
            } else {
                Spacer(minLength: 8)
                Text("kein Außenfühler zugeordnet")
                    .font(Schrift.datenKlein)
                    .foregroundStyle(Farbe.gedaempft)
            }
        }
        .accessibilityElement(children: .combine)
    }

    private func zusatz(_ a: Aussenwerte) -> String {
        var teile: [String] = []
        if let f = a.feuchte, f > 0 {
            teile.append("Feuchte \(Format.mitEinheit(Format.zahl(f, stellen: 0), "%"))")
        }
        teile.append("vor \(Format.dauer(a.alter))")
        return teile.joined(separator: " · ")
    }
}

struct SpeicherKarte: View {
    let speicher: Speicher
    private let kennzahlen = [GridItem(.adaptive(minimum: 150), spacing: 10, alignment: .top)]

    var body: some View {
        let s = speicher
        VStack(alignment: .leading, spacing: 12) {
            Kartenkopf(titel: "Pufferspeicher", erklaerung: "Der Füllstand ist eine Schätzung aus dem Pufferfühler. Ob eine Ladung läuft und wann sie fertig ist, ergibt sich aus Brennerzustand und Kesselspreizung.")
            Zustandszeile(text: s.phase, zusatz: s.phaseSeit.map { "seit \(Format.dauer($0))" },
                          farbe: s.phase == "wird geladen" ? Farbe.waerme : Farbe.blass)
            LazyVGrid(columns: kennzahlen, spacing: 10) {
                Kennzahl(titel: "Füllstand", wert: Format.prozentzahl(s.ladung), einheit: "%",
                         fuss: "geschätzt aus dem Pufferfühler", farbe: Farbe.waerme)
                Kennzahl(titel: "Temperatur", wert: s.temperatur.map { Format.zahl($0) } ?? "–", einheit: "°C",
                         fuss: s.korrektur.flatMap { $0 == 0 ? nil : "Fühler oben, Korrektur \(Format.kelvin($0, vorzeichen: true))" } ?? "Fühler oben")
                Kennzahl(titel: "Leer / Voll", wert: grenzen(s), einheit: "°C",
                         fuss: s.voll == nil ? "noch nicht gelesen" : s.leerGelernt ? "Leerpunkt gelernt" : "eingestellt")
                Kennzahl(titel: "Ladungsende", wert: s.hoechstwertLadung.map { Format.zahl($0) } ?? "–", einheit: "°C",
                         fuss: "Höchstwert der Kalibrierung", farbe: (s.hoechstwertLadung ?? 0) > (s.voll ?? .infinity) ? Farbe.warnung : Farbe.tinte)
            }
            Fortschritt(anteil: s.ladung ?? 0, hoehe: 8)
            if s.warmwasserKnapp {
                Zustandsmarke(text: "Warmwasserreserve knapp", symbol: "drop.triangle", farbe: Farbe.warnung)
            }
            Kartenlink(titel: "Speichereinstellungen", symbol: "slider.horizontal.3", ziel: .einstellungen("speicher"))
        }
        .padding(16)
        .karte()
    }

    /// „35/62“, halbe Grad mit Nachkommastelle
    private func grenzen(_ s: Speicher) -> String {
        guard let leer = s.leer, let voll = s.voll else { return "–" }
        let grad = { (w: Double) in Format.zahl(w, stellen: w.rounded() == w ? 0 : 1) }
        return "\(grad(leer))/\(grad(voll))"
    }
}

struct BrennerKarte: View {
    let kessel: Kessel
    private let kennzahlen = [GridItem(.adaptive(minimum: 150), spacing: 10, alignment: .top)]

    var body: some View {
        let k = kessel
        VStack(alignment: .leading, spacing: 12) {
            Kartenkopf(titel: "Brenner", erklaerung: "Erkannt am Abgasfühler: Liegt er deutlich über der gleitenden Bezugslinie, läuft der Brenner. Die Steuerung greift nicht in Kessel und Brenner ein.")
            Zustandszeile(text: k.brennerLaeuft ? "läuft" : "aus", zusatz: "seit \(Format.dauer(k.brennerSeit))",
                          farbe: k.brennerLaeuft ? Farbe.waerme : Farbe.blass)
            LazyVGrid(columns: kennzahlen, spacing: 10) {
                Kennzahl(titel: "Abgas", wert: k.abgas.map { Format.zahl($0) } ?? "–", einheit: "°C",
                         fuss: "Bezugslinie \(Format.temperatur(k.bezugslinie))", farbe: Farbe.waerme)
                Kennzahl(titel: "Heute", wert: Format.dauer(k.laufzeitHeute),
                         fuss: "\(Format.starts(k.startsHeute)) · \(Format.liter(k.literHeute, stellen: 2))")
                Kennzahl(titel: "Gestern", wert: Format.dauer(k.laufzeitGestern),
                         fuss: Format.starts(k.startsGestern))
                Kennzahl(titel: "Düse", wert: k.duese.flatMap { $0 > 0 ? Format.zahl($0, stellen: 2) : nil } ?? "–", einheit: "l/h",
                         fuss: (k.duese ?? 0) > 0 ? "angenommen, nicht gemessen" : "nicht eingetragen")
            }
            if k.taktbetrieb {
                Zustandsmarke(text: "Taktbetrieb: viele kurze Starts", symbol: "exclamationmark.triangle", farbe: Farbe.warnung)
            }
            Kartenlink(titel: "Brennererkennung", symbol: "slider.horizontal.3", ziel: .einstellungen("brenner"))
        }
        .padding(16)
        .karte()
    }
}

/// Zeile mit Pfeil innerhalb einer Karte, die auf eine Unterseite führt.
struct Kartenlink: View {
    let titel: String
    let symbol: String
    let ziel: Ziel
    var trenner = true

    var body: some View {
        VStack(spacing: 0) {
            if trenner { Divider().overlay(Farbe.linie) }
            NavigationLink(value: ziel) {
                HStack {
                    Label(titel, systemImage: symbol)
                        .foregroundStyle(Color.accentColor)
                    Spacer()
                    Image(systemName: "chevron.right")
                        .font(.caption)
                        .foregroundStyle(Farbe.blass)
                }
                .padding(.vertical, 11)
                .contentShape(.rect)
            }
            .buttonStyle(.plain)
        }
    }
}

extension Heizkreis {
    var pumpenmarke: (text: String, symbol: String, farbe: Color) {
        if relais.gestoert { return ("Relais gestört", "xmark.octagon", Farbe.stoerung) }
        if gesperrt { return ("Von der Kesselregelung gesperrt", "lock", Farbe.warnung) }
        if relais.stromlos { return ("Pumpe aus, Relais ohne Strom", "powerplug", Farbe.gedaempft) }
        if pumpeLaeuft { return ("Pumpe läuft", "fanblades", Farbe.waerme) }
        return ("Pumpe aus", "power", Farbe.gedaempft)
    }
}

struct HeizkreisKarte: View {
    @Environment(AppModell.self) private var modell
    let kreis: Heizkreis
    private let spalten = [GridItem(.adaptive(minimum: 110), spacing: 10, alignment: .top)]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text(kreis.name).font(Schrift.kartentitel)
                Spacer()
                let m = kreis.pumpenmarke
                Zustandsmarke(text: m.text, symbol: m.symbol, farbe: m.farbe)
            }
            Text("Versorgt \(kreis.versorgteVerteiler.joined(separator: " und ")) · \(kreis.grund)")
                .font(Schrift.datenKlein)
                .foregroundStyle(Farbe.gedaempft)
            Picker("Betriebsart", selection: Binding(
                get: { kreis.betriebsart },
                set: { modell.heizkreisBetriebsart(kreis.nummer, $0) }
            )) {
                ForEach(Pumpenbetriebsart.allCases, id: \.self) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            LazyVGrid(columns: spalten, spacing: 10) {
                Kennzahl(titel: "Vorlauf", wert: Format.zahl(kreis.vorlauf ?? 0), einheit: "°C", farbe: Farbe.waerme)
                Kennzahl(titel: "Rücklauf", wert: Format.zahl(kreis.ruecklauf ?? 0), einheit: "°C", farbe: Farbe.kaelte)
                Kennzahl(titel: "Spreizung", wert: Format.zahl(kreis.spreizung ?? 0), einheit: "K",
                         fuss: (kreis.spreizung ?? 0) < 0 ? "Vorlauf kälter als Rücklauf" : nil,
                         farbe: (kreis.spreizung ?? 0) < 0 ? Farbe.warnung : Farbe.tinte)
            }
            if kreis.keinVerteilerErreicht {
                Zustandsmarke(text: "Kein versorgter Verteiler erreicht", symbol: "wifi.exclamationmark", farbe: Farbe.stoerung)
            } else if kreis.bedarfVeraltet {
                Zustandsmarke(text: "Ein versorgter Verteiler antwortet nicht", symbol: "wifi.exclamationmark", farbe: Farbe.warnung)
            }
            if kreis.gesperrt {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Image(systemName: "lock").foregroundStyle(Farbe.warnung)
                    Text("Die Räume fordern Wärme an, aber die Kesselregelung gibt die Pumpe nicht frei: Das Relais \(kreis.relais.adresse) ist ohne Strom.")
                        .font(.subheadline)
                        .fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 0)
                }
                .padding(10)
                .background(Farbe.warnung.opacity(0.08), in: .rect(cornerRadius: 8))
            }
            if kreis.relais.gestoert {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Image(systemName: "xmark.octagon").foregroundStyle(Farbe.stoerung)
                    Text("Relais \(kreis.relais.adresse) antwortet nicht; die Pumpe folgt der Steuerung nicht.")
                        .font(.subheadline)
                        .fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 0)
                }
                .padding(10)
                .background(Farbe.stoerung.opacity(0.08), in: .rect(cornerRadius: 8))
                .overlay { RoundedRectangle(cornerRadius: 8).strokeBorder(Farbe.stoerung.opacity(0.35), lineWidth: 1) }
            }
            Kartenlink(titel: "Verlauf und Heizkurve", symbol: "chart.xyaxis.line", ziel: .heizkreisverlauf(kreis.nummer))
            Kartenlink(titel: "Heizkreis bearbeiten", symbol: "slider.horizontal.3", ziel: .heizkreis(kreis.nummer))
        }
        .padding(16)
        .karte()
    }
}

struct KesselkreispumpeKarte: View {
    @Environment(AppModell.self) private var modell
    let pumpe: Pumpe

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text(pumpe.name).font(Schrift.kartentitel)
                Spacer()
                if pumpe.relais.gestoert {
                    Zustandsmarke(text: "Relais gestört", symbol: "xmark.octagon", farbe: Farbe.stoerung)
                } else if pumpe.relais.stromlos {
                    Zustandsmarke(text: "Pumpe aus, Relais ohne Strom", symbol: "powerplug", farbe: Farbe.gedaempft)
                } else {
                    Zustandsmarke(text: pumpe.laeuft ? "Pumpe läuft" : "Pumpe aus", symbol: pumpe.laeuft ? "fanblades" : "power",
                                  farbe: pumpe.laeuft ? Farbe.waerme : Farbe.gedaempft)
                }
            }
            Text("Läuft, solange der Kesselvorlauf \(Format.kelvin(pumpe.einschaltschwelle)) wärmer ist als der Speicher, und geht unter \(Format.kelvin(pumpe.ausschaltschwelle)) aus. Ohne gültige Messwerte oder über \(Format.temperatur(pumpe.notgrenze, stellen: 0)) läuft sie immer.")
                .font(Schrift.erklaerung)
                .foregroundStyle(Farbe.gedaempft)
                .fixedSize(horizontal: false, vertical: true)
            Picker("Betriebsart", selection: Binding(
                get: { pumpe.betriebsart },
                set: { modell.kesselkreispumpeBetriebsart($0) }
            )) {
                ForEach(Pumpenbetriebsart.allCases, id: \.self) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            Text("Zuletzt: \(pumpe.grund)")
                .font(Schrift.datenKlein)
                .foregroundStyle(Farbe.gedaempft)
            Kartenlink(titel: "Schaltpunkte", symbol: "slider.horizontal.3", ziel: .einstellungen("kkp"))
        }
        .padding(16)
        .karte()
    }
}

struct RelaisZeile: View {
    let relais: Relais

    var body: some View {
        LabeledContent("Relais") {
            VStack(alignment: .trailing, spacing: 2) {
                Text("\(relais.weg) \(relais.adresse), Kanal \(relais.kanal)")
                    .font(Schrift.daten)
                    .foregroundStyle(Farbe.gedaempft)
                if relais.erreichbar {
                    Text(relais.ein ? "ein" : "aus").foregroundStyle(Farbe.gedaempft)
                } else if relais.stromlos {
                    Text("ohne Strom" + (relais.stromlosSeit.map { " seit \(Format.dauer($0))" } ?? ""))
                        .foregroundStyle(Farbe.gedaempft)
                } else {
                    Label("nicht erreichbar", systemImage: "xmark.octagon")
                        .foregroundStyle(Farbe.stoerung)
                }
            }
            .font(.callout)
        }
    }
}

struct HeizkreisAnsicht: View {
    @Environment(AppModell.self) private var modell
    let nummer: Int
    @State private var peers: [String]?
    @State private var frageLoeschen = false
    @State private var pruefung: Tasmota.Pruefung?
    @State private var prueft = false
    @State private var pruefFehler: String?
    @State private var frageRegel = false

    var body: some View {
        if let kreis = modell.anlage.heizkreise.first(where: { $0.nummer == nummer }) {
            let gespeichert = konfigPeers
            let aktuell = peers ?? gespeichert
            Form {
                Section {
                    ForEach(verteilerwahl(aktuell)) { v in
                        Toggle(isOn: Binding(
                            get: { aktuell.contains(v.id) },
                            set: { an in
                                var neu = aktuell.filter { $0 != v.id }
                                if an { neu.append(v.id) }
                                peers = neu
                            })) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(v.name)
                                if let hinweis = v.hinweis {
                                    Text(hinweis).font(.caption).foregroundStyle(.secondary)
                                }
                            }
                        }
                    }
                    // Als Menge verglichen: Aus- und wieder Einschalten ändert nur die Reihenfolge.
                    if let auswahl = peers, Set(auswahl) != Set(gespeichert) {
                        Button("Versorgte Verteiler speichern", systemImage: "checkmark") {
                            modell.versorgteVerteilerSetzen(nummer, aktuell)
                            peers = nil
                        }
                        .disabled(aktuell.count > 4)
                    }
                } header: {
                    Text("Versorgte Verteiler")
                } footer: {
                    Text("Meldet einer dieser Verteiler Wärmebedarf, läuft die Pumpe. Ohne Zuordnung entsteht kein Bedarf, und die Pumpe läuft nur auf „Ein“ oder zum Frostschutz. Zur Wahl stehen die eingebundenen Verteiler und die, die das Heizungsgerät im Netz sieht; gespeicherte Zuordnungen zu Verteilern, die gerade niemand sieht, bleiben erhalten.")
                }
                Section {
                    RelaisZeile(relais: kreis.relais)
                    if let pruefung {
                        Kennwert(titel: "Schaltzustand", wert: pruefung.ein.map { $0 ? "ein" : "aus" } ?? "unbekannt")
                        Kennwert(titel: "Nach Stromausfall", wert: pruefung.einschaltzustand == 1 ? "schaltet ein" : "PowerOnState \(pruefung.einschaltzustand.map(String.init) ?? "–")",
                                 farbe: pruefung.einschaltzustand == 1 ? nil : Farbe.warnung)
                        Kennwert(titel: "Ausfallregel", wert: pruefung.regelAktiv && pruefung.regelPasst ? "eingerichtet" : pruefung.regelPasst ? "hinterlegt, aber aus" : "fehlt",
                                 farbe: pruefung.inOrdnung ? Farbe.gut : Farbe.warnung)
                        if !pruefung.inOrdnung {
                            Button("Ausfallregel einrichten …", systemImage: "wrench.and.screwdriver") { frageRegel = true }
                        }
                    }
                    if let pruefFehler {
                        Label(pruefFehler, systemImage: "xmark.octagon.fill").foregroundStyle(Farbe.stoerung)
                    }
                    Button {
                        pruefen()
                    } label: {
                        HStack {
                            Label("Relais prüfen", systemImage: "stethoscope")
                            if prueft { Spacer(); ProgressView().controlSize(.small) }
                        }
                    }
                    .disabled(prueft)
                } header: {
                    Text("Relais")
                } footer: {
                    Text("Die Ausfallregel schaltet die Pumpe im Relais ein, wenn das Heizungsgerät eine Viertelstunde lang kein Lebenszeichen sendet. Die Prüfung fragt das Relais unmittelbar ab.")
                }
                Section {
                    NavigationLink(value: Ziel.einstellungen("heizkreis", eintrag: "\(nummer)")) {
                        Label("Pumpenlogik und Relais", systemImage: "slider.horizontal.3")
                    }
                    Button("Heizkreis löschen …", systemImage: "trash", role: .destructive) { frageLoeschen = true }
                }
            }
            .formularStil()
            .navigationTitle(kreis.name)
            .fragenKnopf(kreis.name)
            .confirmationDialog("„\(kreis.name)“ löschen?", isPresented: $frageLoeschen, titleVisibility: .visible) {
                Button("Löschen", role: .destructive) { modell.heizkreisLoeschen(nummer) }
            } message: {
                Text("Seine Pumpe wird dabei abgeschaltet; die zugeordneten Fühler bleiben stehen.")
            }
            .confirmationDialog("Ausfallregel einrichten?", isPresented: $frageRegel, titleVisibility: .visible) {
                Button("Einrichten") {
                    Task {
                        do {
                            try await modell.ausfallregelEinrichten(nummer)
                            modell.melden("Ausfallregel eingerichtet.")
                            pruefen()
                        } catch {
                            pruefFehler = error.localizedDescription
                        }
                    }
                }
            } message: {
                Text("Die App setzt am Relais „PowerOnState 1“ und die Regel aus dem Handbuch: Bleibt das Lebenszeichen 15 Minuten aus, schaltet Kanal \(kreis.relais.kanal) ein. Eine vorhandene Regel 1 wird ersetzt.")
            }
        }
    }

    struct Verteilerwahl: Identifiable {
        var id: String
        var name: String
        var hinweis: String?
    }

    /// Eingebundene Verteiler, dann die, die das Heizungsgerät sieht, dann gespeicherte
    /// Kennungen, die keiner der beiden kennt. Die Weboberfläche bietet nur die gefundenen an und
    /// verliert beim Speichern die Zuordnung eines Verteilers, der gerade nicht antwortet.
    private func verteilerwahl(_ aktuell: [String]) -> [Verteilerwahl] {
        var liste = modell.anlage.etagen.map { Verteilerwahl(id: $0.id, name: $0.name) }
        let geraet = modell.istBeispiel ? nil : modell.betrieb.heizkreisgeraet(nummer)
        for n in geraet.flatMap({ modell.betrieb.staende[$0]?.nachbarn }) ?? [] where n.rolle == "manifold" {
            guard let id = n.id, !liste.contains(where: { $0.id == id }) else { continue }
            liste.append(Verteilerwahl(id: id, name: n.ort.flatMap { $0.isEmpty ? nil : $0 } ?? id,
                                       hinweis: "im Netz gefunden, nicht in der App eingebunden · \(id)"))
        }
        for id in aktuell where !liste.contains(where: { $0.id == id }) {
            liste.append(Verteilerwahl(id: id, name: id, hinweis: "zurzeit nicht im Netz"))
        }
        return liste
    }

    /// Die gespeicherten Kennungen, auch die der nicht eingebundenen Verteiler
    private var konfigPeers: [String] {
        let geraet = modell.istBeispiel ? modell.speicherID : modell.betrieb.heizkreisgeraet(nummer)
        return geraet.flatMap { modell.konfiguration($0)?["circuits"]?.alsListe?.first { $0["id"]?.alsGanzzahl == nummer }?["peers"]?.alsListe?.compactMap(\.alsText) } ?? []
    }

    private func pruefen() {
        prueft = true
        pruefFehler = nil
        Task {
            do {
                pruefung = try await modell.relaisPruefen(nummer)
            } catch {
                pruefung = nil
                pruefFehler = error.localizedDescription
            }
            prueft = false
        }
    }
}
