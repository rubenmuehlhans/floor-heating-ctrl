import SwiftUI

/// Fragt, bevor zum ersten Mal Daten der Anlage an Anthropic gehen. Die App-Store-Richtlinie
/// 5.1.2(i) verlangt eine ausdrückliche Erlaubnis, bevor eine App personenbezogene Daten an eine
/// fremde KI weitergibt; ein Hinweistext allein genügt nicht.
struct ClaudeZustimmungAnsicht: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.dismiss) private var schliessen
    /// Nach der Zustimmung, etwa um die wartende Frage zu senden
    let danach: () -> Void

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Text("Für Antworten mit Claude überträgt die App bei jeder Frage und bei jedem Lagebericht Daten an Anthropic, den Anbieter von Claude mit Sitz in den USA:")
                    VStack(alignment: .leading, spacing: 6) {
                        Punkt("Messwerte, Einstellungen, Befunde und Protokolle der Anlage")
                        Punkt("Namen der Räume und Geräte")
                        Punkt("Ihre Fragen und den bisherigen Verlauf des Gesprächs")
                        Punkt("das Handbuch der Anlage")
                    }
                    Text("Kennwörter, WLAN-Zugangsdaten und die Adressen der Relais überträgt die App nicht.")
                    Text("Die Übertragung läuft über Ihr eigenes Konto bei Anthropic; dort gelten die Bedingungen und die Datenschutzerklärung von Anthropic. Ohne Zustimmung stellt die App keine Anfrage an Claude. Widerrufen können Sie die Zustimmung jederzeit unter Einstellungen der App › KI.")
                    VStack(alignment: .leading, spacing: 6) {
                        Link("Datenschutzerklärung der App", destination: AppModell.datenschutzseite)
                        Link("Datenschutzerklärung von Anthropic", destination: AppModell.datenschutzAnthropic)
                    }
                    .font(.callout)
                }
                .fixedSize(horizontal: false, vertical: true)
                .padding()
            }
            .safeAreaInset(edge: .bottom) {
                VStack(spacing: 8) {
                    Button {
                        modell.claudeZustimmen()
                        schliessen()
                        danach()
                    } label: {
                        Text("Zustimmen").frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    Button {
                        schliessen()
                    } label: {
                        Text("Nicht zustimmen").frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }
                .padding()
                .background(.bar)
            }
            .navigationTitle("Daten an Anthropic")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
        }
        #if os(macOS)
        .frame(width: 460, height: 520)
        #endif
    }

    private struct Punkt: View {
        let text: String
        init(_ text: String) { self.text = text }

        var body: some View {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text("–")
                Text(text)
            }
        }
    }
}

extension View {
    /// Das Blatt zur Zustimmung; `danach` läuft nur, wenn zugestimmt wurde.
    func claudeZustimmung(_ zeigen: Binding<Bool>, danach: @escaping () -> Void = {}) -> some View {
        sheet(isPresented: zeigen) {
            ClaudeZustimmungAnsicht(danach: danach)
        }
    }
}
