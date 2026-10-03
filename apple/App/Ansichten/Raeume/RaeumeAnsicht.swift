import SwiftUI
import Charts
import Anlage

struct RaeumeAnsicht: View {
    @Environment(AppModell.self) private var modell

    private let spalten = [GridItem(.adaptive(minimum: 320), spacing: 14, alignment: .top)]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                // Eine Platine ohne Räume, die einen Außenfühler trägt, gehört nicht hierher:
                // Sie empfängt nur ein Funkthermometer, geregelt wird an ihr nichts.
                ForEach(modell.anlage.etagen.filter { !($0.raeume.isEmpty && $0.traegtAussenfuehler) }) { etage in
                    Abschnitt(titel: etage.name, zusatz: etage.bedarf ? "Wärmebedarf" : "kein Bedarf")
                    Verteilerbalken(etage: etage)
                    if etage.raeume.isEmpty {
                        Label("Noch kein Raum eingerichtet. Räume legen Sie unter Geräte › \(etage.name) › Räume und Kanäle an.",
                              systemImage: "square.grid.2x2")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                    }
                    LazyVGrid(columns: spalten, spacing: 14) {
                        ForEach(etage.raeume) { raum in
                            Raumkarte(raum: raum, etage: etage)
                        }
                    }
                }
                if modell.anlage.etagen.isEmpty {
                    ContentUnavailableView("Kein Verteiler eingebunden", systemImage: "square.split.2x2",
                                           description: Text("Verteiler binden Sie unter Geräte › Gerät hinzufügen ein."))
                }
            }
            .padding()
            .frame(maxWidth: 1100)
            .frame(maxWidth: .infinity)
        }
        .background(Farbe.grund)
        .navigationTitle("Räume")
        .fragenKnopf("Räume")
    }
}

/// Alle elf Kanäle in der Reihenfolge am Verteilerbalken, nach Räumen gruppiert; die Füllung
/// zeigt die Ventilstellung. Wie die Karte „Verteiler“ der Weboberfläche.
struct Verteilerbalken: View {
    let etage: Etage

    private struct Gruppe: Identifiable {
        var id: Int { kanaele.first?.nummer ?? 0 }
        var name: String?
        var kanaele: [Kanal]
    }

    private var gruppen: [Gruppe] {
        var liste: [Gruppe] = []
        for k in etage.kanaele.sorted(by: { $0.nummer < $1.nummer }) {
            if let letzte = liste.last, letzte.name == k.raum {
                liste[liste.count - 1].kanaele.append(k)
            } else {
                liste.append(Gruppe(name: k.raum, kanaele: [k]))
            }
        }
        return liste
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Kartenkopf(titel: "Verteiler \(etage.name)", erklaerung: "Die Füllung zeigt die Ventilstellung, gruppiert nach Räumen.")
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(alignment: .top, spacing: 14) {
                    ForEach(gruppen) { gruppe in
                        VStack(spacing: 6) {
                            HStack(spacing: 4) {
                                ForEach(gruppe.kanaele) { k in
                                    VStack(spacing: 4) {
                                        Ventilsaeule(kanal: k)
                                        Text("\(k.nummer)")
                                            .font(Schrift.datenKlein)
                                            .foregroundStyle(Farbe.gedaempft)
                                    }
                                }
                            }
                            Rectangle().fill(Farbe.linieStark).frame(height: 1)
                            Text(gruppe.name ?? "ohne Raum")
                                .font(.caption)
                                .foregroundStyle(gruppe.name == nil ? Farbe.blass : Farbe.gedaempft)
                                .lineLimit(1)
                                .fixedSize()
                        }
                    }
                }
                .padding(.bottom, 2)
            }
        }
        .padding(16)
        .karte()
    }
}

private struct Ventilsaeule: View {
    let kanal: Kanal

    var body: some View {
        let form = RoundedRectangle(cornerRadius: 6)
        ZStack(alignment: .bottom) {
            form.fill(Farbe.vertieft)
            if kanal.raum != nil {
                Rectangle()
                    .fill(kanal.handbetrieb ? Farbe.warnung : Farbe.waerme)
                    .frame(height: 74 * kanal.stellung)
            }
            if kanal.raum == nil {
                form.strokeBorder(Farbe.linieStark, style: StrokeStyle(lineWidth: 1, dash: [3, 2]))
            } else {
                form.strokeBorder(kanal.bewegung != .steht ? Farbe.waerme : Farbe.linieStark, lineWidth: kanal.bewegung != .steht ? 2 : 1)
            }
            if kanal.handbetrieb {
                Image(systemName: "hand.raised.fill")
                    .font(.system(size: 9))
                    .foregroundStyle(.white)
                    .padding(.bottom, 5)
            }
        }
        .clipShape(form)
        .frame(width: 22, height: 74)
        .accessibilityElement()
        .accessibilityLabel("Kanal \(kanal.nummer), \(kanal.raum ?? "frei")")
        .accessibilityValue(Format.prozent(kanal.stellung))
    }
}

/// Raumkarte wie in der Weboberfläche: Messwert, Abstand zum Soll, Stellrad, Kanäle.
struct Raumkarte: View {
    @Environment(AppModell.self) private var modell
    let raum: Raum
    let etage: Etage

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 10) {
                NavigationLink(value: Ziel.raum(raum.id)) {
                    HStack(alignment: .firstTextBaseline) {
                        Text(raum.name).font(Schrift.kartentitel)
                        Image(systemName: "chevron.right")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(Farbe.blass)
                        Spacer()
                        Zustandsmarke(text: raum.zustand.text, symbol: raum.zustand.symbol, farbe: raum.zustand.farbe)
                    }
                    .contentShape(.rect)
                }
                .buttonStyle(.plain)

                HStack(alignment: .firstTextBaseline) {
                    Messzahl(zahl: raum.ist.map { Format.zahl($0) } ?? "–,–", farbe: raum.ist == nil ? Farbe.blass : Farbe.tinte)
                    Spacer()
                    if raum.zustand == .heizt || raum.zustand == .sollErreicht {
                        Text(raum.abstandText)
                            .font(Schrift.daten)
                            .foregroundStyle(raum.abstandFarbe)
                    }
                }
                Text(messzeile)
                    .font(.caption)
                    .foregroundStyle(Farbe.gedaempft)

                if raum.thermometer != nil {
                    SollwertStellrad(wert: modell.soll(raum.id), gedimmt: raum.betriebsart == .aus)
                        .padding(.top, 4)
                }
                HStack {
                    Spacer()
                    Button(raum.betriebsart == .heizen ? "Ausschalten" : "Einschalten") {
                        modell.betriebsartUmschalten(raum.id)
                    }
                    .buttonStyle(.rahmen)
                }
            }
            .padding(16)

            Divider().overlay(Farbe.linie)
            VStack(spacing: 6) {
                ForEach(raum.kanaele, id: \.self) { nummer in
                    let k = etage.kanaele.first { $0.nummer == nummer }
                    HStack(spacing: 10) {
                        Text("Kreis \(nummer)")
                            .font(Schrift.datenKlein)
                            .foregroundStyle(Farbe.gedaempft)
                            .lineLimit(1)
                            .fixedSize()
                            .frame(minWidth: 66, alignment: .leading)
                        Fortschritt(anteil: k?.stellung ?? 0, farbe: k?.handbetrieb == true ? Farbe.warnung : Farbe.waerme)
                        Text(Format.prozent(k?.stellung ?? 0))
                            .font(Schrift.datenKlein)
                            .foregroundStyle(Farbe.gedaempft)
                            .frame(width: 44, alignment: .trailing)
                    }
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)

            Divider().overlay(Farbe.linie)
            Text("Zielstellung \(Format.prozent(raum.zielstellung)) · nächste Prüfung in \(raum.naechstePruefung) s")
                .font(Schrift.datenKlein)
                .foregroundStyle(Farbe.gedaempft)
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
        }
        .karte()
    }

    private var messzeile: String {
        guard raum.thermometer != nil else { return "Ohne zugeordnetes Thermometer wird nicht geregelt." }
        var teile: [String] = []
        if let f = raum.feuchte { teile.append("Luftfeuchte \(Format.zahl(f, stellen: 0)) %") }
        if let b = raum.batterie { teile.append("Batterie \(b) %") }
        if let a = raum.messwertAlter { teile.append("Messwert \(a) s alt") }
        return teile.joined(separator: ", ")
    }
}

// MARK: - Raumdetail

struct RaumDetailAnsicht: View {
    @Environment(AppModell.self) private var modell
    let raumID: Raum.ID

    var body: some View {
        if let raum = modell.anlage.raum(raumID), let etage = modell.anlage.etagen.first(where: { $0.raeume.contains { $0.id == raumID } }) {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack(alignment: .firstTextBaseline) {
                            Kicker(raum.etage)
                            Spacer()
                            Zustandsmarke(text: raum.zustand.text, symbol: raum.zustand.symbol, farbe: raum.zustand.farbe)
                        }
                        HStack(alignment: .firstTextBaseline) {
                            Messzahl(zahl: raum.ist.map { Format.zahl($0) } ?? "–,–",
                                     font: .system(size: 56, weight: .bold, design: .monospaced),
                                     farbe: raum.ist == nil ? Farbe.blass : Farbe.tinte)
                            Spacer()
                            Text(raum.abstandText).font(Schrift.daten).foregroundStyle(raum.abstandFarbe)
                        }
                        if raum.thermometer != nil {
                            SollwertStellrad(wert: modell.soll(raum.id), hinweis: "Wird an den Verteiler gesendet, sobald das Rad steht.")
                                .padding(.top, 6)
                        }
                        HStack {
                            Button {
                                modell.raumPruefen(raum.id)
                            } label: {
                                Label("Jetzt prüfen", systemImage: "arrow.clockwise")
                            }
                            .buttonStyle(.rahmen)
                            Spacer()
                            Button(raum.betriebsart == .heizen ? "Ausschalten" : "Einschalten") {
                                modell.betriebsartUmschalten(raum.id)
                            }
                            .buttonStyle(.rahmen)
                        }
                    }
                    .padding(16)
                    .karte()

                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 140), spacing: 10)], spacing: 10) {
                        Kennzahl(titel: "Luftfeuchte", wert: raum.feuchte.map { Format.zahl($0, stellen: 0) } ?? "–", einheit: "%")
                        Kennzahl(titel: "Batterie", wert: raum.batterie.map { "\($0)" } ?? "–", einheit: "%",
                                 farbe: (raum.batterie ?? 100) < 10 ? Farbe.stoerung : Farbe.tinte)
                        Kennzahl(titel: "Messwert", wert: raum.messwertAlter.map { "\($0)" } ?? "–", einheit: "s alt")
                        Kennzahl(titel: "Zielstellung", wert: Format.prozentzahl(raum.zielstellung), einheit: "%", farbe: Farbe.waerme)
                    }

                    VStack(alignment: .leading, spacing: 10) {
                        Kartenkopf(titel: "Ventile", erklaerung: "Am Sollwert stehen die Ventile halb offen, \(Format.kelvin(raum.regelung.proportionalband)) darunter ganz offen, ebenso viel darüber ganz zu.")
                        ForEach(raum.kanaele, id: \.self) { nummer in
                            if let k = etage.kanaele.first(where: { $0.nummer == nummer }) {
                                NavigationLink(value: Ziel.kanal(etage: etage.id, nummer: nummer)) {
                                    KanalZeile(kanal: k, zeigeRaum: false)
                                        .contentShape(.rect)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                    .padding(16)
                    .karte()

                    RaumVerlaufKarte(etage: etage, raum: raum)

                    NavigationLink(value: Ziel.einstellungen("raum", geraet: etage.id, eintrag: "\(raum.nummer)")) {
                        HStack {
                            Label("Regelparameter", systemImage: "slider.horizontal.3")
                            Spacer()
                            Image(systemName: "chevron.right").font(.caption).foregroundStyle(Farbe.blass)
                        }
                        .padding(16)
                        .contentShape(.rect)
                        .karte()
                    }
                    .buttonStyle(.plain)
                }
                .padding()
                .frame(maxWidth: 820)
                .frame(maxWidth: .infinity)
            }
            .background(Farbe.grund)
            .navigationTitle(raum.name)
            .fragenKnopf(raum.name)
        } else {
            ContentUnavailableView("Raum nicht gefunden", systemImage: "questionmark.square.dashed")
        }
    }
}
