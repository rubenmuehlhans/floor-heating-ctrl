import Foundation
import FoundationModels
import Observation
import Anlage
import Assistent
import Sprachmodelle

/// Die Sitzungen mit dem gewählten Modell.
///
/// Eine Sitzung je Gespräch; sie trägt das Transkript und wird aus der Ablage wiederhergestellt,
/// wenn ein Gespräch nach einem Neustart weitergeht. Claude bekommt Anweisungen samt Handbuch und
/// Konzepten, die Apple-Modelle kürzere Anweisungen und `wissen_suchen`. Die Werkzeuge lesen über
/// die Anlagenanbindung; eingreifen können sie nur mit Vorschlägen.
@MainActor
@Observable
final class Assistenzdienst {
    /// Bausteine der Antwort, die gerade entsteht
    private(set) var laufend: [Beitrag.Baustein] = []
    private(set) var antwortLaeuft = false
    private(set) var berichtLaeuft = false

    @ObservationIgnored private var sitzung: LanguageModelSession?
    @ObservationIgnored private var sitzungskennung: String?
    /// Der Text der laufenden Antwort, so weit er eingetroffen ist
    @ObservationIgnored private var eingetroffen = ""
    @ObservationIgnored let wissen = Wissen.ausBuendel()
    @ObservationIgnored private let privateCloud = PrivateCloudComputeLanguageModel()
    /// Hört, welches Claude-Modell geantwortet und welches abgewiesen hat; setzt das App-Modell.
    @ObservationIgnored var claudeBeobachter: ClaudeBeobachter?

    struct Antwort {
        var bausteine: [Beitrag.Baustein]
        var verbrauch: LanguageModelSession.Usage?
        var transkript: Transcript?
    }

    // MARK: Verfügbarkeit

    /// Private Cloud Compute verlangt die Berechtigung `com.apple.developer.private-cloud-compute`,
    /// die Apple auf Antrag vergibt. Ohne sie beendet FoundationModels die App bei der ersten
    /// Anfrage, statt einen Fehler zu werfen. Das Projekt trägt deshalb in die Info.plist ein, ob
    /// die Berechtigung vorliegt (`HEIZUNG_PCC` in project.yml); ohne sie fragt die App PCC nie an.
    static let privateCloudFreigegeben = (Bundle.main.object(forInfoDictionaryKey: "HeizungPrivateCloudCompute") as? String) == "YES"

    /// `nil`, wenn das Modell bereitsteht, sonst der Grund
    func hindernis(_ modell: AppModell.KIModell, schluessel: String?) -> String? {
        switch modell {
        case .claude, .claudeSonnet, .claudeHaiku:
            return schluessel == nil ? "Für Claude braucht der Assistent einen API-Schlüssel von Anthropic. Hinterlegen Sie ihn unter Geräte › Einstellungen der App." : nil
        case .privateCloud:
            guard Self.privateCloudFreigegeben else {
                return "Private Cloud Compute verlangt eine Freigabe von Apple für diese App, die noch nicht vorliegt. Wählen Sie Claude oder das Apple-Modell auf dem Gerät."
            }
            switch privateCloud.availability {
            case .available:
                if privateCloud.quotaUsage.isLimitReached {
                    let bis = privateCloud.quotaUsage.resetDate.map { " Es erneuert sich \(Format.tagMitZeit($0))." } ?? ""
                    return "Das Kontingent für Private Cloud Compute ist aufgebraucht.\(bis)"
                }
                return nil
            case .unavailable(.deviceNotEligible):
                return "Private Cloud Compute steht auf diesem Gerät nicht zur Verfügung."
            case .unavailable:
                return "Private Cloud Compute ist noch nicht bereit. Apple Intelligence muss eingeschaltet und eingerichtet sein."
            }
        case .geraet:
            switch SystemLanguageModel.default.availability {
            case .available: return nil
            case .unavailable(.deviceNotEligible): return "Dieses Gerät unterstützt das Apple-Modell nicht."
            case .unavailable(.appleIntelligenceNotEnabled): return "Apple Intelligence ist in den Systemeinstellungen ausgeschaltet."
            case .unavailable(.modelNotReady): return "Das Apple-Modell wird noch geladen. Versuchen Sie es später noch einmal."
            case .unavailable: return "Das Apple-Modell steht gerade nicht zur Verfügung."
            }
        }
    }

    /// Stand des Kontingents für die Einstellungen
    var kontingent: String {
        guard Self.privateCloudFreigegeben else { return "für diese App nicht freigegeben" }
        guard case .available = privateCloud.availability else { return "nicht verfügbar" }
        switch privateCloud.quotaUsage.status {
        case .belowLimit(let unter): return unter.isApproachingLimit ? "bald aufgebraucht" : "unter dem Kontingent"
        case .limitReached: return "aufgebraucht"
        @unknown default: return "unbekannt"
        }
    }

    // MARK: Sitzungen

    private func neueSitzung(_ wahl: AppModell.Modellwahl, schluessel: String?, zugriff: any Anlagenzugriff, transkript: Transcript?) -> LanguageModelSession {
        switch wahl.modell {
        case .claude, .claudeSonnet, .claudeHaiku:
            let m = claudeModell(wahl, schluessel: schluessel ?? "")
            let werkzeuge = Werkzeugsatz.claude(zugriff)
            if let transkript { return LanguageModelSession(model: m, tools: werkzeuge, transcript: transkript) }
            return LanguageModelSession(model: m, tools: werkzeuge, instructions: Instructions(Anweisungen.claude(wissen)))
        case .privateCloud:
            let werkzeuge = Werkzeugsatz.apple(zugriff, wissen: wissen)
            if let transkript { return LanguageModelSession(model: privateCloud, tools: werkzeuge, transcript: transkript) }
            return LanguageModelSession(model: privateCloud, tools: werkzeuge, instructions: Instructions(Anweisungen.apple()))
        case .geraet:
            let werkzeuge = Werkzeugsatz.apple(zugriff, wissen: wissen)
            if let transkript { return LanguageModelSession(model: SystemLanguageModel.default, tools: werkzeuge, transcript: transkript) }
            return LanguageModelSession(model: SystemLanguageModel.default, tools: werkzeuge, instructions: Instructions(Anweisungen.apple()))
        }
    }

    /// Die Apple-Modelle geraten bei langen Antworten gelegentlich in Wiederholungen; sie
    /// antworten ohnehin knapp.
    static func optionen(_ modell: AppModell.KIModell) -> GenerationOptions {
        modell.istClaude ? GenerationOptions() : GenerationOptions(maximumResponseTokens: 900)
    }

    private func kontextoptionen(_ modell: AppModell.KIModell, _ stufe: ContextOptions.ReasoningLevel) -> ContextOptions {
        modell.istClaude ? ContextOptions(reasoningLevel: stufe) : ContextOptions()
    }

    /// Wechselt das Modell oder das Gespräch, beginnt die nächste Frage eine neue Sitzung.
    func zuruecksetzen() {
        sitzung = nil
        sitzungskennung = nil
        laufend = []
    }

    // MARK: Fragen

    /// Stellt eine Frage im Gespräch `gespraech`. Während die Antwort entsteht, zeigt `laufend`
    /// Überlegung, Werkzeugaufrufe und den eintreffenden Text.
    func frage(_ frage: String, gespraech: String, modell: AppModell.Modellwahl, schluessel: String?,
               zugriff: any Anlagenzugriff, transkript: Transcript?) async -> Antwort {
        let kennung = "\(gespraech)|\(modell.kennung)|\(schluessel?.suffix(6) ?? "")"
        if sitzung == nil || sitzungskennung != kennung {
            // Ein früheres Transkript passt nur zum selben Modell; die Anweisungen stehen darin.
            sitzung = neueSitzung(modell, schluessel: schluessel, zugriff: zugriff, transkript: transkript)
            sitzungskennung = kennung
        }
        guard let sitzung else { return Antwort(bausteine: []) }
        antwortLaeuft = true
        laufend = []
        defer {
            antwortLaeuft = false
            laufend = []
        }
        let beginn = sitzung.transcript.count
        let vorher = sitzung.usage
        eingetroffen = ""
        // Werkzeugaufrufe erscheinen im Transkript, während kein Text eintrifft.
        let beobachter = Task { @MainActor [weak self] in
            while !Task.isCancelled, let self {
                laufend = Darstellung.bausteine(sitzung.transcript.dropFirst(beginn), laufenderText: eingetroffen)
                try? await Task.sleep(for: .milliseconds(350))
            }
        }
        defer { beobachter.cancel() }
        do {
            for try await teil in sitzung.streamResponse(to: frage, options: Self.optionen(modell.modell), contextOptions: kontextoptionen(modell.modell, .moderate)) {
                eingetroffen = teil.content
                laufend = Darstellung.bausteine(sitzung.transcript.dropFirst(beginn), laufenderText: eingetroffen)
            }
            return Antwort(bausteine: Darstellung.bausteine(sitzung.transcript.dropFirst(beginn)),
                           verbrauch: Self.differenz(sitzung.usage, vorher), transkript: sitzung.transcript)
        } catch {
            // Nach einem Fehler setzt die Sitzung ihr Transkript zurück; was bis dahin zu sehen
            // war, samt Werkzeugaufrufen, bleibt stehen.
            let bisher = Darstellung.bausteine(sitzung.transcript.dropFirst(beginn), laufenderText: eingetroffen)
            var bausteine = laufend.count > bisher.count ? laufend : bisher
            bausteine.append(.fehler(Task.isCancelled || error is CancellationError ? "Angehalten." : Self.meldung(error, modell)))
            // Nach einem Fehler steht die Sitzung womöglich mitten im Werkzeuglauf; die nächste
            // Frage beginnt mit dem Transkript der letzten vollständigen Antwort.
            self.sitzung = nil
            return Antwort(bausteine: bausteine, verbrauch: Self.differenz(sitzung.usage, vorher), transkript: nil)
        }
    }

    // MARK: Lagebericht

    func lagebericht(modell: AppModell.Modellwahl, schluessel: String?, zugriff: any Anlagenzugriff, befunde: [String]) async throws -> (Lagebericht, LanguageModelSession.Usage) {
        berichtLaeuft = true
        defer { berichtLaeuft = false }
        let s = neueSitzung(modell, schluessel: schluessel, zugriff: zugriff, transkript: nil)
        let antwort = try await s.respond(to: Anweisungen.lagebericht, generating: Lageberichtsentwurf.self,
                                          options: Self.optionen(modell.modell), contextOptions: kontextoptionen(modell.modell, .moderate))
        return (antwort.content.lagebericht(erstellt: .now, modell: modell.name, befunde: befunde), antwort.usage)
    }

    private func claudeModell(_ wahl: AppModell.Modellwahl, schluessel: String) -> ClaudeSprachmodell {
        let claude = wahl.claude ?? ClaudeModelle.bekannt[.opus]!
        return ClaudeSprachmodell(konfiguration: ClaudeKonfiguration(
            schluessel: schluessel, modell: claude.kennung, faehigkeiten: claude.faehigkeiten,
            rueckgriff: wahl.rueckgriff, beobachter: claudeBeobachter))
    }

    /// Kleinste Anfrage mit geringem Aufwand; bestätigt Schlüssel und Erreichbarkeit.
    func pruefen(schluessel: String, modell: AppModell.Modellwahl) async throws -> String {
        let s = LanguageModelSession(model: claudeModell(modell, schluessel: schluessel)) {
            "Antworten Sie ausschließlich mit dem Wort OK."
        }
        return try await s.respond(to: "Verbindungstest", contextOptions: ContextOptions(reasoningLevel: .light)).content
    }

    // MARK: Hilfen

    static func differenz(_ nachher: LanguageModelSession.Usage, _ vorher: LanguageModelSession.Usage) -> LanguageModelSession.Usage {
        LanguageModelSession.Usage(
            input: .init(totalTokenCount: nachher.input.totalTokenCount - vorher.input.totalTokenCount,
                         cachedTokenCount: nachher.input.cachedTokenCount - vorher.input.cachedTokenCount),
            output: .init(totalTokenCount: nachher.output.totalTokenCount - vorher.output.totalTokenCount,
                          reasoningTokenCount: nachher.output.reasoningTokenCount - vorher.output.reasoningTokenCount))
    }

    static func meldung(_ fehler: any Error, _ modell: AppModell.Modellwahl) -> String {
        if let f = fehler as? LanguageModelError {
            switch f {
            case .refusal, .guardrailViolation:
                return "Das Modell hat die Anfrage abgelehnt. Formulieren Sie die Frage anders."
            case .contextSizeExceeded:
                return "Das Gespräch ist für \(modell.name) zu lang geworden. Beginnen Sie ein neues Gespräch."
            case .rateLimited:
                return "Zu viele Anfragen in kurzer Zeit. Versuchen Sie es in einer Minute noch einmal."
            case .timeout:
                return "Das Modell hat nicht rechtzeitig geantwortet. Versuchen Sie es noch einmal."
            case .unsupportedCapability, .unsupportedGenerationGuide, .unsupportedTranscriptContent:
                return "\(modell.name) kann diese Anfrage nicht bearbeiten. Wählen Sie in den Einstellungen ein anderes Modell."
            case .unsupportedLanguageOrLocale:
                return "\(modell.name) unterstützt die Sprache dieser Anfrage nicht."
            @unknown default:
                break
            }
        }
        return "Die Anfrage ist gescheitert: \(fehler.localizedDescription)"
    }
}
