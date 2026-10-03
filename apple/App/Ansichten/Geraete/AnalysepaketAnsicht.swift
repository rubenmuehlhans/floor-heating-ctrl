import SwiftUI

/// Verlauf und Ereignisse eines Zeitraums als ZIP zum Teilen, für Auswertungen in einer
/// Tabellenkalkulation, in Python oder einem KI-Werkzeug
struct AnalysepaketAnsicht: View {
    @Environment(AppModell.self) private var modell
    @State private var tage = 30
    @State private var raster: TimeInterval = 900
    @State private var datei: URL?
    @State private var groesse: Int?
    @State private var laeuft = false
    @State private var fehler: String?

    private static let zeitraeume = [(7, "7 Tage"), (30, "30 Tage"), (90, "90 Tage"), (365, "1 Jahr")]
    private static let raster: [(TimeInterval, String)] = [(300, "5 Minuten"), (900, "15 Minuten"), (3600, "1 Stunde"), (86_400, "1 Tag")]

    var body: some View {
        Form {
            Section {
                Picker("Zeitraum", selection: $tage) {
                    ForEach(Self.zeitraeume, id: \.0) { Text($0.1).tag($0.0) }
                }
                Picker("Raster", selection: $raster) {
                    ForEach(Self.raster, id: \.0) { Text($0.1).tag($0.0) }
                }
            } footer: {
                Text("Je Gerät eine CSV-Datei mit den Mittelwerten je Raster, dazu die Ereignisse des Leitstands, der Katalog der Messgrößen und eine Beschreibung der Anlage. Ein feines Raster über einen langen Zeitraum ergibt große Dateien.")
            }
            Section {
                Button {
                    Task { await erstellen() }
                } label: {
                    HStack {
                        Label("Paket erstellen", systemImage: "shippingbox")
                        Spacer()
                        if laeuft { ProgressView() }
                    }
                }
                .disabled(laeuft)
                if let datei {
                    ShareLink(item: datei) {
                        Label("Teilen", systemImage: "square.and.arrow.up")
                    }
                    if let groesse {
                        LabeledContent(datei.lastPathComponent, value: ByteCountFormatter.string(fromByteCount: Int64(groesse), countStyle: .file))
                            .font(.callout)
                    }
                }
                if let fehler {
                    Label(fehler, systemImage: "exclamationmark.triangle").foregroundStyle(Farbe.warnung)
                }
            } footer: {
                Text("Das Paket enthält Gerätekennungen, Orts- und Raumnamen, aber keine Adressen und keine Zugangsdaten. Es ist nicht zur Veröffentlichung bestimmt; die App gibt es nur weiter, wenn Sie es teilen.")
            }
        }
        .formularStil()
        .navigationTitle("Analysepaket")
        .onChange(of: tage) { datei = nil }
        .onChange(of: raster) { datei = nil }
    }

    private func erstellen() async {
        laeuft = true
        fehler = nil
        defer { laeuft = false }
        do {
            let url = try await modell.analysepaket(tage: tage, raster: raster)
            groesse = (try? url.resourceValues(forKeys: [.fileSizeKey]))?.fileSize
            datei = url
        } catch {
            fehler = "Das Paket ließ sich nicht erstellen: \(error.localizedDescription)"
        }
    }
}
