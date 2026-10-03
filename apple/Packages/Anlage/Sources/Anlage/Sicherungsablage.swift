import CryptoKit
import Foundation

/// Eine abgelegte Sicherung.
public struct Sicherungseintrag: Sendable, Hashable, Identifiable {
    public var id: String { datei.lastPathComponent }
    public var geraet: String
    public var datum: Date
    public var datei: URL
}

/// Sicherungen der Gerätekonfiguration, verschlüsselt abgelegt.
///
/// Eine Sicherung aus `GET /api/config/backup` enthält die WLAN-Zugangsdaten im Klartext. Die
/// App legt sie deshalb nur mit AES-GCM verschlüsselt ab; der Schlüssel liegt im Schlüsselbund.
/// Jede Datei ist zusätzlich an ihre Gerätekennung gebunden: Eine umbenannte Sicherung lässt
/// sich einem anderen Gerät nicht unterschieben. An die KI geht keine Sicherung.
public final class Sicherungsablage: Sendable {
    public let verzeichnis: URL
    private let schluessel: SymmetricKey

    public init(verzeichnis: URL, schluessel: SymmetricKey) {
        self.verzeichnis = verzeichnis
        self.schluessel = schluessel
        try? FileManager.default.createDirectory(at: verzeichnis, withIntermediateDirectories: true)
    }

    @discardableResult
    public func sichern(_ daten: Data, geraet: String, datum: Date = .now) throws -> Sicherungseintrag {
        let box = try AES.GCM.seal(daten, using: schluessel, authenticating: Data(geraet.utf8))
        guard let verschluesselt = box.combined else {
            throw CocoaError(.fileWriteUnknown)
        }
        let datei = verzeichnis.appending(path: "\(geraet)_\(Self.stempel.string(from: datum)).sicherung")
        // Dieselbe Schutzklasse wie der Schlüssel im Schlüsselbund (nach erster Entsperrung).
        // „complete“ brächte nichts hinzu, verhinderte aber das Sichern bei gesperrtem Gerät.
        try verschluesselt.write(to: datei, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
        return Sicherungseintrag(geraet: geraet, datum: datum, datei: datei)
    }

    /// Neueste zuerst
    public func eintraege(geraet: String? = nil) throws -> [Sicherungseintrag] {
        let dateien = try FileManager.default.contentsOfDirectory(at: verzeichnis, includingPropertiesForKeys: nil)
        return dateien.compactMap { datei -> Sicherungseintrag? in
            guard datei.pathExtension == "sicherung" else { return nil }
            let name = datei.deletingPathExtension().lastPathComponent
            guard let trenner = name.lastIndex(of: "_") else { return nil }
            let id = String(name[..<trenner])
            guard geraet == nil || geraet == id,
                  let datum = Self.stempel.date(from: String(name[name.index(after: trenner)...])) else { return nil }
            return Sicherungseintrag(geraet: id, datum: datum, datei: datei)
        }
        .sorted { $0.datum > $1.datum }
    }

    public func lesen(_ eintrag: Sicherungseintrag) throws -> Data {
        let box = try AES.GCM.SealedBox(combined: try Data(contentsOf: eintrag.datei))
        return try AES.GCM.open(box, using: schluessel, authenticating: Data(eintrag.geraet.utf8))
    }

    /// Behält je Gerät die neuesten `behalten` Sicherungen.
    public func aufraeumen(behalten: Int = 10) throws {
        let alle = try eintraege()
        for geraet in Set(alle.map(\.geraet)) {
            for alt in alle.filter({ $0.geraet == geraet }).dropFirst(behalten) {
                try FileManager.default.removeItem(at: alt.datei)
            }
        }
    }

    private static var stempel: DateFormatter {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyyMMdd-HHmmss"
        return f
    }
}
