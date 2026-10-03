import SwiftUI
import Anlage

/// Ziele innerhalb eines Bereichs. Ein gemeinsamer Typ statt einzelner Kennungen, weil Räume und
/// Geräte beide über Zeichenketten gefunden werden.
enum Ziel: Hashable {
    case raum(Raum.ID)
    case befunde
    case befund(Befund.ID)
    case geraet(Geraet.ID)
    /// Der Leitstand, nach seiner Kennung
    case leitstand(String)
    case kanal(etage: Etage.ID, nummer: Int)
    case messfahrt(etage: Etage.ID)
    case sensoren(Geraet.ID)
    case raumverwaltung(etage: Etage.ID)
    case fuehler(Geraet.ID)
    case heizkreis(Int)
    case einstellung(Einstellungsziel)
    case system(Systemseite, geraet: Geraet.ID)
    case verlauf
    case heizkreisverlauf(Int)
    case verbrauchslinie
    case waermepumpencheck
    case protokolle
    case aufzeichnung
    case aenderungen
    case lageberichte
    case appEinstellungen
    case analysepaket
}

extension Ziel {
    static func einstellungen(_ gruppe: String, geraet: String? = nil, eintrag: String? = nil) -> Ziel {
        .einstellung(Einstellungsziel(gruppe: gruppe, geraet: geraet, eintrag: eintrag))
    }
}

/// Ein Formular aus dem Parameterkatalog: Gruppe, Gerät und bei Listen der Eintrag. Fehlt das
/// Gerät, wählt die App es nach der Gruppe, etwa den Kessel für die Brennererkennung.
struct Einstellungsziel: Hashable {
    var gruppe: String
    var geraet: String?
    var eintrag: String?
}

enum Systemseite: String, Hashable, CaseIterable {
    case netzwerk = "Netzwerk"
    case mqtt = "MQTT"
    case betrieb = "Betrieb"
    case tasten = "Tasten am Gerät"
    case geraet = "Bezeichnung"
    case zeit = "Zeit und Termine"
    case bus = "1-Wire-Bus"
    case bedarfsabfrage = "Bedarfsabfrage"
    case firmware = "Firmware"
    case sicherung = "Sicherung"

    var symbol: String {
        switch self {
        case .netzwerk: "wifi"
        case .mqtt: "point.3.connected.trianglepath.dotted"
        case .betrieb: "gearshape"
        case .tasten: "hand.tap"
        case .geraet: "character.cursor.ibeam"
        case .zeit: "clock"
        case .bus: "cable.connector"
        case .bedarfsabfrage: "antenna.radiowaves.left.and.right"
        case .firmware: "shippingbox"
        case .sicherung: "externaldrive"
        }
    }
}

extension View {
    /// Alle Ziele eines Bereichs; einmal je `NavigationStack` angebracht.
    func zielnavigation() -> some View {
        navigationDestination(for: Ziel.self) { ziel in
            ZielAnsicht(ziel: ziel)
        }
    }
}

struct ZielAnsicht: View {
    let ziel: Ziel

    var body: some View {
        switch ziel {
        case .raum(let id): RaumDetailAnsicht(raumID: id)
        case .befunde: BefundeAnsicht()
        case .befund(let id): BefundAnsicht(befundID: id)
        case .geraet(let id): GeraetAnsicht(geraetID: id)
        case .leitstand(let id): LeitstandAnsicht(leitstandID: id)
        case .kanal(let etage, let nummer): KanalAnsicht(etageID: etage, nummer: nummer)
        case .messfahrt(let etage): MessfahrtAnsicht(etageID: etage)
        case .sensoren(let id): SensorenAnsicht(geraetID: id)
        case .raumverwaltung(let etage): RaumverwaltungAnsicht(etageID: etage)
        case .fuehler(let id): FuehlerAnsicht(geraetID: id)
        case .heizkreis(let nummer): HeizkreisAnsicht(nummer: nummer)
        case .einstellung(let ziel): EinstellungsFormular(ziel: ziel)
        case .system(let seite, let geraet): SystemAnsicht(seite: seite, geraetID: geraet)
        case .verlauf: VerlaufAnsicht()
        case .heizkreisverlauf(let nummer): HeizkreisVerlaufAnsicht(nummer: nummer)
        case .verbrauchslinie: VerbrauchslinieAnsicht()
        case .waermepumpencheck: WaermepumpencheckAnsicht()
        case .protokolle: ProtokolleAnsicht()
        case .aufzeichnung: AufzeichnungAnsicht()
        case .aenderungen: AenderungenAnsicht()
        case .lageberichte: LageberichteAnsicht()
        case .appEinstellungen: AppEinstellungenAnsicht()
        case .analysepaket: AnalysepaketAnsicht()
        }
    }
}

/// Schaltfläche „Fragen“ in der Werkzeugleiste: öffnet den Assistenten mit dem Bezug der Seite –
/// auf iPad und Mac als Seitenbereich, auf dem iPhone als Blatt.
/// Blendet den Assistenten als Seitenbereich ein – mit der Seite als Bezug – und wieder aus.
/// Auf dem iPhone erscheint der Seitenbereich als Sheet und wird weggewischt.
struct FragenKnopf: View {
    @Environment(AppModell.self) private var modell
    let bezug: String

    var body: some View {
        Toggle(isOn: Binding(
            get: { modell.zeigeAssistent },
            set: { an in
                if an { modell.fragen(bezug) } else { modell.zeigeAssistent = false }
            }
        )) {
            Label("Fragen", systemImage: "sparkles")
        }
        .toggleStyle(.button)
        .help(modell.zeigeAssistent ? "Assistent ausblenden" : "Den Assistenten zu „\(bezug)“ fragen")
    }
}

extension View {
    func fragenKnopf(_ bezug: String) -> some View {
        toolbar {
            ToolbarItem(placement: .primaryAction) {
                FragenKnopf(bezug: bezug)
            }
        }
    }
}
