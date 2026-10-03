import Foundation
import Observation
import Geraeteschnittstelle

/// Sammelt die Abtastungen der laufenden Abfragen und schreibt je Gerät und Platz den
/// Mittelwert. Ein Schaltzustand wird so zum Anteil: Lief der Brenner drei von fünf Minuten,
/// steht dort 0,6.
@MainActor
@Observable
public final class Verlaufsaufzeichnung {
    /// Steigt nach jedem Schreiben; Diagramme laden daraufhin neu.
    public private(set) var revision = 0

    @ObservationIgnored public let speicher: Verlaufsspeicher
    @ObservationIgnored private var laufend: [String: Sammler] = [:]
    @ObservationIgnored private var kette: Task<Void, Never>?

    struct Sammler {
        var index: Int
        var ort: String
        var summen: [String: Double] = [:]
        var anzahl: [String: Int] = [:]
        var bezeichnungen: [String: String] = [:]

        var zeile: Messzeile {
            Messzeile(geraet: "", ort: ort, index: index,
                      werte: summen.reduce(into: [:]) { $0[$1.key] = $1.value / Double(anzahl[$1.key] ?? 1) },
                      bezeichnungen: bezeichnungen)
        }
    }

    public init(speicher: Verlaufsspeicher) {
        self.speicher = speicher
    }

    public func abtasten(_ a: Abtastung) {
        guard !a.leer else { return }
        let index = Raster.index(a.zeit)
        if let s = laufend[a.geraet], s.index != index {
            // Der Platz ist vorbei; ein Rücksprung der Uhr beginnt ebenso einen neuen.
            schreiben(a.geraet, s)
            laufend[a.geraet] = nil
        }
        var s = laufend[a.geraet] ?? Sammler(index: index, ort: a.ort)
        for (schluessel, wert) in a.werte {
            s.summen[schluessel, default: 0] += wert
            s.anzahl[schluessel, default: 0] += 1
        }
        s.bezeichnungen.merge(a.bezeichnungen) { $1 }
        if !a.ort.isEmpty { s.ort = a.ort }
        laufend[a.geraet] = s
    }

    /// Schreibt die angefangenen Plätze, etwa bevor die App in den Hintergrund geht. Die
    /// Sammlung läuft weiter; am Ende des Platzes wird der vollständige Mittelwert geschrieben.
    public func sichern() async {
        for (geraet, s) in laufend {
            schreiben(geraet, s)
        }
        await kette?.value
    }

    /// Übernimmt den Verlauf eines Heizungsgeräts in die Lücken, beschränkt auf seine eigenen
    /// Fühler: Der Geräteverlauf enthält auch Werte des Nachbargeräts, die dort gezählt werden.
    public func verlaufUebernehmen(geraet: String, zustand: Heizgeraetezustand?, antwort: Verlaufsantwort) {
        guard let zustand, let juengster = antwort.juengsterEpoch, juengster > 1_600_000_000 else { return }
        let rollen = Abtastung.eigeneRollen(zustand)
        var reihen: [String: [Messpunkt]] = [:]
        var namen: [String: String] = [:]
        for (rolle, werte) in antwort.reihen ?? [:] {
            guard let name = rollen[rolle] else { continue }
            let schluessel = Messgroesse.fuehler(rolle)
            reihen[schluessel] = werte.enumerated().compactMap { i, wert in
                guard let wert, let zeit = antwort.zeitpunkt(i, anzahl: werte.count) else { return nil }
                return Messpunkt(zeit: zeit, wert: wert)
            }
            namen[schluessel] = name
        }
        guard !reihen.isEmpty else { return }
        let ort = zustand.geraet?.ort ?? ""
        let speicher = speicher
        let fertig = reihen, bezeichnungen = namen
        einreihen {
            try? await speicher.auffuellen(geraet: geraet, ort: ort, reihen: fertig, bezeichnungen: bezeichnungen)
        }
    }

    private func schreiben(_ geraet: String, _ s: Sammler) {
        var zeile = s.zeile
        zeile.geraet = geraet
        let speicher = speicher, fertig = zeile
        einreihen { try? await speicher.schreiben([fertig]) }
    }

    /// Schreibvorgänge laufen nacheinander, damit ein Zwischenstand nie einen späteren,
    /// vollständigen Mittelwert überschreibt.
    private func einreihen(_ arbeit: @escaping @Sendable () async -> Void) {
        let vorher = kette
        kette = Task { [weak self] in
            await vorher?.value
            await arbeit()
            self?.revision += 1
        }
    }
}
