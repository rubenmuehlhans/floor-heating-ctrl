import Foundation

/// Zugang zu einer Verteilerplatine.
public struct Verteiler: Geraeteclient {
    public static let art = Geraeteart.verteiler
    public let verbindung: Geraeteverbindung

    public init(adresse: URL, sitzung: URLSession = .geraete) {
        verbindung = Geraeteverbindung(basis: adresse, sitzung: sitzung)
    }

    /// Zustand mit ETag. Bordfühler, Tastenrohwerte und Netz erhöhen den Änderungszähler nicht;
    /// die Weboberfläche holt deshalb jede Minute ohne ETag, und so hält es die App auch.
    public func zustand(etagNutzen: Bool = true) async throws -> Abfrage<Verteilerzustand> {
        try await verbindung.abfragen("/api/state", etagNutzen: etagNutzen)
    }

    public func bedarf() async throws -> Bedarfsantwort {
        try await verbindung.holen("/api/demand")
    }

    public func thermometer() async throws -> [Thermometerliste.Funkthermometer] {
        try await thermometerliste().geraete ?? []
    }

    public func thermometerliste() async throws -> Thermometerliste {
        try await verbindung.holen("/api/ble", als: Thermometerliste.self)
    }

    /// Schlüssel eines verschlüsselt sendenden Thermometers hinterlegen; `nil` entfernt ihn. Ob er
    /// passt, zeigt `/api/ble` mit dem nächsten Rundruf des Thermometers.
    public func thermometerSchluessel(mac: String, schluessel: String?) async throws {
        try await verbindung.senden("/api/ble/key", json: ["mac": .text(mac), "bindkey": .text(schluessel ?? "")])
    }

    /// Messreihe der laufenden oder letzten Messfahrt ab Punkt `ab`.
    public func messreihe(ab: Int = 0) async throws -> Messreihe {
        try await verbindung.holen("/api/calib?from=\(ab)")
    }

    // MARK: Räume

    /// Werte außerhalb 5–35 °C bestätigt die Firmware, ohne sie zu übernehmen.
    public func sollwert(raum: Int, _ grad: Double) async throws {
        try await verbindung.senden("/api/room/\(raum)/target", json: ["target_c": .zahl(grad)])
    }

    public func betriebsart(raum: Int, heizen: Bool) async throws {
        try await verbindung.senden("/api/room/\(raum)/mode", json: ["mode": heizen ? "heat" : "off"])
    }

    /// Regelung des Raums sofort auslösen, statt das Prüfintervall abzuwarten.
    public func regelungAusloesen(raum: Int) async throws {
        try await verbindung.senden("/api/room/\(raum)/check")
    }

    // MARK: Kanäle

    public func kanal(_ nummer: Int, _ befehl: Kanalbefehl) async throws {
        try await verbindung.senden("/api/channel/\(nummer)/cmd", json: befehl.json)
    }

    /// Für den Notfall, in dem die ganze Anlage auf oder zu soll. Eine Zwischenstellung für alle
    /// bestätigt die Firmware, ohne sie auszuführen; die App lässt sie deshalb gar nicht zu.
    public func alleKanaele(_ befehl: Kanalbefehl) async throws {
        if case .stellung = befehl {
            throw Geraetefehler.unzulaessig("Eine Zwischenstellung gilt nur für einzelne Kreise.")
        }
        try await verbindung.senden("/api/channel/all/cmd", json: befehl.json)
    }

    // MARK: Messfahrt

    public func messfahrtStarten(kanal: Int) async throws {
        try await verbindung.senden("/api/calib/\(kanal)/start")
    }

    public func messfahrtAbbrechen() async throws {
        try await verbindung.senden("/api/calib/abort")
    }

    public func messfahrtUebernehmen() async throws {
        try await verbindung.senden("/api/calib/accept")
    }

    public func messfahrtVerwerfen() async throws {
        try await verbindung.senden("/api/calib/discard")
    }

    // MARK: Schutzfahrt

    public func schutzfahrt(_ befehl: Schutzfahrtbefehl) async throws {
        try await verbindung.senden("/api/system/\(befehl.rawValue)")
    }
}

/// Befehl an einen Kanal (`POST /api/channel/{n}/cmd`).
public enum Kanalbefehl: Sendable, Equatable {
    /// Ganz auf; der Kreis geht in den Handbetrieb.
    case auf
    /// Ganz zu; der Kreis geht in den Handbetrieb.
    case zu
    /// Anhalten; der Kreis geht in den Handbetrieb.
    case anhalten
    /// Zurück an die Regelung
    case regeln
    /// Zwischenstellung 0…1 ohne Handbetrieb; der nächste Regeldurchlauf kann sie verwerfen.
    case stellung(Double)

    var json: JSONWert {
        switch self {
        case .auf: ["cmd": "open"]
        case .zu: ["cmd": "close"]
        case .anhalten: ["cmd": "stop"]
        case .regeln: ["cmd": "auto"]
        case .stellung(let s): ["cmd": "position", "position": .zahl(min(max(s, 0), 1))]
        }
    }
}

public enum Schutzfahrtbefehl: String, Sendable, CaseIterable {
    /// Kreise, die sich seit der letzten Fahrt nicht bewegt haben
    case jetzt = "seize"
    /// alle Kreise, auch die zwischenzeitlich bewegten
    case alle = "seize-all"
    /// offene Kreise streichen
    case abbrechen = "seize-abort"
}
