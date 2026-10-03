import Foundation
import FoundationModels

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
    public static let standardmodell = "claude-opus-5"
    /// Günstiger als Opus (2 $ statt 5 $ je Million Token Eingabe, 10 $ statt 25 $ Ausgabe), mit
    /// derselben Anfrageform: adaptives Denken, Aufwand von `low` bis `max`, 128 000 Token Ausgabe.
    public static let sonnet = "claude-sonnet-5"
    /// Modelle, für die das serverseitige Ersatzmodell (`fallbacks: "default"`) beschrieben ist.
    /// Bei anderen Modellen ließe eine unbekannte Angabe die Anfrage scheitern; eine Ablehnung
    /// kommt dort als `refusal` zurück.
    public static let mitErsatzmodell: Set<String> = [standardmodell]
    public static let messagesAPI = URL(string: "https://api.anthropic.com/v1/messages")!

    /// API-Schlüssel; er liegt in der App im Schlüsselbund.
    public var schluessel: String
    public var modell: String
    /// Obergrenze für Denken und Antwort zusammen
    public var maximaleToken: Int
    /// Lehnen die Sicherheitsklassifikatoren eine Anfrage ab, beantwortet sie serverseitig das
    /// empfohlene Ersatzmodell (`fallbacks: "default"`). Ohne Angabe nur bei den Modellen in
    /// `mitErsatzmodell`.
    public var ersatzmodell: Bool
    public var adresse: URL
    /// Wartezeiten vor den Wiederholungen bei Überlastung und Serverfehlern; ihre Anzahl ist die
    /// Zahl der Wiederholungen.
    public var wartezeiten: [Duration]
    public var transport: ClaudeTransport

    public init(
        schluessel: String, modell: String = Self.standardmodell, maximaleToken: Int = 64_000,
        ersatzmodell: Bool? = nil, adresse: URL = Self.messagesAPI,
        wartezeiten: [Duration] = [.seconds(2), .seconds(6)], transport: ClaudeTransport = .standard
    ) {
        self.schluessel = schluessel
        self.modell = modell
        self.maximaleToken = maximaleToken
        self.ersatzmodell = ersatzmodell ?? Self.mitErsatzmodell.contains(modell)
        self.adresse = adresse
        self.wartezeiten = wartezeiten
        self.transport = transport
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
