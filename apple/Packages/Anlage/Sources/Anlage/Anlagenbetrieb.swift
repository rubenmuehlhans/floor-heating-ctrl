import Foundation
import Observation
import Geraeteschnittstelle
import Verlauf

/// Fragt die eingebundenen Geräte ab, hält ihre Stände und das daraus zusammengeführte Bild,
/// und führt Befehle aus.
///
/// Der Takt folgt der Weboberfläche: Verteiler jede Sekunde, solange sich etwas bewegt, sonst alle
/// sechs Sekunden mit ETag und jede Minute ohne, weil einige Werte den Änderungszähler nicht
/// erhöhen; Heizungsgeräte alle fünf Sekunden. Jeder Befehl wird zurückgelesen, denn die Firmware
/// quittiert manche Befehle, ohne sie auszuführen – etwa einen Sollwert außerhalb 5–35 °C oder
/// einen Befehl an einen Kanal in der Messfahrt.
@MainActor
@Observable
public final class Anlagenbetrieb {
    public private(set) var staende: [String: Geraetestand] = [:]
    public private(set) var bild: Anlagenbild?

    @ObservationIgnored private var abfragen: [String: Task<Void, Never>] = [:]
    @ObservationIgnored private var wecker: [String: Wecker] = [:]
    @ObservationIgnored private let sitzung: URLSession
    @ObservationIgnored private let sicherungen: Sicherungsablage?
    @ObservationIgnored private let takt: Takt
    @ObservationIgnored private let verlauf: Verlaufsaufzeichnung?
    /// Meldet ein Gerät einen anderen Ort oder eine andere Firmware, als das Verzeichnis kennt.
    @ObservationIgnored public var gemeldet: (@MainActor (_ id: String, _ ort: String?, _ firmware: String?) -> Void)?
    /// Nach jeder weiteren fehlgeschlagenen Abfrage eines Geräts, das als nicht erreichbar gilt;
    /// vielleicht hat es eine neue Adresse bekommen.
    @ObservationIgnored public var unerreichbar: (@MainActor (_ id: String) -> Void)?
    /// Nach jedem neuen Bild, etwa für das Befundgedächtnis
    @ObservationIgnored public var neuesBild: (@MainActor (Anlagenbild) -> Void)?
    /// Letzte Sicherung je Gerät; nur die Zeitpunkte, die Sicherungen selbst liegen in der Ablage.
    @ObservationIgnored private var letzteSicherungen: [String: Date] = [:]
    /// Ein Stand hat sich seit dem letzten Bild geändert.
    @ObservationIgnored private var bildVeraltet = false
    @ObservationIgnored private var bildGeplant: Task<Void, Never>?
    /// Der umgerechnete Verlauf der Heizungsgeräte mit der Kennung der Antworten, aus denen er stammt
    @ObservationIgnored private var kurzverlauf: (kennung: String, wert: Kurzverlauf)?
    /// Der letzte Kontakt allein zählt erst nach dieser Zeit als Änderung. Er ändert sich mit jeder
    /// Abfrage und würde sonst Bild und Ansichten jede Sekunde neu aufbauen.
    static let kontaktraster: TimeInterval = 30
    @ObservationIgnored private var sicherungVersucht: [String: Date] = [:]

    /// Abstände der Abfragen; Tests verkürzen sie.
    public struct Takt: Sendable {
        public var beschaeftigt: Duration = .seconds(1)
        public var verteiler: Duration = .seconds(6)
        public var heizung: Duration = .seconds(5)
        public var ohneETag: Duration = .seconds(60)
        public var konfiguration: Duration = .seconds(300)
        public var thermometer: Duration = .seconds(30)
        /// Der Geräteverlauf füllt nur Lücken der eigenen Aufzeichnung; die erste Abfrage nach
        /// dem Start holt die letzten 24 Stunden.
        public var verlauf: Duration = .seconds(1800)
        public var protokolle: Duration = .seconds(600)
        /// Die Weboberfläche fragt die Nachbarn jede Minute ab.
        public var nachbarn: Duration = .seconds(60)
        /// Zwischen zwei Versuchen, einen Befehl zurückzulesen
        public var ruecklesen: Duration = .milliseconds(600)
        /// Eine Sicherung gilt so lange als frisch; danach sichert die App vor dem Schreiben neu.
        public var sicherungFrisch: Duration = .seconds(600)
        /// Abstand der selbsttätigen Sicherung; `nil` schaltet sie ab.
        public var sicherungAbstand: Duration? = .seconds(7 * 86_400)

        public init() {}
    }

    public init(sitzung: URLSession = .geraete, sicherungen: Sicherungsablage? = nil,
                verlauf: Verlaufsaufzeichnung? = nil, takt: Takt = Takt()) {
        self.sitzung = sitzung
        self.sicherungen = sicherungen
        self.verlauf = verlauf
        self.takt = takt
        sicherungsstandLesen()
    }

    /// Alle Geräte haben mindestens einmal geantwortet oder gelten als nicht erreichbar. Vorher
    /// fehlt dem Bild noch etwas, und ein fehlender Befund heißt nicht, dass er erledigt ist.
    public var vollstaendigAbgefragt: Bool {
        !staende.isEmpty && staende.values.allSatisfy { s in
            s.verteiler != nil || s.heizgeraet != nil || (!s.erreichbar && s.fehler != nil)
        }
    }

    private func sicherungsstandLesen() {
        guard let sicherungen, let eintraege = try? sicherungen.eintraege() else { return }
        letzteSicherungen = [:]
        for e in eintraege where (letzteSicherungen[e.geraet] ?? .distantPast) < e.datum {
            letzteSicherungen[e.geraet] = e.datum
        }
    }

    /// Einmal in der Woche sichern, sobald das Gerät erreichbar ist; ein Fehlschlag wird nach
    /// einer Stunde wiederholt. Sicherungen lesen nur, sie ändern am Gerät nichts.
    private func sicherungFaellig(_ id: String) async {
        guard sicherungen != nil, let abstand = takt.sicherungAbstand else { return }
        let jetzt = Date.now
        if let letzte = letzteSicherungen[id], jetzt.timeIntervalSince(letzte) < Double(abstand.components.seconds) { return }
        if let versucht = sicherungVersucht[id], jetzt.timeIntervalSince(versucht) < 3600 { return }
        sicherungVersucht[id] = jetzt
        _ = try? await sichern(id)
    }

    // MARK: - Abfrage

    /// Startet und beendet Abfragen, bis sie zu den Geräten des Verzeichnisses passen.
    public func abgleichen(_ geraete: [BekanntesGeraet]) {
        let ids = Set(geraete.map(\.id))
        for (id, aufgabe) in abfragen where !ids.contains(id) {
            aufgabe.cancel()
            abfragen[id] = nil
            wecker[id] = nil
            staende[id] = nil
        }
        for g in geraete {
            if let alt = staende[g.id], alt.geraet.adresse != g.adresse {
                abfragen[g.id]?.cancel()
                abfragen[g.id] = nil
            }
            staende[g.id, default: Geraetestand(geraet: g)].geraet = g
            guard abfragen[g.id] == nil else { continue }
            let w = Wecker()
            wecker[g.id] = w
            abfragen[g.id] = Task { [weak self] in
                switch g.art {
                case .verteiler: await self?.verteilerAbfragen(g, wecker: w)
                case .heizung: await self?.heizgeraetAbfragen(g, wecker: w)
                }
            }
        }
        bildNeu()
    }

    public func beenden() {
        abfragen.values.forEach { $0.cancel() }
        abfragen.removeAll()
        wecker.removeAll()
    }

    /// Fragt ein Gerät außer der Reihe ab, etwa nach einem Befehl.
    public func jetztAbfragen(_ id: String) {
        wecker[id]?.wecken()
    }

    /// Ändert einen Stand und veröffentlicht ihn nur, wenn sich etwas geändert hat; sonst bauten
    /// alle Ansichten nach jeder unveränderten Antwort neu auf.
    private func aendern(_ id: String, _ aenderung: (inout Geraetestand) -> Void) {
        guard let alt = staende[id] else { return }
        var neu = alt
        aenderung(&neu)
        if let a = alt.letzterKontakt, let n = neu.letzterKontakt, n > a, n.timeIntervalSince(a) < Self.kontaktraster {
            var ohneKontakt = neu
            ohneKontakt.letzterKontakt = a
            if ohneKontakt == alt { return }
        } else if neu == alt {
            return
        }
        staende[id] = neu
        bildVeraltet = true
    }

    /// Gleicht Ort und Firmware aus dem Zustand mit dem Verzeichniseintrag ab.
    private func kennungPruefen(_ id: String) {
        guard let s = staende[id] else { return }
        let ort = (s.verteiler?.geraet?.ort ?? s.heizgeraet?.geraet?.ort).flatMap { $0.isEmpty ? nil : $0 }
        let firmware = (s.verteiler?.version ?? s.heizgeraet?.version).flatMap { $0.isEmpty ? nil : $0 }
        guard (ort != nil && ort != s.geraet.ort) || (firmware != nil && firmware != s.geraet.firmware) else { return }
        aendern(id) { st in
            if let ort { st.geraet.ort = ort }
            if let firmware { st.geraet.firmware = firmware }
        }
        gemeldet?(id, ort, firmware)
    }

    private func bildNeu() {
        bildVeraltet = false
        bildGeplant?.cancel()
        bildGeplant = nil
        guard !staende.isEmpty else {
            bild = nil
            return
        }
        let alle = Array(staende.values)
        let kennung = Zusammenfuehrung.verlaufskennung(alle)
        if kurzverlauf?.kennung != kennung {
            kurzverlauf = (kennung, Zusammenfuehrung.verlauf(alle, jetzt: .now))
        }
        let neu = Zusammenfuehrung.bild(alle, sicherungen: sicherungen == nil ? nil : letzteSicherungen, verlauf: kurzverlauf?.wert)
        bild = neu
        neuesBild?(neu)
    }

    /// Nach einer Abfrage: ein neues Bild nur, wenn sich ein Stand geändert hat, und höchstens
    /// eines je Viertelsekunde, auch wenn mehrere Geräte kurz nacheinander antworten.
    private func bildNachAbfrage() {
        guard bildVeraltet, bildGeplant == nil else { return }
        bildGeplant = Task { @MainActor [weak self] in
            try? await Task.sleep(for: .milliseconds(250))
            guard let self, !Task.isCancelled else { return }
            self.bildGeplant = nil
            if self.bildVeraltet { self.bildNeu() }
        }
    }

    /// Fragt jedes Gerät einmal ab, etwa im Hintergrundabruf, und zeichnet die Werte auf. Die
    /// laufenden Abfragen bleiben davon unberührt.
    public func einmalAbfragen() async {
        let aufgaben = staende.keys.map { id in
            Task { await self.einmalAbfragen(id) }
        }
        for a in aufgaben { await a.value }
        bildNeu()
    }

    private func einmalAbfragen(_ id: String) async {
        do {
            try await zustandLesen(id)
            aendern(id) { s in
                s.erreichbar = true
                s.letzterKontakt = .now
                s.fehler = nil
            }
            if let z = staende[id]?.verteiler {
                verlauf?.abtasten(.verteiler(z, geraet: id, zeit: .now))
            } else if let z = staende[id]?.heizgeraet {
                verlauf?.abtasten(.heizgeraet(z, geraet: id, zeit: .now))
            }
        } catch {
            // Im Hintergrund gibt es keinen zweiten Versuch; ein Fehlschlag zählt.
            ausgefallen(id, error, 2)
        }
    }

    private func verteilerAbfragen(_ g: BekanntesGeraet, wecker: Wecker) async {
        let client = Verteiler(adresse: g.adresse, sitzung: sitzung)
        let uhr = ContinuousClock()
        var vollAbgefragt: ContinuousClock.Instant?
        var konfigGelesen: ContinuousClock.Instant?
        var thermometerGelesen: ContinuousClock.Instant?
        var fehlerInFolge = 0
        while !Task.isCancelled {
            do {
                let jetzt = uhr.now
                let voll = vollAbgefragt.map { jetzt - $0 >= takt.ohneETag } ?? true
                if case .neu(let z, let roh) = try await client.zustand(etagNutzen: !voll) {
                    aendern(g.id) { s in
                        s.verteiler = z
                        s.rohzustand = roh
                    }
                }
                if voll { vollAbgefragt = jetzt }
                // Nebenabfragen scheitern einzeln; erreichbar bleibt das Gerät, solange der
                // Zustand kommt.
                if konfigGelesen.map({ jetzt - $0 >= takt.konfiguration }) ?? true {
                    if let k = try? await client.konfiguration() {
                        aendern(g.id) { $0.konfiguration = k }
                        konfigGelesen = jetzt
                    }
                }
                if thermometerGelesen.map({ jetzt - $0 >= takt.thermometer }) ?? true {
                    if let t = try? await client.thermometerliste() {
                        aendern(g.id) {
                            $0.thermometer = t.geraete ?? []
                            $0.thermometerSchluessel = t.schluessel ?? []
                        }
                    }
                    thermometerGelesen = jetzt
                }
                try? await messreiheNachladen(g.id, client)
                aendern(g.id) { s in
                    s.erreichbar = true
                    s.letzterKontakt = .now
                    s.fehler = nil
                }
                kennungPruefen(g.id)
                await sicherungFaellig(g.id)
                // Auch nach 304 zählt der letzte Stand als Abtastung: Er gilt unverändert weiter.
                if let z = staende[g.id]?.verteiler {
                    verlauf?.abtasten(.verteiler(z, geraet: g.id, zeit: .now))
                }
                fehlerInFolge = 0
            } catch is CancellationError {
                return
            } catch {
                fehlerInFolge += 1
                ausgefallen(g.id, error, fehlerInFolge)
            }
            bildNachAbfrage()
            let z = staende[g.id]?.verteiler
            await wecker.warten(Self.beschaeftigt(z) ? takt.beschaeftigt : takt.verteiler)
        }
    }

    /// Solange sich ein Ventil bewegt, eine Messfahrt oder Schutzfahrt läuft, fragt die
    /// Weboberfläche jede Sekunde ab.
    nonisolated static func beschaeftigt(_ z: Verteilerzustand?) -> Bool {
        guard let z else { return false }
        if z.messfahrt?.zustand == "running" || z.schutzfahrt?.laeuft == true { return true }
        return (z.kanaele ?? []).contains { ($0.vorgang ?? "idle") != "idle" || $0.befehlWartet == true || $0.belegt == true }
    }

    /// Lädt neue Messpunkte der Messfahrt seitenweise nach; im Ruhezustand verwirft er sie.
    private func messreiheNachladen(_ id: String, _ client: Verteiler) async throws {
        guard let c = staende[id]?.verteiler?.messfahrt else { return }
        if (c.zustand ?? "idle") == "idle" {
            if !(staende[id]?.messreihe.isEmpty ?? true) { aendern(id) { $0.messreihe = [] } }
            return
        }
        var vorhanden = staende[id]?.messreihe ?? []
        // Eine neue Fahrt beginnt wieder bei null.
        if let anzahl = c.messpunkte, anzahl < vorhanden.count { vorhanden = [] }
        var runden = 0
        while vorhanden.count < (c.messpunkte ?? 0), runden < 12 {
            let seite = try await client.messreihe(ab: vorhanden.count)
            let werte = seite.werteMv ?? []
            if werte.isEmpty { break }
            vorhanden += werte
            runden += 1
        }
        aendern(id) { $0.messreihe = vorhanden }
    }

    private func heizgeraetAbfragen(_ g: BekanntesGeraet, wecker: Wecker) async {
        let client = Heizgeraet(adresse: g.adresse, sitzung: sitzung)
        let uhr = ContinuousClock()
        var konfigGelesen: ContinuousClock.Instant?
        var verlaufGelesen: ContinuousClock.Instant?
        var protokolleGelesen: ContinuousClock.Instant?
        var nachbarnGelesen: ContinuousClock.Instant?
        var protokollstand: Heizgeraetezustand.Protokollstand?
        var fehlerInFolge = 0
        while !Task.isCancelled {
            do {
                let jetzt = uhr.now
                if case .neu(let z, let roh) = try await client.zustand() {
                    aendern(g.id) { s in
                        s.heizgeraet = z
                        s.rohzustand = roh
                    }
                }
                // Nebenabfragen scheitern einzeln; erreichbar bleibt das Gerät, solange der
                // Zustand kommt. Ein Fehlschlag wird beim nächsten Durchlauf wiederholt.
                if konfigGelesen.map({ jetzt - $0 >= takt.konfiguration }) ?? true {
                    if let k = try? await client.konfiguration() {
                        aendern(g.id) { $0.konfiguration = k }
                        konfigGelesen = jetzt
                    }
                }
                if verlaufGelesen.map({ jetzt - $0 >= takt.verlauf }) ?? true {
                    // Das volle Raster zu zwei Minuten, 24 Stunden
                    if let v = try? await client.verlauf(schritt: 1, hoechstens: 720) {
                        aendern(g.id) { $0.verlauf = v }
                        verlauf?.verlaufUebernehmen(geraet: g.id, zustand: staende[g.id]?.heizgeraet, antwort: v)
                    }
                    verlaufGelesen = jetzt
                }
                let stand = staende[g.id]?.heizgeraet?.protokolle
                if stand != protokollstand || protokolleGelesen.map({ jetzt - $0 >= takt.protokolle }) ?? true {
                    if let ladungen = try? await client.ladungsprotokoll(), let tage = try? await client.tagesprotokoll() {
                        aendern(g.id) { s in
                            s.ladungen = ladungen
                            s.tage = tage
                        }
                    }
                    protokollstand = stand
                    protokolleGelesen = jetzt
                }
                if nachbarnGelesen.map({ jetzt - $0 >= takt.nachbarn }) ?? true {
                    if let n = try? await client.nachbarn() {
                        aendern(g.id) { $0.nachbarn = n }
                    }
                    nachbarnGelesen = jetzt
                }
                aendern(g.id) { s in
                    s.erreichbar = true
                    s.letzterKontakt = .now
                    s.fehler = nil
                }
                kennungPruefen(g.id)
                await sicherungFaellig(g.id)
                if let z = staende[g.id]?.heizgeraet {
                    verlauf?.abtasten(.heizgeraet(z, geraet: g.id, zeit: .now))
                }
                fehlerInFolge = 0
            } catch is CancellationError {
                return
            } catch {
                fehlerInFolge += 1
                ausgefallen(g.id, error, fehlerInFolge)
            }
            bildNachAbfrage()
            await wecker.warten(takt.heizung)
        }
    }

    /// Eine einzelne verlorene Antwort im WLAN ist normal; erst die zweite in Folge gilt.
    private func ausgefallen(_ id: String, _ fehler: any Error, _ inFolge: Int) {
        guard inFolge >= 2 else { return }
        aendern(id) { s in
            s.erreichbar = false
            s.fehler = fehler.localizedDescription
        }
        unerreichbar?(id)
    }

    // MARK: - Zugriff

    private func stand(_ id: String) throws -> Geraetestand {
        guard let s = staende[id] else { throw Befehlsfehler.geraetUnbekannt }
        return s
    }

    private func verteiler(_ id: String) throws -> Verteiler {
        let s = try stand(id)
        guard s.geraet.art == .verteiler else { throw Befehlsfehler.geraetUnbekannt }
        return Verteiler(adresse: s.geraet.adresse, sitzung: sitzung)
    }

    private func heizgeraet(_ id: String) throws -> Heizgeraet {
        let s = try stand(id)
        guard s.geraet.art == .heizung else { throw Befehlsfehler.geraetUnbekannt }
        return Heizgeraet(adresse: s.geraet.adresse, sitzung: sitzung)
    }

    private func client(_ id: String) throws -> any Geraeteclient {
        let s = try stand(id)
        return switch s.geraet.art {
        case .verteiler: Verteiler(adresse: s.geraet.adresse, sitzung: sitzung)
        case .heizung: Heizgeraet(adresse: s.geraet.adresse, sitzung: sitzung)
        }
    }

    /// `fbh_xxxxxx/3` → Verteiler und Raumnummer
    nonisolated static func raumadresse(_ id: Raum.ID) throws -> (geraet: String, nummer: Int) {
        let teile = id.split(separator: "/")
        guard teile.count == 2, let n = Int(teile[1]) else { throw Befehlsfehler.geraetUnbekannt }
        return (String(teile[0]), n)
    }

    /// Liest den Zustand ohne ETag neu und prüft, ob der Befehl angekommen ist. Die Firmware
    /// braucht für manche Befehle einen Regeldurchlauf; deshalb bis zu drei Versuche.
    private func ruecklesen(_ id: String, meldung: String, bedingung: (Geraetestand) -> Bool) async throws {
        for versuch in 0..<3 {
            if versuch > 0 { try await Task.sleep(for: takt.ruecklesen) }
            try await zustandLesen(id)
            if let s = staende[id], bedingung(s) {
                bildNeu()
                jetztAbfragen(id)
                return
            }
        }
        bildNeu()
        jetztAbfragen(id)
        throw Befehlsfehler.nichtUebernommen(meldung)
    }

    private func zustandLesen(_ id: String) async throws {
        let s = try stand(id)
        switch s.geraet.art {
        case .verteiler:
            if case .neu(let z, let roh) = try await Verteiler(adresse: s.geraet.adresse, sitzung: sitzung).zustand(etagNutzen: false) {
                aendern(id) { s in
                    s.verteiler = z
                    s.rohzustand = roh
                }
            }
        case .heizung:
            if case .neu(let z, let roh) = try await Heizgeraet(adresse: s.geraet.adresse, sitzung: sitzung).zustand() {
                aendern(id) { s in
                    s.heizgeraet = z
                    s.rohzustand = roh
                }
            }
        }
    }

    /// Liest die Konfiguration neu, etwa vor dem Schreiben einer vollständigen Raumliste: Ein
    /// Sollwert, der inzwischen am Gerät oder über Home Assistant geändert wurde, bliebe sonst
    /// auf dem alten Stand.
    public func frischeKonfiguration(_ id: String) async throws -> JSONWert {
        try await konfigurationLesen(id)
        return staende[id]?.konfiguration ?? [:]
    }

    /// Konfiguration und Zustand frisch vom Gerät, etwa bevor ein Vorschlag übernommen wird:
    /// Ein Wert, der sich seitdem am Gerät geändert hat, soll auffallen.
    public func frischerStand(_ id: String) async throws -> Geraetestand {
        try await konfigurationLesen(id)
        try await zustandLesen(id)
        bildNeu()
        return try stand(id)
    }

    private func konfigurationLesen(_ id: String) async throws {
        let k = try await client(id).konfiguration()
        aendern(id) { $0.konfiguration = k }
    }

    // MARK: - Räume

    public func sollwert(raum id: Raum.ID, _ grad: Double) async throws {
        let (geraet, nummer) = try Self.raumadresse(id)
        // Außerhalb 5–35 °C quittiert die Firmware, ohne zu übernehmen.
        let wert = min(35, max(5, (grad * 2).rounded() / 2))
        try await verteiler(geraet).sollwert(raum: nummer, wert)
        try await ruecklesen(geraet, meldung: "Der Verteiler hat den Sollwert nicht übernommen.") { s in
            s.verteiler?.raeume?.first { $0.id == nummer }?.sollC.map { abs($0 - wert) < 0.01 } ?? false
        }
    }

    public func betriebsart(raum id: Raum.ID, heizen: Bool) async throws {
        let (geraet, nummer) = try Self.raumadresse(id)
        try await verteiler(geraet).betriebsart(raum: nummer, heizen: heizen)
        try await ruecklesen(geraet, meldung: "Der Verteiler hat die Betriebsart nicht übernommen.") { s in
            (s.verteiler?.raeume?.first { $0.id == nummer }?.betriebsart == "off") == !heizen
        }
    }

    public func regelungAusloesen(raum id: Raum.ID) async throws {
        let (geraet, nummer) = try Self.raumadresse(id)
        try await verteiler(geraet).regelungAusloesen(raum: nummer)
        try await zustandLesen(geraet)
        bildNeu()
        jetztAbfragen(geraet)
    }

    // MARK: - Kanäle

    /// Auf, Zu und Anhalten setzen den Handbetrieb, „Regeln“ hebt ihn auf. Einen Kanal in der
    /// Messfahrt fasst die App nicht an: Auf und Zu blieben wirkungslos, Anhalten bräche die
    /// Fahrt ab.
    public func kanal(etage: String, nummer: Int, _ befehl: Kanalbefehl) async throws {
        let s = try stand(etage)
        let kanal = s.verteiler?.kanaele?.first { $0.id == nummer }
        if kanal?.belegt == true { throw Befehlsfehler.kanalBelegt(nummer) }
        let client = try verteiler(etage)
        // Ein Befehl an einen fahrenden Kanal wartet, bis die Fahrt endet; zum Umkehren erst anhalten.
        let faehrt = (kanal?.vorgang ?? "idle") != "idle"
        switch befehl {
        case .auf where faehrt && kanal?.vorgang != "opening", .zu where faehrt && kanal?.vorgang != "closing":
            try await client.kanal(nummer, .anhalten)
        default:
            break
        }
        try await client.kanal(nummer, befehl)
        let hand: Bool? = switch befehl {
        case .auf, .zu, .anhalten: true
        case .regeln: false
        case .stellung: nil
        }
        try await ruecklesen(etage, meldung: "Der Verteiler hat den Befehl für Kanal \(nummer) nicht übernommen.") { s in
            guard let hand else { return true }
            return s.verteiler?.kanaele?.first { $0.id == nummer }?.handbetrieb == hand
        }
    }

    /// Für alle Kanäle. Eine laufende Messfahrt würde „Alle anhalten“ abbrechen; die App lehnt
    /// das dann ab.
    public func alleKanaele(etage: String, _ befehl: Kanalbefehl) async throws {
        let s = try stand(etage)
        if befehl == .anhalten, s.verteiler?.messfahrt?.zustand == "running" {
            throw Befehlsfehler.messfahrtLaeuft
        }
        try await verteiler(etage).alleKanaele(befehl)
        let hand: Bool? = switch befehl {
        case .auf, .zu, .anhalten: true
        case .regeln: false
        case .stellung: nil
        }
        try await ruecklesen(etage, meldung: "Der Verteiler hat den Befehl nicht für alle Kanäle übernommen.") { s in
            guard let hand else { return true }
            return (s.verteiler?.kanaele ?? []).allSatisfy { $0.belegt == true || $0.handbetrieb == hand }
        }
    }

    // MARK: - Messfahrt

    public func messfahrtStarten(etage: String, kanal: Int) async throws {
        aendern(etage) { $0.messreihe = [] }
        try await verteiler(etage).messfahrtStarten(kanal: kanal)
        try await ruecklesen(etage, meldung: "Die Messfahrt hat nicht begonnen.") { s in
            let c = s.verteiler?.messfahrt
            return c?.zustand == "running" && c?.kanal == kanal
        }
    }

    public func messfahrtAbbrechen(etage: String) async throws {
        try await verteiler(etage).messfahrtAbbrechen()
        try await ruecklesen(etage, meldung: "Die Messfahrt lässt sich nicht abbrechen.") { s in
            s.verteiler?.messfahrt?.zustand != "running"
        }
    }

    /// Übernimmt die vorgeschlagenen Werte und räumt danach auf, wie die Weboberfläche; ohne
    /// „Verwerfen“ bliebe der Zustand auf „fertig“ stehen.
    public func messfahrtUebernehmen(etage: String) async throws {
        let kanal = staende[etage]?.verteiler?.messfahrt?.kanal
        try await sicherungSicherstellen(etage)
        let client = try verteiler(etage)
        try await client.messfahrtUebernehmen()
        try await konfigurationLesen(etage)
        let kalibriert = staende[etage]?.konfiguration?["channels"]?.alsListe?
            .first { $0["id"]?.alsGanzzahl == kanal }?["calibrated"]?.alsBool == true
        guard kalibriert else { throw Befehlsfehler.nichtUebernommen("Der Verteiler hat die Werte der Messfahrt nicht übernommen.") }
        try await client.messfahrtVerwerfen()
        aendern(etage) { $0.messreihe = [] }
        try await zustandLesen(etage)
        bildNeu()
    }

    public func messfahrtVerwerfen(etage: String) async throws {
        try await verteiler(etage).messfahrtVerwerfen()
        aendern(etage) { $0.messreihe = [] }
        try await ruecklesen(etage, meldung: "Die Messfahrt lässt sich nicht verwerfen.") { s in
            (s.verteiler?.messfahrt?.zustand ?? "idle") == "idle"
        }
    }

    // MARK: - Schutzfahrt

    /// Liefert die Zahl der Kanäle, die die Fahrt übernimmt; 0 heißt: nichts zu tun.
    @discardableResult
    public func schutzfahrt(etage: String, _ befehl: Schutzfahrtbefehl) async throws -> Int {
        let client = try verteiler(etage)
        let antwort = (try? JSONWert.lesen(try await client.verbindung.senden("/api/system/\(befehl.rawValue)"))) ?? .null
        try await zustandLesen(etage)
        bildNeu()
        jetztAbfragen(etage)
        return antwort["channels"]?.alsGanzzahl ?? 0
    }

    // MARK: - Heizungsgeräte

    /// Heizkreise gehören zu dem Heizungsgerät, bei dem sie angelegt sind.
    public func heizkreisgeraet(_ nummer: Int) -> String? {
        let heizung = staende.values.filter { $0.geraet.art == .heizung }
        guard let s = Zusammenfuehrung.speichergeraet(Array(heizung)),
              (s.heizgeraet?.heizkreise ?? []).contains(where: { $0.id == nummer }) else { return nil }
        return s.geraet.id
    }

    public func kesselkreispumpengeraet() -> String? {
        staende.values.first { $0.heizgeraet?.kesselkreispumpe?.aktiv == true }?.geraet.id
    }

    public func heizkreis(_ nummer: Int, _ modus: Pumpenmodus) async throws {
        guard let id = heizkreisgeraet(nummer) else { throw Befehlsfehler.geraetUnbekannt }
        try await heizgeraet(id).heizkreis(nummer, modus)
        try await ruecklesen(id, meldung: "Das Heizungsgerät hat die Betriebsart nicht übernommen.") { s in
            s.heizgeraet?.heizkreise?.first { $0.id == nummer }?.betriebsart == modus.rawValue
        }
    }

    public func kesselkreispumpe(_ modus: Pumpenmodus) async throws {
        guard let id = kesselkreispumpengeraet() else { throw Befehlsfehler.geraetUnbekannt }
        try await heizgeraet(id).kesselkreispumpe(modus)
        try await ruecklesen(id, meldung: "Das Heizungsgerät hat die Betriebsart der Kesselkreispumpe nicht übernommen.") { s in
            s.heizgeraet?.kesselkreispumpe?.betriebsart == modus.rawValue
        }
    }

    public func fuehlerNeuSuchen(geraet id: String) async throws {
        try await heizgeraet(id).fuehlerNeuSuchen()
        jetztAbfragen(id)
    }

    /// Schlüssel eines verschlüsselt sendenden Thermometers am Verteiler hinterlegen; `nil`
    /// entfernt ihn. Ob er passt, entscheidet erst der nächste Rundruf des Thermometers, etwa
    /// zwei Sekunden später; die Liste wird deshalb gleich und noch einmal danach gelesen.
    public func thermometerSchluessel(etage id: String, mac: String, schluessel: String?) async throws {
        let client = try verteiler(id)
        try await client.thermometerSchluessel(mac: mac, schluessel: schluessel)
        for warten in [Duration.zero, .seconds(3)] {
            try? await Task.sleep(for: warten)
            if let t = try? await client.thermometerliste() {
                aendern(id) {
                    $0.thermometer = t.geraete ?? []
                    $0.thermometerSchluessel = t.schluessel ?? []
                }
                bildNeu()
            }
        }
    }

    /// Scharf schalten und Starten verwerfen eine vorhandene Aufzeichnung ohne Rückfrage; die
    /// Oberfläche fragt deshalb vorher.
    public func aufzeichnung(geraet id: String, _ befehl: Aufzeichnungsbefehl) async throws {
        try await heizgeraet(id).aufzeichnung(befehl)
        try await zustandLesen(id)
        bildNeu()
        jetztAbfragen(id)
    }

    public func aufzeichnungHolen(geraet id: String) async throws -> String {
        try await heizgeraet(id).aufzeichnung()
    }

    public func protokollHolen(geraet id: String, tage: Bool) async throws -> String {
        let pfad = tage ? "/api/log/days" : "/api/log/charges"
        return String(decoding: try await heizgeraet(id).verbindung.holenRoh(pfad, zeitlimit: 15), as: UTF8.self)
    }

    /// Verwirft Ladungs- und Tagesprotokoll unwiderruflich.
    public func protokolleLoeschen(geraet id: String) async throws {
        try await heizgeraet(id).protokolleLoeschen()
        aendern(id) { s in
            s.ladungen = []
            s.tage = []
        }
        jetztAbfragen(id)
    }

    // MARK: - Konfiguration

    /// Schreibt eine Teilangabe und prüft, ob sie übernommen wurde. Vorher sichert die App das
    /// Gerät, sofern die letzte Sicherung nicht ganz frisch ist.
    public func konfigurationAendern(geraet id: String, _ teil: JSONWert) async throws {
        try await sicherungSicherstellen(id)
        try await client(id).konfigurationAendern(teil)
        try await konfigurationLesen(id)
        if let k = staende[id]?.konfiguration {
            let abweichend = Self.abweichungen(teil, k)
            if !abweichend.isEmpty {
                throw Befehlsfehler.nichtUebernommen("Das Gerät hat nicht alle Werte übernommen: \(abweichend.joined(separator: ", ")).")
            }
        }
        try await zustandLesen(id)
        bildNeu()
        jetztAbfragen(id)
    }

    /// Schlüssel, die nach dem Schreiben nicht den gesendeten Wert tragen. Kennwörter kann man
    /// nicht zurücklesen; Listen mit `id` oder `rom` werden elementweise verglichen.
    nonisolated static func abweichungen(_ teil: JSONWert, _ konfiguration: JSONWert, pfad: String = "") -> [String] {
        switch teil {
        case .objekt(let felder):
            return felder.sorted { $0.key < $1.key }.flatMap { schluessel, wert -> [String] in
                if ["pass", "ap_pass"].contains(schluessel) { return [] }
                let name = pfad.isEmpty ? schluessel : "\(pfad).\(schluessel)"
                guard let ist = konfiguration[schluessel] else { return [name] }
                return abweichungen(wert, ist, pfad: name)
            }
        case .liste(let elemente):
            guard let istListe = konfiguration.alsListe else { return [pfad] }
            return elemente.enumerated().flatMap { i, element -> [String] in
                for schluessel in ["id", "rom"] {
                    if let k = element[schluessel] {
                        let passt = { (e: JSONWert) -> Bool in
                            if e[schluessel] == k { return true }
                            guard let a = e[schluessel]?.alsText, let b = k.alsText else { return false }
                            return a.uppercased() == b.uppercased()
                        }
                        guard let ist = istListe.first(where: passt) else {
                            return ["\(pfad)[\(k.kompakt)]"]
                        }
                        return abweichungen(element, ist, pfad: "\(pfad)[\(k.kompakt)]")
                    }
                }
                guard i < istListe.count else { return ["\(pfad)[\(i)]"] }
                return abweichungen(element, istListe[i], pfad: "\(pfad)[\(i)]")
            }
        case .zahl(let soll):
            guard let ist = konfiguration.alsZahl else { return [pfad] }
            return abs(ist - soll) <= max(0.001, abs(soll) * 1e-5) ? [] : [pfad]
        case .text(let soll):
            // Die Firmware kürzt zu lange Texte; ein Präfix gilt als übernommen.
            guard let ist = konfiguration.alsText else { return soll.isEmpty ? [] : [pfad] }
            let a = soll.trimmingCharacters(in: .whitespaces).uppercased()
            let b = ist.trimmingCharacters(in: .whitespaces).uppercased()
            return a == b || (!b.isEmpty && a.hasPrefix(b)) ? [] : [pfad]
        case .bool(let soll):
            return konfiguration.alsBool == soll ? [] : [pfad]
        case .null:
            return konfiguration == .null || konfiguration.alsText == "" ? [] : [pfad]
        }
    }

    /// Sichert ein Gerät, wenn die letzte Sicherung älter ist als `takt.sicherungFrisch`.
    public func sicherungSicherstellen(_ id: String) async throws {
        guard let sicherungen else { return }
        let grenze = Date.now.addingTimeInterval(-Double(takt.sicherungFrisch.components.seconds))
        if let letzte = try? sicherungen.eintraege(geraet: id).first, letzte.datum > grenze { return }
        try await sichern(id)
    }

    @discardableResult
    public func sichern(_ id: String) async throws -> Sicherungseintrag? {
        guard let sicherungen else { return nil }
        let daten = try await client(id).sicherung()
        let eintrag = try sicherungen.sichern(daten, geraet: id)
        try? sicherungen.aufraeumen(behalten: 10)
        letzteSicherungen[id] = eintrag.datum
        // Der Befund zur Sicherung hängt daran; das Bild soll ihn mit der nächsten Abfrage verlieren.
        bildVeraltet = true
        return eintrag
    }

    public func sicherungEinspielen(geraet id: String, _ daten: Data) async throws {
        try await sicherungSicherstellen(id)
        try await client(id).sicherungEinspielen(daten)
        try await konfigurationLesen(id)
        try await zustandLesen(id)
        bildNeu()
    }

    public func firmwareEinspielen(geraet id: String, _ datei: Firmwaredatei) async throws {
        try await sicherungSicherstellen(id)
        try await client(id).firmwareEinspielen(datei)
    }

    public func neustart(geraet id: String) async throws {
        try await client(id).neustart()
    }

    /// Die Werksvorgabe wirkt erst nach einem Neustart vollständig; bis dahin liefe das Gerät mit
    /// den alten Einstellungen weiter. Die App startet es deshalb gleich neu.
    public func werksvorgabe(geraet id: String) async throws {
        try await sichern(id)
        let c = try client(id)
        try await c.werksvorgabe()
        try await c.neustart()
    }

    public func netzeSuchen(geraet id: String) async throws -> [Netzsuche.Netz] {
        let c = try client(id)
        try await c.netzsucheStarten()
        for _ in 0..<30 {
            try await Task.sleep(for: .milliseconds(700))
            guard let stand = try? await c.netzsuche() else { continue }
            if stand.laeuft != true {
                var bester: [String: Netzsuche.Netz] = [:]
                for n in stand.netze ?? [] {
                    guard let name = n.name, !name.isEmpty else { continue }
                    if (bester[name]?.signal ?? .min) < (n.signal ?? .min) { bester[name] = n }
                }
                return bester.values.sorted { ($0.signal ?? .min) > ($1.signal ?? .min) }
            }
        }
        throw Befehlsfehler.nichtUebernommen("Die Suche nach Netzen antwortet nicht.")
    }
}

public enum Befehlsfehler: Error, Sendable, Equatable {
    case geraetUnbekannt
    case nichtUebernommen(String)
    case kanalBelegt(Int)
    case messfahrtLaeuft
}

extension Befehlsfehler: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .geraetUnbekannt:
            "Das Gerät ist nicht eingebunden oder noch nicht abgefragt."
        case .nichtUebernommen(let meldung):
            meldung
        case .kanalBelegt(let n):
            "Kanal \(n) ist durch eine Messfahrt belegt. Brechen Sie die Messfahrt ab oder warten Sie, bis sie endet."
        case .messfahrtLaeuft:
            "Während einer Messfahrt würde „Alle anhalten“ die Fahrt abbrechen. Brechen Sie die Messfahrt vorher ab."
        }
    }
}

/// Wartet eine Zeit lang oder bis jemand weckt, was zuerst eintritt.
@MainActor
final class Wecker {
    private var fortsetzung: CheckedContinuation<Void, Never>?
    private var runde = 0
    private var vorgemerkt = false

    func warten(_ dauer: Duration) async {
        if vorgemerkt {
            vorgemerkt = false
            return
        }
        runde += 1
        let meine = runde
        await withTaskCancellationHandler {
            await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
                if Task.isCancelled {
                    c.resume()
                    return
                }
                fortsetzung = c
                Task { @MainActor [weak self] in
                    try? await Task.sleep(for: dauer)
                    guard let self, self.runde == meine else { return }
                    self.aufwecken()
                }
            }
        } onCancel: {
            Task { @MainActor [weak self] in self?.aufwecken() }
        }
    }

    func wecken() {
        if fortsetzung != nil {
            aufwecken()
        } else {
            vorgemerkt = true
        }
    }

    private func aufwecken() {
        runde += 1
        let f = fortsetzung
        fortsetzung = nil
        f?.resume()
    }
}
