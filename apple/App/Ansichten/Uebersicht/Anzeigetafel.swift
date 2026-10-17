import SwiftUI
import Anlage

/// Die Übersicht im Querformat bei compact × compact, etwa auf dem Außendisplay des iPhone Duo,
/// als Zelt auf dem Tisch: sechs Werte in großer Schrift, aus Abstand zu lesen. Antippen öffnet die
/// Seite zum Wert.
struct Anzeigetafel: View {
    @Environment(AppModell.self) private var modell

    var body: some View {
        let a = modell.anlage
        let bericht = modell.aktuellerLagebericht
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 12) {
                Text("Anlage · Stand \(Format.uhrzeit(a.stand))".uppercased())
                    .font(.system(size: 13, weight: .semibold))
                    .tracking(1.1)
                    .foregroundStyle(Farbe.gedaempft)
                Zustandsmarke(text: bericht.zustand.rawValue, symbol: bericht.zustand.symbol, farbe: bericht.zustand.farbe)
            }
            .padding(.horizontal, 4)
            Grid(horizontalSpacing: 10, verticalSpacing: 10) {
                GridRow {
                    kachel("Außen", wert: a.aussen.map { Format.zahl($0.temperatur) } ?? "–", einheit: "°C",
                           fuss: a.aussen?.feuchte.map { "Feuchte \(Format.zahl($0, stellen: 0)) %" }, farbe: Farbe.kaelte) {
                        modell.bereich = .heizung
                    }
                    kachel("Speicher", wert: a.speicher?.temperatur.map { Format.zahl($0) } ?? "–", einheit: "°C",
                           fuss: a.speicher.map { "Füllstand \(Format.prozent($0.ladung)) · \($0.phase)" }, farbe: Farbe.waerme) {
                        modell.bereich = .heizung
                    }
                    kachel("Brenner", wert: a.kessel.map { $0.brennerLaeuft ? "läuft" : "aus" } ?? "–", zahl: false,
                           fuss: a.kessel.map { "gestern \(Format.dauer($0.laufzeitGestern)) · \(Format.starts($0.startsGestern))" },
                           farbe: a.kessel?.brennerLaeuft == true ? Farbe.waerme : Farbe.tinte) {
                        modell.bereich = .heizung
                    }
                }
                GridRow {
                    let heizen = a.raeume.filter { $0.zustand == .heizt }
                    kachel("Räume", wert: "\(heizen.count)", einheit: "von \(a.raeume.count)",
                           fuss: heizen.isEmpty ? "kein Raum heizt" : "heizen: " + heizen.map(\.name).joined(separator: ", ")) {
                        modell.bereich = .raeume
                    }
                    let gestoert = a.heizkreise.filter(\.relais.gestoert).count
                        + (a.kesselkreispumpe.map { $0.relais.gestoert ? 1 : 0 } ?? 0)
                    kachel("Pumpen", wert: gestoert > 0 ? "gestört" : "in Ordnung", zahl: false,
                           fuss: gestoert > 0 ? "\(gestoert)\u{00A0}Relais nicht erreichbar" : "Relais erreichbar",
                           farbe: gestoert > 0 ? Farbe.stoerung : Farbe.gut) {
                        modell.bereich = .heizung
                    }
                    ZielLink(.befunde) {
                        tafelkachel("Befunde", wert: "\(a.befunde.count)", einheit: "", zahl: true,
                                    fuss: befundzeile(a.befunde), farbe: Farbe.tinte)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Farbe.grund)
        #if os(iOS)
        .toolbar(.hidden, for: .navigationBar)
        #endif
    }

    private func befundzeile(_ befunde: [Befund]) -> String {
        let anzahl = { (s: Befund.Schwere) in befunde.filter { $0.schwere == s }.count }
        // Zahl und Wort bleiben beim Umbruch zusammen
        return "\(anzahl(.stoerung))\u{00A0}Störung · \(anzahl(.warnung))\u{00A0}Warnung · \(anzahl(.hinweis))\u{00A0}Hinweise"
    }

    private func kachel(_ titel: String, wert: String, einheit: String = "", zahl: Bool = true, fuss: String?,
                        farbe: Color = Farbe.tinte, aktion: @escaping () -> Void) -> some View {
        Button(action: aktion) {
            tafelkachel(titel, wert: wert, einheit: einheit, zahl: zahl, fuss: fuss, farbe: farbe)
        }
        .buttonStyle(.plain)
    }

    private func tafelkachel(_ titel: String, wert: String, einheit: String, zahl: Bool, fuss: String?,
                             farbe: Color) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(titel.uppercased())
                .font(.system(size: 13, weight: .semibold))
                .tracking(1.1)
                .foregroundStyle(Farbe.gedaempft)
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(wert)
                    .font(zahl ? .system(size: 44, weight: .bold, design: .monospaced) : .system(size: 32, weight: .bold))
                    .foregroundStyle(farbe)
                if !einheit.isEmpty {
                    Text(einheit).font(.title3).foregroundStyle(Farbe.gedaempft)
                }
            }
            .lineLimit(1)
            .minimumScaleFactor(0.5)
            if let fuss {
                Text(Format.einheitenFest(fuss))
                    .font(.system(size: 14, design: .monospaced))
                    .foregroundStyle(Farbe.gedaempft)
                    .lineLimit(3)
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .karte()
        .contentShape(.rect)
    }
}
