import Foundation
import Observation
import Anlage

/// Merkt sich über Abfragen und Neustarts hinweg, seit wann ein Befund ansteht, ob er gemeldet
/// wurde und ob er stumm geschaltet ist. Die Regeln selbst prüfen nur den jetzigen Stand; hier
/// entsteht daraus der Ablauf eines Befunds:
///
/// - Ein Befund erscheint, sobald er seine `mindestdauer` ununterbrochen ansteht.
/// - Erscheint er, geht einmal eine Mitteilung hinaus, sofern seine Schwere gemeldet werden soll
///   und er nicht stumm geschaltet ist. Steigt seine Schwere, wird erneut gemeldet.
/// - Verschwindet er, gilt er als erledigt, und seine Mitteilung wird zurückgenommen. Tritt er
///   binnen `ruhezeit` wieder auf, setzt er den alten Ablauf fort: kein neuer Beginn, und die
///   Mitteilung kehrt still zurück, ohne Ton und Banner. So bleibt die Mitteilungszentrale
///   richtig, ohne dass ein flatterndes Gerät ständig meldet.
/// - Erledigte Befunde bleiben `aufbewahrung` lang für die Liste des Erledigten.
@MainActor
@Observable
public final class Befundgedaechtnis {
    public struct Eintrag: Codable, Sendable, Hashable, Identifiable {
        public var id: String
        public var schwere: Int
        public var titel: String
        public var text: String
        public var ort: String
        public var quelle: String
        /// Beginn des jetzigen Ablaufs
        public var erstmals: Date
        public var zuletzt: Date
        public var erledigt: Date?
        /// Seit wann der Befund sichtbar ist, also seine Mindestdauer erreicht hat
        public var sichtbarSeit: Date?
        /// Die höchste bereits gemeldete Schwere
        public var gemeldeteSchwere: Int?
        public var stumm: Bool
        /// Die Mitteilung wurde beim Erledigen zurückgenommen.
        public var zurueckgenommen: Bool? = nil

        public var befundschwere: Befund.Schwere { Befund.Schwere(rawValue: schwere) ?? .hinweis }
    }

    /// Ein sichtbarer Befund mit dem, was das Gedächtnis über ihn weiß
    public struct Stand: Identifiable, Sendable, Hashable {
        public var befund: Befund
        public var seit: Date?
        public var stumm: Bool
        public var id: String { befund.id }

        public init(befund: Befund, seit: Date? = nil, stumm: Bool = false) {
            self.befund = befund
            self.seit = seit
            self.stumm = stumm
        }
    }

    public struct Mitteilung: Sendable, Equatable, Identifiable {
        public var id: String
        public var schwere: Befund.Schwere
        public var titel: String
        public var text: String
        public var ort: String
        /// Ohne Ton und Banner, nur in der Mitteilungszentrale
        public var still: Bool = false
    }

    public struct Ergebnis: Sendable, Equatable {
        public var sichtbar: [Stand] = []
        /// Neu zu meldende Befunde
        public var mitteilungen: [Mitteilung] = []
        /// Erledigte Befunde, deren Mitteilung zurückgenommen werden kann
        public var erledigt: [String] = []
    }

    public private(set) var eintraege: [String: Eintrag] = [:]
    /// Die sichtbaren Befunde nach dem letzten Abgleich
    public private(set) var sichtbar: [Stand] = []

    public var ruhezeit: TimeInterval = 3600
    public var aufbewahrung: TimeInterval = 30 * 86_400

    @ObservationIgnored private let datei: URL?
    @ObservationIgnored private var zuletztGesichert = Date.distantPast
    @ObservationIgnored private var ungesichert = false
    /// Beim allerersten Lauf, etwa nach der Einrichtung, sieht man die vorhandenen Befunde in der
    /// App; sie werden still übernommen. Gemeldet wird, was danach hinzukommt.
    @ObservationIgnored private var ersterLauf: Bool

    /// `datei` nil hält das Gedächtnis nur im Speicher, etwa für Tests.
    public init(datei: URL?) {
        self.datei = datei
        ersterLauf = datei.map { !FileManager.default.fileExists(atPath: $0.path(percentEncoded: false)) } ?? false
        if let datei, let daten = try? Data(contentsOf: datei),
           let liste = try? Self.decoder.decode([Eintrag].self, from: daten) {
            eintraege = Dictionary(liste.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
        }
    }

    public static func standard() -> Befundgedaechtnis {
        let ordner = URL.applicationSupportDirectory.appending(path: "Heizung", directoryHint: .isDirectory)
        try? FileManager.default.createDirectory(at: ordner, withIntermediateDirectories: true)
        return Befundgedaechtnis(datei: ordner.appending(path: "befunde.json"))
    }

    /// Gleicht die Befunde des jetzigen Stands ab. `vollstaendig` heißt: Alle Geräte wurden
    /// mindestens einmal abgefragt. Vorher gilt kein Befund als erledigt, nur weil ein Gerät
    /// noch nicht geantwortet hat.
    @discardableResult
    public func abgleichen(_ befunde: [Befund], jetzt: Date, vollstaendig: Bool, melden: Set<Befund.Schwere>) -> Ergebnis {
        var ergebnis = Ergebnis()
        var geaendert = false
        let ids = Set(befunde.map(\.id))

        for b in befunde {
            var e: Eintrag
            if let alt = eintraege[b.id], alt.erledigt.map({ jetzt.timeIntervalSince($0) < ruhezeit }) ?? true {
                e = alt
                if e.erledigt != nil { geaendert = true }
                e.erledigt = nil
            } else {
                // Neuer Ablauf; die Stummschaltung gilt weiter.
                e = Eintrag(id: b.id, schwere: b.schwere.rawValue, titel: b.titel, text: b.text, ort: b.ort, quelle: b.quelle,
                            erstmals: jetzt, zuletzt: jetzt, erledigt: nil, sichtbarSeit: nil, gemeldeteSchwere: nil,
                            stumm: eintraege[b.id]?.stumm ?? false)
                geaendert = true
            }
            e.zuletzt = jetzt
            if e.schwere != b.schwere.rawValue || e.titel != b.titel || e.text != b.text {
                e.schwere = b.schwere.rawValue
                e.titel = b.titel
                e.text = b.text
                e.ort = b.ort
                e.quelle = b.quelle
                geaendert = true
            }
            if jetzt.timeIntervalSince(e.erstmals) >= b.mindestdauer {
                if e.sichtbarSeit == nil {
                    e.sichtbarSeit = jetzt
                    geaendert = true
                }
                ergebnis.sichtbar.append(Stand(befund: b, seit: e.erstmals, stumm: e.stumm))
                let hoeher = e.gemeldeteSchwere.map { b.schwere.rawValue > $0 } ?? true
                if hoeher, !e.stumm, !ersterLauf, melden.contains(b.schwere) {
                    ergebnis.mitteilungen.append(Mitteilung(id: b.id, schwere: b.schwere, titel: b.titel, text: b.text, ort: b.ort))
                    e.gemeldeteSchwere = b.schwere.rawValue
                    e.zurueckgenommen = false
                    geaendert = true
                } else if e.zurueckgenommen == true, !e.stumm, melden.contains(b.schwere) {
                    ergebnis.mitteilungen.append(Mitteilung(id: b.id, schwere: b.schwere, titel: b.titel, text: b.text, ort: b.ort, still: true))
                    e.zurueckgenommen = false
                    geaendert = true
                } else if e.gemeldeteSchwere == nil {
                    // Nicht gemeldet, weil diese Schwere gerade nicht gemeldet wird: Ein späteres
                    // Einschalten der Mitteilungen soll nicht alle alten Befunde nachholen.
                    e.gemeldeteSchwere = b.schwere.rawValue
                    geaendert = true
                }
            }
            eintraege[b.id] = e
        }

        if vollstaendig {
            for (id, var e) in eintraege where e.erledigt == nil && !ids.contains(id) {
                e.erledigt = jetzt
                if e.sichtbarSeit != nil {
                    ergebnis.erledigt.append(id)
                    e.zurueckgenommen = true
                }
                eintraege[id] = e
                geaendert = true
            }
        }
        // Aufräumen: lange Erledigtes und nie Sichtbares entfällt.
        for (id, e) in eintraege {
            guard let erledigt = e.erledigt else { continue }
            if jetzt.timeIntervalSince(erledigt) > aufbewahrung || (e.sichtbarSeit == nil && jetzt.timeIntervalSince(erledigt) > ruhezeit) {
                eintraege[id] = nil
                geaendert = true
            }
        }

        // Nur bei Änderung veröffentlichen; sonst bauten die Ansichten nach jedem Bild neu auf.
        if sichtbar != ergebnis.sichtbar { sichtbar = ergebnis.sichtbar }
        if ersterLauf, vollstaendig {
            // Die Datei entsteht jetzt, auch ohne Befund; der nächste Start ist kein erster mehr.
            ersterLauf = false
            geaendert = true
        }
        if geaendert { ungesichert = true }
        // Nur `zuletzt` hat sich geändert: höchstens alle zehn Minuten schreiben.
        if ungesichert, geaendert || jetzt.timeIntervalSince(zuletztGesichert) > 600 {
            speichern(jetzt)
        }
        return ergebnis
    }

    public func stand(_ id: String) -> Stand? {
        sichtbar.first { $0.id == id }
    }

    /// Erledigte Befunde, jüngste zuerst, die einmal sichtbar waren
    public var erledigte: [Eintrag] {
        eintraege.values.filter { $0.erledigt != nil && $0.sichtbarSeit != nil }
            .sorted { ($0.erledigt ?? .distantPast) > ($1.erledigt ?? .distantPast) }
    }

    /// Stumm geschaltete Befunde erscheinen weiter, lösen aber keine Mitteilung aus.
    public func stummSchalten(_ id: String, _ stumm: Bool) {
        guard var e = eintraege[id] else { return }
        e.stumm = stumm
        eintraege[id] = e
        sichtbar = sichtbar.map { $0.id == id ? Stand(befund: $0.befund, seit: $0.seit, stumm: stumm) : $0 }
        speichern(.now)
    }

    public func istStumm(_ id: String) -> Bool {
        eintraege[id]?.stumm ?? false
    }

    private func speichern(_ jetzt: Date) {
        zuletztGesichert = jetzt
        ungesichert = false
        guard let datei, let daten = try? Self.encoder.encode(eintraege.values.sorted { $0.id < $1.id }) else { return }
        try? daten.write(to: datei, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }

    private static var encoder: JSONEncoder {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return e
    }

    private static var decoder: JSONDecoder {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }
}
