import Foundation
#if os(iOS)
import NetworkExtension
#endif

/// Beitritt zum Einrichtungs-Zugangspunkt eines Geräts.
///
/// Unter iOS übernimmt das die App selbst (Berechtigung „Hotspot Configuration“): Sie tritt dem
/// ersten Netz bei, dessen Name mit dem Präfix beginnt. Der Mac kennt diese Schnittstelle nicht;
/// dort wechselt man das WLAN von Hand, und die App erkennt das Gerät unter 192.168.4.1.
public enum Zugangspunkt {
    public static var beitrittMoeglich: Bool {
        #if os(iOS)
        true
        #else
        false
        #endif
    }

    #if os(iOS)
    /// Tritt dem Zugangspunkt bei. Die Verbindung gilt nur, solange die App im Vordergrund ist.
    public static func beitreten(praefix: String, kennwort: String) async throws {
        let konfiguration = kennwort.isEmpty
            ? NEHotspotConfiguration(ssidPrefix: praefix)
            : NEHotspotConfiguration(ssidPrefix: praefix, passphrase: kennwort, isWEP: false)
        konfiguration.joinOnce = true
        do {
            try await NEHotspotConfigurationManager.shared.apply(konfiguration)
        } catch let fehler as NSError where fehler.domain == NEHotspotConfigurationErrorDomain {
            switch NEHotspotConfigurationError(rawValue: fehler.code) {
            case .alreadyAssociated?:
                return
            case .userDenied?:
                throw Zugangspunktfehler.abgelehnt
            case .invalidWPAPassphrase?:
                throw Zugangspunktfehler.kennwortUngueltig
            default:
                throw Zugangspunktfehler.nichtGefunden(fehler.localizedDescription)
            }
        }
    }

    /// Verlässt den Zugangspunkt, indem die App ihre eigenen Konfigurationen entfernt. Andere
    /// Netze legt sie nie an.
    public static func verlassen() async {
        let manager = NEHotspotConfigurationManager.shared
        for ssid in await manager.configuredSSIDs() {
            manager.removeConfiguration(forSSID: ssid)
        }
    }
    #endif
}

public enum Zugangspunktfehler: Error, Sendable, Equatable {
    case abgelehnt
    case kennwortUngueltig
    case nichtGefunden(String)
}

extension Zugangspunktfehler: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .abgelehnt:
            "Die Verbindung mit dem Zugangspunkt wurde nicht erlaubt."
        case .kennwortUngueltig:
            "Das Kennwort des Zugangspunkts ist ungültig; es braucht mindestens acht Zeichen."
        case .nichtGefunden:
            "Kein Zugangspunkt eines Geräts in Reichweite. Steht das Gerät im Einrichtungsbetrieb, und ist es nah genug?"
        }
    }
}
