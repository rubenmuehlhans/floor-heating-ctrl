import Foundation
import Anlage
import Geraeteschnittstelle

/// Einschätzung der Anlage durch die KI, oben auf der Übersicht.
public struct Lagebericht: Sendable, Hashable, Codable {
    public enum Zustand: String, Sendable, Hashable, Codable, CaseIterable {
        case inOrdnung = "In Ordnung"
        case beobachten = "Beobachten"
        case handeln = "Handeln"
    }

    public struct Hinweis: Identifiable, Sendable, Hashable, Codable {
        public var id: String
        public var schwere: Befund.Schwere
        public var text: String
        /// Kennung eines Befunds oder Vorschlags, auf den sich der Hinweis bezieht.
        public var bezug: String?

        public init(id: String, schwere: Befund.Schwere, text: String, bezug: String?) {
            self.id = id
            self.schwere = schwere
            self.text = text
            self.bezug = bezug
        }
    }

    public var zustand: Zustand
    public var kurztext: String
    public var hinweise: [Hinweis]
    public var erstellt: Date
    public var modell: String
    /// Die Befunde, die beim Erstellen sichtbar waren; ein neuer Befund macht den Bericht alt.
    public var befunde: [String] = []

    public init(zustand: Zustand, kurztext: String, hinweise: [Hinweis], erstellt: Date, modell: String, befunde: [String] = []) {
        self.zustand = zustand
        self.kurztext = kurztext
        self.hinweise = hinweise
        self.erstellt = erstellt
        self.modell = modell
        self.befunde = befunde
    }
}

/// Vorschlag der KI für eine geänderte Einstellung oder eine Aktion. Wirksam wird er erst nach
/// Bestätigung; vorher sichert die App die betroffenen Geräte.
public struct Vorschlag: Identifiable, Sendable, Hashable, Codable {
    public enum Status: Sendable, Hashable, Codable {
        case offen
        case uebernommen(Date)
        case verworfen
        case zurueckgenommen
        /// Übernahme oder Rücknahme ist gescheitert; die Meldung sagt, welcher Wert gilt.
        case gescheitert(String)
    }

    /// Ein Wert an einem Gerät, so wie er übernommen und zurückgenommen wird.
    public struct Ziel: Sendable, Hashable, Codable {
        public var geraet: String
        /// Kennung im Parameterkatalog wie `heizung/buffer.voll_c` oder ein eigener Weg:
        /// `heizung/heizkreis.betriebsart`, `heizung/kesselkreispumpe.betriebsart`,
        /// `heizung/circuits[].peers`
        public var parameter: String
        /// Listeneintrag: Raumnummer oder Heizkreis
        public var eintrag: JSONWert?
        public var bisher: JSONWert
        public var neu: JSONWert
        /// Ort des Geräts zur Anzeige
        public var ort: String = ""
        /// Einstellung und Eintrag zur Anzeige, etwa „Proportionalband · Wohnzimmer“
        public var bezeichnung: String = ""

        public init(geraet: String, parameter: String, eintrag: JSONWert? = nil, bisher: JSONWert, neu: JSONWert,
                    ort: String = "", bezeichnung: String = "") {
            self.geraet = geraet
            self.parameter = parameter
            self.eintrag = eintrag
            self.bisher = bisher
            self.neu = neu
            self.ort = ort
            self.bezeichnung = bezeichnung
        }
    }

    /// Aktionen, die die KI vorschlagen darf. Sie verändern keine Einstellung und lassen sich
    /// deshalb auch nicht zurücknehmen.
    public enum Aktion: String, Sendable, Hashable, Codable, CaseIterable {
        case messfahrt, schutzfahrt, fuehlersuche, ladungsaufzeichnung, relaispruefung, neustart

        public var bezeichnung: String {
            switch self {
            case .messfahrt: "Messfahrt"
            case .schutzfahrt: "Schutzfahrt"
            case .fuehlersuche: "Fühlersuche"
            case .ladungsaufzeichnung: "Ladungsaufzeichnung"
            case .relaispruefung: "Relaisprüfung"
            case .neustart: "Neustart"
            }
        }
    }

    public var id: String
    public var titel: String
    /// Orte der betroffenen Geräte, etwa Kessel und Pufferspeicher
    public var geraete: [String]
    /// Bezeichnung der Einstellung zur Anzeige
    public var parameter: String
    public var bisher: String
    public var neu: String
    public var begruendung: String
    public var wirkung: String
    public var status: Status
    public var ziele: [Ziel] = []
    public var aktion: Aktion?
    public var erstellt: Date = .now
    /// Das Gespräch, in dem der Vorschlag entstand
    public var gespraech: String?
    /// Die Sicherungen vor der Übernahme, je Gerät der Dateiname in der Sicherungsablage
    public var sicherungen: [String: String] = [:]
    public var kontrolle: Wirkungskontrolle?
    /// Ergebnis einer Aktion, etwa der Relaisprüfung
    public var ergebnis: String?

    public init(
        id: String, titel: String, geraete: [String], parameter: String, bisher: String, neu: String,
        begruendung: String, wirkung: String, status: Status = .offen, ziele: [Ziel] = [], aktion: Aktion? = nil,
        erstellt: Date = .now, gespraech: String? = nil
    ) {
        self.id = id
        self.titel = titel
        self.geraete = geraete
        self.parameter = parameter
        self.bisher = bisher
        self.neu = neu
        self.begruendung = begruendung
        self.wirkung = wirkung
        self.status = status
        self.ziele = ziele
        self.aktion = aktion
        self.erstellt = erstellt
        self.gespraech = gespraech
    }

    public var istOffen: Bool { status == .offen }

    public var istAktion: Bool { aktion != nil }
}

/// Vergleich der Kennzahlen vor und nach einer Übernahme. Die App trägt die Werte vorher beim
/// Übernehmen ein und die Werte nachher, sobald der Zeitraum verstrichen ist.
public struct Wirkungskontrolle: Sendable, Hashable, Codable {
    public struct Wert: Sendable, Hashable, Codable, Identifiable {
        public var id: String { name }
        public var name: String
        public var vorher: Double?
        public var nachher: Double?

        public init(name: String, vorher: Double?, nachher: Double?) {
            self.name = name
            self.vorher = vorher
            self.nachher = nachher
        }
    }

    /// Ab dann liegen genug Tage nach der Änderung vor.
    public var faellig: Date
    public var vorher: [String: Double]
    public var nachher: [String: Double]?
    public var ausgewertet: Date?

    public init(faellig: Date, vorher: [String: Double], nachher: [String: Double]? = nil, ausgewertet: Date? = nil) {
        self.faellig = faellig
        self.vorher = vorher
        self.nachher = nachher
        self.ausgewertet = ausgewertet
    }

    /// Werte, die vorher oder nachher vorliegen, in fester Reihenfolge
    public var werte: [Wert] {
        let namen = Set(vorher.keys).union(nachher.map { Set($0.keys) } ?? [])
        return namen.sorted().map { Wert(name: $0, vorher: vorher[$0], nachher: nachher?[$0]) }
    }
}

public struct Werkzeugaufruf: Identifiable, Sendable, Hashable, Codable {
    public var id: String
    /// Name des Werkzeugs, z. B. `verlauf`.
    public var name: String
    /// Was das Werkzeug gelesen hat, in einem Satz.
    public var beschreibung: String
    public var ergebnis: String
    /// `false`, solange das Werkzeug noch arbeitet
    public var fertig: Bool = true

    public init(id: String, name: String, beschreibung: String, ergebnis: String, fertig: Bool = true) {
        self.id = id
        self.name = name
        self.beschreibung = beschreibung
        self.ergebnis = ergebnis
        self.fertig = fertig
    }
}

public struct Beitrag: Identifiable, Sendable, Hashable, Codable {
    public enum Rolle: String, Sendable, Hashable, Codable {
        case nutzer, assistent
    }

    public enum Baustein: Sendable, Hashable, Codable {
        case text(String)
        case ueberlegung(String)
        case werkzeug(Werkzeugaufruf)
        case vorschlag(String)
        case bild(String)
        /// Eine gescheiterte Anfrage; der Text erklärt, was zu tun ist.
        case fehler(String)
    }

    public var id: String
    public var rolle: Rolle
    public var bausteine: [Baustein]
    public var zeit: Date
    /// Das Modell, das geantwortet hat
    public var modell: String?

    public init(id: String, rolle: Rolle, bausteine: [Baustein], zeit: Date, modell: String? = nil) {
        self.id = id
        self.rolle = rolle
        self.bausteine = bausteine
        self.zeit = zeit
        self.modell = modell
    }
}

public struct Gespraech: Identifiable, Sendable, Hashable, Codable {
    public var id: String
    public var titel: String
    public var beitraege: [Beitrag]
    public var begonnen: Date = .now
    public var zuletzt: Date = .now

    public init(id: String, titel: String, beitraege: [Beitrag], begonnen: Date = .now, zuletzt: Date = .now) {
        self.id = id
        self.titel = titel
        self.beitraege = beitraege
        self.begonnen = begonnen
        self.zuletzt = zuletzt
    }

    public static func neu() -> Gespraech {
        Gespraech(id: UUID().uuidString, titel: "Neues Gespräch", beitraege: [])
    }
}

/// Eintrag im Änderungsprotokoll: was übernommen oder zurückgenommen wurde, wann und von wem.
public struct Aenderung: Identifiable, Sendable, Hashable, Codable {
    public var id: String
    public var zeit: Date
    /// Ort des Geräts zur Anzeige
    public var geraet: String
    public var parameter: String
    public var bisher: String
    public var neu: String
    public var ausloeser: String
    /// Kennung des Geräts und des Parameters im Katalog, für die KI und die Neustartprüfung
    public var geraetekennung: String?
    public var parameterkennung: String?
    public var vorschlag: String?

    public init(
        id: String, zeit: Date, geraet: String, parameter: String, bisher: String, neu: String, ausloeser: String,
        geraetekennung: String? = nil, parameterkennung: String? = nil, vorschlag: String? = nil
    ) {
        self.id = id
        self.zeit = zeit
        self.geraet = geraet
        self.parameter = parameter
        self.bisher = bisher
        self.neu = neu
        self.ausloeser = ausloeser
        self.geraetekennung = geraetekennung
        self.parameterkennung = parameterkennung
        self.vorschlag = vorschlag
    }
}
