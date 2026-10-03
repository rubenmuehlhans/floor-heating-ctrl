#if os(macOS)
import SwiftUI
import Anlage

/// Fenster hinter dem Symbol in der Menüleiste: Zustand auf einen Blick, auch wenn das
/// Hauptfenster geschlossen ist. Die App fragt dann weiter ab und zeichnet auf.
struct Menueleiste: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.openWindow) private var fensterOeffnen

    var body: some View {
        let a = modell.anlage
        let bericht = modell.aktuellerLagebericht
        let geraete = modell.betrieb.staende.values
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Image(systemName: "flame").foregroundStyle(.tint)
                Text("Heizung").font(.headline)
                Spacer()
                Zustandsmarke(text: bericht.zustand.rawValue, symbol: bericht.zustand.symbol, farbe: bericht.zustand.farbe)
            }
            Text(bericht.kurztext)
                .font(.callout)
                .fixedSize(horizontal: false, vertical: true)

            Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 6) {
                GridRow {
                    Text("Außen").foregroundStyle(.secondary)
                    Text(Format.temperatur(a.aussen?.temperatur)).monospacedDigit()
                }
                if let speicher = a.speicher {
                    GridRow {
                        Text("Speicher").foregroundStyle(.secondary)
                        Text("\(Format.temperatur(speicher.temperatur)) · Ladung \(Format.prozent(speicher.ladung))").monospacedDigit()
                    }
                }
                if let kessel = a.kessel {
                    GridRow {
                        Text("Brenner").foregroundStyle(.secondary)
                        Text(kessel.brennerLaeuft ? "läuft seit \(Format.dauer(kessel.brennerSeit))" : "aus")
                    }
                }
                GridRow {
                    Text("Räume").foregroundStyle(.secondary)
                    Text("\(a.raeume.filter { $0.zustand == .heizt }.count) heizen, \(a.raeume.filter { $0.zustand == .sollErreicht }.count) im Soll")
                }
                if !modell.istBeispiel {
                    GridRow {
                        Text("Geräte").foregroundStyle(.secondary)
                        Text("\(geraete.filter(\.erreichbar).count) von \(geraete.count) erreichbar")
                    }
                }
            }
            .font(.callout)

            let befunde = a.befundeNachSchwere
            if !befunde.isEmpty {
                Divider()
                ForEach(Array(befunde.prefix(5))) { b in
                    Button {
                        modell.geoeffneterBefund = b.id
                        anlageOeffnen()
                    } label: {
                        HStack(alignment: .firstTextBaseline, spacing: 8) {
                            SchwereSymbol(schwere: b.schwere)
                            VStack(alignment: .leading, spacing: 1) {
                                Text(b.titel).font(.callout).multilineTextAlignment(.leading)
                                if let seit = modell.befundSeit(b.id) {
                                    Text("\(b.ort) · seit \(Format.seit(seit))")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                            Spacer(minLength: 0)
                        }
                        .contentShape(.rect)
                    }
                    .buttonStyle(.plain)
                }
                if befunde.count > 5 {
                    Text("und \(befunde.count - 5) weitere").font(.caption).foregroundStyle(.secondary)
                }
            }
            Divider()
            HStack {
                Button("Anlage öffnen", action: anlageOeffnen)
                    .buttonStyle(.glassProminent)
                Button("Verlauf") {
                    fensterOeffnen(id: "verlauf")
                    NSApp.activate()
                }
                Spacer()
                SettingsLink {
                    Image(systemName: "gearshape")
                }
                .help("Einstellungen")
                Button("Beenden") { NSApp.terminate(nil) }
            }
            Text(modell.aufzeichnenAufDemMac
                 ? "Fragt ab und zeichnet auf, auch wenn kein Fenster offen ist."
                 : "Endet mit dem letzten Fenster; dauerhaftes Aufzeichnen lässt sich in den Einstellungen einschalten.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(16)
        .frame(width: 360)
    }

    private func anlageOeffnen() {
        fensterOeffnen(id: "anlage")
        NSApp.activate()
    }
}
#endif
