import Foundation
import FoundationModels
import Anlage
import Assistent
import Geraeteschnittstelle

/// Gespräch, Lagebericht und Vorschläge. In der Beispielanlage antwortet ein vorgegebener Text,
/// und Vorschläge ändern nur die Beispieldaten; im Betrieb arbeitet das gewählte Modell mit den
/// Werkzeugen, und Vorschläge gehen über den Anlagenbetrieb an die Geräte.
extension AppModell {
    var anbindung: Anlagenanbindung { Anlagenanbindung(modell: self) }

    var kiSchluessel: String? { Schluesselbund.lesen(Self.schluesselkonto) }

    /// Warum das gewählte Modell gerade nicht antworten kann; `nil`, wenn es bereitsteht.
    var kiHindernis: String? {
        if let h = assistenzdienst.hindernis(kiModell, schluessel: kiSchluessel) { return h }
        if kiModell.istClaude, claudeZustimmung == nil {
            return "Für Claude braucht die App Ihre Zustimmung, Daten der Anlage an Anthropic zu übertragen."
        }
        return nil
    }

    // MARK: Zustimmung

    static let datenschutzseite = URL(string: "https://rubenmuehlhans.github.io/floor-heating-ctrl/datenschutz.html")!
    static let datenschutzAnthropic = URL(string: "https://www.anthropic.com/legal/privacy")!

    /// Schlüssel vorhanden, Zustimmung fehlt: Die Oberfläche fragt, statt nur zu melden.
    var kiBrauchtZustimmung: Bool { kiModell.istClaude && claudeZustimmung == nil && kiSchluessel != nil }

    func claudeZustimmen() {
        claudeZustimmung = .now
    }

    /// Danach geht nichts mehr an Anthropic, auch kein Lagebericht.
    func claudeZustimmungWiderrufen() {
        claudeZustimmung = nil
        antwortAufgabe?.cancel()
        assistenzdienst.zuruecksetzen()
    }

    // MARK: Gespräch

    func fragen(_ bezug: String) {
        fragebezug = bezug
        if bereich != .assistent { zeigeAssistent = true }
    }

    func senden(_ text: String) {
        let frage = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !frage.isEmpty, !antwortLaeuft else { return }
        let volltext = fragebezug.map { "Zu „\($0)“: \(frage)" } ?? frage
        gespraech.beitraege.append(Beitrag(id: UUID().uuidString, rolle: .nutzer, bausteine: [.text(volltext)], zeit: .now))
        if gespraech.titel == "Neues Gespräch" { gespraech.titel = Self.titel(frage) }
        fragebezug = nil

        if istBeispiel {
            beispielAntworten()
            return
        }
        if let hindernis = kiHindernis {
            gespraech.beitraege.append(Beitrag(id: UUID().uuidString, rolle: .assistent, bausteine: [.fehler(hindernis)], zeit: .now))
            return
        }
        let modell = kiModell
        antwortAufgabe = Task {
            let antwort = await assistenzdienst.frage(volltext, gespraech: gespraech.id, modell: modell, schluessel: kiSchluessel,
                                                      zugriff: anbindung, transkript: transkript)
            gespraech.beitraege.append(Beitrag(id: UUID().uuidString, rolle: .assistent, bausteine: antwort.bausteine, zeit: .now,
                                               modell: modell.rawValue))
            gespraech.zuletzt = .now
            if let t = antwort.transkript { transkript = t }
            if let v = antwort.verbrauch, v.input.totalTokenCount > 0 || v.output.totalTokenCount > 0 {
                ablage.verbrauchErfassen(modell: modell.rawValue, v)
            }
            ablage.gespraechSichern(gespraech, transkript: transkript)
        }
    }

    func antwortAnhalten() {
        antwortAufgabe?.cancel()
    }

    /// Der Anfang der ersten Frage, auf ganze Wörter gekürzt
    static func titel(_ frage: String) -> String {
        guard frage.count > 48 else { return frage }
        let kurz = frage.prefix(48)
        return (kurz.lastIndex(of: " ").map { String(kurz[..<$0]) } ?? String(kurz)) + " …"
    }

    private func beispielAntworten() {
        beispielLaeuft = true
        Task {
            try? await Task.sleep(for: .seconds(1.4))
            gespraech.beitraege.append(Beitrag(
                id: UUID().uuidString, rolle: .assistent,
                bausteine: [
                    .werkzeug(Werkzeugaufruf(id: UUID().uuidString, name: "anlage_status", beschreibung: "Zustand aller Geräte gelesen", ergebnis: "5 Geräte erreichbar, 3 Relais nicht")),
                    .text("In der Beispielanlage ist diese Antwort vorgegeben. Sobald Geräte eingebunden sind, beantwortet das gewählte Modell die Frage mit den Daten der Anlage und nennt, woher die Zahlen stammen."),
                ],
                zeit: .now))
            beispielLaeuft = false
        }
    }

    func neuesGespraech() {
        if !istBeispiel { ablage.gespraechSichern(gespraech, transkript: transkript) }
        gespraech = .neu()
        transkript = nil
        assistenzdienst.zuruecksetzen()
    }

    func gespraechOeffnen(_ id: String) {
        guard !antwortLaeuft, id != gespraech.id, let g = ablage.gespraechLaden(id) else { return }
        ablage.gespraechSichern(gespraech, transkript: transkript)
        gespraech = g.gespraech
        // Ein Transkript eines anderen Modells passt nicht zur Sitzung; dann beginnt der
        // Assistent mit dem Gesprächsverlauf nur als Anzeige neu.
        let modellDesGespraechs = g.gespraech.beitraege.last { $0.rolle == .assistent }?.modell
        transkript = modellDesGespraechs == nil || modellDesGespraechs == kiModell.rawValue ? g.transkript : nil
        assistenzdienst.zuruecksetzen()
    }

    func gespraechLoeschen(_ id: String) {
        ablage.gespraechLoeschen(id)
        if id == gespraech.id {
            gespraech = .neu()
            transkript = nil
            assistenzdienst.zuruecksetzen()
        }
    }

    // MARK: Lagebericht

    /// Den Lagebericht schreibt Claude oder Private Cloud Compute; das Modell auf dem Gerät ist
    /// für die Auswertung der ganzen Anlage zu klein und bleibt bei kurzen Auskünften.
    var lageberichtMoeglich: Bool { kiModell != .geraet }

    /// Erstellt den Lagebericht auf Wunsch.
    func lageberichtErstellen() {
        guard !istBeispiel, !assistenzdienst.berichtLaeuft else { return }
        guard lageberichtMoeglich else {
            melden("Den Lagebericht erstellt Claude oder Private Cloud Compute; das Apple-Modell auf dem Gerät ist dafür zu klein.", fehler: true)
            return
        }
        if let hindernis = kiHindernis {
            melden(hindernis, fehler: true)
            return
        }
        berichten()
    }

    /// Von selbst höchstens alle sechs Stunden, nach neuen Befunden frühestens nach einer halben
    /// Stunde, und nur, solange die App im Vordergrund ist und alle Geräte geantwortet haben.
    func lageberichtPruefen(jetzt: Date = .now) {
        guard lageberichtAutomatisch, lageberichtMoeglich, vordergrund, !istBeispiel, betrieb.vollstaendigAbgefragt,
              !assistenzdienst.berichtLaeuft else { return }
        if let v = letzterBerichtsversuch, jetzt.timeIntervalSince(v) < 600 { return }
        let offen = Set(anlage.befunde.map(\.id))
        if let letzter = ablage.letzterLagebericht {
            let alter = jetzt.timeIntervalSince(letzter.erstellt)
            let neueBefunde = !offen.isSubset(of: Set(letzter.befunde))
            guard alter >= 6 * 3600 || (neueBefunde && alter >= 1800) else { return }
        }
        // Erst jetzt der Schlüsselbund, und ohne bereites Modell erst in zehn Minuten wieder:
        // Die Prüfung läuft nach jeder Abfrage.
        guard kiHindernis == nil else {
            letzterBerichtsversuch = jetzt
            return
        }
        berichten()
    }

    private func berichten() {
        letzterBerichtsversuch = .now
        let modell = kiModell
        let befunde = anlage.befunde.map(\.id)
        Task {
            do {
                let (bericht, verbrauch) = try await assistenzdienst.lagebericht(
                    modell: modell, schluessel: kiSchluessel, zugriff: anbindung, befunde: befunde)
                ablage.lageberichtSichern(bericht)
                ablage.verbrauchErfassen(modell: modell.rawValue, verbrauch)
            } catch {
                melden("Der Lagebericht ließ sich nicht erstellen: \(Assistenzdienst.meldung(error, modell))", fehler: true)
            }
        }
    }

    // MARK: Vorschläge

    func vorschlag(_ id: String) -> Vorschlag? {
        vorschlaege.first { $0.id == id }
    }

    var offeneVorschlaege: [Vorschlag] {
        vorschlaege.filter { $0.status == .offen }
    }

    func uebernehmen(_ id: String) {
        if istBeispiel {
            guard let i = beispielvorschlaege.firstIndex(where: { $0.id == id }) else { return }
            beispielvorschlaege[i].status = .uebernommen(.now)
            wirke(beispielvorschlaege[i], rueckgaengig: false)
            protokolliere(beispielvorschlaege[i], rueckgaengig: false)
            return
        }
        guard let v = ablage.vorschlag(id), v.status == .offen, !vorschlagLaeuft.contains(id) else { return }
        vorschlagLaeuft.insert(id)
        Task {
            let neu = await Vorschlagsablauf.uebernehmen(v, mit: anbindung)
            ablage.vorschlagSichern(neu)
            vorschlagLaeuft.remove(id)
            switch neu.status {
            case .uebernommen where neu.aktion == nil: melden("Übernommen und am Gerät bestätigt.")
            case .gescheitert(let meldung): melden(meldung, fehler: true)
            default: break
            }
        }
    }

    func verwerfen(_ id: String) {
        if istBeispiel {
            guard let i = beispielvorschlaege.firstIndex(where: { $0.id == id }) else { return }
            beispielvorschlaege[i].status = .verworfen
            return
        }
        guard var v = ablage.vorschlag(id) else { return }
        v.status = .verworfen
        ablage.vorschlagSichern(v)
    }

    func zuruecknehmen(_ id: String) {
        if istBeispiel {
            guard let i = beispielvorschlaege.firstIndex(where: { $0.id == id }) else { return }
            beispielvorschlaege[i].status = .zurueckgenommen
            wirke(beispielvorschlaege[i], rueckgaengig: true)
            protokolliere(beispielvorschlaege[i], rueckgaengig: true)
            return
        }
        guard let v = ablage.vorschlag(id), !vorschlagLaeuft.contains(id) else { return }
        vorschlagLaeuft.insert(id)
        Task {
            let neu = await Vorschlagsablauf.zuruecknehmen(v, mit: anbindung)
            ablage.vorschlagSichern(neu)
            vorschlagLaeuft.remove(id)
            if neu.status == .zurueckgenommen {
                melden("Der bisherige Wert gilt wieder.")
            } else if let meldung = neu.ergebnis, neu.status != v.status || meldung != v.ergebnis {
                melden(meldung, fehler: true)
            }
        }
    }

    /// Trägt die Kennzahlen nach dem Kontrollzeitraum ein; höchstens alle halbe Stunde geprüft.
    func wirkungskontrollenPruefen(jetzt: Date = .now) {
        guard !istBeispiel else { return }
        if let l = letzteWirkungskontrolle, jetzt.timeIntervalSince(l) < 1800 { return }
        letzteWirkungskontrolle = jetzt
        let faellig = ablage.vorschlaege.filter { v in
            guard case .uebernommen = v.status, let k = v.kontrolle else { return false }
            return k.ausgewertet == nil && jetzt >= k.faellig
        }
        guard !faellig.isEmpty else { return }
        Task {
            for v in faellig {
                ablage.vorschlagSichern(await Vorschlagsablauf.wirkungPruefen(v, mit: anbindung, jetzt: jetzt))
            }
        }
    }

    // MARK: Beispielanlage

    private func wirke(_ v: Vorschlag, rueckgaengig: Bool) {
        switch v.id {
        case "voll_68":
            anlage.speicher?.voll = rueckgaengig ? 62 : 68
            for id in heizgeraetIDs {
                var k = konfiguration(id) ?? [:]
                k["buffer"]?["voll_c"] = rueckgaengig ? 62 : 68
                beispielkonfigurationen[id] = k
            }
            anlage.befunde.removeAll { $0.id == "voll_zu_niedrig" && !rueckgaengig }
            if rueckgaengig, !anlage.befunde.contains(where: { $0.id == "voll_zu_niedrig" }),
               let alt = Anlagenbild.beispiel.befunde.first(where: { $0.id == "voll_zu_niedrig" }) {
                anlage.befunde.append(alt)
            }
        default:
            break
        }
    }

    private func protokolliere(_ v: Vorschlag, rueckgaengig: Bool) {
        for geraet in v.geraete {
            beispielaenderungen.insert(Aenderung(
                id: UUID().uuidString, zeit: .now, geraet: geraet, parameter: v.titel,
                bisher: rueckgaengig ? v.neu : v.bisher, neu: rueckgaengig ? v.bisher : v.neu,
                ausloeser: rueckgaengig ? "Rücknahme" : "Vorschlag der KI, bestätigt"
            ), at: 0)
        }
    }
}
