import Foundation
import Geraeteschnittstelle

/// Liest Mitschnitte im Format von `messungen/verlauf.jsonl`: je Zeile ein Zeitpunkt (`epoch`)
/// und unter frei gewählten Namen der vollständige Zustand jedes Geräts, wie ihn `/api/state`
/// liefert. Ob ein Eintrag ein Verteiler oder ein Heizungsgerät ist, folgt aus seinem Inhalt;
/// fehlgeschlagene Abfragen (`{"fehler": …}`) und die Bedarfsabfragen (`…_demand`) werden
/// übergangen.
///
/// Die Werte werden wie bei der eigenen Aufzeichnung je Platz gemittelt und nur in Lücken
/// geschrieben. Ein erneuter Import desselben Mitschnitts ändert deshalb nichts.
public enum Mitschnittimport {
    public struct Ergebnis: Sendable, Equatable {
        public var zeilen = 0
        public var unlesbar = 0
        public var abtastungen = 0
        public var plaetze = 0
        /// Kennung → Ort
        public var geraete: [String: String] = [:]
        public var von: Date?
        public var bis: Date?
    }

    /// `fortschritt` erhält den gelesenen Anteil der Datei zwischen 0 und 1.
    public static func importieren(
        _ url: URL, in speicher: Verlaufsspeicher,
        fortschritt: @escaping @Sendable (Double) -> Void = { _ in }
    ) async throws -> Ergebnis {
        let gesamt = Double((try? FileManager.default.attributesOfItem(atPath: url.path(percentEncoded: false))[.size] as? Int) ?? 0)
        var ergebnis = Ergebnis()
        var sammler: [String: Sammlung] = [:]
        var puffer: [Messzeile] = []
        var gelesen = 0.0
        var zuletztGemeldet = 0.0
        let decoder = JSONDecoder()

        for try await zeile in url.lines {
            try Task.checkCancellation()
            gelesen += Double(zeile.utf8.count + 1)
            ergebnis.zeilen += 1
            guard let satz = try? decoder.decode(Mitschnittzeile.self, from: Data(zeile.utf8)), let epoch = satz.epoch else {
                ergebnis.unlesbar += 1
                continue
            }
            let zeit = Date(timeIntervalSince1970: TimeInterval(epoch))
            ergebnis.von = min(ergebnis.von ?? zeit, zeit)
            ergebnis.bis = max(ergebnis.bis ?? zeit, zeit)
            for (name, zustand) in satz.zustaende {
                let a: Abtastung
                switch zustand {
                case .verteiler(let z):
                    a = .verteiler(z, geraet: z.geraet?.id ?? "mitschnitt-\(name)", zeit: zeit)
                case .heizgeraet(let z):
                    a = .heizgeraet(z, geraet: z.geraet?.id ?? "mitschnitt-\(name)", zeit: zeit)
                }
                guard !a.leer else { continue }
                ergebnis.abtastungen += 1
                ergebnis.geraete[a.geraet] = a.ort
                let index = Raster.index(zeit)
                if let s = sammler[a.geraet], s.index != index {
                    puffer.append(s.zeile(a.geraet))
                    sammler[a.geraet] = nil
                }
                var s = sammler[a.geraet] ?? Sammlung(index: index, ort: a.ort)
                s.hinzu(a)
                sammler[a.geraet] = s
            }
            if puffer.count >= 500 {
                ergebnis.plaetze += puffer.count
                try await speicher.schreiben(puffer, nurLuecken: true)
                puffer.removeAll(keepingCapacity: true)
            }
            if gesamt > 0, gelesen / gesamt - zuletztGemeldet >= 0.01 {
                zuletztGemeldet = gelesen / gesamt
                fortschritt(min(1, zuletztGemeldet))
            }
        }
        for (geraet, s) in sammler { puffer.append(s.zeile(geraet)) }
        ergebnis.plaetze += puffer.count
        try await speicher.schreiben(puffer, nurLuecken: true)
        fortschritt(1)
        return ergebnis
    }

    struct Sammlung {
        var index: Int
        var ort: String
        var summen: [String: Double] = [:]
        var anzahl: [String: Int] = [:]
        var bezeichnungen: [String: String] = [:]

        mutating func hinzu(_ a: Abtastung) {
            for (k, v) in a.werte {
                summen[k, default: 0] += v
                anzahl[k, default: 0] += 1
            }
            bezeichnungen.merge(a.bezeichnungen) { $1 }
            if !a.ort.isEmpty { ort = a.ort }
        }

        func zeile(_ geraet: String) -> Messzeile {
            Messzeile(geraet: geraet, ort: ort, index: index,
                      werte: summen.reduce(into: [:]) { $0[$1.key] = $1.value / Double(anzahl[$1.key] ?? 1) },
                      bezeichnungen: bezeichnungen)
        }
    }
}

/// Eine Zeile des Mitschnitts. Die Namen der Geräte wählt, wer mitschneidet; erkannt wird die
/// Geräteart am Inhalt: Räume hat nur der Verteiler, Fühler und Brenner nur das Heizungsgerät.
struct Mitschnittzeile: Decodable {
    enum Zustand {
        case verteiler(Verteilerzustand)
        case heizgeraet(Heizgeraetezustand)
    }

    var epoch: Int?
    var zustaende: [(String, Zustand)] = []

    struct Schluessel: CodingKey {
        var stringValue: String
        var intValue: Int? { nil }
        init(_ s: String) { stringValue = s }
        init?(stringValue: String) { self.stringValue = stringValue }
        init?(intValue: Int) { nil }
    }

    init(from decoder: any Decoder) throws {
        let c = try decoder.container(keyedBy: Schluessel.self)
        epoch = try c.decodeIfPresent(Int.self, forKey: Schluessel("epoch"))
        for k in c.allKeys where !["epoch", "zeit"].contains(k.stringValue) && !k.stringValue.hasSuffix("_demand") {
            guard let inhalt = try? c.nestedContainer(keyedBy: Schluessel.self, forKey: k) else { continue }
            if inhalt.contains(Schluessel("rooms")) {
                if let z = try? c.decode(Verteilerzustand.self, forKey: k) { zustaende.append((k.stringValue, .verteiler(z))) }
            } else if inhalt.contains(Schluessel("probes")) || inhalt.contains(Schluessel("burner")) {
                if let z = try? c.decode(Heizgeraetezustand.self, forKey: k) { zustaende.append((k.stringValue, .heizgeraet(z))) }
            }
        }
    }
}
