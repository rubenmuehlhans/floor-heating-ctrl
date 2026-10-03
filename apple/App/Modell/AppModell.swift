import SwiftUI
import Observation
import Anlage
import Assistent
import FoundationModels
import Geraeteschnittstelle
import Verlauf
import Sprachmodelle
import Diagnose
import UserNotifications
#if os(iOS)
import BackgroundTasks
#endif

/// Zustand der App. Ohne eingebundene Geräte zeigt sie die Beispielanlage, und die Handlungen
/// verändern nur deren Daten; mit Geräten kommen Bild und Befehle aus dem Anlagenbetrieb.
@MainActor
@Observable
final class AppModell {
    enum Bereich: Hashable {
        case uebersicht, raeume, assistent, heizung, geraete
    }

    enum KIModell: String, CaseIterable, Identifiable {
        case claude = "Claude Opus 5"
        case claudeSonnet = "Claude Sonnet 5"
        case privateCloud = "Apple Private Cloud Compute"
        case geraet = "Apple-Modell auf dem Gerät"

        var id: String { rawValue }

        var istClaude: Bool { claudeModell != nil }

        /// Kennung für die Messages-API
        var claudeModell: String? {
            switch self {
            case .claude: ClaudeKonfiguration.standardmodell
            case .claudeSonnet: ClaudeKonfiguration.sonnet
            case .privateCloud, .geraet: nil
            }
        }

        var erklaerung: String {
            switch self {
            case .claude: "Stärkste Auswertung. Braucht einen API-Schlüssel von Anthropic; Messwerte, Einstellungen und Raumnamen werden dorthin übertragen."
            case .claudeSonnet: "Kostet je Token etwa zwei Fünftel von Opus und genügt für die meisten Fragen und den Lagebericht. Braucht denselben API-Schlüssel; Messwerte, Einstellungen und Raumnamen werden an Anthropic übertragen."
            case .privateCloud: "Rechnet auf Servern von Apple, ohne dass Apple die Daten einsehen kann. Kein Schlüssel nötig, begrenztes Kontingent."
            case .geraet: "Rechnet vollständig auf diesem Gerät und funktioniert ohne Netz. Für kurze Auskünfte, nicht für längere Auswertungen."
            }
        }
    }

    enum Kanalbefehl: Equatable {
        case auf, zu, stopp, regelung
        /// Zwischenstellung 0…1 ohne Handbetrieb
        case stellung(Double)
    }

    enum Aufzeichnung: String {
        case aus = "aus"
        case scharf = "scharf"
        case laeuft = "läuft"
        case fertig = "fertig"
    }

    enum Verbindungsstand: Equatable {
        case ungeprueft
        case pruefe
        case verbunden
        case fehler(String)
    }

    static let schluesselkonto = "anthropic"

    // MARK: Zustand

    var bereich: Bereich = .uebersicht
    /// Das angezeigte Bild: die Beispielanlage, solange kein Gerät eingebunden ist.
    /// Im Betrieb mit den Befunden, die das Befundgedächtnis sichtbar macht: nach ihrer
    /// Mindestdauer und ohne kurzes Flattern.
    var anlage: Anlagenbild {
        get {
            if istBeispiel { return beispielbild }
            var bild = betrieb.bild ?? .leer
            bild.befunde = befundgedaechtnis.sichtbar.map(\.befund)
            return bild
        }
        set { if istBeispiel { beispielbild = newValue } }
    }
    private var beispielbild: Anlagenbild = .beispiel
    var istBeispiel: Bool { verzeichnis.geraete.isEmpty }
    /// Kurze Rückmeldung nach einem Befehl, etwa ein Fehler des Geräts
    private(set) var rueckmeldung: Rueckmeldung?
    /// Sollwerte, die das Stellrad schon zeigt, die aber noch nicht gesendet sind
    private(set) var ausstehendeSollwerte: [Raum.ID: Double] = [:]
    @ObservationIgnored private var sollwertAufgaben: [Raum.ID: Task<Void, Never>] = [:]
    var lagebericht: Lagebericht = .beispiel
    var gespraech: Gespraech = .beispiel
    /// Im Betrieb liegen Vorschläge und Änderungen in der Ablage; die Beispielanlage hat eigene.
    var vorschlaege: [Vorschlag] { istBeispiel ? beispielvorschlaege : ablage.vorschlaege }
    var aenderungen: [Aenderung] { istBeispiel ? beispielaenderungen : ablage.aenderungen }
    var beispielvorschlaege: [Vorschlag] = Vorschlag.beispiele
    var beispielaenderungen: [Aenderung] = Aenderung.beispiele
    /// Vorschläge, deren Übernahme oder Rücknahme gerade läuft
    var vorschlagLaeuft: Set<String> = []
    let ablage = Assistenzablage.standard()
    /// Das Transkript des Gesprächs, so wie es die Sitzung zuletzt vollständig hatte
    @ObservationIgnored var transkript: Transcript?
    /// Konfigurationen der Beispielanlage, entstehen beim ersten Zugriff
    var beispielkonfigurationen: [String: JSONWert] = [:]

    /// Seitenbereich des Assistenten auf iPad und Mac, Blatt auf dem iPhone.
    var zeigeAssistent = false
    /// Worauf sich die nächste Frage bezieht, z. B. ein Raum.
    var fragebezug: String?
    var zeigeEinrichtung = false
    var antwortLaeuft: Bool { beispielLaeuft || assistenzdienst.antwortLaeuft }
    var beispielLaeuft = false
    /// Bausteine der Antwort, die gerade entsteht
    var laufendeBausteine: [Beitrag.Baustein] { assistenzdienst.laufend }
    var lageberichtLaeuft: Bool { assistenzdienst.berichtLaeuft }
    var aufzeichnung: Aufzeichnung = .aus

    var kiModell: KIModell = KIModell(rawValue: UserDefaults.standard.string(forKey: "kiModell") ?? "") ?? .claude {
        didSet {
            guard kiModell != oldValue else { return }
            UserDefaults.standard.set(kiModell.rawValue, forKey: "kiModell")
            verbindung = .ungeprueft
            // Ein Transkript gehört zu einem Modell; mit dem Wechsel beginnt ein neues Gespräch.
            if !istBeispiel, !gespraech.beitraege.isEmpty { neuesGespraech() } else { assistenzdienst.zuruecksetzen() }
        }
    }
    /// Lagebericht der KI von selbst: bei geöffneter App höchstens alle sechs Stunden und nach
    /// neuen Befunden
    var lageberichtAutomatisch = AppModell.einstellung("lageberichtAutomatisch", true) {
        didSet { UserDefaults.standard.set(lageberichtAutomatisch, forKey: "lageberichtAutomatisch") }
    }
    /// Zustimmung, Daten der Anlage an Anthropic zu übertragen, mit Zeitpunkt. Ohne sie stellt die
    /// App keine Anfrage an Claude (App-Store-Richtlinie 5.1.2(i): ausdrückliche Erlaubnis vor der
    /// Weitergabe an eine fremde KI).
    var claudeZustimmung: Date? = UserDefaults.standard.object(forKey: "claudeZustimmung") as? Date {
        didSet { UserDefaults.standard.set(claudeZustimmung, forKey: "claudeZustimmung") }
    }
    @ObservationIgnored var vordergrund = true
    /// Die laufende Antwort; „Anhalten“ bricht sie ab.
    @ObservationIgnored var antwortAufgabe: Task<Void, Never>?
    @ObservationIgnored var letzterBerichtsversuch: Date?
    @ObservationIgnored var letzteWirkungskontrolle: Date?
    /// Letzte Zeichen des hinterlegten API-Schlüssels; der Schlüssel selbst liegt nur im
    /// Schlüsselbund.
    private(set) var schluesselEnde: String? = Schluesselbund.lesen(AppModell.schluesselkonto).map { String($0.suffix(4)) }
    var verbindung: Verbindungsstand = .ungeprueft
    let assistenzdienst = Assistenzdienst()

    /// Die eingebundenen Geräte; die Einrichtung füllt das Verzeichnis.
    let verzeichnis = Geraeteverzeichnis.standard()
    let sicherungen: Sicherungsablage? = Schluesselbund.sicherungsschluessel().map {
        Sicherungsablage(
            verzeichnis: URL.applicationSupportDirectory.appending(path: "Heizung/Sicherungen", directoryHint: .isDirectory),
            schluessel: $0)
    }
    let betrieb: Anlagenbetrieb
    /// Abfrage der Leitstände, getrennt von den Regelgeräten
    let leitstandbetrieb = Leitstandbetrieb()
    /// Übernahme des Verlaufs von der Karte der Leitstände
    let leitstandabgleich = Leitstandabgleich()
    /// Sucht Geräte und Leitstände, die der Router unter eine neue Adresse gelegt hat
    @ObservationIgnored private(set) lazy var adressnachfuehrung = Adressnachfuehrung(verzeichnis: verzeichnis)
    /// Der gespeicherte Verlauf aller Geräte. Lässt sich die Datei nicht öffnen, zeigt die App
    /// nur den 24-Stunden-Verlauf der Heizungsgeräte und zeichnet nichts auf.
    let verlaufsspeicher: Verlaufsspeicher? = try? Verlaufsspeicher.oeffnen(
        datei: URL.applicationSupportDirectory.appending(path: "Heizung/Verlauf/Verlauf.store"))
    let verlaufsaufzeichnung: Verlaufsaufzeichnung?
    /// Fortschritt eines laufenden Imports zwischen 0 und 1
    var importFortschritt: Double?
    @ObservationIgnored private var liveBegonnen = false
    @ObservationIgnored private var laufendeEinrichtung: Einrichtungsablauf?
    var mitteilungStoerungen = AppModell.einstellung("mitteilungStoerungen", true) {
        didSet { UserDefaults.standard.set(mitteilungStoerungen, forKey: "mitteilungStoerungen") }
    }
    var mitteilungWarnungen = AppModell.einstellung("mitteilungWarnungen", true) {
        didSet { UserDefaults.standard.set(mitteilungWarnungen, forKey: "mitteilungWarnungen") }
    }
    var mitteilungHinweise = AppModell.einstellung("mitteilungHinweise", false) {
        didSet { UserDefaults.standard.set(mitteilungHinweise, forKey: "mitteilungHinweise") }
    }
    /// Auf dem Mac: nach dem letzten Fenster in der Menüleiste weiterlaufen und aufzeichnen
    var aufzeichnenAufDemMac = AppModell.einstellung("aufzeichnenAufDemMac", true) {
        didSet {
            UserDefaults.standard.set(aufzeichnenAufDemMac, forKey: "aufzeichnenAufDemMac")
            #if os(macOS)
            aktivitaetAbgleichen()
            #endif
        }
    }
    #if os(macOS)
    /// Ohne sichtbares Fenster drosselt App Nap die Abfragen um Minuten. Solange aufgezeichnet
    /// wird, gilt die App deshalb als tätig; der Mac darf trotzdem schlafen.
    @ObservationIgnored private var aktivitaet: NSObjectProtocol?

    private func aktivitaetAbgleichen() {
        let noetig = aufzeichnenAufDemMac && !istBeispiel
        if noetig, aktivitaet == nil {
            aktivitaet = ProcessInfo.processInfo.beginActivity(
                options: [.userInitiatedAllowingIdleSystemSleep], reason: "Aufzeichnung der Heizungsanlage")
        } else if !noetig, let a = aktivitaet {
            ProcessInfo.processInfo.endActivity(a)
            aktivitaet = nil
        }
    }
    #endif

    static func einstellung(_ schluessel: String, _ vorgabe: Bool) -> Bool {
        UserDefaults.standard.object(forKey: schluessel) as? Bool ?? vorgabe
    }

    /// Seit wann Befunde anstehen, was gemeldet und was stumm geschaltet ist
    let befundgedaechtnis = Befundgedaechtnis.standard()
    let mitteilungen = Mitteilungsdienst()
    var mitteilungsstatus: UNAuthorizationStatus?
    /// Nach dem Antippen einer Mitteilung
    var geoeffneterBefund: String?

    init() {
        verlaufsaufzeichnung = verlaufsspeicher.map { Verlaufsaufzeichnung(speicher: $0) }
        betrieb = Anlagenbetrieb(sicherungen: sicherungen, verlauf: verlaufsaufzeichnung)
        // Die Abfragen beginnen mit der App, nicht mit dem ersten Fenster: Auf dem Mac läuft sie
        // auch ganz ohne Fenster in der Menüleiste.
        betriebAbgleichen()
    }

    // MARK: Befunde und Mitteilungen

    var meldeSchweren: Set<Befund.Schwere> {
        var schweren: Set<Befund.Schwere> = []
        if mitteilungStoerungen { schweren.insert(.stoerung) }
        if mitteilungWarnungen { schweren.insert(.warnung) }
        if mitteilungHinweise { schweren.insert(.hinweis) }
        return schweren
    }

    private func befundeAbgleichen(_ bild: Anlagenbild) {
        defer {
            lageberichtPruefen()
            wirkungskontrollenPruefen()
        }
        let ergebnis = befundgedaechtnis.abgleichen(bild.befunde, jetzt: .now, vollstaendig: betrieb.vollstaendigAbgefragt, melden: meldeSchweren)
        mitteilungen.senden(ergebnis.mitteilungen)
        mitteilungen.zuruecknehmen(ergebnis.erledigt)
    }

    /// Seit wann ein Befund ansteht; in der Beispielanlage unbekannt
    func befundSeit(_ id: String) -> Date? {
        istBeispiel ? nil : befundgedaechtnis.stand(id)?.seit
    }

    func befundIstStumm(_ id: String) -> Bool {
        befundgedaechtnis.istStumm(id)
    }

    func befundStumm(_ id: String, _ stumm: Bool) {
        befundgedaechtnis.stummSchalten(id, stumm)
    }

    func mitteilungsstatusLesen() async {
        mitteilungsstatus = await mitteilungen.erlaubnis()
    }

    func mitteilungenErlauben() async {
        _ = await mitteilungen.erlauben()
        await mitteilungsstatusLesen()
    }

    // MARK: Hintergrundabruf

    static let abrufkennung = "de.simplytech.heizung.abruf"

    /// Fragt alle Geräte einmal ab, zeichnet auf und gleicht die Befunde ab; im Hintergrund
    /// unter iOS, dort sind die laufenden Abfragen angehalten.
    func hintergrundabruf() async {
        #if os(iOS)
        hintergrundabrufPlanen()
        #endif
        guard !istBeispiel else { return }
        await betrieb.einmalAbfragen()
        await verlaufSichern()
    }

    #if os(iOS)
    /// Das System entscheidet, wann der Abruf tatsächlich läuft; frühestens nach 15 Minuten.
    func hintergrundabrufPlanen() {
        guard !istBeispiel else { return }
        let anfrage = BGAppRefreshTaskRequest(identifier: Self.abrufkennung)
        anfrage.earliestBeginDate = .now.addingTimeInterval(15 * 60)
        try? BGTaskScheduler.shared.submit(anfrage)
    }
    #endif

    // MARK: Verlauf

    /// Steigt mit jedem geschriebenen Platz; Diagramme laden daraufhin neu.
    var verlaufsrevision: Int { (verlaufsaufzeichnung?.revision ?? 0) + leitstandabgleich.revision }

    /// Der gespeicherte Verlauf geht vor. Die Beispielanlage füllt nur, solange ohne Geräte nichts
    /// gespeichert ist, etwa vor einem Import.
    func verlaufsauszug(_ zeitraum: Verlaufszeitraum, geraet: String? = nil, schluessel: Set<String>? = nil) async -> Verlaufsauszug {
        let bis = Date.now
        let von = bis.addingTimeInterval(-zeitraum.dauer)
        if let s = verlaufsspeicher,
           let a = try? await s.auszug(von: von, bis: bis, schritt: zeitraum.schritt, geraet: geraet, schluessel: schluessel),
           !a.leer || !istBeispiel {
            return a
        }
        guard istBeispiel else { return .leer(von: von, bis: bis) }
        var a = Verlaufsauszug.beispiel(anlage)
        a.reihen = a.reihen.filter { r in
            (geraet == nil || r.geraet == geraet) && (schluessel?.contains(r.schluessel) ?? true)
        }
        return a
    }

    /// Ereignisse aus dem Protokoll des Leitstands für denselben Zeitraum wie der Verlauf
    func ereignisse(_ zeitraum: Verlaufszeitraum, geraet: String? = nil, arten: Set<String>? = nil) async -> [Ereignis] {
        let bis = Date.now
        return (try? await verlaufsspeicher?.ereignisse(von: bis.addingTimeInterval(-zeitraum.dauer), bis: bis,
                                                        geraet: geraet, arten: arten)) ?? []
    }

    /// Stundenmittel der letzten `tage` für den Wärmepumpen-Check
    func stundenauszug(tage: Int) async -> Verlaufsauszug? {
        guard let speicher = verlaufsspeicher else { return nil }
        await verlaufSichern()
        let bis = Date.now
        return try? await speicher.auszug(von: bis.addingTimeInterval(-Double(tage) * 86_400), bis: bis, schritt: 3600)
    }

    /// Brennerstunden zwischen zwei Zeitpunkten und der Anteil der Zeit, für den der Verlauf
    /// den Brenner kennt
    func brennerstunden(von: Date, bis: Date) async -> (stunden: Double, abdeckung: Double) {
        guard let speicher = verlaufsspeicher, bis > von,
              let a = try? await speicher.auszug(von: von, bis: bis, schritt: 3600, schluessel: ["brenner"]) else { return (0, 0) }
        return Waermepumpencheck.brennerstunden(a)
    }

    /// Verlauf und Ereignisse der letzten `tage` als ZIP im temporären Ordner, zum Teilen
    func analysepaket(tage: Int, raster: TimeInterval) async throws -> URL {
        guard let speicher = verlaufsspeicher else { throw CocoaError(.fileNoSuchFile) }
        await verlaufSichern()
        let bis = Date.now
        let von = bis.addingTimeInterval(-Double(tage) * 86_400)
        let auszug = try await speicher.auszug(von: von, bis: bis, schritt: raster)
        let ereignisse = try await speicher.ereignisse(von: von, bis: bis)
        let bild = anlage
        let katalog = Bundle.main.url(forResource: "katalog-messgroessen", withExtension: "md")
            .flatMap { try? String(contentsOf: $0, encoding: .utf8) }
        let tag = bis.formatted(.iso8601.year().month().day())
        return try await Task.detached(priority: .userInitiated) {
            let inhalt = Analysepaket.inhalt(auszug: auszug, ereignisse: ereignisse, bild: bild, katalog: katalog, von: von, bis: bis)
            let tmp = FileManager.default.temporaryDirectory.appending(path: "Analysepaket-\(UUID().uuidString)")
            let ordner = tmp.appending(path: "Heizung Analysepaket \(tag)")
            try Analysepaket.schreiben(inhalt, nach: ordner)
            let zip = tmp.appending(path: "Heizung-Analysepaket-\(tag).zip")
            try Analysepaket.zip(ordner, nach: zip)
            try? FileManager.default.removeItem(at: ordner)
            return zip
        }.value
    }

    /// Schreibt angefangene Plätze, bevor die App in den Hintergrund geht.
    func verlaufSichern() async {
        await verlaufsaufzeichnung?.sichern()
    }

    func verlaufsumfang() async -> Verlaufsumfang? {
        try? await verlaufsspeicher?.umfang()
    }

    func ereignisumfang() async -> (tage: Int, ereignisse: Int, groesse: Int)? {
        try? await verlaufsspeicher?.ereignisumfang()
    }

    /// Liest einen Mitschnitt ein und meldet, was er enthielt.
    func mitschnittImportieren(_ url: URL) async {
        guard let speicher = verlaufsspeicher else {
            return melden("Der Verlauf lässt sich auf diesem Gerät nicht speichern.", fehler: true)
        }
        let zugriff = url.startAccessingSecurityScopedResource()
        defer { if zugriff { url.stopAccessingSecurityScopedResource() } }
        importFortschritt = 0
        defer { importFortschritt = nil }
        do {
            let e = try await Mitschnittimport.importieren(url, in: speicher) { [weak self] anteil in
                Task { @MainActor in self?.importFortschritt = anteil }
            }
            guard let von = e.von, let bis = e.bis, e.plaetze > 0 else {
                return melden("Die Datei enthält keine lesbaren Zustände. Erwartet wird je Zeile ein Zeitpunkt „epoch“ und der Zustand der Geräte.", fehler: true)
            }
            let geraete = e.geraete.values.filter { !$0.isEmpty }.sorted().joined(separator: ", ")
            melden("Mitschnitt vom \(Format.tagMitZeit(von)) bis \(Format.tagMitZeit(bis)) übernommen: \(e.geraete.count) Geräte (\(geraete)), \(e.abtastungen) Abtastungen\(e.unlesbar > 0 ? ", \(e.unlesbar) Zeilen unlesbar" : "").")
        } catch is CancellationError {
        } catch {
            melden("Der Mitschnitt ließ sich nicht lesen: \(error.localizedDescription)", fehler: true)
        }
    }

    func verlaufLoeschen() {
        guard let speicher = verlaufsspeicher else { return }
        ausfuehren("Gespeicherter Verlauf gelöscht.") { [weak self] in
            try await speicher.loeschen()
            // Der Verlauf des Leitstands wird danach neu übernommen.
            guard let self else { return }
            await leitstandabgleich.zuruecksetzen(verzeichnis.leitstaende, speicher: speicher)
        }
    }

    // MARK: Anlagenbetrieb

    /// Bringt die Abfragen mit dem Verzeichnis in Einklang; nach der Einrichtung und nach dem
    /// Entfernen eines Geräts.
    func betriebAbgleichen() {
        betrieb.gemeldet = { [weak self] id, ort, firmware in
            self?.verzeichnis.gemeldet(id, ort: ort, firmware: firmware)
        }
        leitstandbetrieb.gemeldet = { [weak self] id, ort, firmware in
            self?.verzeichnis.leitstandGemeldet(id, ort: ort, firmware: firmware)
        }
        betrieb.unerreichbar = { [weak self] _ in
            self?.adressenNachfuehren(.unerreichbar)
        }
        leitstandbetrieb.unerreichbar = { [weak self] _ in
            self?.adressenNachfuehren(.unerreichbar)
        }
        adressnachfuehrung.nachgefuehrt = { [weak self] _ in
            self?.betriebAbgleichen()
        }
        leitstandbetrieb.abgleichen(verzeichnis.leitstaende)
        leitstandabgleich.abgleichen(verzeichnis.leitstaende, speicher: verlaufsspeicher)
        betrieb.neuesBild = { [weak self] bild in
            self?.befundeAbgleichen(bild)
        }
        mitteilungen.geoeffnet = { [weak self] id in
            self?.geoeffneterBefund = id
        }
        // Mit dem ersten Gerät endet das Beispielgespräch; das letzte eigene Gespräch geht weiter.
        if !istBeispiel && !liveBegonnen {
            liveBegonnen = true
            if let letztes = ablage.gespraeche.first, let g = ablage.gespraechLaden(letztes.id) {
                gespraech = g.gespraech
                // Ein Transkript gehört zu dem Modell, das es geführt hat; mit einem anderen
                // Modell beginnt die Sitzung neu, der Verlauf bleibt nur zur Anzeige.
                let modellDesGespraechs = g.gespraech.beitraege.last { $0.rolle == .assistent }?.modell
                transkript = modellDesGespraechs == nil || modellDesGespraechs == kiModell.rawValue ? g.transkript : nil
            } else {
                gespraech = .neu()
                transkript = nil
            }
            assistenzdienst.zuruecksetzen()
        }
        betrieb.abgleichen(verzeichnis.geraete)
        #if os(macOS)
        aktivitaetAbgleichen()
        #endif
    }

    /// Der Lagebericht der KI, solange er zu den sichtbaren Befunden passt und nicht älter als
    /// einen Tag ist; sonst der Bericht aus den Befunden, bis die KI einen neuen liefert.
    var aktuellerLagebericht: Lagebericht {
        if istBeispiel { return lagebericht }
        let befunde = anlage.befundeNachSchwere
        if let ki = ablage.letzterLagebericht, ki.modell != KIModell.geraet.rawValue, Set(befunde.map(\.id)) == Set(ki.befunde),
           Date.now.timeIntervalSince(ki.erstellt) < 86_400 {
            return ki
        }
        return Lagebericht.ausBefunden(befunde, stand: anlage.stand)
    }

    /// Sucht kurz im Netz nach den bekannten Geräten und übernimmt neue Adressen; die Abfragen
    /// folgen ihnen über `betriebAbgleichen()`. Unter iOS nur im Vordergrund: Im Hintergrundabruf
    /// bleibt für eine Suche keine Zeit.
    func adressenNachfuehren(_ anlass: Adressnachfuehrung.Anlass) {
        #if os(iOS)
        guard vordergrund else { return }
        #endif
        adressnachfuehrung.anstossen(anlass)
    }

    func geraetEntfernen(_ id: String) {
        verzeichnis.entfernen(id)
        betriebAbgleichen()
    }

    // MARK: Leitstand

    func leitstandEntfernen(_ id: String) {
        verzeichnis.leitstandEntfernen(id)
        betriebAbgleichen()
    }

    /// Schlüssel eines Funkthermometers am Leitstand; `nil` entfernt ihn. Er liegt danach nur auf
    /// dem Leitstand.
    func leitstandSchluesselSetzen(_ id: String, mac: String, schluessel: String?) async throws {
        try await leitstandbetrieb.client(id).thermometerSchluessel(mac: mac, schluessel: schluessel)
        await leitstandbetrieb.jetztAbfragen(id)
    }

    /// Außenfühler des Leitstands; `nil` hebt die Zuordnung auf. Die Heizungsgeräte übernehmen
    /// den Wert beim nächsten Abruf, spätestens nach einer Minute.
    func leitstandAussenfuehlerSetzen(_ id: String, mac: String?) {
        ausfuehren(mac == nil ? "Zuordnung des Außenfühlers aufgehoben." : "Außenfühler zugeordnet.") { [weak self] in
            guard let self else { return }
            try await self.leitstandbetrieb.client(id).aussenfuehler(mac)
            await self.leitstandbetrieb.jetztAbfragen(id)
        }
    }

    func leitstandSeiteZeigen(_ id: String, _ seite: Leitstand.Seite) async throws {
        try await leitstandbetrieb.client(id).seiteZeigen(seite)
    }

    func leitstandBildschirm(_ id: String) async throws -> Data {
        try await leitstandbetrieb.client(id).bildschirm()
    }

    struct Rueckmeldung: Identifiable, Equatable {
        let id = UUID()
        let text: String
        let fehler: Bool
    }

    func melden(_ text: String, fehler: Bool = false) {
        let meldung = Rueckmeldung(text: text, fehler: fehler)
        rueckmeldung = meldung
        Task {
            try? await Task.sleep(for: .seconds(fehler ? 6 : 3))
            if rueckmeldung == meldung { rueckmeldung = nil }
        }
    }

    func rueckmeldungSchliessen() {
        rueckmeldung = nil
    }

    /// Führt einen Befehl an ein Gerät aus und meldet Fehler; `erfolg` erscheint nur, wenn es
    /// etwas zu sagen gibt.
    func ausfuehren(_ erfolg: String? = nil, _ aktion: @escaping @MainActor () async throws -> Void) {
        Task {
            do {
                try await aktion()
                if let erfolg { melden(erfolg) }
            } catch is CancellationError {
            } catch {
                melden(error.localizedDescription, fehler: true)
            }
        }
    }

    // MARK: Abfragen

    var menueleistenSymbol: String {
        switch aktuellerLagebericht.zustand {
        case .inOrdnung: "flame"
        case .beobachten: "flame.fill"
        case .handeln: "exclamationmark.triangle.fill"
        }
    }


    // MARK: Einstellungen

    struct Einstellungskontext {
        var art: Geraeteart
        /// Das Gerät, dessen Werte das Formular zeigt
        var geraet: String
        /// Weitere Geräte für anlagenweite Werte, etwa die Speichergrenzen
        var weitere: [String]
        var konfiguration: JSONWert
        /// Listeneintrag: Raumnummer, Kanal, Heizkreis oder ROM
        var eintrag: JSONWert?
        var titel: String
        var erklaerung: String?
        var parameter: [Parameter]
    }

    enum Einstellungsfehler: LocalizedError {
        case ungueltig([String])
        case keineKonfiguration

        var errorDescription: String? {
            switch self {
            case .ungueltig(let fehler): fehler.joined(separator: " ")
            case .keineKonfiguration: "Die Konfiguration des Geräts ist noch nicht gelesen."
            }
        }
    }

    var kesselID: String? { anlage.geraete.first { $0.art == .kessel }?.id }
    var speicherID: String? { anlage.geraete.first { $0.art == .speicher }?.id }
    var heizgeraetIDs: [String] { anlage.geraete.filter { $0.art == .kessel || $0.art == .speicher }.map(\.id) }

    /// Die Konfiguration eines Geräts, im Beispiel aus der Beispielanlage gebildet
    func konfiguration(_ geraet: String) -> JSONWert? {
        guard istBeispiel else { return betrieb.staende[geraet]?.konfiguration }
        if let k = beispielkonfigurationen[geraet] { return k }
        let bild = beispielbild
        if let etage = bild.etage(geraet) { return Beispielkonfiguration.verteiler(etage) }
        if let g = bild.geraet(geraet), g.art == .kessel || g.art == .speicher {
            return Beispielkonfiguration.heizung(bild, kessel: g.art == .kessel)
        }
        return nil
    }

    func geraeteart(_ geraet: String) -> Geraeteart? {
        switch anlage.geraet(geraet)?.art {
        case .verteiler: .verteiler
        case .kessel, .speicher: .heizung
        default: nil
        }
    }

    func einstellungskontext(_ z: Einstellungsziel) -> Einstellungskontext? {
        let geraet: String?
        var eintrag: JSONWert?
        var weitere: [String] = []
        var titel: String
        var erklaerung: String?
        switch z.gruppe {
        case "raum":
            geraet = z.geraet
            eintrag = z.eintrag.flatMap(Int.init).map { .zahl(Double($0)) }
            titel = z.eintrag.flatMap { n in anlage.etage(z.geraet ?? "")?.raeume.first { "\($0.nummer)" == n }?.name }.map { "Regelung \($0)" } ?? "Regelung"
            erklaerung = "Die Ventile stehen am Sollwert halb offen, ein Proportionalband darunter ganz offen und eines darüber ganz zu."
        case "kanal":
            geraet = z.geraet
            eintrag = z.eintrag.flatMap(Int.init).map { .zahl(Double($0)) }
            titel = "Kanal \(z.eintrag ?? "")"
            erklaerung = "Kalibrierte Werte stammen aus einer Messfahrt, alle anderen sind Vorgaben. Die Zuordnung der Kanäle zu Messgruppen ist verdrahtet."
        case "brenner":
            geraet = z.geraet ?? kesselID
            titel = "Brennererkennung"
            erklaerung = "Der Brenner gilt als angelaufen, wenn das Abgas die Bezugslinie um die Einschaltschwelle übersteigt, und als aus, wenn es um den Ausschlag unter den Höchstwert der Fahrt fällt. Die Bezugslinie ist das Minimum der letzten 24 Stunden."
        case "speicher":
            geraet = z.geraet ?? speicherID ?? kesselID
            weitere = heizgeraetIDs.filter { $0 != geraet }
            titel = "Pufferspeicher"
            erklaerung = "Der Ladezustand wird linear zwischen „leer“ und „voll“ geschätzt. Die App trägt diese Werte auf allen Heizungsgeräten gleich ein, weil jedes denselben Speicher schätzt."
        case "kkp":
            geraet = z.geraet ?? (istBeispiel ? kesselID : betrieb.kesselkreispumpengeraet() ?? kesselID)
            titel = "Kesselkreispumpe"
            erklaerung = "Die Pumpe läuft, solange der Kesselvorlauf wärmer ist als der Rücklauf aus dem Speicher; nur dann gibt der Kessel Wärme ab. Ohne gültige Messwerte oder über der Notgrenze läuft sie in jedem Fall."
        case "heizkreis":
            geraet = z.geraet ?? speicherID
            eintrag = z.eintrag.flatMap(Int.init).map { .zahl(Double($0)) }
            titel = z.eintrag.flatMap { n in anlage.heizkreise.first { "\($0.nummer)" == n }?.name } ?? "Heizkreis"
            erklaerung = "Geschaltet wird über MQTT, sobald ein Broker eingerichtet ist; sonst geht der Befehl unmittelbar an die Weboberfläche des Relais."
        case "fuehler":
            geraet = z.geraet
            eintrag = z.eintrag.map { .text($0) }
            titel = "Fühler"
        case "betrieb": geraet = z.geraet; titel = "Betrieb"
        case "tasten":
            geraet = z.geraet
            titel = "Tasten am Gerät"
            erklaerung = "Eine Taste gilt als berührt, sobald ihr Rohwert unter die Schwelle fällt. Die Rohwerte stehen unter „Sensoren“."
        case "zeit":
            geraet = z.geraet
            titel = geraeteart(z.geraet ?? "") == .verteiler ? "Zeit und Schutzfahrt" : "Zeit und Schutzlauf"
        case "netz":
            geraet = z.geraet
            titel = "Netzwerk"
            erklaerung = "Ändert sich WLAN-Name, Kennwort oder Gerätename, verbindet sich das Gerät kurz nach dem Speichern neu."
        case "mqtt":
            geraet = z.geraet
            titel = "MQTT"
            erklaerung = "Für Home Assistant über MQTT-Discovery. Änderungen wirken nach einem Neustart."
        case "geraet": geraet = z.geraet; titel = "Gerät"
        case "bus":
            geraet = z.geraet
            titel = "1-Wire-Bus"
            erklaerung = "Die beiden Heizungsgeräte sind unterschiedlich verdrahtet; die Belegung steht deshalb hier. Eine Änderung wirkt sofort."
        case "bedarf":
            geraet = z.geraet
            titel = "Bedarfsabfrage"
            erklaerung = "Wie oft die Verteiler nach Wärmebedarf gefragt werden und ab wann ein verstummter Verteiler als bedarfsmeldend gilt."
        default:
            return nil
        }
        guard let geraet, let art = geraeteart(geraet), let k = konfiguration(geraet) else { return nil }
        if z.gruppe == "fuehler", let rom = eintrag?.alsText,
           let rolle = k["probes"]?.alsListe?.first(where: { $0["rom"]?.alsText == rom })?["role"]?.alsText, !rolle.isEmpty {
            titel = Parameterkatalog.rollenname(rolle)
        }
        return Einstellungskontext(
            art: art, geraet: geraet, weitere: weitere.filter { konfiguration($0) != nil }, konfiguration: k, eintrag: eintrag,
            titel: titel, erklaerung: erklaerung, parameter: Parameterkatalog.gruppe(z.gruppe, art))
    }

    /// Prüft und schreibt geänderte Werte; liefert, wann sie wirken.
    @discardableResult
    func einstellungenSpeichern(_ kontext: Einstellungskontext, _ werte: [Parameter.ID: JSONWert]) async throws -> Set<Parameter.Wirkung> {
        let aenderungen = werte.compactMap { id, wert in
            Parameterkatalog.parameter(id).map { Wertaenderung($0, eintrag: kontext.eintrag, wert: wert) }
        }
        let wirkungen = Set(aenderungen.map(\.parameter.wirkung))
        for (i, geraet) in ([kontext.geraet] + kontext.weitere).enumerated() {
            let betroffen = i == 0 ? aenderungen : aenderungen.filter(\.parameter.anlagenweit)
            guard !betroffen.isEmpty else { continue }
            let k: JSONWert
            if istBeispiel {
                guard let vorhanden = konfiguration(geraet) else { throw Einstellungsfehler.keineKonfiguration }
                k = vorhanden
            } else {
                k = try await betrieb.frischeKonfiguration(geraet)
            }
            let teil = Schreibweg.teil(betroffen, konfiguration: k)
            let neu = Schreibweg.zusammengefuehrt(k, teil)
            let fehler = Schreibweg.pruefen(neu, kontext.art)
            if !fehler.isEmpty { throw Einstellungsfehler.ungueltig(fehler) }
            if istBeispiel {
                beispielkonfigurationen[geraet] = neu
                beispielbildNachfuehren(geraet, neu)
            } else {
                try await betrieb.konfigurationAendern(geraet: geraet, teil)
                let ort = betrieb.staende[geraet]?.ort ?? geraet
                ablage.protokollieren(betroffen.map { a in
                    let eintragsname = a.eintrag.flatMap { e in
                        a.parameter.ort.liste.flatMap { k[$0]?.alsListe?.first { $0[a.parameter.ort.schluessel] == e }?["name"]?.alsText }
                    }
                    return Aenderung(
                        id: UUID().uuidString, zeit: .now, geraet: ort,
                        parameter: eintragsname.map { "\(a.parameter.bezeichnung) · \($0)" } ?? a.parameter.bezeichnung,
                        bisher: Zielwerte.wert(a.parameter, eintrag: a.eintrag, in: k).map { Zielwerte.anzeige(a.parameter, $0) } ?? "–",
                        neu: Zielwerte.anzeige(a.parameter, a.wert), ausloeser: "In der App geändert",
                        geraetekennung: geraet, parameterkennung: a.parameter.id)
                })
            }
        }
        return wirkungen
    }

    /// Übernimmt die wichtigsten Werte einer geänderten Beispielkonfiguration ins Beispielbild.
    private func beispielbildNachfuehren(_ geraet: String, _ k: JSONWert) {
        if var etage = beispielbild.etage(geraet) {
            for i in etage.raeume.indices {
                guard let r = k["rooms"]?.alsListe?.first(where: { $0["id"]?.alsGanzzahl == etage.raeume[i].nummer }) else { continue }
                etage.raeume[i].soll = r["target_c"]?.alsZahl ?? etage.raeume[i].soll
                etage.raeume[i].regelung = Regelparameter(
                    proportionalband: r["p_band_k"]?.alsZahl ?? 1, pruefintervall: r["interval_s"]?.alsGanzzahl ?? 30,
                    raster: r["step"]?.alsZahl ?? 0.1, mindestaenderung: r["min_delta"]?.alsZahl ?? 0.01)
            }
            beispielbild.aendereEtage(geraet) { $0 = etage }
        }
        if let puffer = k["buffer"] {
            beispielbild.speicher?.voll = puffer["voll_c"]?.alsZahl ?? beispielbild.speicher?.voll
            beispielbild.speicher?.leer = puffer["leer_c"]?.alsZahl ?? beispielbild.speicher?.leer
            beispielbild.speicher?.warngrenze = puffer["warn_c"]?.alsZahl ?? beispielbild.speicher?.warngrenze
        }
        if let duese = k["burner"]?["duese_l_h"]?.alsZahl { beispielbild.kessel?.duese = duese }
    }

    /// Neustart nach Einstellungen, die erst dann wirken
    func neustart(_ geraet: String) {
        guard !istBeispiel else { return melden("Im Beispiel startet kein Gerät neu.") }
        ausfuehren("Das Gerät startet neu.") { try await self.betrieb.neustart(geraet: geraet) }
    }

    /// Sichert, setzt auf die Werksvorgabe und startet neu; das Gerät öffnet danach seinen
    /// Einrichtungs-Zugangspunkt.
    func werksvorgabe(_ geraet: String) {
        guard !istBeispiel else { return melden("Im Beispiel wird nichts zurückgesetzt.") }
        ausfuehren("Zurückgesetzt. Das Gerät startet neu und öffnet seinen Einrichtungs-Zugangspunkt.") {
            try await self.betrieb.werksvorgabe(geraet: geraet)
        }
    }

    // MARK: System

    func netzeSuchen(_ geraet: String) async -> [Netzsuche.Netz] {
        if istBeispiel {
            try? await Task.sleep(for: .seconds(1))
            return [.init(name: "Heimnetz", signal: -54, verschluesselt: true), .init(name: "Heimnetz-Gast", signal: -61, verschluesselt: true)]
        }
        do {
            return try await betrieb.netzeSuchen(geraet: geraet)
        } catch {
            melden(error.localizedDescription, fehler: true)
            return []
        }
    }

    func schutzfahrt(_ etage: String, _ befehl: Schutzfahrtbefehl) {
        guard !istBeispiel else { return melden("Im Beispiel fährt kein Ventil.") }
        ausfuehren {
            let n = try await self.betrieb.schutzfahrt(etage: etage, befehl)
            switch befehl {
            case .jetzt: self.melden(n == 0 ? "Alle Kreise sind seither gefahren; nichts zu tun." : "Schutzfahrt gestartet, \(n) \(n == 1 ? "Kreis" : "Kreise").")
            case .alle: self.melden("Schutzfahrt für \(n) Kreise gestartet.")
            case .abbrechen: self.melden("Schutzfahrt abgebrochen; angefangene Fahrten laufen zu Ende.")
            }
        }
    }

    func fuehlerNeuSuchen(_ geraet: String) {
        guard !istBeispiel else { return melden("Der Bus wird neu durchsucht.") }
        ausfuehren("Der Bus wird neu durchsucht.") { try await self.betrieb.fuehlerNeuSuchen(geraet: geraet) }
    }

    func firmwareEinspielen(_ geraet: String, _ datei: Firmwaredatei) {
        guard !istBeispiel else { return melden("Im Beispiel wird keine Firmware übertragen.") }
        ausfuehren("Übertragen. Das Gerät startet mit der neuen Fassung; schlägt der Start fehl, kehrt es zur alten zurück.") {
            try await self.betrieb.firmwareEinspielen(geraet: geraet, datei)
        }
    }

    // MARK: Sicherungen

    func sicherungseintraege(_ geraet: String) -> [Sicherungseintrag] {
        (try? sicherungen?.eintraege(geraet: geraet)) ?? []
    }

    func sichern(_ geraet: String) async {
        guard !istBeispiel else { return melden("Im Beispiel gibt es nichts zu sichern.") }
        do {
            try await betrieb.sichern(geraet)
            melden("Gesichert.")
        } catch {
            melden(error.localizedDescription, fehler: true)
        }
    }

    func sicherungLesen(_ eintrag: Sicherungseintrag) -> Data? {
        try? sicherungen?.lesen(eintrag)
    }

    func sicherungEinspielen(_ geraet: String, _ daten: Data) {
        guard !istBeispiel else { return melden("Im Beispiel wird nichts eingespielt.") }
        ausfuehren("Einstellungen zurückgespielt. Zeitzone und Neustartzeit wirken nach dem nächsten Neustart.") {
            try await self.betrieb.sicherungEinspielen(geraet: geraet, daten)
        }
    }

    // MARK: Konfiguration mit eigenem Schreibweg

    /// Schreibt eine Teilangabe; im Beispiel in die Beispielkonfiguration.
    func konfigurationSchreiben(_ geraet: String, _ baue: @escaping (JSONWert) throws -> JSONWert, erfolg: String? = nil) {
        guard let art = geraeteart(geraet) else { return }
        ausfuehren(erfolg) {
            let k = self.istBeispiel ? (self.konfiguration(geraet) ?? [:]) : try await self.betrieb.frischeKonfiguration(geraet)
            let teil = try baue(k)
            let neu = Schreibweg.zusammengefuehrt(k, teil)
            let fehler = Schreibweg.pruefen(neu, art)
            if !fehler.isEmpty { throw Einstellungsfehler.ungueltig(fehler) }
            if self.istBeispiel {
                self.beispielkonfigurationen[geraet] = neu
                self.beispielbildNachfuehren(geraet, neu)
            } else {
                try await self.betrieb.konfigurationAendern(geraet: geraet, teil)
            }
        }
    }

    /// Räume eines Verteilers vollständig: Name, Kanäle, Thermometer, alle Regelwerte.
    func raeumeSchreiben(_ etage: String, _ raeume: [JSONWert]) {
        konfigurationSchreiben(etage, { _ in ["rooms": .liste(raeume)] }, erfolg: "Räume gespeichert.")
    }

    func aussenfuehlerSetzen(_ etage: String, mac: String?) {
        konfigurationSchreiben(etage, { _ in ["outdoor_mac": mac.map { .text($0) } ?? .null] }, erfolg: "Außenfühler gespeichert.")
    }

    /// Schlüssel eines verschlüsselt sendenden Thermometers am Verteiler hinterlegen; `nil`
    /// entfernt ihn. Er liegt danach nur auf dem Verteiler und in dessen Sicherungen.
    func funkschluesselSetzen(_ etage: String, mac: String, schluessel: String?) async throws {
        guard !istBeispiel else { throw Einstellungsfehler.keineKonfiguration }
        try await betrieb.thermometerSchluessel(etage: etage, mac: mac, schluessel: schluessel)
    }

    /// Die versorgten Verteiler eines Heizkreises
    func versorgteVerteilerSetzen(_ kreis: Int, _ peers: [String]) {
        guard let geraet = istBeispiel ? speicherID : betrieb.heizkreisgeraet(kreis) else { return }
        konfigurationSchreiben(geraet, { k in
            ["circuits": .liste((k["circuits"]?.alsListe ?? []).compactMap { c in
                guard let id = c["id"] else { return nil }
                return id.alsGanzzahl == kreis ? ["id": id, "peers": .liste(peers.map { .text($0) })] : ["id": id]
            })]
        }, erfolg: "Versorgte Verteiler gespeichert.")
    }

    func heizkreisAnlegen() {
        guard let geraet = speicherID else { return }
        konfigurationSchreiben(geraet, { k in
            let alle = k["circuits"]?.alsListe ?? []
            let belegt = Set(alle.compactMap { $0["id"]?.alsGanzzahl })
            guard let frei = (1...4).first(where: { !belegt.contains($0) }) else {
                throw Einstellungsfehler.ungueltig(["Mehr als 4 Heizkreise sind nicht vorgesehen."])
            }
            return ["circuits": .liste(alle.compactMap { $0["id"].map { ["id": $0] } } + [["id": .zahl(Double(frei)), "name": .text("Heizkreis \(frei)")]])]
        }, erfolg: "Heizkreis angelegt.")
    }

    /// Die Firmware schaltet die Pumpe eines entfernten Kreises einmal ab.
    func heizkreisLoeschen(_ kreis: Int) {
        guard let geraet = istBeispiel ? speicherID : betrieb.heizkreisgeraet(kreis) else { return }
        konfigurationSchreiben(geraet, { k in
            ["circuits": .liste((k["circuits"]?.alsListe ?? []).compactMap { c in
                guard let id = c["id"], id.alsGanzzahl != kreis else { return nil }
                return ["id": id]
            })]
        }, erfolg: "Heizkreis gelöscht.")
    }

    /// Fühlerzuordnung: alle Fühler der Konfiguration gehen mit, damit keiner entfällt, auch
    /// wenn er gerade nicht am Bus hängt.
    func fuehlerZuordnen(_ geraet: String, rom: String, rolle: String, name: String?) {
        konfigurationSchreiben(geraet, { k in
            var liste: [JSONWert] = (k["probes"]?.alsListe ?? []).compactMap { p in p["rom"].map { ["rom": $0] } }
            if !liste.contains(where: { $0["rom"]?.alsText?.uppercased() == rom.uppercased() }) {
                liste.append(["rom": .text(rom)])
            }
            for i in liste.indices where liste[i]["rom"]?.alsText?.uppercased() == rom.uppercased() {
                liste[i]["role"] = .text(rolle)
                if let name { liste[i]["name"] = .text(name) }
            }
            return ["probes": .liste(liste)]
        }, erfolg: "Zuordnung gespeichert.")
    }

    /// Setzt den Bezugspunkt der Abgasauswertung neu: die folgenden Ladungen bilden den
    /// sauberen Kessel ab.
    func kesselGereinigt() {
        guard let geraet = kesselID else { return }
        let jetzt = Int(Date.now.timeIntervalSince1970)
        konfigurationSchreiben(geraet, { _ in ["burner": ["wartung_epoch": .zahl(Double(jetzt))]] },
                               erfolg: "Reinigung vermerkt; die folgenden Ladungen bilden den sauberen Zustand ab.")
    }

    // MARK: Relais

    enum Relaisfehler: LocalizedError {
        case keineAdresse, mitKennwort, beispiel

        var errorDescription: String? {
            switch self {
            case .keineAdresse: "Für dieses Relais ist keine Adresse eingetragen; es wird über MQTT geschaltet."
            case .mitKennwort: "Das Relais verlangt ein Kennwort. Prüfen Sie die Regel in seiner Weboberfläche."
            case .beispiel: "Im Beispiel gibt es kein Relais."
            }
        }
    }

    private func relaiszugang(_ kreis: Int) throws -> (Tasmota, Int) {
        guard !istBeispiel else { throw Relaisfehler.beispiel }
        guard let geraet = betrieb.heizkreisgeraet(kreis),
              let pumpe = konfiguration(geraet)?["circuits"]?.alsListe?.first(where: { $0["id"]?.alsGanzzahl == kreis })?["pump"],
              let adresse = Tasmota.adresse(pumpe["host"]?.alsText ?? "") else { throw Relaisfehler.keineAdresse }
        if pumpe["pass_set"]?.alsBool == true { throw Relaisfehler.mitKennwort }
        return (Tasmota(adresse: adresse, benutzer: pumpe["user"]?.alsText), pumpe["relay"]?.alsGanzzahl ?? 1)
    }

    func relaisPruefen(_ kreis: Int) async throws -> Tasmota.Pruefung {
        let (relais, kanal) = try relaiszugang(kreis)
        return try await relais.pruefen(relais: kanal)
    }

    func ausfallregelEinrichten(_ kreis: Int) async throws {
        let (relais, kanal) = try relaiszugang(kreis)
        try await relais.ausfallregelEinrichten(relais: kanal)
    }

    // MARK: Protokolle und Aufzeichnung

    /// Das Heizungsgerät, dessen Protokolle gelten: das mit dem Abgasfühler
    var protokollgeraetID: String? { kesselID ?? speicherID }

    func protokollHolen(tage: Bool) async -> String? {
        guard let geraet = protokollgeraetID, !istBeispiel else { return nil }
        do {
            return try await betrieb.protokollHolen(geraet: geraet, tage: tage)
        } catch {
            melden(error.localizedDescription, fehler: true)
            return nil
        }
    }

    func protokolleLoeschen() {
        guard let geraet = protokollgeraetID else { return }
        guard !istBeispiel else { return melden("Im Beispiel wird nichts gelöscht.") }
        ausfuehren("Ladungs- und Tagesprotokoll gelöscht.") { try await self.betrieb.protokolleLoeschen(geraet: geraet) }
    }

    /// Jedes Heizungsgerät zeichnet seine eigenen Fühler auf. Zuerst steht der Speicher: Seine
    /// Aufzeichnung ist die Grundlage für „voll“ und „leer“.
    var aufzeichnungsgeraete: [String] {
        var ids: [String] = []
        for id in [speicherID, kesselID].compactMap({ $0 }) where !ids.contains(id) { ids.append(id) }
        return ids
    }

    func aufzeichnungsstand(_ geraet: String?) -> Heizgeraetezustand.Aufzeichnungsstand? {
        geraet.flatMap { betrieb.staende[$0]?.heizgeraet?.aufzeichnung }
    }

    func aufzeichnung(_ befehl: Aufzeichnungsbefehl, geraet: String?) {
        guard let geraet else { return }
        if istBeispiel { return aufzeichnungWeiter() }
        ausfuehren { try await self.betrieb.aufzeichnung(geraet: geraet, befehl) }
    }

    func aufzeichnungHolen(geraet: String?) async -> String? {
        guard let geraet, !istBeispiel else { return nil }
        do {
            return try await betrieb.aufzeichnungHolen(geraet: geraet)
        } catch {
            melden(error.localizedDescription, fehler: true)
            return nil
        }
    }

    // MARK: Räume

    /// Für das Stellrad. Der Wert geht erst an den Verteiler, wenn das Rad eine Weile steht;
    /// bis dahin zeigt die App den gewählten Wert.
    func soll(_ raum: Raum.ID) -> Binding<Double> {
        Binding(
            get: { [weak self] in self?.ausstehendeSollwerte[raum] ?? self?.anlage.raum(raum)?.soll ?? 20 },
            set: { [weak self] neu in self?.sollSetzen(raum, auf: neu) }
        )
    }

    func sollSetzen(_ raum: Raum.ID, auf wert: Double) {
        let gerundet = min(35, max(5, (wert * 2).rounded() / 2))
        if istBeispiel {
            anlage.aendereRaum(raum) { r in
                r.soll = gerundet
                r.zielstellung = r.berechneZielstellung()
            }
            stellungenNachfuehren(raum)
            return
        }
        ausstehendeSollwerte[raum] = gerundet
        sollwertAufgaben[raum]?.cancel()
        sollwertAufgaben[raum] = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(900))
            guard !Task.isCancelled, let self else { return }
            do {
                try await self.betrieb.sollwert(raum: raum, gerundet)
            } catch is CancellationError {
                return
            } catch {
                self.melden(error.localizedDescription, fehler: true)
            }
            if self.ausstehendeSollwerte[raum] == gerundet { self.ausstehendeSollwerte[raum] = nil }
        }
    }

    func sollAendern(_ raum: Raum.ID, um schritt: Double) {
        let aktuell = ausstehendeSollwerte[raum] ?? anlage.raum(raum)?.soll ?? 20
        sollSetzen(raum, auf: aktuell + schritt)
    }

    func betriebsartUmschalten(_ raum: Raum.ID) {
        if istBeispiel {
            anlage.aendereRaum(raum) { r in
                r.betriebsart = r.betriebsart == .heizen ? .aus : .heizen
                r.zielstellung = r.berechneZielstellung()
            }
            stellungenNachfuehren(raum)
            return
        }
        let heizen = anlage.raum(raum)?.betriebsart != .heizen
        ausfuehren { try await self.betrieb.betriebsart(raum: raum, heizen: heizen) }
    }

    /// Regelung des Raums sofort auslösen, statt das Prüfintervall abzuwarten.
    func raumPruefen(_ raum: Raum.ID) {
        guard !istBeispiel else { return melden("Der Verteiler prüft den Raum jetzt.") }
        ausfuehren("Der Verteiler prüft den Raum jetzt.") { try await self.betrieb.regelungAusloesen(raum: raum) }
    }

    /// Im Klickmodell stehen die Ventile sofort in der Zielstellung.
    private func stellungenNachfuehren(_ raumID: Raum.ID) {
        guard let raum = anlage.raum(raumID),
              let etage = anlage.etagen.first(where: { $0.raeume.contains { $0.id == raumID } }) else { return }
        for nummer in raum.kanaele {
            anlage.aendereKanal(etage: etage.id, nummer: nummer) { k in
                if !k.handbetrieb { k.stellung = raum.zielstellung }
            }
        }
    }

    // MARK: Kanäle

    private func geraetebefehl(_ befehl: Kanalbefehl) -> Geraeteschnittstelle.Kanalbefehl {
        switch befehl {
        case .auf: .auf
        case .zu: .zu
        case .stopp: .anhalten
        case .regelung: .regeln
        case .stellung(let s): .stellung(s)
        }
    }

    func kanal(_ befehl: Kanalbefehl, etage: Etage.ID, nummer: Int) {
        if !istBeispiel {
            let b = geraetebefehl(befehl)
            ausfuehren { try await self.betrieb.kanal(etage: etage, nummer: nummer, b) }
            return
        }
        anlage.aendereKanal(etage: etage, nummer: nummer) { k in
            switch befehl {
            case .auf: k.stellung = 1; k.handbetrieb = true; k.bewegung = .steht
            case .zu: k.stellung = 0; k.handbetrieb = true; k.bewegung = .steht
            case .stopp: k.handbetrieb = true; k.bewegung = .steht
            case .regelung: k.handbetrieb = false
            case .stellung(let s): k.stellung = s
            }
        }
        if befehl == .regelung, let e = anlage.etage(etage),
           let raum = e.raeume.first(where: { $0.kanaele.contains(nummer) }) {
            stellungenNachfuehren(raum.id)
        }
    }

    func alleKanaele(_ befehl: Kanalbefehl, etage: Etage.ID) {
        if !istBeispiel {
            let b = geraetebefehl(befehl)
            ausfuehren("Befehl an alle Kanäle angenommen.") { try await self.betrieb.alleKanaele(etage: etage, b) }
            return
        }
        for nummer in 1...11 { kanal(befehl, etage: etage, nummer: nummer) }
    }

    func messfahrtUebernehmen(etage: Etage.ID) {
        if !istBeispiel {
            ausfuehren("Werte der Messfahrt übernommen.") { try await self.betrieb.messfahrtUebernehmen(etage: etage) }
            return
        }
        guard let m = anlage.etage(etage)?.messfahrt, let v = m.vorschlag else { return }
        anlage.aendereKanal(etage: etage, nummer: m.kanal) { k in
            k.kalibriert = true
            k.fahrzeitZu = v.fahrzeitZu
            k.fahrzeitAuf = v.fahrzeitAuf
            k.maximal = v.maximal
            k.schwelle = v.schwelle
            k.hysterese = v.hysterese
        }
        anlage.aendereEtage(etage) { $0.messfahrt = nil }
    }

    func messfahrtStarten(etage: Etage.ID, kanal: Int) {
        if !istBeispiel {
            ausfuehren("Messfahrt läuft.") { try await self.betrieb.messfahrtStarten(etage: etage, kanal: kanal) }
            return
        }
        guard let raum = anlage.etage(etage)?.kanaele.first(where: { $0.nummer == kanal })?.raum else { return }
        anlage.aendereEtage(etage) { e in
            var m = Messfahrt.beispiel
            m.kanal = kanal
            m.raum = raum
            e.messfahrt = m
        }
    }

    func messfahrtAbbrechen(etage: Etage.ID) {
        if !istBeispiel {
            ausfuehren { try await self.betrieb.messfahrtAbbrechen(etage: etage) }
            return
        }
        anlage.aendereEtage(etage) { $0.messfahrt = nil }
    }

    func messfahrtVerwerfen(etage: Etage.ID) {
        if !istBeispiel {
            ausfuehren { try await self.betrieb.messfahrtVerwerfen(etage: etage) }
            return
        }
        anlage.aendereEtage(etage) { $0.messfahrt = nil }
    }

    // MARK: Pumpen

    private func pumpenmodus(_ art: Pumpenbetriebsart) -> Pumpenmodus {
        switch art {
        case .automatik: .automatik
        case .ein: .ein
        case .aus: .aus
        }
    }

    func heizkreisBetriebsart(_ nummer: Int, _ art: Pumpenbetriebsart) {
        if !istBeispiel {
            let modus = pumpenmodus(art)
            ausfuehren { try await self.betrieb.heizkreis(nummer, modus) }
            return
        }
        guard let i = anlage.heizkreise.firstIndex(where: { $0.nummer == nummer }) else { return }
        anlage.heizkreise[i].betriebsart = art
        anlage.heizkreise[i].pumpeLaeuft = art == .ein || (art == .automatik && anlage.heizkreise[i].bedarf)
    }

    func kesselkreispumpeBetriebsart(_ art: Pumpenbetriebsart) {
        if !istBeispiel {
            let modus = pumpenmodus(art)
            ausfuehren { try await self.betrieb.kesselkreispumpe(modus) }
            return
        }
        anlage.kesselkreispumpe?.betriebsart = art
        anlage.kesselkreispumpe?.laeuft = art == .ein
    }

    // MARK: Einrichtung

    /// Ein Ablauf je geöffnetem Assistenten
    func einrichtung() -> Einrichtungsablauf {
        if let laufendeEinrichtung { return laufendeEinrichtung }
        let neu = Einrichtungsablauf(verzeichnis: verzeichnis, sicherungen: sicherungen)
        laufendeEinrichtung = neu
        return neu
    }

    func einrichtungBeendet() {
        laufendeEinrichtung?.beenden()
        laufendeEinrichtung = nil
        assistenzdienst.zuruecksetzen()
        betriebAbgleichen()
    }

    // MARK: Schlüssel

    func schluesselSpeichern(_ wert: String) {
        let schluessel = wert.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !schluessel.isEmpty, Schluesselbund.speichern(schluessel, konto: Self.schluesselkonto) else { return }
        schluesselEnde = String(schluessel.suffix(4))
        assistenzdienst.zuruecksetzen()
        verbindungPruefen()
    }

    func schluesselEntfernen() {
        Schluesselbund.loeschen(Self.schluesselkonto)
        schluesselEnde = nil
        verbindung = .ungeprueft
        assistenzdienst.zuruecksetzen()
    }

    func verbindungPruefen() {
        guard let schluessel = Schluesselbund.lesen(Self.schluesselkonto) else { return }
        verbindung = .pruefe
        Task {
            do {
                _ = try await assistenzdienst.pruefen(schluessel: schluessel, modell: kiModell.claudeModell ?? ClaudeKonfiguration.standardmodell)
                verbindung = .verbunden
            } catch {
                verbindung = .fehler(error.localizedDescription)
            }
        }
    }

    // MARK: Ladungsaufzeichnung

    func aufzeichnungWeiter() {
        aufzeichnung = switch aufzeichnung {
        case .aus: .scharf
        case .scharf: .laeuft
        case .laeuft: .fertig
        case .fertig: .aus
        }
    }
}
