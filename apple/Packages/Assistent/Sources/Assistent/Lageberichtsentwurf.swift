import Foundation
import FoundationModels
import Anlage

/// Was die KI für den Lagebericht liefert, als geführte Ausgabe. Die App macht daraus einen
/// `Lagebericht` mit Zeitpunkt und Modell.
@Generable
public struct Lageberichtsentwurf {
    @Guide(description: "Gesamtlage", .anyOf(["inOrdnung", "beobachten", "handeln"]))
    public var zustand: String
    @Guide(description: "Ein bis zwei Sätze zur Lage in der Sie-Form")
    public var kurztext: String
    @Guide(description: "Höchstens drei Hinweise, der wichtigste zuerst", .maximumCount(3))
    public var hinweise: [Hinweisentwurf]

    @Generable
    public struct Hinweisentwurf {
        @Guide(description: "Schwere", .anyOf(["stoerung", "warnung", "hinweis"]))
        public var schwere: String
        @Guide(description: "Ein bis zwei Sätze mit dem, was zu tun ist")
        public var text: String
        @Guide(description: "Kennung des Befunds oder Vorschlags, sonst leer")
        public var bezug: String?
    }

    public func lagebericht(erstellt: Date, modell: String, befunde: [String]) -> Lagebericht {
        let zustand: Lagebericht.Zustand = switch zustand {
        case "handeln": .handeln
        case "beobachten": .beobachten
        default: .inOrdnung
        }
        return Lagebericht(
            zustand: zustand, kurztext: kurztext.trimmingCharacters(in: .whitespacesAndNewlines),
            hinweise: hinweise.prefix(3).enumerated().map { i, h in
                Lagebericht.Hinweis(
                    id: "h\(i)", schwere: h.schwere == "stoerung" ? .stoerung : h.schwere == "warnung" ? .warnung : .hinweis,
                    text: h.text.trimmingCharacters(in: .whitespacesAndNewlines),
                    bezug: h.bezug.flatMap { $0.trimmingCharacters(in: .whitespaces).isEmpty ? nil : $0 })
            },
            erstellt: erstellt, modell: modell, befunde: befunde)
    }
}

extension Lagebericht {
    /// Ohne KI: der Bericht aus den Befunden, wie ihn die Übersicht bis zum ersten Bericht der KI
    /// und ohne Modell zeigt
    public static func ausBefunden(_ befunde: [Befund], stand: Date) -> Lagebericht {
        let sortiert = befunde.sorted { $0.schwere > $1.schwere }
        let stoerungen = sortiert.filter { $0.schwere == .stoerung }.count
        let warnungen = sortiert.filter { $0.schwere == .warnung }.count
        let hinweise = sortiert.filter { $0.schwere == .hinweis }.count
        let zustand: Zustand = stoerungen > 0 ? .handeln : warnungen > 0 ? .beobachten : .inOrdnung
        let kurztext = if stoerungen > 0 {
            stoerungen == 1 ? "Eine Störung braucht Ihre Aufmerksamkeit." : "\(stoerungen) Störungen brauchen Ihre Aufmerksamkeit."
        } else if warnungen > 0 {
            warnungen == 1 ? "Die Anlage arbeitet; ein Punkt sollte geprüft werden." : "Die Anlage arbeitet; \(warnungen) Punkte sollten geprüft werden."
        } else if hinweise > 0 {
            hinweise == 1 ? "Die Anlage arbeitet ohne Störung. Ein Hinweis liegt vor." : "Die Anlage arbeitet ohne Störung. \(hinweise) Hinweise liegen vor."
        } else {
            "Die Anlage arbeitet ohne Auffälligkeiten."
        }
        return Lagebericht(
            zustand: zustand, kurztext: kurztext,
            hinweise: sortiert.prefix(3).map { Hinweis(id: $0.id, schwere: $0.schwere, text: $0.titel, bezug: $0.id) },
            erstellt: stand, modell: "Prüfungen der App", befunde: sortiert.map(\.id))
    }
}
