import Foundation

/// Zugang zu einem Heizungsgerät am Kessel oder am Pufferspeicher.
public struct Heizgeraet: Geraeteclient {
    public static let art = Geraeteart.heizung
    public let verbindung: Geraeteverbindung

    public init(adresse: URL, sitzung: URLSession = .geraete) {
        verbindung = Geraeteverbindung(basis: adresse, sitzung: sitzung)
    }

    /// Das Heizungsgerät sendet kein ETag; jede Abfrage liefert den vollständigen Zustand.
    public func zustand() async throws -> Abfrage<Heizgeraetezustand> {
        try await verbindung.abfragen("/api/state", etagNutzen: false)
    }

    public func messwerte() async throws -> Messwertantwort {
        try await verbindung.holen("/api/measurements")
    }

    /// `schritt` zählt Rasterschritte zu zwei Minuten; die Vorgabe 5 ergibt einen Punkt je zehn
    /// Minuten. Das Gerät liefert höchstens 1440 Punkte.
    public func verlauf(schritt: Int = 5, hoechstens: Int = 288) async throws -> Verlaufsantwort {
        try await verbindung.holen("/api/history?step=\(schritt)&max=\(hoechstens)")
    }

    public func ladungsprotokoll() async throws -> [Ladungseintrag] {
        try Protokolle.ladungen(try await csv("/api/log/charges"))
    }

    public func tagesprotokoll() async throws -> [Tageseintrag] {
        try Protokolle.tage(try await csv("/api/log/days"))
    }

    /// Ladungsaufzeichnung als CSV im Fünf-Sekunden-Raster
    public func aufzeichnung() async throws -> String {
        try await csv("/api/record")
    }

    public func aufzeichnung(_ befehl: Aufzeichnungsbefehl) async throws {
        try await verbindung.senden("/api/record/\(befehl.rawValue)")
    }

    public func heizkreis(_ id: Int, _ modus: Pumpenmodus) async throws {
        try await verbindung.senden("/api/circuit/\(id)/mode", json: ["mode": .text(modus.rawValue)])
    }

    public func kesselkreispumpe(_ modus: Pumpenmodus) async throws {
        try await verbindung.senden("/api/boilerpump/\(modus.rawValue)")
    }

    public func fuehlerNeuSuchen() async throws {
        try await verbindung.senden("/api/probes/rescan")
    }

    /// Verwirft Ladungs- und Tagesprotokoll unwiderruflich.
    public func protokolleLoeschen() async throws {
        try await verbindung.senden("/api/system/clear-logs")
    }

    private func csv(_ pfad: String) async throws -> String {
        String(decoding: try await verbindung.holenRoh(pfad, zeitlimit: 15), as: UTF8.self)
    }
}

/// Betriebsart einer Pumpe
public enum Pumpenmodus: String, Sendable, CaseIterable {
    case automatik = "auto"
    case ein
    case aus
}

public enum Aufzeichnungsbefehl: String, Sendable, CaseIterable {
    /// bei der nächsten Ladung aufzeichnen
    case scharfSchalten = "arm"
    case starten = "start"
    /// beenden, im scharfen Zustand abbrechen
    case beenden = "stop"
    /// verwerfen und Speicher freigeben
    case verwerfen = "discard"
}
