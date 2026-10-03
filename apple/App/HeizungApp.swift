import SwiftUI

@main
struct HeizungApp: App {
    @State private var modell = AppModell()
    #if os(macOS)
    @NSApplicationDelegateAdaptor(AppDelegat.self) private var delegat
    #endif

    var body: some Scene {
        WindowGroup(id: "anlage") {
            Hauptansicht()
                .environment(modell)
                .environment(\.locale, Format.deutsch)
        }
        #if os(iOS)
        .backgroundTask(.appRefresh(AppModell.abrufkennung)) {
            await modell.hintergrundabruf()
        }
        #endif
        #if os(macOS)
        .defaultSize(width: 1320, height: 880)
        .commands {
            CommandMenu("Anlage") {
                Button("Assistent ein- oder ausblenden") {
                    modell.zeigeAssistent.toggle()
                }
                .keyboardShortcut("a", modifiers: [.command, .option])
                Button("Gerät hinzufügen …") {
                    modell.zeigeEinrichtung = true
                }
                .keyboardShortcut("n", modifiers: [.command, .shift])
                Divider()
                VerlaufFensterKnopf()
            }
        }
        #endif

        #if os(macOS)
        // Der Verlauf in eigenem Fenster, etwa neben der Anlage auf einem zweiten Bildschirm
        Window("Verlauf", id: "verlauf") {
            NavigationStack {
                VerlaufAnsicht()
            }
            .environment(modell)
            .environment(\.locale, Format.deutsch)
            .frame(minWidth: 640, minHeight: 520)
        }
        .defaultSize(width: 980, height: 780)

        MenuBarExtra {
            Menueleiste()
                .environment(modell)
                .environment(\.locale, Format.deutsch)
        } label: {
            Label("Heizung", systemImage: modell.menueleistenSymbol)
        }
        .menuBarExtraStyle(.window)

        Settings {
            NavigationStack {
                AppEinstellungenAnsicht()
            }
            .environment(modell)
            .environment(\.locale, Format.deutsch)
            .frame(width: 560, height: 640)
        }
        #endif
    }
}

#if os(macOS)
/// Menübefehl „Verlauf öffnen“; `openWindow` gibt es nur in einer Ansicht.
struct VerlaufFensterKnopf: View {
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Button("Verlauf öffnen") {
            openWindow(id: "verlauf")
        }
        .keyboardShortcut("v", modifiers: [.command, .option])
    }
}
#endif

#if os(macOS)
/// Mit „Anlage dauerhaft aufzeichnen“ läuft die App nach dem letzten Fenster in der Menüleiste
/// weiter, fragt ab und zeichnet auf; sonst endet sie mit ihm.
final class AppDelegat: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        !AppModell.einstellung("aufzeichnenAufDemMac", true)
    }
}
#endif
