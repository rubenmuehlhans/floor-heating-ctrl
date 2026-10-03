import Foundation

/// Zeiträume der Verlaufsdiagramme mit ihrem Raster: Jedes Diagramm zeigt einige hundert Punkte.
enum Verlaufszeitraum: String, CaseIterable, Identifiable, Hashable {
    case sechsStunden = "6 Std."
    case tag = "24 Std."
    case woche = "7 Tage"
    case monat = "30 Tage"

    var id: String { rawValue }

    var dauer: TimeInterval {
        switch self {
        case .sechsStunden: 6 * 3600
        case .tag: 24 * 3600
        case .woche: 7 * 86_400
        case .monat: 30 * 86_400
        }
    }

    var schritt: TimeInterval {
        switch self {
        case .sechsStunden, .tag: 300
        case .woche: 1800
        case .monat: 7200
        }
    }

    /// Abstand der Achsenbeschriftung
    var achse: (Calendar.Component, Int) {
        switch self {
        case .sechsStunden: (.hour, 1)
        case .tag: (.hour, 4)
        case .woche: (.day, 1)
        case .monat: (.day, 7)
        }
    }

    /// Brennerläufe als Hinterlegung lohnen nur, solange einzelne Läufe erkennbar bleiben.
    var brennerHinterlegen: Bool { self == .sechsStunden || self == .tag }
}
