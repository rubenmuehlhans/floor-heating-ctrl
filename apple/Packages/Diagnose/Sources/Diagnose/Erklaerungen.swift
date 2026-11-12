import Foundation
import Observation
import Anlage

/// Erklärung des Betreibers zu einem Befund, etwa: Die Relais hinter der Kesselregelung sind
/// nachts stromlos, das ist gewollt. Eine gemerkte Erklärung nimmt den Befund aus dem Lagebericht
/// und aus den Mitteilungen, solange er so aussieht wie beim Erklären:
///
/// - dieselbe Kennung,
/// - keine Orte außer denen beim Erklären (fällt ein weiteres Relais aus, meldet die App wieder),
/// - keine höhere Schwere,
/// - nicht abgelaufen und, mit Höchstdauer, nicht länger am Stück als erlaubt.
///
/// Ob die Ursache dieselbe ist, kann die App nicht erkennen; dagegen hilft die Höchstdauer.
public struct Erklaerung: Codable, Sendable, Hashable, Identifiable {
    public enum Status: String, Codable, Sendable, Hashable {
        /// Vom Assistenten angelegt, noch nicht bestätigt
        case offen
        case gemerkt
        case verworfen
    }

    public var id: String
    /// Kennung des Befunds
    public var befund: String
    public var titel: String
    /// Die Orte des Befunds beim Erklären
    public var orte: [String]
    public var schwere: Befund.Schwere
    /// Die Erklärung im Wortlaut des Betreibers
    public var text: String
    public var erstellt: Date
    /// Gilt bis zu diesem Zeitpunkt; nil bis auf Widerruf
    public var bis: Date?
    /// So lange darf der Befund am Stück anstehen; nil ohne Grenze
    public var hoechstdauer: TimeInterval?
    public var status: Status
    public var gemerkt: Date?

    public init(id: String = UUID().uuidString, befund: Befund, text: String, erstellt: Date,
                bis: Date? = nil, hoechstdauer: TimeInterval? = nil, status: Status = .offen) {
        self.id = id
        self.befund = befund.id
        self.titel = befund.titel
        self.orte = Self.orte(befund.ort)
        self.schwere = befund.schwere
        self.text = text
        self.erstellt = erstellt
        self.bis = bis
        self.hoechstdauer = hoechstdauer
        self.status = status
    }

    /// Ob die Erklärung einen Befund abdeckt, und wenn nicht, warum
    public enum Deckung: Sendable, Hashable {
        case gedeckt
        /// Der Befund steht an weiteren Orten an
        case weitereOrte([String])
        case schwerer(Befund.Schwere)
        case abgelaufen
        /// Der Befund steht länger an als die Höchstdauer
        case zuLange

        public var gedeckt: Bool { self == .gedeckt }

        /// Grund in einem Satz für die Anzeige, nil wenn gedeckt
        public var grund: String? {
            switch self {
            case .gedeckt: nil
            case .weitereOrte(let orte): "Der Befund betrifft jetzt auch \(orte.joined(separator: ", "))."
            case .schwerer: "Der Befund ist schwerer als beim Erklären."
            case .abgelaufen: "Die Erklärung ist abgelaufen."
            case .zuLange: "Der Befund steht länger an als erlaubt."
            }
        }
    }

    /// `seit`: Beginn des jetzigen Ablaufs laut Befundgedächtnis; ohne Angabe gilt die
    /// Höchstdauer als eingehalten. nil, wenn die Erklärung nicht zu diesem Befund gehört.
    public func deckung(_ b: Befund, seit: Date?, jetzt: Date) -> Deckung? {
        guard b.id == befund else { return nil }
        if let bis, jetzt >= bis { return .abgelaufen }
        let weitere = Self.orte(b.ort).filter { !orte.contains($0) }
        if !weitere.isEmpty { return .weitereOrte(weitere) }
        if b.schwere > schwere { return .schwerer(b.schwere) }
        if let hoechstdauer, let seit, jetzt.timeIntervalSince(seit) > hoechstdauer { return .zuLange }
        return .gedeckt
    }

    public func abgelaufen(_ jetzt: Date) -> Bool {
        bis.map { jetzt >= $0 } ?? false
    }

    /// Der Ort eines Befunds kann mehrere nennen, etwa „Heizkreis 1, Kesselkreispumpe“.
    public static func orte(_ ort: String) -> [String] {
        ort.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }
}

/// Die Erklärungen des Betreibers, über Neustarts hinweg. Je Befund gilt höchstens eine gemerkte.
@MainActor
@Observable
public final class Erklaerungsliste {
    /// Alle Erklärungen, jüngste zuerst, auch offene und verworfene für die Karten im Gespräch
    public private(set) var eintraege: [Erklaerung] = []

    @ObservationIgnored private let datei: URL?

    /// `datei` nil hält die Liste nur im Speicher, etwa für Tests und die Beispielanlage.
    public init(datei: URL?) {
        self.datei = datei
        if let datei, let daten = try? Data(contentsOf: datei) {
            let d = JSONDecoder()
            d.dateDecodingStrategy = .iso8601
            eintraege = (try? d.decode([Erklaerung].self, from: daten)) ?? []
        }
    }

    public static func standard() -> Erklaerungsliste {
        let ordner = URL.applicationSupportDirectory.appending(path: "Heizung", directoryHint: .isDirectory)
        try? FileManager.default.createDirectory(at: ordner, withIntermediateDirectories: true)
        return Erklaerungsliste(datei: ordner.appending(path: "erklaerungen.json"))
    }

    public func erklaerung(_ id: String) -> Erklaerung? {
        eintraege.first { $0.id == id }
    }

    /// Die gemerkten Erklärungen
    public var gemerkte: [Erklaerung] {
        eintraege.filter { $0.status == .gemerkt }
    }

    /// Die gemerkte Erklärung zu einem Befund
    public func gemerkte(befund id: String) -> Erklaerung? {
        eintraege.first { $0.status == .gemerkt && $0.befund == id }
    }

    /// Ob eine gemerkte Erklärung den Befund abdeckt; nil, wenn es zu ihm keine gibt.
    public func deckung(_ b: Befund, seit: Date?, jetzt: Date) -> Erklaerung.Deckung? {
        gemerkte(befund: b.id)?.deckung(b, seit: seit, jetzt: jetzt)
    }

    public func anlegen(_ e: Erklaerung) {
        eintraege.insert(e, at: 0)
        // Offene und verworfene Karten älter als 30 Tage entfallen.
        let grenze = e.erstellt.addingTimeInterval(-30 * 86_400)
        eintraege.removeAll { $0.status != .gemerkt && $0.erstellt < grenze }
        speichern()
    }

    /// Bestätigt eine Erklärung; eine ältere zum selben Befund entfällt.
    public func merken(_ id: String, jetzt: Date = .now) {
        guard let i = eintraege.firstIndex(where: { $0.id == id }), eintraege[i].status != .gemerkt else { return }
        let befund = eintraege[i].befund
        eintraege[i].status = .gemerkt
        eintraege[i].gemerkt = jetzt
        eintraege.removeAll { $0.id != id && $0.befund == befund && $0.status == .gemerkt }
        speichern()
    }

    public func verwerfen(_ id: String) {
        guard let i = eintraege.firstIndex(where: { $0.id == id }), eintraege[i].status == .offen else { return }
        eintraege[i].status = .verworfen
        speichern()
    }

    /// Widerruft eine gemerkte Erklärung; die Karte im Gespräch zeigt sie danach als verworfen.
    public func loeschen(_ id: String) {
        guard let i = eintraege.firstIndex(where: { $0.id == id }) else { return }
        eintraege[i].status = .verworfen
        speichern()
    }

    /// Teilt die sichtbaren Befunde in offene und abgedeckte.
    public func einordnen(_ staende: [Befundgedaechtnis.Stand], jetzt: Date) -> Einordnung {
        var ergebnis = Einordnung()
        for s in staende {
            if let e = gemerkte(befund: s.befund.id), e.deckung(s.befund, seit: s.seit, jetzt: jetzt) == .gedeckt {
                ergebnis.erklaert.append(.init(stand: s, erklaerung: e))
            } else {
                ergebnis.offen.append(s)
            }
        }
        return ergebnis
    }

    public func einordnen(_ befunde: [Befund], jetzt: Date) -> Einordnung {
        einordnen(befunde.map { Befundgedaechtnis.Stand(befund: $0) }, jetzt: jetzt)
    }

    public struct Einordnung: Sendable {
        public var offen: [Befundgedaechtnis.Stand] = []
        public var erklaert: [Erklaert] = []
    }

    public struct Erklaert: Sendable, Identifiable {
        public var stand: Befundgedaechtnis.Stand
        public var erklaerung: Erklaerung
        public var id: String { stand.id }

        public init(stand: Befundgedaechtnis.Stand, erklaerung: Erklaerung) {
            self.stand = stand
            self.erklaerung = erklaerung
        }
    }

    private func speichern() {
        guard let datei else { return }
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        guard let daten = try? e.encode(eintraege) else { return }
        try? daten.write(to: datei, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }
}
