import Foundation
import UserNotifications
import Anlage
import Diagnose

/// Lokale Mitteilungen zu Befunden. Was gemeldet wird, entscheidet das Befundgedächtnis; hier
/// werden sie nur zugestellt und wieder zurückgenommen, sobald ein Befund erledigt ist.
@MainActor
final class Mitteilungsdienst: NSObject, UNUserNotificationCenterDelegate {
    private let zentrale = UNUserNotificationCenter.current()
    /// Der Befund hinter einer angetippten Mitteilung
    var geoeffnet: (@MainActor (String) -> Void)?

    override init() {
        super.init()
        zentrale.delegate = self
    }

    func erlaubnis() async -> UNAuthorizationStatus {
        await zentrale.notificationSettings().authorizationStatus
    }

    /// Fragt einmal nach der Erlaubnis; eine Ablehnung lässt sich nur in den Systemeinstellungen
    /// ändern.
    func erlauben() async -> Bool {
        (try? await zentrale.requestAuthorization(options: [.alert, .sound, .badge])) ?? false
    }

    func senden(_ liste: [Befundgedaechtnis.Mitteilung]) {
        guard !liste.isEmpty else { return }
        Task {
            let status = await erlaubnis()
            guard status == .authorized || status == .provisional else { return }
            for m in liste {
                let inhalt = UNMutableNotificationContent()
                inhalt.title = m.titel
                inhalt.subtitle = m.ort
                inhalt.body = m.text
                inhalt.threadIdentifier = "befunde"
                inhalt.userInfo = ["befund": m.id]
                // Kehrt ein Befund kurz nach seiner Erledigung zurück, erscheint er still wieder.
                inhalt.interruptionLevel = m.still || m.schwere == .hinweis ? .passive : .active
                if m.schwere == .stoerung, !m.still { inhalt.sound = .default }
                try? await zentrale.add(UNNotificationRequest(identifier: Self.kennung(m.id), content: inhalt, trigger: nil))
            }
        }
    }

    /// Ein erledigter Befund verschwindet auch aus der Mitteilungszentrale.
    func zuruecknehmen(_ ids: [String]) {
        guard !ids.isEmpty else { return }
        zentrale.removeDeliveredNotifications(withIdentifiers: ids.map(Self.kennung))
    }

    private static func kennung(_ befund: String) -> String { "befund:\(befund)" }

    // MARK: UNUserNotificationCenterDelegate

    /// Auch bei geöffneter App erscheint die Mitteilung; auf dem Mac läuft die App oft ohne
    /// sichtbares Fenster in der Menüleiste.
    nonisolated func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification) async -> UNNotificationPresentationOptions {
        notification.request.content.interruptionLevel == .passive ? [.list] : [.banner, .list, .sound]
    }

    nonisolated func userNotificationCenter(_ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse) async {
        guard let id = response.notification.request.content.userInfo["befund"] as? String else { return }
        await MainActor.run { geoeffnet?(id) }
    }
}
