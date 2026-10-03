import CryptoKit
import Foundation
import Security

/// Geheimnisse der App im Schlüsselbund, etwa der API-Schlüssel von Anthropic.
enum Schluesselbund {
    static let dienst = "de.simplytech.heizung"

    static func lesen(_ konto: String) -> String? {
        var abfrage = basis(konto)
        abfrage[kSecReturnData as String] = true
        abfrage[kSecMatchLimit as String] = kSecMatchLimitOne
        var ergebnis: AnyObject?
        var status = SecItemCopyMatching(abfrage as CFDictionary, &ergebnis)
        if status == errSecMissingEntitlement {
            abfrage[kSecUseDataProtectionKeychain as String] = nil
            status = SecItemCopyMatching(abfrage as CFDictionary, &ergebnis)
        }
        guard status == errSecSuccess, let daten = ergebnis as? Data else { return nil }
        return String(data: daten, encoding: .utf8)
    }

    /// Ein leerer Wert entfernt den Eintrag.
    @discardableResult
    static func speichern(_ wert: String, konto: String) -> Bool {
        loeschen(konto)
        guard !wert.isEmpty else { return true }
        var eintrag = basis(konto)
        eintrag[kSecValueData as String] = Data(wert.utf8)
        eintrag[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        var status = SecItemAdd(eintrag as CFDictionary, nil)
        if status == errSecMissingEntitlement {
            // Ohne Bereitstellungsprofil fehlt dem Mac der Datenschutz-Schlüsselbund; dann gilt
            // der Anmeldeschlüsselbund.
            eintrag[kSecUseDataProtectionKeychain as String] = nil
            status = SecItemAdd(eintrag as CFDictionary, nil)
        }
        return status == errSecSuccess
    }

    static func loeschen(_ konto: String) {
        var abfrage = basis(konto)
        if SecItemDelete(abfrage as CFDictionary) == errSecMissingEntitlement {
            abfrage[kSecUseDataProtectionKeychain as String] = nil
            SecItemDelete(abfrage as CFDictionary)
        }
    }

    private static func basis(_ konto: String) -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: dienst,
            kSecAttrAccount as String: konto,
            kSecUseDataProtectionKeychain as String: true,
        ]
    }
}

extension Schluesselbund {
    /// Schlüssel der verschlüsselten Sicherungen; beim ersten Aufruf erzeugt. Geht er verloren,
    /// lassen sich die abgelegten Sicherungen nicht mehr lesen – sie enthalten ohnehin nur, was
    /// die Geräte selbst noch einmal liefern können.
    static func sicherungsschluessel() -> SymmetricKey? {
        if let text = lesen("sicherungen"), let daten = Data(base64Encoded: text), daten.count == 32 {
            return SymmetricKey(data: daten)
        }
        let neu = SymmetricKey(size: .bits256)
        let text = neu.withUnsafeBytes { Data($0).base64EncodedString() }
        return speichern(text, konto: "sicherungen") ? neu : nil
    }
}
