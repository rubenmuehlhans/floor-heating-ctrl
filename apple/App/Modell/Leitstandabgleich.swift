import Foundation
import Observation
import Anlage
import Geraeteschnittstelle
import Verlauf

/// Übernimmt den Verlauf der Leitstände in den eigenen Verlaufsspeicher.
///
/// Der Leitstand fragt alle Geräte rund um die Uhr ab und hält den Verlauf auf seiner Karte; die
/// App sieht nur, was sie selbst abfragt, solange sie läuft. Beim Verbinden und danach alle fünf
/// Minuten holt sie deshalb die Fünfminutenmittel aller Tage, die ihr fehlen, und vom laufenden
/// Tag nur, was seit dem letzten Mal hinzukam. Die Werte des Leitstands haben Vorrang vor der
/// eigenen Aufzeichnung, weil sie den ganzen Platz abdecken.
///
/// Der Stand je Leitstand liegt als JSON neben dem Verlauf und wird nach jeder Datei gesichert:
/// Bricht ein Abgleich ab, setzt der nächste an derselben Stelle fort.
@MainActor
@Observable
final class Leitstandabgleich {
    struct Stand: Equatable {
        var zuletzt: Date?
        var uebernommen = 0
        var ereignisse = 0
        var tage = 0
        var laeuft = false
        var fehler: String?
    }

    private(set) var staende: [String: Stand] = [:]
    /// Steigt nach jedem Abgleich mit neuen Plätzen; Diagramme laden daraufhin neu.
    private(set) var revision = 0

    @ObservationIgnored private var aufgaben: [String: Task<Void, Never>] = [:]
    @ObservationIgnored private let takt: Duration
    @ObservationIgnored private let sitzung: URLSession
    @ObservationIgnored private let ordner: URL

    init(takt: Duration = .seconds(300), sitzung: URLSession = .geraete,
         ordner: URL = URL.applicationSupportDirectory.appending(path: "Heizung/Verlauf")) {
        self.takt = takt
        self.sitzung = sitzung
        self.ordner = ordner
    }

    /// Bringt die Abgleiche mit dem Verzeichnis in Einklang.
    func abgleichen(_ eintraege: [BekannterLeitstand], speicher: Verlaufsspeicher?) {
        guard let speicher else { return }
        let ids = Set(eintraege.map(\.id))
        for (id, a) in aufgaben where !ids.contains(id) {
            a.cancel()
            aufgaben[id] = nil
            staende[id] = nil
        }
        for e in eintraege where aufgaben[e.id] == nil {
            let leitstand = Leitstand(adresse: e.adresse, sitzung: sitzung)
            let takt = takt
            aufgaben[e.id] = Task { [weak self] in
                while !Task.isCancelled {
                    await self?.einmal(e.id, leitstand, speicher)
                    try? await Task.sleep(for: takt)
                }
            }
        }
    }

    func beenden() {
        aufgaben.values.forEach { $0.cancel() }
        aufgaben.removeAll()
    }

    /// Nach dem Löschen des Verlaufs: Abgleiche anhalten, ihren Stand verwerfen und von vorn
    /// beginnen. Sonst gälten vergangene Tage weiter als übernommen und kämen nie zurück.
    func zuruecksetzen(_ eintraege: [BekannterLeitstand], speicher: Verlaufsspeicher?) async {
        let laufend = Array(aufgaben.values)
        beenden()
        for a in laufend { await a.value }
        staende = [:]
        for e in eintraege { try? FileManager.default.removeItem(at: standdatei(e.id)) }
        abgleichen(eintraege, speicher: speicher)
    }

    func standdatei(_ id: String) -> URL {
        ordner.appending(path: "Abgleich-\(id).json")
    }

    private func einmal(_ id: String, _ leitstand: Leitstand, _ speicher: Verlaufsspeicher) async {
        var s = staende[id] ?? Stand()
        guard !s.laeuft else { return }
        s.laeuft = true
        staende[id] = s
        do {
            let e = try await Self.uebernehmen(leitstand, quelle: id, speicher: speicher, datei: standdatei(id))
            s.uebernommen = e.plaetze
            s.ereignisse = e.ereignisse
            s.tage = e.tage
            s.zuletzt = .now
            s.fehler = nil
            if e.plaetze + e.ereignisse > 0 { revision += 1 }
        } catch is CancellationError {
        } catch {
            s.fehler = error.localizedDescription
        }
        s.laeuft = false
        staende[id] = s
    }

    /// Ein Abgleich. Liefert die übernommenen Plätze und Ereignisse und die Zahl der Tage, die
    /// der Leitstand kennt. Außerhalb des Main Actors, damit die Oberfläche beim ersten Abgleich
    /// eines Jahres nicht stockt.
    nonisolated static func uebernehmen(_ l: Leitstand, quelle: String, speicher: Verlaufsspeicher, datei: URL,
                                         jetzt: Date = .now) async throws -> (plaetze: Int, ereignisse: Int, tage: Int) {
        var stand = Abgleichstand.laden(datei)
        let tage = try await l.protokollTage()
        var plaetze = 0
        var ereignisse = 0
        for tag in tage where !stand.abgeschlossen.contains(tag) {
            try Task.checkCancellation()
            guard let beginn = Protokollabgleich.tagesbeginn(tag) else { continue }
            let dateien = try await l.protokollDateien(tag: tag)
            let orte = Dictionary((try? await l.protokollGeraete(tag: tag))?.map { ($0.kennung, $0.ort) } ?? [],
                                  uniquingKeysWith: { $1 })
            var ganz = true
            for d in dateien.sorted(by: { $0.name < $1.name }) {
                if d.name == "ereignisse.jsonl" {
                    // Die abgelegte Länge ist die Stelle; ein eigener Stand ist nicht nötig.
                    var ab = try await speicher.ereignisstelle(quelle: quelle, tag: tag)
                    // Kürzer als abgelegt: Karte getauscht oder formatiert, der Tag beginnt neu.
                    if d.byte < ab {
                        try await speicher.ereignisseVerwerfen(quelle: quelle, tag: tag)
                        ab = 0
                    }
                    if d.byte > ab {
                        let daten = try await l.protokollDatei(tag: tag, name: d.name, ab: ab)
                        ereignisse += try await speicher.ereignisseAnhaengen(quelle: quelle, tag: tag, daten: daten, ab: ab)
                    }
                    if try await speicher.ereignisstelle(quelle: quelle, tag: tag) < d.byte { ganz = false }
                    continue
                }
                guard let geraet = d.mittelFuer else { continue }
                var alt = stand.dateien[tag]?[d.name] ?? .init()
                // Ebenso hier; die Werte überschreiben die zuvor übernommenen.
                if d.byte < alt.stelle { alt = .init() }
                guard d.byte > alt.stelle else { continue }
                let daten = try await l.protokollDatei(tag: tag, name: d.name, ab: alt.stelle)
                let stueck = Protokollabgleich.lesen(daten, stand: alt)
                let zeilen = stueck.zeilen.map {
                    Messzeile(geraet: geraet, ort: orte[geraet] ?? "", index: $0.index, werte: $0.werte, bezeichnungen: [:])
                }
                try await speicher.schreiben(zeilen)
                plaetze += zeilen.count
                stand.dateien[tag, default: [:]][d.name] = stueck.stand
                if stueck.stand.stelle < d.byte { ganz = false }
                try stand.sichern(datei)
            }
            // Das letzte Mittel eines Tages schreibt der Leitstand erst mit der ersten Abfrage
            // des nächsten Platzes; zehn Minuten nach Mitternacht ist der Tag sicher fertig.
            if ganz, jetzt.timeIntervalSince(beginn) > 86_400 + 600 {
                stand.abgeschlossen.insert(tag)
                stand.dateien[tag] = nil
            }
        }
        stand.zuletzt = jetzt
        stand.uebernommen = plaetze
        try stand.sichern(datei)
        return (plaetze, ereignisse, tage.count)
    }
}
