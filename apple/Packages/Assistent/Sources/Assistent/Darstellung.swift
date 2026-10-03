import Foundation
import FoundationModels
import Geraeteschnittstelle

/// Macht aus den Einträgen des Transkripts die Bausteine eines Beitrags: Überlegung,
/// Werkzeugaufrufe mit einer Zeile, was gelesen wurde, Vorschlagskarten und Text.
public enum Darstellung {
    /// `laufenderText` ist der Text, der gerade eintrifft und noch in keinem Eintrag steht.
    public static func bausteine(_ eintraege: some Collection<Transcript.Entry>, laufenderText: String? = nil) -> [Beitrag.Baustein] {
        var ausgaben: [String: String] = [:]
        for eintrag in eintraege {
            if case .toolOutput(let o) = eintrag { ausgaben[o.id] = text(o.segments) }
        }
        var bausteine: [Beitrag.Baustein] = []
        for eintrag in eintraege {
            switch eintrag {
            case .reasoning(let r):
                let t = text(r.segments).trimmingCharacters(in: .whitespacesAndNewlines)
                if !t.isEmpty { bausteine.append(.ueberlegung(t)) }
            case .toolCalls(let aufrufe):
                for a in aufrufe {
                    let argumente = (try? JSONWert.lesen(Data(a.arguments.jsonString.utf8))) ?? [:]
                    let ausgabe = ausgaben[a.id]
                    bausteine.append(.werkzeug(Werkzeugaufruf(
                        id: a.id, name: a.toolName, beschreibung: beschreibung(a.toolName, argumente, ausgabe: ausgabe),
                        ergebnis: ausgabe.map { zusammenfassung(a.toolName, $0) } ?? "", fertig: ausgabe != nil)))
                    if let ausgabe, let id = vorschlagskennung(a.toolName, ausgabe) {
                        bausteine.append(.vorschlag(id))
                    }
                }
            case .response(let r):
                let t = text(r.segments).trimmingCharacters(in: .whitespacesAndNewlines)
                if !t.isEmpty { bausteine.append(.text(t)) }
            default:
                break
            }
        }
        if let laufenderText, !laufenderText.isEmpty {
            // Steht die Antwort schon teilweise im Transkript, ersetzt der längere Stand sie.
            if case .text(let t)? = bausteine.last, laufenderText.hasPrefix(t) || t.hasPrefix(laufenderText) {
                bausteine[bausteine.count - 1] = .text(t.count >= laufenderText.count ? t : laufenderText)
            } else {
                bausteine.append(.text(laufenderText))
            }
        }
        return bausteine
    }

    static func text(_ segmente: [Transcript.Segment]) -> String {
        segmente.compactMap {
            if case .text(let t) = $0 { t.content } else { nil }
        }.joined(separator: "\n")
    }

    static func vorschlagskennung(_ werkzeug: String, _ ausgabe: String) -> String? {
        guard werkzeug == "aenderung_vorschlagen" || werkzeug == "aktion_vorschlagen",
              let json = try? JSONWert.lesen(Data(ausgabe.utf8)), json["angelegt"] == true else { return nil }
        return json["vorschlag"]?.alsText
    }

    /// Eine Zeile, was das Werkzeug gelesen oder angelegt hat
    static func beschreibung(_ werkzeug: String, _ a: JSONWert, ausgabe: String?) -> String {
        let geraet = a["geraet"]?.alsText.flatMap { $0.isEmpty ? nil : $0 }
        switch werkzeug {
        case "anlage_status":
            if a["rohdaten"] == true, let geraet { return "Vollständige Antwort von \(geraet) gelesen" }
            if let geraet { return "Zustand von \(geraet) gelesen" }
            return switch a["bereich"]?.alsText {
            case "verteiler": "Zustand der Verteiler gelesen"
            case "heizung": "Zustand der Heizungsgeräte gelesen"
            default: "Zustand aller Geräte gelesen"
            }
        case "verlauf":
            let reihen = (a["reihen"]?.alsListe ?? []).compactMap(\.alsText)
            let stunden = a["stunden"]?.alsGanzzahl ?? 24
            let zeitraum = stunden % 24 == 0 && stunden > 24 ? "\(stunden / 24) Tage" : "\(stunden) Std."
            return reihen.isEmpty ? "Vorhandene Verlaufsreihen gelesen" : "Verlauf gelesen: \(reihen.prefix(4).joined(separator: ", "))\(reihen.count > 4 ? " …" : ""), \(zeitraum)"
        case "ereignisse":
            let arten = (a["arten"]?.alsListe ?? []).compactMap(\.alsText)
            let stunden = a["stunden"]?.alsGanzzahl ?? 24
            let zeitraum = stunden % 24 == 0 && stunden > 24 ? "\(stunden / 24) Tage" : "\(stunden) Std."
            let was = arten.isEmpty ? "Ereignisse" : "Ereignisse (\(arten.prefix(3).joined(separator: ", ")))"
            return "\(was) gelesen\(geraet.map { " für \($0)" } ?? ""), \(zeitraum)"
        case "feinverlauf":
            let reihen = (a["reihen"]?.alsListe ?? []).compactMap(\.alsText)
            return "Feinverlauf vom Leitstand gelesen\(geraet.map { " für \($0)" } ?? ""): \(reihen.prefix(3).joined(separator: ", "))\(reihen.count > 3 ? " …" : ""), \(a["stunden"]?.alsGanzzahl ?? 1) Std."
        case "protokolle":
            return switch a["art"]?.alsText {
            case "tage": "Tagesprotokoll gelesen"
            case "aenderungen": "Änderungsprotokoll gelesen"
            default: "Ladungsprotokoll gelesen"
            }
        case "befunde":
            return a["erledigte"] == true ? "Offene und erledigte Befunde gelesen" : "Offene Befunde gelesen"
        case "einstellungen_lesen":
            let gruppe = a["gruppe"]?.alsText.flatMap { $0.isEmpty ? nil : $0 }
            return "Einstellungen gelesen" + ([geraet, gruppe.map { "Gruppe \($0)" }].compactMap { $0 }.joined(separator: ", ").nichtLeer.map { " (\($0))" } ?? "")
        case "kennzahlen":
            return "Kennzahlen über \(a["tage"]?.alsGanzzahl ?? 7) Tage berechnet"
        case "wissen_suchen":
            return "Im Handbuch nachgeschlagen: \(a["anfrage"]?.alsText ?? "")"
        case "aenderung_vorschlagen", "aktion_vorschlagen":
            guard let ausgabe else { return "Vorschlag wird geprüft" }
            return vorschlagskennung(werkzeug, ausgabe) != nil ? "Vorschlag angelegt" : "Vorschlag abgelehnt, die KI berichtigt"
        default:
            return werkzeug
        }
    }

    /// Das Ergebnis in wenigen Worten; aufgeklappt zeigt die App es an.
    static func zusammenfassung(_ werkzeug: String, _ ausgabe: String) -> String {
        let json = try? JSONWert.lesen(Data(ausgabe.utf8))
        switch werkzeug {
        case "anlage_status":
            guard let json else { break }
            if let f = json["fehler"]?.alsText { return f }
            let verteiler = json["verteiler"]?.alsListe ?? []
            let heizung = json["heizungsgeraete"]?.alsListe ?? []
            let aus = (verteiler + heizung).filter { $0["erreichbar"] == false }.count
            var teile: [String] = []
            if !verteiler.isEmpty { teile.append("\(verteiler.count) Verteiler") }
            if !heizung.isEmpty { teile.append("\(heizung.count) \(heizung.count == 1 ? "Heizungsgerät" : "Heizungsgeräte")") }
            if json["rohdaten"] != nil { return "Rohdaten, \(ausgabe.utf8.count) Zeichen" }
            var satz = teile.isEmpty ? "keine Geräte eingebunden" : teile.joined(separator: ", ")
            if aus > 0 { satz += ", \(aus) nicht erreichbar" }
            return satz
        case "befunde":
            guard let json else { break }
            let offen = json["offen"]?.alsListe ?? []
            if offen.isEmpty { return "keine offenen Befunde" }
            return offen.prefix(3).compactMap { $0["titel"]?.alsText }.joined(separator: "; ") + (offen.count > 3 ? " und \(offen.count - 3) weitere" : "")
        case "aenderung_vorschlagen", "aktion_vorschlagen":
            guard let json else { break }
            if json["angelegt"] == true { return json["titel"]?.alsText ?? "angelegt" }
            return (json["fehler"]?.alsListe ?? []).compactMap(\.alsText).joined(separator: " ")
        default:
            break
        }
        let kurz = ausgabe.count > 600 ? String(ausgabe.prefix(600)) + " …" : ausgabe
        return kurz
    }
}

extension String {
    fileprivate var nichtLeer: String? { isEmpty ? nil : self }
}
