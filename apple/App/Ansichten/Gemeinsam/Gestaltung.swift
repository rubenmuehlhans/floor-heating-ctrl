import SwiftUI

// Gestaltung nach der Weboberfläche der Geräte (`www/stil.css`): kühles Grüngrau als Grund,
// Petrol für die Bedienung, der Wärmeton für heiße Medien und „läuft“, Blau für Rückläufe.
// Messwerte stehen in Festbreitenschrift, Beschriftungen in kleinen gesperrten Versalien.

enum Farbe {
    static let waerme = Color("Waerme")
    static let kaelte = Color("Kaelte")
    static let gut = Color("Gut")
    static let warnung = Color("Warnung")
    static let stoerung = Color("Stoerung")

    static let grund = Color("Grund")
    static let flaeche = Color("Flaeche")
    static let vertieft = Color("Vertieft")
    static let tinte = Color("Tinte")
    static let gedaempft = Color("Gedaempft")
    static let blass = Color("Blass")
    static let linie = Color("Linie")
    static let linieStark = Color("LinieStark")
    static let akzentWeich = Color("AkzentWeich")
    static let waermeWeich = Color("WaermeWeich")
    static let kaelteWeich = Color("KaelteWeich")

    static var seite: Color { grund }
    static var karte: Color { flaeche }
}

enum Schrift {
    /// Große Messwerte, etwa die Raumtemperatur.
    static let messwertGross = Font.system(.largeTitle, design: .monospaced, weight: .bold)
    static let messwert = Font.system(.title2, design: .monospaced, weight: .semibold)
    static let wert = Font.system(.body, design: .monospaced, weight: .semibold)
    static let daten = Font.system(.footnote, design: .monospaced)
    static let datenKlein = Font.system(.caption, design: .monospaced)
    static let kicker = Font.system(.caption2, weight: .semibold)
    static let kartentitel = Font.system(.headline)
    static let erklaerung = Font.system(.subheadline)
}

// MARK: - Karte

struct KartenStil: ViewModifier {
    var betont = false

    func body(content: Content) -> some View {
        content
            .frame(maxWidth: .infinity, alignment: .leading)
            // Die Schatten gehören an die Form im Hintergrund. An der ganzen Karte zeichnet
            // SwiftUI sie unter jedes einzelne Element, jeden Text und jeden Strich des Stellrads;
            // auf dem iPhone ruckelt das Scrollen dann an den vielen Weichzeichnungen.
            .background {
                RoundedRectangle(cornerRadius: 12)
                    .fill(Farbe.flaeche)
                    .shadow(color: .black.opacity(0.05), radius: 1, y: 1)
                    .shadow(color: .black.opacity(0.08), radius: 12, y: 8)
            }
            .overlay {
                RoundedRectangle(cornerRadius: 12)
                    .strokeBorder(betont ? Color.accentColor.opacity(0.45) : Farbe.linie, lineWidth: 1)
            }
    }
}

extension View {
    func karte(betont: Bool = false) -> some View {
        modifier(KartenStil(betont: betont))
    }

    /// Formulare auf dem Grund der Anlage statt im Grau der Systemeinstellungen.
    func formularStil() -> some View {
        formStyle(.grouped)
            .scrollContentBackground(.hidden)
            .background(Farbe.grund)
    }
}

/// Kopf einer Karte: Titel und ein erklärender Satz, wie in der Weboberfläche.
struct Kartenkopf: View {
    let titel: String
    var erklaerung: String? = nil
    var symbol: String? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let symbol {
                Label(titel, systemImage: symbol).font(Schrift.kartentitel)
            } else {
                Text(titel).font(Schrift.kartentitel)
            }
            if let erklaerung {
                Text(erklaerung)
                    .font(Schrift.erklaerung)
                    .foregroundStyle(Farbe.gedaempft)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }
}

/// Kleine gesperrte Versalien über einem Wert: „VORLAUF", „FÜLLSTAND".
struct Kicker: View {
    let text: String
    var farbe: Color = Farbe.gedaempft

    init(_ text: String, farbe: Color = Farbe.gedaempft) {
        self.text = text
        self.farbe = farbe
    }

    var body: some View {
        Text(text)
            .font(Schrift.kicker)
            .textCase(.uppercase)
            .tracking(1.1)
            .foregroundStyle(farbe)
            .lineLimit(1)
    }
}

/// Großer Messwert mit kleiner Einheit daneben: „20,5 °C".
struct Messzahl: View {
    let zahl: String
    var einheit: String = "°C"
    var font: Font = Schrift.messwertGross
    var farbe: Color = Farbe.tinte

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 4) {
            Text(zahl)
                .font(font)
                .foregroundStyle(farbe)
                .contentTransition(.numericText())
            if !einheit.isEmpty {
                Text(einheit)
                    .font(.system(.callout))
                    .foregroundStyle(Farbe.gedaempft)
            }
        }
        .lineLimit(1)
        .minimumScaleFactor(0.7)
    }
}

/// Kachel mit Kopfzeile, Wert und Fußzeile: „FÜLLSTAND 96 % geschätzt aus dem Pufferfühler".
struct Kennzahl: View {
    let titel: String
    let wert: String
    var einheit: String = ""
    var fuss: String? = nil
    var farbe: Color = Farbe.tinte

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Kicker(titel)
            Messzahl(zahl: wert, einheit: einheit, font: Schrift.messwert, farbe: farbe)
            if let fuss {
                Text(fuss)
                    .font(Schrift.datenKlein)
                    .foregroundStyle(Farbe.gedaempft)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .background(Farbe.vertieft, in: .rect(cornerRadius: 8))
        .overlay { RoundedRectangle(cornerRadius: 8).strokeBorder(Farbe.linie, lineWidth: 1) }
    }
}

/// Umrandete Marke mit Symbol, wie „heizt" oder „kein Thermometer" in der Weboberfläche.
struct Zustandsmarke: View {
    let text: String
    let symbol: String
    let farbe: Color
    var gefuellt = true

    var body: some View {
        Label(text, systemImage: symbol)
            .font(.caption.weight(.medium))
            .labelStyle(.titleAndIcon)
            .foregroundStyle(farbe)
            .lineLimit(1)
            .fixedSize()
            .padding(.horizontal, 9)
            .padding(.vertical, 3)
            .background(gefuellt ? farbe.opacity(0.1) : Farbe.flaeche, in: .capsule)
            .overlay { Capsule().strokeBorder(farbe.opacity(0.45), lineWidth: 1) }
    }
}

/// Punkt vor einem Zustand: „● wird geladen seit 40 min".
struct Zustandszeile: View {
    let text: String
    var zusatz: String? = nil
    var farbe: Color = Farbe.waerme

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Circle().fill(farbe).frame(width: 9, height: 9)
            Text(text).font(.body.weight(.semibold))
            if let zusatz {
                Text(zusatz).font(.subheadline).foregroundStyle(Farbe.gedaempft)
            }
        }
    }
}

/// Schmaler Balken für Füllstand und Ventilstellung.
struct Fortschritt: View {
    let anteil: Double
    var farbe: Color = Farbe.waerme
    var hoehe: CGFloat = 6

    var body: some View {
        GeometryReader { g in
            ZStack(alignment: .leading) {
                Capsule().fill(Farbe.vertieft)
                Capsule().strokeBorder(Farbe.linie, lineWidth: 0.5)
                Capsule()
                    .fill(farbe)
                    .frame(width: g.size.width * min(max(anteil, 0), 1))
            }
        }
        .frame(height: hoehe)
        .accessibilityElement()
        .accessibilityValue(Format.prozent(anteil))
    }
}

/// Umrandete Schaltfläche wie in der Weboberfläche; für Bedienelemente innerhalb von Karten.
struct RahmenKnopfStil: ButtonStyle {
    var betont = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.subheadline.weight(.medium))
            .foregroundStyle(betont ? Color.white : Color.accentColor)
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
            .background(betont ? Color.accentColor : Farbe.flaeche, in: .rect(cornerRadius: 8))
            .overlay {
                RoundedRectangle(cornerRadius: 8)
                    .strokeBorder(betont ? Color.accentColor : Farbe.linieStark, lineWidth: 1)
            }
            .opacity(configuration.isPressed ? 0.7 : 1)
    }
}

extension ButtonStyle where Self == RahmenKnopfStil {
    static var rahmen: RahmenKnopfStil { RahmenKnopfStil() }
    static var rahmenBetont: RahmenKnopfStil { RahmenKnopfStil(betont: true) }
}

/// Überschrift eines Abschnitts auf einer Seite aus Karten.
struct Abschnitt: View {
    let titel: String
    var zusatz: String? = nil

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Kicker(titel)
            Spacer()
            if let zusatz {
                Text(zusatz).font(Schrift.datenKlein).foregroundStyle(Farbe.gedaempft)
            }
        }
        .padding(.top, 10)
        .padding(.horizontal, 4)
    }
}
