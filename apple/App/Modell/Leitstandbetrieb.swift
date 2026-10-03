import Foundation
import Observation
import Anlage
import Geraeteschnittstelle

/// Abfrage der Leitstände.
///
/// Getrennt vom Anlagenbetrieb, weil der Leitstand kein Regelgerät ist: Er hat keine Parameter
/// und keine Sicherung, und nichts in der Anlage hängt an seinem Zustand. Die App liest alle zehn
/// Sekunden seinen Zustand und seine Funkthermometer.
@MainActor
@Observable
final class Leitstandbetrieb {
    struct Stand: Equatable {
        var eintrag: BekannterLeitstand
        var zustand: Leitstandzustand?
        var thermometer: Thermometerliste?
        /// Fehler der letzten Abfrage; `nil`, sobald wieder eine gelang
        var fehler: String?
        var abgefragt: Date?
        /// Fehlgeschlagene Abfragen in Folge
        var fehlerInFolge = 0

        var erreichbar: Bool { fehler == nil && zustand != nil }
    }

    enum Fehler: LocalizedError {
        case unbekannt

        var errorDescription: String? {
            "Dieser Leitstand ist in der App nicht eingebunden."
        }
    }

    private(set) var staende: [String: Stand] = [:]
    /// Ort und Firmware, wie der Leitstand sie meldet; das Verzeichnis folgt ihnen.
    @ObservationIgnored var gemeldet: (@MainActor (_ id: String, _ ort: String?, _ firmware: String?) -> Void)?
    /// Ab der zweiten fehlgeschlagenen Abfrage in Folge; vielleicht hat der Leitstand eine neue
    /// Adresse bekommen.
    @ObservationIgnored var unerreichbar: (@MainActor (_ id: String) -> Void)?
    @ObservationIgnored private var abfragen: [String: Task<Void, Never>] = [:]
    @ObservationIgnored private let takt: Duration
    @ObservationIgnored private let sitzung: URLSession

    init(takt: Duration = .seconds(10), sitzung: URLSession = .geraete) {
        self.takt = takt
        self.sitzung = sitzung
    }

    /// Bringt die Abfragen mit dem Verzeichnis in Einklang.
    func abgleichen(_ eintraege: [BekannterLeitstand]) {
        let ids = Set(eintraege.map(\.id))
        for (id, aufgabe) in abfragen where !ids.contains(id) {
            aufgabe.cancel()
            abfragen[id] = nil
            staende[id] = nil
        }
        for e in eintraege {
            if let alt = staende[e.id], alt.eintrag.adresse != e.adresse {
                abfragen[e.id]?.cancel()
                abfragen[e.id] = nil
            }
            staende[e.id, default: Stand(eintrag: e)].eintrag = e
            guard abfragen[e.id] == nil else { continue }
            abfragen[e.id] = Task { [weak self] in
                await self?.schleife(e)
            }
        }
    }

    func beenden() {
        abfragen.values.forEach { $0.cancel() }
        abfragen.removeAll()
    }

    func client(_ id: String) throws -> Leitstand {
        guard let e = staende[id]?.eintrag else { throw Fehler.unbekannt }
        return Leitstand(adresse: e.adresse, sitzung: sitzung)
    }

    func jetztAbfragen(_ id: String) async {
        guard let c = try? client(id) else { return }
        await einmal(id, c)
    }

    private func schleife(_ e: BekannterLeitstand) async {
        let c = Leitstand(adresse: e.adresse, sitzung: sitzung)
        while !Task.isCancelled {
            await einmal(e.id, c)
            try? await Task.sleep(for: takt)
        }
    }

    private func einmal(_ id: String, _ c: Leitstand) async {
        do {
            let z = try await c.zustand()
            let t = try await c.thermometerliste()
            guard !Task.isCancelled, staende[id] != nil else { return }
            staende[id]?.zustand = z
            staende[id]?.thermometer = t
            staende[id]?.fehler = nil
            staende[id]?.fehlerInFolge = 0
            staende[id]?.abgefragt = .now
            gemeldet?(id, z.geraet?.ort, z.version)
        } catch is CancellationError {
        } catch {
            guard !Task.isCancelled, staende[id] != nil else { return }
            staende[id]?.fehler = error.localizedDescription
            staende[id]?.fehlerInFolge += 1
            if staende[id]?.fehlerInFolge ?? 0 >= 2 { unerreichbar?(id) }
        }
    }
}
