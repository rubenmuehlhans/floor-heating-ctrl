import Foundation
import Anlage
import Assistent
import Diagnose

/// Erklärungen des Betreibers zu Befunden. Eine gemerkte Erklärung nimmt den Befund aus dem
/// Lagebericht, aus der Zählung der Übersicht und aus den Mitteilungen, solange er an denselben
/// Orten und nicht schwerer ansteht. Die Beispielanlage hat eine eigene Liste nur im Speicher.
extension AppModell {
    var erklaerungen: Erklaerungsliste { istBeispiel ? beispielerklaerungen : erklaerungsliste }

    /// Die sichtbaren Befunde, geteilt in offene und erklärte
    var befundeinordnung: Erklaerungsliste.Einordnung {
        istBeispiel
            ? erklaerungen.einordnen(anlage.befunde, jetzt: .now)
            : erklaerungen.einordnen(befundgedaechtnis.sichtbar, jetzt: .now)
    }

    /// Befunde ohne wirksame Erklärung, der schwerste zuerst
    var offeneBefunde: [Befund] {
        befundeinordnung.offen.map(\.befund).sorted { $0.schwere > $1.schwere }
    }

    var erklaerteBefunde: [Erklaerungsliste.Erklaert] {
        befundeinordnung.erklaert
    }

    func befundIstErklaert(_ id: String) -> Bool {
        erklaerteBefunde.contains { $0.id == id }
    }

    /// Die gemerkte Erklärung zu einem Befund, auch wenn sie gerade nicht greift
    func befundErklaerung(_ id: String) -> Erklaerung? {
        erklaerungen.gemerkte(befund: id)
    }

    /// Warum eine gemerkte Erklärung gerade nicht greift; nil, wenn sie greift oder der Befund
    /// nicht ansteht
    func erklaerungsgrund(_ e: Erklaerung) -> String? {
        if e.abgelaufen(.now) { return Erklaerung.Deckung.abgelaufen.grund }
        guard let befund = anlage.befunde.first(where: { $0.id == e.befund }) else { return nil }
        return e.deckung(befund, seit: befundSeit(befund.id), jetzt: .now)?.grund
    }

    /// Die Karte im Gespräch wurde bestätigt.
    func erklaerungMerken(_ id: String) {
        erklaerungen.merken(id)
        melden("Erklärung gemerkt. Der Befund erscheint nicht mehr im Lagebericht.")
    }

    func erklaerungVerwerfen(_ id: String) {
        erklaerungen.verwerfen(id)
    }

    func erklaerungLoeschen(_ id: String) {
        erklaerungen.loeschen(id)
        melden("Erklärung gelöscht. Der Befund wird wieder gemeldet.")
    }

    /// Erklärung ohne Gespräch, aus der Ansicht des Befunds; sie gilt sofort.
    func befundErklaeren(_ befund: Befund, text: String, tage: Int, stunden: Int) {
        let jetzt = Date.now
        let e = Erklaerung(befund: befund, text: text.trimmingCharacters(in: .whitespacesAndNewlines), erstellt: jetzt,
                           bis: tage > 0 ? jetzt.addingTimeInterval(Double(tage) * 86_400) : nil,
                           hoechstdauer: stunden > 0 ? Double(stunden) * 3600 : nil)
        erklaerungen.anlegen(e)
        erklaerungMerken(e.id)
    }

    /// Für das Befundgedächtnis: ob eine Erklärung den Befund seit `seit` abdeckt
    func erklaerungDeckt(_ befund: Befund, seit: Date, jetzt: Date) -> Bool? {
        erklaerungsliste.deckung(befund, seit: seit, jetzt: jetzt)?.gedeckt
    }
}

extension Erklaerung {
    /// Geltung in Worten, etwa „bis auf Widerruf · höchstens 14 Stunden am Stück“
    var geltung: String {
        var teile = [bis.map { "bis \(Format.tagMitZeit($0))" } ?? "bis auf Widerruf"]
        if let hoechstdauer {
            let stunden = Int((hoechstdauer / 3600).rounded())
            teile.append("höchstens \(stunden == 1 ? "eine Stunde" : "\(stunden) Stunden") am Stück")
        }
        return teile.joined(separator: " · ")
    }
}
