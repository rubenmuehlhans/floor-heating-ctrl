import SwiftUI
import Charts
import Anlage
import Verlauf

/// Ereignisse aus dem Protokoll des Leitstands als Markierung im Diagramm: Ausfälle als grauer
/// Streifen, Neustarts, Firmwarewechsel und neue Befunde als senkrechte Linie mit Zeichen.
struct Ereignismarken: ChartContent {
    let ereignisse: [Ereignis]
    let bis: Date

    var body: some ChartContent {
        ForEach(Array(Ereignistext.ausfaelle(ereignisse, bis: bis).enumerated()), id: \.offset) { _, a in
            RectangleMark(xStart: .value("Beginn", a.von), xEnd: .value("Ende", a.bis))
                .foregroundStyle(Color.gray.opacity(0.22))
        }
        ForEach(ereignisse.filter { Ereignismarken.symbol($0) != nil }) { e in
            RuleMark(x: .value("Zeit", e.zeit))
                .foregroundStyle(Ereignismarken.farbe(e).opacity(0.7))
                .lineStyle(StrokeStyle(lineWidth: 1, dash: [3, 3]))
                .annotation(position: .top, spacing: 2) {
                    Image(systemName: Ereignismarken.symbol(e) ?? "circle")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(Ereignismarken.farbe(e))
                }
        }
    }

    /// Zeichen der Markierung; nil heißt: nicht als Linie markieren
    static func symbol(_ e: Ereignis) -> String? {
        switch e.art {
        case "neustart": "arrow.clockwise"
        case "version": "shippingbox"
        case "befund" where e.felder["stand"]?.alsText != "erledigt": "exclamationmark.triangle"
        case "betriebsart": "power"
        default: nil
        }
    }

    static func farbe(_ e: Ereignis) -> Color {
        switch e.art {
        case "neustart" where ["panic", "int_wdt", "task_wdt", "wdt", "brownout", "cpu_lockup"]
            .contains(e.felder["grund"]?.alsText ?? ""): .red
        case "befund": .orange
        default: .secondary
        }
    }

    static func listensymbol(_ e: Ereignis) -> String {
        symbol(e) ?? {
            switch e.art {
            case "nicht_erreichbar": "wifi.slash"
            case "erreichbar": "wifi"
            case "befund": "checkmark.circle"
            case "brenner": "flame"
            case "pumpe": "fan"
            case "sollwert": "thermometer.medium"
            case "funk": "dot.radiowaves.left.and.right"
            default: "circle"
            }
        }()
    }
}

/// Die Ereignisse eines Zeitraums als Liste, neueste zuerst
struct Ereignisliste: View {
    let ereignisse: [Ereignis]
    /// Kennung → Ort, aus dem Verlaufsauszug
    let orte: [String: String]
    var hoechstens = 30

    var body: some View {
        let liste = Array(ereignisse.reversed().prefix(hoechstens))
        ForEach(liste) { e in
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Image(systemName: Ereignismarken.listensymbol(e))
                    .foregroundStyle(Ereignismarken.farbe(e))
                    .frame(width: 20)
                VStack(alignment: .leading, spacing: 2) {
                    Text(Ereignistext.titel(e))
                    let details = [orte[e.geraet] ?? e.geraet, Ereignistext.einzelheiten(e, befundtitel: Zusammenfuehrung.geraetebefundTitel)].filter { !$0.isEmpty }
                    Text(details.joined(separator: " · "))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Text(e.zeit, format: .dateTime.weekday(.abbreviated).hour().minute())
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            .accessibilityElement(children: .combine)
        }
        if ereignisse.count > hoechstens {
            Text("\(ereignisse.count - hoechstens) ältere Ereignisse nicht angezeigt")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}
