import Foundation
import Observation
import Anlage
import Geraeteschnittstelle
import Geraetesuche

/// Ablauf des Einrichtungsassistenten: Geräte im Heimnetz aufnehmen, neue Geräte über ihren
/// Zugangspunkt einbinden, zum Schluss alle Geräte sichern.
@MainActor
@Observable
final class Einrichtungsablauf {
    enum Vorgang: Equatable {
        case ruht
        case laeuft
        case erledigt
        case fehler(String)
    }

    /// Stationen beim Einbinden eines neuen Geräts
    enum Phase: Equatable {
        case bereit
        case verbinden
        case netzeSuchen
        case eingabe
        case schreiben
        case warten
        case verlassen
        case fertig
        case fehler(String)
    }

    let suche = Geraetesuche()
    let verzeichnis: Geraeteverzeichnis
    private let sicherungen: Sicherungsablage?

    /// Adresse im Einrichtungsbetrieb. In der Entwicklung lässt sie sich mit dem Startargument
    /// `-einrichtungsadresse http://127.0.0.1:8321` auf die Attrappe legen.
    let einrichtungsadresse: URL = UserDefaults.standard.string(forKey: "einrichtungsadresse")
        .flatMap(URL.init(string:)) ?? Einbindung.zugangspunkt

    // Aufnahme aus der Suche und von Hand
    private(set) var aufnahme: [String: Vorgang] = [:]
    var adresseEingabe = ""
    private(set) var handeingabe: Vorgang = .ruht

    // Neues Gerät
    var art: Einbindungsart = .verteiler {
        didSet {
            // Das Kennwort folgt der Art, solange niemand ein eigenes eingetragen hat
            if zugangspunktKennwort == Einbindung.werkskennwort(oldValue) {
                zugangspunktKennwort = Einbindung.werkskennwort(art)
            }
        }
    }
    var zugangspunktKennwort = Einbindung.werkskennwort
    private(set) var phase: Phase = .bereit
    private(set) var erkannt: Einbindung.Geraet?
    private(set) var netze: [Netzsuche.Netz] = []
    var netz = ""
    var kennwort = ""
    var ort = ""
    private(set) var neueAdresse: String?
    private(set) var eingebunden: Aufnahme?
    /// Das WLAN des Geräts wurde von Hand gewählt; danach muss man es auch von Hand verlassen.
    private(set) var vonHandVerbunden = !Zugangspunkt.beitrittMoeglich

    // Abschluss
    private(set) var sicherung: [String: Vorgang] = [:]

    init(verzeichnis: Geraeteverzeichnis, sicherungen: Sicherungsablage?) {
        self.verzeichnis = verzeichnis
        self.sicherungen = sicherungen
    }

    // MARK: Suche

    func starten() {
        suche.starten()
    }

    func beenden() {
        suche.beenden()
    }

    /// Regelgeräte und Leitstände mit aufgelöster Adresse
    var gefunden: [GefundenesGeraet] {
        suche.geraete.filter { $0.adresse != nil && ($0.art != nil || $0.istLeitstand) }
    }

    var offene: [GefundenesGeraet] {
        gefunden.filter { !kennt($0.id) }
    }

    func kennt(_ id: String) -> Bool {
        verzeichnis.kennt(id) || verzeichnis.kenntLeitstand(id)
    }

    func aufnehmen(_ g: GefundenesGeraet) async {
        guard let adresse = g.adresse else { return }
        aufnahme[g.id] = .laeuft
        do {
            Self.aufnehmen(try await Self.pruefen(adresse), in: verzeichnis)
            aufnahme[g.id] = .erledigt
        } catch {
            aufnahme[g.id] = .fehler(error.localizedDescription)
        }
    }

    func alleAufnehmen() async {
        for g in offene {
            await aufnehmen(g)
        }
    }

    /// Adresse von Hand: „192.168.1.50“, mit Port oder mit „http://“.
    func adresseAufnehmen() async {
        var text = adresseEingabe.trimmingCharacters(in: .whitespaces)
        if !text.contains("://") { text = "http://" + text }
        guard let url = URL(string: text), let host = url.host(), Self.gueltigerHost(host) else {
            handeingabe = .fehler("Das ist keine gültige Adresse. Beispiel: 192.168.1.77 oder 192.168.1.77:8080")
            return
        }
        handeingabe = .laeuft
        do {
            Self.aufnehmen(try await Self.pruefen(url), in: verzeichnis)
            handeingabe = .erledigt
            adresseEingabe = ""
        } catch {
            handeingabe = .fehler(error.localizedDescription)
        }
    }

    /// IP-Adresse oder Name aus Buchstaben, Ziffern, Punkt und Bindestrich; IPv6 in Klammern.
    static func gueltigerHost(_ host: String) -> Bool {
        if host.contains(":") { return true }
        return !host.isEmpty && host.unicodeScalars.allSatisfy {
            ("a"..."z").contains($0) || ("A"..."Z").contains($0) || ("0"..."9").contains($0) || $0 == "." || $0 == "-"
        }
    }

    /// Was die Prüfung einer Adresse ergibt: ein Regelgerät oder ein Leitstand
    enum Aufnahme: Equatable {
        case geraet(BekanntesGeraet)
        case leitstand(BekannterLeitstand)

        var bezeichnung: String {
            switch self {
            case .geraet(let g): g.bezeichnung
            case .leitstand(let l): l.ort
            }
        }

        var adresse: URL {
            switch self {
            case .geraet(let g): g.adresse
            case .leitstand(let l): l.adresse
            }
        }
    }

    /// Liest Zustand und Art eines Geräts; was nicht antwortet oder weder Heizungsgerät noch
    /// Leitstand ist, wird nicht aufgenommen.
    static func pruefen(_ adresse: URL) async throws -> Aufnahme {
        let zustand = try await Geraeteverbindung(basis: adresse).holen("/api/state", als: JSONWert.self)
        guard let id = zustand["device"]?["id"]?.alsText else { throw Einbindungsfehler.unbekanntesGeraet }
        let ort = zustand["device"]?["site"]?.alsText.flatMap { $0.isEmpty ? nil : $0 }
        if Einbindung.istLeitstand(zustand) {
            return .leitstand(BekannterLeitstand(id: id, ort: ort ?? "Leitstand", adresse: adresse,
                                                 firmware: zustand["version"]?.alsText))
        }
        guard let art = Einbindung.art(zustand) else { throw Einbindungsfehler.unbekanntesGeraet }
        return .geraet(BekanntesGeraet(
            id: id, art: art, ort: ort ?? (art == .verteiler ? "ohne Etage" : "ohne Ort"),
            adresse: adresse, firmware: zustand["version"]?.alsText, zuletztGesehen: .now))
    }

    static func aufnehmen(_ a: Aufnahme, in verzeichnis: Geraeteverzeichnis) {
        switch a {
        case .geraet(let g): verzeichnis.aufnehmen(g)
        case .leitstand(let l): verzeichnis.leitstandAufnehmen(l)
        }
    }

    // MARK: Neues Gerät

    func neuBeginnen() {
        phase = .bereit
        erkannt = nil
        netze = []
        netz = ""
        kennwort = ""
        ort = ""
        neueAdresse = nil
        eingebunden = nil
    }

    private var einbindung: Einbindung {
        Einbindung(adresse: einrichtungsadresse)
    }

    /// Unter iOS tritt die App dem Zugangspunkt selbst bei; am Mac hat man das WLAN schon von
    /// Hand gewechselt.
    func verbinden() async {
        phase = .verbinden
        vonHandVerbunden = !Zugangspunkt.beitrittMoeglich
        do {
            #if os(iOS)
            var beitrittGescheitert = false
            if einrichtungsadresse == Einbindung.zugangspunkt {
                do {
                    try await Zugangspunkt.beitreten(praefix: Einbindung.praefix(art), kennwort: zugangspunktKennwort)
                } catch Zugangspunktfehler.abgelehnt {
                    throw Zugangspunktfehler.abgelehnt
                } catch {
                    // Ohne Berechtigung oder bei einem Fehler des Systems: Vielleicht ist das
                    // WLAN schon von Hand gewechselt.
                    beitrittGescheitert = true
                }
            }
            let g: Einbindung.Geraet
            do {
                g = try await einbindung.erkennen(zeitgrenze: beitrittGescheitert ? .seconds(6) : .seconds(45))
            } catch Einbindungsfehler.keinGeraet where beitrittGescheitert {
                throw Einrichtungsfehler.beitrittVonHand(netz: "\(Einbindung.praefix(art))XXXX", kennwort: zugangspunktKennwort)
            }
            vonHandVerbunden = beitrittGescheitert
            #else
            let g = try await einbindung.erkennen(zeitgrenze: .seconds(45))
            #endif
            erkannt = g
            art = g.art
            ort = g.ort ?? ""
            await netzeSuchen()
        } catch {
            phase = .fehler(error.localizedDescription)
        }
    }

    /// Nach einem Fehler: mit erkanntem Gerät zurück zur Eingabe, sonst von vorn.
    func erneutVersuchen() {
        if erkannt != nil, !netze.isEmpty {
            phase = .eingabe
        } else {
            neuBeginnen()
        }
    }

    var laeuft: Bool {
        switch phase {
        case .verbinden, .netzeSuchen, .schreiben, .warten, .verlassen: true
        default: false
        }
    }

    func netzeSuchen() async {
        phase = .netzeSuchen
        do {
            netze = try await einbindung.netzeSuchen()
            if netz.isEmpty || !netze.contains(where: { $0.name == netz }) {
                netz = netze.first?.name ?? ""
            }
            phase = .eingabe
        } catch {
            phase = .fehler(error.localizedDescription)
        }
    }

    /// Der Gerätename, den das Gerät bekommt, solange noch die Werksvorgabe gilt
    var geraetename: String? {
        guard let erkannt else { return nil }
        return Einbindung.konfiguration(erkannt, netz: netz, kennwort: "", ort: ort)["wifi"]?["hostname"]?.alsText
            ?? erkannt.hostname
    }

    var eingabeVollstaendig: Bool {
        !netz.isEmpty && !ort.trimmingCharacters(in: .whitespaces).isEmpty
    }

    func einrichten() async {
        guard let erkannt else { return }
        phase = .schreiben
        do {
            try await einbindung.einrichten(erkannt, netz: netz, kennwort: kennwort, ort: ort)
            phase = .warten
            let ip = try await einbindung.imHeimnetz(erkannt.art, zeitgrenze: .seconds(90))
            neueAdresse = ip
            phase = .verlassen
            #if os(iOS)
            if einrichtungsadresse == Einbindung.zugangspunkt {
                await Zugangspunkt.verlassen()
            }
            #endif
            // Im Entwicklungsbetrieb meldet die Attrappe eine Heimnetzadresse, die es nicht gibt;
            // dort bleibt das Gerät unter der Einrichtungsadresse.
            let adresse = einrichtungsadresse == Einbindung.zugangspunkt
                ? URL(string: "http://\(ip)")! : einrichtungsadresse
            let id = erkannt.id ?? "unbekannt"
            let ort = ort.trimmingCharacters(in: .whitespaces)
            let aufnahme: Aufnahme = if let art = erkannt.art.geraeteart {
                .geraet(BekanntesGeraet(id: id, art: art, ort: ort, adresse: adresse,
                                        firmware: erkannt.firmware, zuletztGesehen: .now))
            } else {
                .leitstand(BekannterLeitstand(id: id, ort: ort, adresse: adresse,
                                              firmware: erkannt.firmware, zuletztGesehen: .now))
            }
            Self.aufnehmen(aufnahme, in: verzeichnis)
            eingebunden = aufnahme
            phase = .fertig
        } catch {
            phase = .fehler(error.localizedDescription)
        }
    }

    // MARK: Abschluss

    /// Sichert die Konfiguration aller Geräte, verschlüsselt, bevor irgendetwas geändert wird.
    func allesSichern() async {
        guard let sicherungen else { return }
        for g in verzeichnis.geraete where sicherung[g.id] != .erledigt {
            sicherung[g.id] = .laeuft
            do {
                let daten: Data = switch g.art {
                case .verteiler: try await Verteiler(adresse: g.adresse).sicherung()
                case .heizung: try await Heizgeraet(adresse: g.adresse).sicherung()
                }
                try sicherungen.sichern(daten, geraet: g.id)
                sicherung[g.id] = .erledigt
            } catch {
                sicherung[g.id] = .fehler(error.localizedDescription)
            }
        }
        try? sicherungen.aufraeumen(behalten: 10)
    }
}

enum Einrichtungsfehler: LocalizedError {
    case beitrittVonHand(netz: String, kennwort: String)

    var errorDescription: String? {
        switch self {
        case .beitrittVonHand(let netz, let kennwort):
            "Die App konnte dem WLAN des Geräts nicht selbst beitreten. Wählen Sie in den Einstellungen unter WLAN das Netz „\(netz)“\(kennwort.isEmpty ? "" : " mit dem Kennwort „\(kennwort)“"), kehren Sie zur App zurück und verbinden Sie erneut."
        }
    }
}
