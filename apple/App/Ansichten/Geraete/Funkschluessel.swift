import SwiftUI
import Anlage

/// Das Thermometer, dessen Schlüssel das Blatt betrifft
struct Schluesselziel: Identifiable {
    var id: String { mac }
    /// Leer: Die Adresse wird im Blatt eingegeben, etwa für ein Thermometer außer Reichweite.
    let mac: String
    let name: String
    /// Stand laut Gerät; `nil`, wenn das Thermometer gerade nicht empfangen wird
    let zustand: Schluesselzustand?
    /// Am Gerät liegt schon ein Schlüssel für diese Adresse.
    let hinterlegt: Bool
}

/// Das Gerät, das den Schlüssel bekommt: ein Verteiler oder der Leitstand
enum Schluesselort {
    case verteiler(Geraet.ID)
    case leitstand(String)

    var nominativ: String {
        switch self {
        case .verteiler: "der Verteiler"
        case .leitstand: "der Leitstand"
        }
    }

    /// Wo der Schlüssel danach liegt; der Leitstand hat keine Sicherung.
    var ablage: String {
        switch self {
        case .verteiler: "auf dem Verteiler und in dessen Sicherungen"
        case .leitstand: "auf dem Leitstand"
        }
    }

    /// Was ohne Schlüssel fehlt
    var folge: String {
        switch self {
        case .verteiler: "ein Raum, dem es zugeordnet ist, wird dann nicht mehr geregelt"
        case .leitstand: "ist es als Außenfühler zugeordnet, fehlt den Heizungsgeräten die Außentemperatur"
        }
    }
}

/// Schlüssel eines verschlüsselt sendenden Thermometers eingeben, ersetzen oder entfernen. Die App
/// behält ihn nicht: Er geht an das Gerät und steht danach nur dort.
struct FunkschluesselBlatt: View {
    @Environment(AppModell.self) private var modell
    @Environment(\.dismiss) private var schliessen
    let ort: Schluesselort
    let ziel: Schluesselziel
    @State private var text = ""
    @State private var adresseText = ""
    @State private var laeuft = false
    @State private var fehler: String?
    @State private var entfernenFragen = false

    /// Die Adresse, zu der der Schlüssel gehört: die des Thermometers oder die eingegebene
    private var adresse: String? {
        ziel.mac.isEmpty ? Funkschluessel.adresse(adresseText) : ziel.mac
    }

    var body: some View {
        let schluessel = Funkschluessel.normalisiert(text)
        NavigationStack {
            Form {
                if ziel.mac.isEmpty {
                    Section {
                        TextField("C0:FF:EE:12:34:56", text: $adresseText)
                            .font(.body.monospaced())
                            .autocorrectionDisabled()
                            #if os(iOS)
                            .textInputAutocapitalization(.characters)
                            .keyboardType(.asciiCapable)
                            #endif
                    } header: {
                        Text("Adresse")
                    } footer: {
                        Text("Die Adresse zeigt die camperSense-App neben dem Schlüssel. So lässt sich der Schlüssel auch für ein Thermometer hinterlegen, das \(ort.nominativ) gerade nicht empfängt.")
                    }
                } else {
                    Section {
                        LabeledContent("Adresse") {
                            Text(ziel.mac).font(.body.monospaced())
                        }
                        if let z = ziel.zustand {
                            LabeledContent("Stand", value: z.text)
                        }
                    }
                }
                Section {
                    TextField("32 Zeichen aus 0–9 und a–f", text: $text, axis: .vertical)
                        .font(.body.monospaced())
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .textInputAutocapitalization(.never)
                        .keyboardType(.asciiCapable)
                        #endif
                        .onSubmit { if let schluessel, adresse != nil { speichern(schluessel) } }
                    if !text.isEmpty {
                        Text(Funkschluessel.stand(text))
                            .font(.caption)
                            .foregroundStyle(schluessel == nil ? Color.secondary : Farbe.gut)
                    }
                } header: {
                    Text(ziel.hinterlegt ? "Neuer Schlüssel" : "Schlüssel")
                } footer: {
                    Text("Den Schlüssel zeigt die camperSense-App beim jeweiligen Sensor an. Leerzeichen und Bindestriche dürfen mit eingegeben werden. Gespeichert wird er \(ort.ablage); angezeigt wird er danach nicht mehr.")
                }
                if let fehler {
                    Section {
                        Label(fehler, systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(Farbe.stoerung)
                    }
                }
                if ziel.hinterlegt {
                    Section {
                        Button("Schlüssel entfernen", systemImage: "trash", role: .destructive) {
                            entfernenFragen = true
                        }
                        .disabled(laeuft)
                    } footer: {
                        Text("Das Thermometer liefert danach keine Werte mehr; \(ort.folge).")
                    }
                }
            }
            .formularStil()
            .navigationTitle("Schlüssel")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Abbrechen") { schliessen() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Speichern") {
                        if let schluessel { speichern(schluessel) }
                    }
                    .disabled(schluessel == nil || adresse == nil || laeuft)
                }
            }
            .confirmationDialog("Schlüssel entfernen?", isPresented: $entfernenFragen, titleVisibility: .visible) {
                Button("Entfernen", role: .destructive) { speichern(nil) }
            }
        }
        #if os(macOS)
        .frame(width: 460, height: 440)
        #endif
    }

    private func speichern(_ schluessel: String?) {
        guard let adresse else { return }
        laeuft = true
        fehler = nil
        Task {
            defer { laeuft = false }
            do {
                switch ort {
                case .verteiler(let geraet):
                    try await modell.funkschluesselSetzen(geraet, mac: adresse, schluessel: schluessel)
                case .leitstand(let id):
                    try await modell.leitstandSchluesselSetzen(id, mac: adresse, schluessel: schluessel)
                }
                modell.melden(schluessel == nil
                              ? "Schlüssel entfernt."
                              : "Schlüssel hinterlegt. Mit dem nächsten Rundruf des Thermometers erscheinen seine Werte.")
                schliessen()
            } catch {
                fehler = error.localizedDescription
            }
        }
    }
}
