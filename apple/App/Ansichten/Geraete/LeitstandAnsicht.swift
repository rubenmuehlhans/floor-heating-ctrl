import SwiftUI
import Anlage
import Geraeteschnittstelle

/// Der Leitstand in der Geräteliste
struct LeitstandZeile: View {
    let eintrag: BekannterLeitstand
    let stand: Leitstandbetrieb.Stand?

    var body: some View {
        let erreichbar = stand?.erreichbar ?? false
        HStack(spacing: 12) {
            Image(systemName: Geraetesymbol.leitstand)
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(eintrag.ort)
                Text(untertitel)
                    .font(.caption)
                    .foregroundStyle(stand?.fehler == nil ? Color.secondary : Farbe.stoerung)
            }
            Spacer()
            if stand?.zustand != nil || stand?.fehler != nil {
                Image(systemName: erreichbar ? "circle.fill" : "xmark.octagon.fill")
                    .font(erreichbar ? .caption2 : .body)
                    .foregroundStyle(erreichbar ? Farbe.gut : Farbe.stoerung)
                    .accessibilityLabel(erreichbar ? "erreichbar" : "nicht erreichbar")
            } else {
                ProgressView().controlSize(.small)
            }
        }
    }

    private var untertitel: String {
        let anschrift = Leitstandanschrift.text(eintrag.adresse)
        if stand?.fehler != nil { return "\(anschrift) · nicht erreichbar" }
        if let a = stand?.zustand?.aussen, a.gueltig == true, let t = a.temperaturC {
            return "\(anschrift) · außen \(Format.temperatur(t))"
        }
        return [anschrift, stand?.zustand?.version ?? eintrag.firmware ?? ""].filter { !$0.isEmpty }.joined(separator: " · ")
    }
}

/// Ein Leitstand, wie ihn die Einrichtung aufgenommen hat
struct LeitstandEingebundenZeile: View {
    let leitstand: BekannterLeitstand

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: Geraetesymbol.leitstand)
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(leitstand.ort == "Leitstand" ? "Leitstand" : "Leitstand \(leitstand.ort)")
                Text([Leitstandanschrift.text(leitstand.adresse), leitstand.firmware ?? "", leitstand.id]
                    .filter { !$0.isEmpty }.joined(separator: " · "))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }
}

enum Leitstandanschrift {
    /// Adresse ohne „http://“; der Port nur, wenn er vom üblichen abweicht.
    static func text(_ url: URL) -> String {
        let host = url.host() ?? ""
        return url.port.map { "\(host):\($0)" } ?? host
    }
}

/// Zustand, Außenfühler, Funkthermometer und Anzeige des Leitstands
struct LeitstandAnsicht: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.dismiss) private var zurueck
    let leitstandID: String
    @State private var frageKopplungen = false
    @State private var schluesselFuer: Schluesselziel?
    @State private var auswahl: Funkthermometer?
    @State private var bild: Image?
    @State private var bildFehler: String?
    @State private var bildLaedt = false
    @State private var entfernenFragen = false

    var body: some View {
        if let stand = modell.leitstandbetrieb.staende[leitstandID] {
            inhalt(stand)
        } else {
            ContentUnavailableView("Leitstand nicht eingebunden", systemImage: Geraetesymbol.leitstand,
                                   description: Text("Nehmen Sie ihn unter Geräte › Gerät hinzufügen auf."))
        }
    }

    /// HomeKit-Brücke: Stand und Löschen der Kopplungen. Der Code steht nur auf der Anzeige.
    private func homekitAbschnitt(_ h: Leitstandzustand.HomeKit) -> some View {
        Section {
            if h.aktiv == true {
                LabeledContent("Gekoppelte Geräte", value: "\(h.steuerungen ?? 0)")
                LabeledContent("Zubehör", value: "\(h.zubehoer ?? 0)")
                if (h.steuerungen ?? 0) > 0 {
                    Button("Kopplungen löschen …", systemImage: "trash", role: .destructive) { frageKopplungen = true }
                        .confirmationDialog("Alle Kopplungen mit Home löschen?", isPresented: $frageKopplungen, titleVisibility: .visible) {
                            Button("Löschen", role: .destructive) {
                                let id = leitstandID
                                modell.ausfuehren("Kopplungen gelöscht.") {
                                    try await modell.leitstandbetrieb.client(id).kopplungenLoeschen()
                                    await modell.leitstandbetrieb.jetztAbfragen(id)
                                }
                            }
                        } message: {
                            Text("Die Räume verschwinden aus der Home-App, samt Automationen und Zuordnung zu Zimmern. Danach lässt sich der Leitstand mit dem Code auf der Anzeige neu hinzufügen.")
                        }
                }
            } else {
                Text(h.grund ?? "HomeKit läuft nicht.").foregroundStyle(.secondary)
            }
        } header: {
            Text("HomeKit")
        } footer: {
            Text((h.steuerungen ?? 0) > 0
                 ? "Je Raum ein Thermostat mit Soll- und Isttemperatur und Betriebsart, dazu Außen, Pufferspeicher und Kesselvorlauf. Weitere Personen lädt der Besitzer in der Home-App ein."
                 : "Zum Hinzufügen in der Home-App den Code verwenden, den der Leitstand auf der Seite Leitstand mit Taste B zeigt. Die App zeigt ihn bewusst nicht an.")
        }
    }

    /// Stand der Übernahme des Protokolls in den Verlauf der App
    private func verlaufAbschnitt() -> some View {
        let a = modell.leitstandabgleich.staende[leitstandID]
        return Section {
            if let a {
                LabeledContent("Zuletzt abgeglichen") {
                    if a.laeuft {
                        ProgressView()
                    } else if let z = a.zuletzt {
                        Text(z, format: .relative(presentation: .named))
                    } else {
                        Text("noch nicht")
                    }
                }
                LabeledContent("Tage auf der Karte", value: "\(a.tage)")
                LabeledContent("Zuletzt übernommen", value: a.uebernommen == 1 ? "1 Fünfminutenwert" : "\(a.uebernommen) Fünfminutenwerte")
                LabeledContent("Zuletzt übernommene Ereignisse", value: "\(a.ereignisse)")
                if let f = a.fehler {
                    Label(f, systemImage: "exclamationmark.triangle")
                        .foregroundStyle(.orange)
                }
            } else {
                Text("Wird beim nächsten Abgleich gelesen.").foregroundStyle(.secondary)
            }
        } header: {
            Text("Verlauf")
        } footer: {
            Text("Der Leitstand zeichnet alle Geräte rund um die Uhr auf. Die App übernimmt seine Fünfminutenmittel und Ereignisse alle fünf Minuten in den Verlauf; seine Werte haben Vorrang vor der eigenen Aufzeichnung. Neustarts, Firmwarewechsel und Ausfälle erscheinen als Markierung im Verlauf.")
        }
    }

    private func inhalt(_ stand: Leitstandbetrieb.Stand) -> some View {
        let z = stand.zustand
        let aussenMac = (stand.thermometer?.aussenfuehler).flatMap { $0.isEmpty ? nil : $0 } ?? z?.aussen?.mac
        let thermometer = (stand.thermometer?.geraete ?? []).compactMap { t in
            Zusammenfuehrung.funkthermometer(t, zuordnung: gleich(t.mac, aussenMac) ? "Außenfühler" : nil)
        }
        let schluessel = stand.thermometer?.schluessel ?? []
        let empfangen = Set(thermometer.map { Zusammenfuehrung.normalisiert($0.mac) })
        let ohneGeraet = schluessel.filter { !empfangen.contains(Zusammenfuehrung.normalisiert($0)) }
        let hinterlegt = { (mac: String) in schluessel.contains { gleich($0, mac) } }

        return Form {
            zustandAbschnitt(stand)
            aussenAbschnitt(z?.aussen)
            verlaufAbschnitt()
            if let h = z?.homekit {
                homekitAbschnitt(h)
            }

            Section {
                ForEach(thermometer.sorted { $0.signal > $1.signal }) { t in
                    Button {
                        auswahl = t
                    } label: {
                        ThermometerZeile(t: t)
                            .contentShape(.rect)
                    }
                    .buttonStyle(.plain)
                    // An der Zeile, damit die Auswahl auf das angetippte Thermometer zeigt
                    .confirmationDialog(t.name, isPresented: Binding(
                        get: { auswahl?.id == t.id },
                        set: { if !$0, auswahl?.id == t.id { auswahl = nil } }),
                        titleVisibility: .visible) {
                        if !gleich(t.mac, aussenMac) {
                            Button("Als Außenfühler zuordnen") {
                                modell.leitstandAussenfuehlerSetzen(leitstandID, mac: t.mac)
                            }
                        }
                        if t.verschluesselt {
                            Button(hinterlegt(t.mac) ? "Schlüssel ändern …" : "Schlüssel eingeben …") {
                                schluesselFuer = Schluesselziel(mac: t.mac, name: t.name, zustand: t.schluessel, hinterlegt: hinterlegt(t.mac))
                            }
                        }
                    } message: {
                        Text("\(t.mac) · \(t.formatbezeichnung)")
                    }
                }
                if thermometer.isEmpty {
                    Text("Noch kein Thermometer empfangen.").foregroundStyle(.secondary)
                }
                Button("Schlüssel zu einer Adresse hinterlegen …", systemImage: "key") {
                    schluesselFuer = Schluesselziel(mac: "", name: "", zustand: nil, hinterlegt: false)
                }
            } header: {
                Text("Funkthermometer in Reichweite")
            } footer: {
                Text("Tippen Sie auf ein Thermometer, um es als Außenfühler zuzuordnen oder seinen Schlüssel einzugeben. Verschlüsselt sendende Thermometer wie der Climate-Sat von camperSense liefern erst mit Schlüssel Werte.")
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
                    Text("Für diese Adressen liegt am Leitstand ein Schlüssel, das Thermometer ist aber gerade nicht zu empfangen. Einen nicht mehr gebrauchten Schlüssel entfernen Sie durch Antippen.")
                }
            }

            anzeigeAbschnitt(stand)

            Section {
                Button("Aus der App entfernen", systemImage: "trash", role: .destructive) {
                    entfernenFragen = true
                }
                .confirmationDialog("Leitstand aus der App entfernen?", isPresented: $entfernenFragen, titleVisibility: .visible) {
                    Button("Entfernen", role: .destructive) {
                        zurueck()
                        modell.leitstandEntfernen(leitstandID)
                    }
                } message: {
                    Text("Die App fragt ihn danach nicht mehr ab. Über die Einrichtung lässt er sich wieder aufnehmen.")
                }
            } footer: {
                Text("Betrifft nur die App; der Leitstand arbeitet weiter.")
            }
        }
        .formularStil()
        .navigationTitle(stand.eintrag.ort)
        .task(id: leitstandID) { await bildLaden() }
        .refreshable {
            await modell.leitstandbetrieb.jetztAbfragen(leitstandID)
            await bildLaden()
        }
        .sheet(item: $schluesselFuer) { ziel in
            FunkschluesselBlatt(ort: .leitstand(leitstandID), ziel: ziel)
        }
    }

    // MARK: Abschnitte

    private func zustandAbschnitt(_ stand: Leitstandbetrieb.Stand) -> some View {
        let z = stand.zustand
        return Section {
            Kennwert(titel: "Zustand", wert: stand.erreichbar ? "erreichbar" : stand.fehler == nil ? "wird abgefragt" : "nicht erreichbar",
                     farbe: stand.erreichbar ? Farbe.gut : stand.fehler == nil ? nil : Farbe.stoerung)
            if let fehler = stand.fehler {
                Text(fehler)
                    .font(.caption)
                    .foregroundStyle(Farbe.stoerung)
            }
            Kennwert(titel: "Gerät", wert: z?.geraet?.platine ?? "–")
            Kennwert(titel: "Adresse", wert: Leitstandanschrift.text(stand.eintrag.adresse))
            Kennwert(titel: "WLAN", wert: Format.signal(z?.netz?.signal))
            Kennwert(titel: "Laufzeit seit Start", wert: z?.laufzeitSekunden.map(Format.dauer) ?? "–")
            if let frei = z?.freierSpeicher {
                let tiefst = z?.tiefsterSpeicher
                Kennwert(titel: "Freier Speicher",
                         wert: Format.mitEinheit("\(frei / 1024)", "kB") + (tiefst.map { ", Tiefstwert " + Format.mitEinheit("\($0 / 1024)", "kB") } ?? ""),
                         farbe: (tiefst ?? .max) < 30 * 1024 ? Farbe.warnung : nil)
            }
            if let a = z?.anlage {
                let gesamt = (a.heizungsgeraete ?? 0) + (a.verteiler ?? 0)
                Kennwert(titel: "Geräte der Anlage", wert: "\(a.erreichbar ?? 0) von \(gesamt) erreichbar",
                         farbe: (a.erreichbar ?? 0) < gesamt ? Farbe.warnung : nil)
            }
            Kennwert(titel: "Uhrzeit", wert: z?.netz?.uhrzeitGueltig == true ? "gestellt" : "noch nicht gestellt",
                     farbe: z?.netz?.uhrzeitGueltig == false ? Farbe.warnung : nil)
            Kennwert(titel: "Firmware", wert: z?.version ?? stand.eintrag.firmware ?? "–")
            Kennwert(titel: "Kennung", wert: stand.eintrag.id)
        } header: {
            Text("Leitstand")
        } footer: {
            Text("Der Leitstand regelt nichts. Er empfängt Funkthermometer in Kesselnähe, gibt die Außentemperatur an Kessel und Speicher weiter und zeigt den Zustand der Anlage auf seinem Bildschirm.")
        }
    }

    private func aussenAbschnitt(_ a: Leitstandzustand.Aussen?) -> some View {
        Section {
            if let a, a.zugeordnet == true {
                LabeledContent {
                    if a.gueltig == true, let t = a.temperaturC {
                        Text("\(Format.temperatur(t))\(a.feuchte.map { " · " + Format.mitEinheit(Format.zahl($0, stellen: 0), "%") } ?? "")")
                            .monospacedDigit()
                    } else {
                        Text("kein Empfang").foregroundStyle(Farbe.warnung)
                    }
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Text((a.name ?? "").isEmpty ? (a.mac ?? "") : a.name ?? "")
                        if let mac = a.mac, !(a.name ?? "").isEmpty {
                            Text(mac).font(.caption.monospaced()).foregroundStyle(.secondary)
                        }
                    }
                }
                if let alter = a.alterSekunden {
                    Kennwert(titel: "Zuletzt empfangen", wert: Format.alter(alter))
                }
                Button("Zuordnung aufheben", systemImage: "xmark.circle", role: .destructive) {
                    modell.leitstandAussenfuehlerSetzen(leitstandID, mac: nil)
                }
            } else {
                Text("Kein Außenfühler zugeordnet. Tippen Sie unten auf ein Thermometer, um es zuzuordnen.")
                    .foregroundStyle(.secondary)
            }
        } header: {
            Text("Außenfühler")
        } footer: {
            Text("Kessel und Speicher übernehmen die Außentemperatur vom Leitstand, sobald ihre Firmware ihn kennt, sonst weiterhin von einem Verteiler mit Außenfühler. Sie geht in keine Regelung ein.")
        }
    }

    private func anzeigeAbschnitt(_ stand: Leitstandbetrieb.Stand) -> some View {
        Section {
            if let bild {
                bild
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(4 / 3, contentMode: .fit)
                    .clipShape(.rect(cornerRadius: 6))
                    .accessibilityLabel("Bildschirm des Leitstands")
            } else if bildLaedt {
                HStack(spacing: 10) {
                    ProgressView().controlSize(.small)
                    Text("Bild wird gelesen …").foregroundStyle(.secondary)
                }
            } else if let bildFehler {
                Label(bildFehler, systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(Farbe.stoerung)
            }
            seitenwahl(stand)
            Button("Bild neu lesen", systemImage: "arrow.clockwise") {
                Task { await bildLaden() }
            }
            .disabled(bildLaedt)
        } header: {
            Text("Anzeige am Gerät")
        } footer: {
            Text("Der Inhalt des Bildschirms, aus dem Gerät gelesen. Die Wahl der Seite gilt für die Anzeige am Gerät.")
        }
    }

    // MARK: Vorgänge

    private func gleich(_ a: String?, _ b: String?) -> Bool {
        guard let a, let b, !a.isEmpty, !b.isEmpty else { return false }
        return Zusammenfuehrung.normalisiert(a) == Zusammenfuehrung.normalisiert(b)
    }

    private func bildLaden() async {
        bildLaedt = true
        defer { bildLaedt = false }
        do {
            let daten = try await modell.leitstandBildschirm(leitstandID)
            #if os(iOS)
            guard let b = UIImage(data: daten) else { throw Bildfehler.unlesbar }
            bild = Image(uiImage: b)
            #else
            guard let b = NSImage(data: daten) else { throw Bildfehler.unlesbar }
            bild = Image(nsImage: b)
            #endif
            bildFehler = nil
        } catch is CancellationError {
        } catch {
            bildFehler = error.localizedDescription
        }
    }

    /// Zwei Seiten als Segmente, sechs (Core2) als Menü
    @ViewBuilder
    private func seitenwahl(_ stand: Leitstandbetrieb.Stand) -> some View {
        let seiten = Leitstand.Seite.verfuegbar(psram: (stand.zustand?.psramFrei ?? 0) > 0)
        let wahl = Binding(
            get: { Leitstand.Seite(rawValue: stand.zustand?.anzeige?.seite ?? "") ?? .anlage },
            set: { neu in seiteWaehlen(neu) })
        if seiten.count > 2 {
            Picker("Seite", selection: wahl) {
                ForEach(seiten, id: \.self) { Text($0.titel).tag($0) }
            }
            .pickerStyle(.menu)
        } else {
            Picker("Seite", selection: wahl) {
                ForEach(seiten, id: \.self) { Text($0.titel).tag($0) }
            }
            .pickerStyle(.segmented)
        }
    }

    private func seiteWaehlen(_ seite: Leitstand.Seite) {
        Task {
            do {
                try await modell.leitstandSeiteZeigen(leitstandID, seite)
                await modell.leitstandbetrieb.jetztAbfragen(leitstandID)
                // Die Anzeige zeichnet die neue Seite in einem Durchgang; danach stimmt das Bild.
                try await Task.sleep(for: .milliseconds(800))
                await bildLaden()
            } catch is CancellationError {
            } catch {
                modell.melden(error.localizedDescription, fehler: true)
            }
        }
    }

    enum Bildfehler: LocalizedError {
        case unlesbar
        var errorDescription: String? { "Das Bild des Bildschirms ließ sich nicht lesen." }
    }
}
