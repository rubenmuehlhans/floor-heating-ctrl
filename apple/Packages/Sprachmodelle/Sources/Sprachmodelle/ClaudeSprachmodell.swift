import Foundation
import FoundationModels
import Synchronization

/// Claude als Sprachmodell für `LanguageModelSession`.
///
/// Die App arbeitet für alle Modelle mit derselben Sitzungsschicht: Werkzeuge, `@Generable` und
/// Transkript gehören FoundationModels. Dieses Modell übersetzt nur die Anfrage in die
/// Messages-API und den Datenstrom zurück in Ereignisse der Sitzung.
public struct ClaudeSprachmodell: LanguageModel {
    public typealias Executor = ClaudeAusfuehrer

    public let konfiguration: ClaudeKonfiguration

    public init(konfiguration: ClaudeKonfiguration) {
        self.konfiguration = konfiguration
    }

    public init(schluessel: String, modell: String = ClaudeKonfiguration.standardmodell) {
        self.init(konfiguration: ClaudeKonfiguration(schluessel: schluessel, modell: modell))
    }

    public var capabilities: LanguageModelCapabilities {
        LanguageModelCapabilities([.toolCalling, .reasoning, .vision, .guidedGeneration])
    }

    public var executorConfiguration: ClaudeKonfiguration {
        konfiguration
    }
}

public struct ClaudeKonfiguration: Hashable, Sendable {
    /// Die App nimmt das neueste Modell einer Reihe aus der Models-API (`ClaudeModelle`); diese
    /// Kennungen gelten, bis sie geantwortet hat.
    public static let standardmodell = ClaudeModelle.bekannt[.opus]!.kennung
    /// Günstiger als Opus (2 $ statt 4 $ je Million Token Eingabe, 10 $ statt 20 $ Ausgabe), mit
    /// derselben Anfrageform: adaptives Denken, Aufwand von `low` bis `max`, 128 000 Token Ausgabe.
    public static let sonnet = ClaudeModelle.bekannt[.sonnet]!.kennung
    /// Das serverseitige Ersatzmodell (`fallbacks: "default"`) ist ab Opus 5 und Sonnet 5.5
    /// beschrieben; neuere Modelle beider Reihen sollen es behalten. Bei älteren ließe die
    /// unbekannte Angabe die Anfrage scheitern; eine Ablehnung kommt dort als `refusal` zurück.
    public static func mitErsatzmodell(_ modell: String) -> Bool {
        if let v = version(modell, .opus) { return v.lexicographicallyPrecedes([5]) == false }
        if let v = version(modell, .sonnet) { return v.lexicographicallyPrecedes([5, 5]) == false }
        return false
    }

    /// `claude-opus-5-5` → `[5, 5]`; ein angehängtes Datum zählt als letzte Stelle.
    static func version(_ modell: String, _ reihe: ClaudeReihe) -> [Int]? {
        guard modell.hasPrefix(reihe.praefix) else { return nil }
        let stellen = modell.dropFirst(reihe.praefix.count).split(separator: "-").map { Int($0) }
        guard !stellen.isEmpty, !stellen.contains(nil) else { return nil }
        return stellen.compactMap { $0 }
    }
    public static let messagesAPI = URL(string: "https://api.anthropic.com/v1/messages")!

    /// API-Schlüssel; er liegt in der App im Schlüsselbund.
    public var schluessel: String
    public var modell: String
    public var faehigkeiten: ClaudeFaehigkeiten
    /// Weist die API eine Anfrage an `modell` mit 400 oder 404 ab, geht sie unverändert an dieses
    /// Modell. Für ein neu erschienenes Modell, das die Anfrageform dieser App-Version nicht annimmt.
    public var rueckgriff: ClaudeModelleintrag?
    public var beobachter: ClaudeBeobachter?
    /// Obergrenze für Denken und Antwort zusammen
    public var maximaleToken: Int
    /// Lehnen die Sicherheitsklassifikatoren eine Anfrage ab, beantwortet sie serverseitig das
    /// empfohlene Ersatzmodell (`fallbacks: "default"`). Ohne Angabe nur bei den Modellen, für die
    /// `mitErsatzmodell` zutrifft.
    public var ersatzmodell: Bool
    public var adresse: URL
    /// Wartezeiten vor den Wiederholungen bei Überlastung und Serverfehlern; ihre Anzahl ist die
    /// Zahl der Wiederholungen.
    public var wartezeiten: [Duration]
    public var transport: ClaudeTransport

    public init(
        schluessel: String, modell: String = Self.standardmodell, faehigkeiten: ClaudeFaehigkeiten? = nil,
        maximaleToken: Int = 64_000, ersatzmodell: Bool? = nil, rueckgriff: ClaudeModelleintrag? = nil,
        beobachter: ClaudeBeobachter? = nil, adresse: URL = Self.messagesAPI,
        wartezeiten: [Duration] = [.seconds(2), .seconds(6)], transport: ClaudeTransport = .standard
    ) {
        self.schluessel = schluessel
        self.modell = modell
        self.faehigkeiten = faehigkeiten ?? .vermutet(modell)
        self.rueckgriff = rueckgriff
        self.beobachter = beobachter
        self.maximaleToken = maximaleToken
        self.ersatzmodell = ersatzmodell ?? Self.mitErsatzmodell(modell)
        self.adresse = adresse
        self.wartezeiten = wartezeiten
        self.transport = transport
    }
}

extension ClaudeKonfiguration {
    /// Dieselbe Konfiguration mit dem Rückgriffsmodell, ohne weiteren Rückgriff
    func mitRueckgriff() -> ClaudeKonfiguration? {
        guard let r = rueckgriff else { return nil }
        var k = self
        k.modell = r.kennung
        k.faehigkeiten = r.faehigkeiten ?? .vermutet(r.kennung)
        k.ersatzmodell = Self.mitErsatzmodell(r.kennung)
        k.rueckgriff = nil
        return k
    }
}

/// Meldet der App, welches Modell geantwortet hat und welches die API abgewiesen hat, und merkt
/// sich die abgewiesenen, damit die übrigen Anfragen einer Sitzung gleich zum Rückgriff gehen.
/// Eine Klasse aus demselben Grund wie `ClaudeTransport`: verglichen wird über die Identität.
public final class ClaudeBeobachter: Hashable, Sendable {
    public enum Ereignis: Sendable, Equatable {
        /// Das Modell hat eine Anfrage beantwortet.
        case bewaehrt(String)
        /// Das Modell hat die Anfrage abgewiesen, der Rückgriff hat sie beantwortet.
        case abgewiesen(String, meldung: String)
    }

    private let melden: @Sendable (Ereignis) -> Void
    private let abgewiesene = Mutex<Set<String>>([])

    public init(_ melden: @escaping @Sendable (Ereignis) -> Void) {
        self.melden = melden
    }

    func istAbgewiesen(_ modell: String) -> Bool {
        abgewiesene.withLock { $0.contains(modell) }
    }

    func melde(_ ereignis: Ereignis) {
        if case .abgewiesen(let modell, _) = ereignis {
            abgewiesene.withLock { _ = $0.insert(modell) }
        }
        melden(ereignis)
    }

    public static func == (a: ClaudeBeobachter, b: ClaudeBeobachter) -> Bool {
        a === b
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(ObjectIdentifier(self))
    }
}

/// Die URLSession für die API. Eigener Typ, weil die Konfiguration eines Modells `Hashable` sein
/// muss; verglichen wird über die Kennung. Tests setzen hier eine vorgetäuschte Sitzung ein.
public struct ClaudeTransport: Hashable, Sendable {
    public let kennung: UUID
    public let sitzung: URLSession

    public init(sitzung: URLSession) {
        kennung = UUID()
        self.sitzung = sitzung
    }

    /// Antworten mit hohem Aufwand dürfen Minuten dauern. Zwischen zwei Ereignissen vergeht
    /// dagegen wenig Zeit, weil die API im Strom regelmäßig `ping` sendet.
    public static let standard: ClaudeTransport = {
        let k = URLSessionConfiguration.default
        k.timeoutIntervalForRequest = 120
        k.timeoutIntervalForResource = 30 * 60
        k.waitsForConnectivity = true
        return ClaudeTransport(sitzung: URLSession(configuration: k))
    }()

    public static func == (a: Self, b: Self) -> Bool {
        a.kennung == b.kennung
    }

    public func hash(into hasher: inout Hasher) {
        hasher.combine(kennung)
    }
}
