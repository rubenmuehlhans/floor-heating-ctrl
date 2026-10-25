import Foundation
import FoundationModels

/// Der Verlauf eines Gesprächs, wie ihn das Modell sieht.
///
/// Scheitert eine Antwort, setzt `LanguageModelSession` ihr Transkript von sich aus auf den Stand
/// vor der Frage zurück. Die App zeigt Frage, Werkzeugaufrufe und den Text bis zum Fehler aber
/// weiter an; die nächste Frage verwiese dann auf etwas, das das Modell nie gesehen hat. Deshalb
/// behalten die Gesprächssitzungen ihr Transkript, und `nachAbbruch` macht es wieder vollständig.
public enum Gespraechsverlauf {
    /// Sitzung für ein Gespräch: mit dem bisherigen Transkript, sonst mit den Anweisungen
    public static func sitzung(model: some LanguageModel, tools: [any Tool], anweisungen: String,
                               transkript: Transcript?) -> LanguageModelSession {
        let sitzung = transkript.map { LanguageModelSession(model: model, tools: tools, transcript: $0) }
            ?? LanguageModelSession(model: model, tools: tools, instructions: Instructions(anweisungen))
        sitzung.transcriptErrorHandlingPolicy = .preserveTranscript
        return sitzung
    }

    /// Das Transkript nach einer gescheiterten oder angehaltenen Antwort. Jeder Werkzeugaufruf
    /// ohne Ergebnis erhält eines, das `grund` nennt: Die Messages-API verlangt zu jedem
    /// `tool_use` ein `tool_result`, und das Modell erfährt, dass der Aufruf nicht ausgeführt wurde.
    public static func nachAbbruch(_ transkript: Transcript, grund: String) -> Transcript {
        var eintraege = Array(transkript)
        let beantwortet = Set(eintraege.compactMap { e -> String? in
            if case .toolOutput(let o) = e { o.id } else { nil }
        })
        var i = 0
        while i < eintraege.count {
            guard case .toolCalls(let aufrufe) = eintraege[i] else {
                i += 1
                continue
            }
            // Die fehlenden Ergebnisse stehen hinter denen, die schon vorliegen.
            var ende = i + 1
            while ende < eintraege.count, case .toolOutput = eintraege[ende] { ende += 1 }
            let fehlend = aufrufe.filter { !beantwortet.contains($0.id) }.map { a in
                Transcript.Entry.toolOutput(Transcript.ToolOutput(
                    id: a.id, toolName: a.toolName,
                    segments: [.text(Transcript.TextSegment(content: "Nicht ausgeführt: \(grund)"))]))
            }
            eintraege.insert(contentsOf: fehlend, at: ende)
            i = ende + fehlend.count
        }
        return Transcript(entries: eintraege)
    }
}
