import Foundation

/// Firmwareabbild für `POST /api/ota`.
///
/// Vor dem Übertragen liest die App den App-Deskriptor (`esp_app_desc_t`) aus der Datei: Er steht
/// hinter dem Bildkopf (24 Byte) und dem Kopf des ersten Segments (8 Byte). Der Projektname
/// verhindert, dass ein Verteiler das Abbild des Heizungsgeräts erhält oder umgekehrt.
public struct Firmwaredatei: Sendable {
    public let daten: Data
    public let projekt: String
    public let version: String

    static let deskriptor = 32
    static let deskriptorKennung: UInt32 = 0xABCD_5432

    public init(daten: Data) throws {
        let d = Self.deskriptor
        guard daten.count >= d + 80, daten[daten.startIndex] == 0xE9 else {
            throw Geraetefehler.unzulaessig("Die Datei ist kein Firmwareabbild für den ESP32.")
        }
        let kennung = daten.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: d, as: UInt32.self) }
        guard UInt32(littleEndian: kennung) == Self.deskriptorKennung else {
            throw Geraetefehler.unzulaessig("Die Datei enthält keinen App-Deskriptor.")
        }
        self.daten = daten
        version = Self.text(daten, ab: d + 16, laenge: 32)
        projekt = Self.text(daten, ab: d + 48, laenge: 32)
    }

    /// Nullterminierte Zeichenkette fester Länge.
    private static func text(_ daten: Data, ab: Int, laenge: Int) -> String {
        let start = daten.startIndex + ab
        let bytes = daten[start..<(start + laenge)].prefix { $0 != 0 }
        return String(decoding: bytes, as: UTF8.self)
    }
}
