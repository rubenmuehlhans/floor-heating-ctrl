import Foundation

/// Was beide Gerätearten gemeinsam haben: Konfiguration, Sicherung, Netz, Neustart, Firmware.
public protocol Geraeteclient: Sendable {
    static var art: Geraeteart { get }
    var verbindung: Geraeteverbindung { get }
}

extension Geraeteclient {
    public var adresse: URL { verbindung.basis }

    /// Konfiguration ohne Kennwörter; diese erscheinen nur als `pass_set`.
    public func konfiguration() async throws -> JSONWert {
        try await verbindung.holen("/api/config")
    }

    /// Teilangabe nach den Regeln des Zusammenführens (Handbuch, „Konfiguration im Einzelnen“):
    /// Gruppen ändern nur, was genannt ist; Listen ersetzen die gespeicherte Liste; ein leeres
    /// Kennwort löscht das Kennwort.
    public func konfigurationAendern(_ teil: JSONWert) async throws {
        try await verbindung.senden("/api/config", methode: "PUT", json: teil)
        await verbindung.etagsVergessen()
    }

    /// Sicherungsdatei, mit Zugangsdaten im Klartext. Sie gehört verschlüsselt abgelegt und nie
    /// an die KI.
    public func sicherung() async throws -> Data {
        try await verbindung.holenRoh("/api/config/backup", zeitlimit: 15)
    }

    /// Baut von der Werksvorgabe aus auf; der Netzzugang bleibt.
    public func sicherungEinspielen(_ daten: Data) async throws {
        try await verbindung.senden("/api/config/restore", daten: daten, inhaltstyp: "application/json", zeitlimit: 15)
        await verbindung.etagsVergessen()
    }

    public func nachbarn() async throws -> [Nachbarliste.Nachbar] {
        try await verbindung.holen("/api/peers", als: Nachbarliste.self).nachbarn ?? []
    }

    /// Während des Suchlaufs ist der eigene Zugangspunkt kurz nicht erreichbar.
    public func netzsucheStarten() async throws {
        try await verbindung.senden("/api/wifi/scan")
    }

    public func netzsuche() async throws -> Netzsuche {
        try await verbindung.holen("/api/wifi/scan")
    }

    public func neustart() async throws {
        try await verbindung.senden("/api/system/restart")
        await verbindung.etagsVergessen()
    }

    /// Setzt alles auf die Werksvorgabe, auch den Netzzugang.
    public func werksvorgabe() async throws {
        try await verbindung.senden("/api/system/factory")
    }

    public func firmwareEinspielen(_ datei: Firmwaredatei) async throws {
        guard datei.projekt == Self.art.projekt else {
            throw Geraetefehler.falscheFirmware(erwartet: Self.art.projekt, gefunden: datei.projekt)
        }
        try await verbindung.senden("/api/ota", daten: datei.daten, zeitlimit: 180)
    }
}
