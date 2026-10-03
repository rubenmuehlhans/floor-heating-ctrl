import Foundation
import Anlage
import Geraetesuche

/// Führt die Adressen bekannter Geräte und Leitstände nach, wenn der Router ihnen neue gegeben
/// hat, etwa nach seinem Neustart.
///
/// Eine kurze Bonjour-Suche läuft, sobald die App in den Vordergrund kommt und solange ein Gerät
/// nicht antwortet. Die Treffer werden über die Kennung aus dem TXT-Eintrag zugeordnet. Eine
/// Adresse gilt erst, wenn unter ihr eine Verbindung zustande kam; ein veralteter Eintrag im
/// Bonjour-Zwischenspeicher ändert deshalb nichts. Die Einrichtung muss dafür nicht offen sein.
@MainActor
final class Adressnachfuehrung {
    enum Anlass {
        case vordergrund
        case unerreichbar
    }

    /// Nach einer Änderung, mit den Kennungen der Geräte und Leitstände, deren Adresse neu ist
    var nachgefuehrt: (@MainActor (Set<String>) -> Void)?

    private let verzeichnis: Geraeteverzeichnis
    private let dauer: Duration
    private var suche: Task<Void, Never>?
    private var letzteSuche: ContinuousClock.Instant?

    init(verzeichnis: Geraeteverzeichnis, dauer: Duration = .seconds(6)) {
        self.verzeichnis = verzeichnis
        self.dauer = dauer
    }

    /// Mindestabstand zur vorigen Suche. Ein Gerät ohne Strom meldet sich alle paar Sekunden als
    /// nicht erreichbar; gesucht wird dann alle zwei Minuten. Der Wechsel in den Vordergrund
    /// sucht fast immer, nur nicht bei schnellem Hin und Her zwischen Apps oder Fenstern.
    private static func abstand(_ anlass: Anlass) -> Duration {
        switch anlass {
        case .vordergrund: .seconds(30)
        case .unerreichbar: .seconds(120)
        }
    }

    func anstossen(_ anlass: Anlass) {
        let gesucht = Set(verzeichnis.geraete.map(\.id) + verzeichnis.leitstaende.map(\.id))
        // Ohne eingebundene Geräte gibt es nichts nachzuführen, und die Beispielanlage soll nicht
        // nach dem Zugriff auf das lokale Netzwerk fragen.
        guard suche == nil, !gesucht.isEmpty else { return }
        if let letzteSuche, ContinuousClock.now - letzteSuche < Self.abstand(anlass) { return }
        letzteSuche = .now
        suche = Task { [weak self, dauer] in
            let treffer = await Geraetesuche.kurz(dauer: dauer, gesucht: gesucht)
            guard let self else { return }
            self.suche = nil
            self.letzteSuche = .now
            self.uebernehmen(treffer)
        }
    }

    private func uebernehmen(_ treffer: [GefundenesGeraet]) {
        var adressen: [String: URL] = [:]
        for t in treffer {
            guard let adresse = t.adresse else { continue }
            adressen[t.id] = adresse
        }
        let geaendert = verzeichnis.nachfuehren(adressen)
        if !geaendert.isEmpty { nachgefuehrt?(geaendert) }
    }
}
