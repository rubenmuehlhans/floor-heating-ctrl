import SwiftUI
import UniformTypeIdentifiers
import Anlage
import Geraeteschnittstelle

// MARK: - Firmware

/// Firmware übertragen. Vorher prüft die App, dass die Datei zu diesem Gerätetyp passt: Ein
/// Abbild des Heizungsgeräts auf einem Verteiler startete nicht.
struct FirmwareAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraetID: String
    @State private var zeigeDatei = false
    @State private var datei: Firmwaredatei?
    @State private var dateiname: String?
    @State private var fehler: String?
    @State private var frage = false

    var body: some View {
        let g = modell.anlage.geraet(geraetID)
        let art = modell.geraeteart(geraetID)
        Form {
            Section {
                Kennwert(titel: "Installiert", wert: g?.firmware ?? "–")
                Button("Firmware-Datei wählen …", systemImage: "doc.badge.arrow.up") { zeigeDatei = true }
            } footer: {
                Text("Nach dem Übertragen startet das Gerät neu. Schlägt der Start fehl, kehrt es von selbst zur vorherigen Fassung zurück.")
            }
            if let datei {
                let passt = art.map { datei.projekt == $0.projekt } ?? false
                Section("Gewählte Datei") {
                    Kennwert(titel: "Datei", wert: dateiname ?? "–")
                    Kennwert(titel: "Projekt", wert: datei.projekt)
                    Kennwert(titel: "Fassung", wert: datei.version)
                    Kennwert(titel: "Größe", wert: Format.mitEinheit("\(datei.daten.count / 1024)", "kB"))
                    if passt {
                        Button("Übertragen", systemImage: "arrow.up.circle") { frage = true }
                    } else {
                        Label("Die Datei gehört zu „\(datei.projekt)“, dieses Gerät braucht „\(art?.projekt ?? "")“.", systemImage: "xmark.octagon.fill")
                            .foregroundStyle(Farbe.stoerung)
                    }
                }
            }
            if let fehler {
                Section {
                    Label(fehler, systemImage: "xmark.octagon.fill").foregroundStyle(Farbe.stoerung)
                }
            }
        }
        .formularStil()
        .navigationTitle("Firmware")
        .fileImporter(isPresented: $zeigeDatei, allowedContentTypes: [.data]) { ergebnis in
            fehler = nil
            do {
                let url = try ergebnis.get()
                let zugriff = url.startAccessingSecurityScopedResource()
                defer { if zugriff { url.stopAccessingSecurityScopedResource() } }
                datei = try Firmwaredatei(daten: try Data(contentsOf: url))
                dateiname = url.lastPathComponent
            } catch {
                datei = nil
                fehler = error.localizedDescription
            }
        }
        .confirmationDialog("Firmware übertragen?", isPresented: $frage, titleVisibility: .visible) {
            Button("Übertragen") {
                if let datei { modell.firmwareEinspielen(geraetID, datei) }
                datei = nil
            }
        } message: {
            Text("Das Gerät ist während der Übertragung und des Neustarts etwa eine Minute nicht erreichbar. Vorher sichert die App seine Konfiguration.")
        }
    }
}

// MARK: - Sicherung

/// Sicherungen eines Geräts: anlegen, einspielen, als Datei weitergeben. Sie enthalten die
/// WLAN-Zugangsdaten im Klartext und liegen deshalb verschlüsselt in der App.
struct SicherungAnsicht: View {
    @Environment(AppModell.self) private var modell
    let geraetID: String
    @State private var eintraege: [Sicherungseintrag] = []
    @State private var einspielen: Sicherungseintrag?
    @State private var zeigeDatei = false
    @State private var dateiDaten: Data?
    @State private var dateiKopf: String?
    @State private var fehler: String?
    @State private var export: Sicherungsdokument?

    var body: some View {
        Form {
            Section {
                Button("Jetzt sichern", systemImage: "externaldrive.badge.plus") {
                    Task {
                        await modell.sichern(geraetID)
                        neuLaden()
                    }
                }
            } footer: {
                Text("Vor jeder Änderung einer Einstellung sichert die App das Gerät ohnehin; sie behält die letzten zehn Sicherungen je Gerät.")
            }

            Section {
                if eintraege.isEmpty {
                    Text("Noch keine Sicherung.").foregroundStyle(.secondary)
                }
                ForEach(eintraege) { e in
                    HStack {
                        Label(Format.tagMitZeit(e.datum), systemImage: "lock.doc")
                        Spacer()
                        Menu {
                            Button("Einspielen …", systemImage: "externaldrive.badge.timemachine") { einspielen = e }
                            Button("Als Datei weitergeben …", systemImage: "square.and.arrow.up") {
                                if let daten = modell.sicherungLesen(e) {
                                    export = Sicherungsdokument(daten: daten)
                                }
                            }
                        } label: {
                            Image(systemName: "ellipsis.circle")
                        }
                        .menuStyle(.button)
                        .buttonStyle(.plain)
                    }
                }
            } header: {
                Text("In der App")
            } footer: {
                Text("Verschlüsselt auf diesem Gerät. Die Sicherungen enthalten die WLAN-Zugangsdaten im Klartext und gehen nie an die KI.")
            }

            Section {
                Button("Aus Datei einspielen …", systemImage: "doc.badge.arrow.up") { zeigeDatei = true }
                if let fehler {
                    Label(fehler, systemImage: "xmark.octagon.fill").foregroundStyle(Farbe.stoerung)
                }
            } footer: {
                Text("Beim Einspielen baut das Gerät von der Werksvorgabe aus auf, damit nicht zwei Stände durcheinandergeraten. Der Netzzugang bleibt unverändert.")
            }
        }
        .formularStil()
        .navigationTitle("Sicherung")
        .onAppear(perform: neuLaden)
        .confirmationDialog("Sicherung einspielen?", isPresented: Binding(get: { einspielen != nil }, set: { if !$0 { einspielen = nil } }),
                            titleVisibility: .visible) {
            Button("Einspielen", role: .destructive) {
                if let e = einspielen, let daten = modell.sicherungLesen(e) { modell.sicherungEinspielen(geraetID, daten) }
                einspielen = nil
            }
        } message: {
            Text("Stand vom \(einspielen.map { Format.tagMitZeit($0.datum) } ?? ""). Alle jetzigen Einstellungen des Geräts werden ersetzt; der Netzzugang bleibt.")
        }
        .confirmationDialog("Sicherung aus Datei einspielen?", isPresented: Binding(get: { dateiDaten != nil }, set: { if !$0 { dateiDaten = nil } }),
                            titleVisibility: .visible) {
            Button("Einspielen", role: .destructive) {
                if let daten = dateiDaten { modell.sicherungEinspielen(geraetID, daten) }
                dateiDaten = nil
            }
        } message: {
            Text("\(dateiKopf ?? "Sicherung"). Alle jetzigen Einstellungen des Geräts werden ersetzt; der Netzzugang bleibt.")
        }
        .fileImporter(isPresented: $zeigeDatei, allowedContentTypes: [.json]) { ergebnis in
            fehler = nil
            do {
                let url = try ergebnis.get()
                let zugriff = url.startAccessingSecurityScopedResource()
                defer { if zugriff { url.stopAccessingSecurityScopedResource() } }
                let daten = try Data(contentsOf: url)
                let json = try JSONWert.lesen(daten)
                // Die Firmware nimmt nur Sicherungen mit Kopf an; ein Rumpf ohne Kopf setzte sonst
                // alles auf die Werksvorgabe zurück.
                let erwartet = modell.geraeteart(geraetID)?.projekt
                guard let app = json["backup"]?["app"]?.alsText else {
                    fehler = "Die Datei ist keine Sicherung eines Geräts: Ihr fehlt der Kopf „backup“."
                    return
                }
                if app != erwartet {
                    fehler = "Diese Sicherung stammt von „\(app)“ und passt nicht zu diesem Gerät."
                    return
                }
                let kopf = json["backup"]
                let datum = kopf?["epoch"]?.alsZahl.map { Format.tagMitZeit(Date(timeIntervalSince1970: $0)) }
                dateiKopf = kopf.map { k in
                    "Sicherung von \(k["site"]?.alsText ?? k["device_id"]?.alsText ?? "unbekannt")\(datum.map { ", gesichert am \($0)" } ?? "")"
                }
                dateiDaten = daten
            } catch {
                fehler = "Die Datei ist kein lesbares JSON."
            }
        }
        .fileExporter(isPresented: Binding(get: { export != nil }, set: { if !$0 { export = nil } }), document: export,
                      contentType: .json, defaultFilename: "einstellungen-\(geraetID).json") { _ in
            export = nil
        }
    }

    private func neuLaden() {
        eintraege = modell.sicherungseintraege(geraetID)
    }
}

/// Eine Sicherung als Datei zum Weitergeben
struct Sicherungsdokument: FileDocument {
    static let readableContentTypes: [UTType] = [.json]
    var daten: Data

    init(daten: Data) {
        self.daten = daten
    }

    init(configuration: ReadConfiguration) throws {
        daten = configuration.file.regularFileContents ?? Data()
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: daten)
    }
}

/// Text als Datei, etwa ein Protokoll oder eine Aufzeichnung im CSV-Format
struct Textdokument: FileDocument {
    static let readableContentTypes: [UTType] = [.commaSeparatedText]
    var text: String

    init(text: String) {
        self.text = text
    }

    init(configuration: ReadConfiguration) throws {
        text = String(decoding: configuration.file.regularFileContents ?? Data(), as: UTF8.self)
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: Data(text.utf8))
    }
}
