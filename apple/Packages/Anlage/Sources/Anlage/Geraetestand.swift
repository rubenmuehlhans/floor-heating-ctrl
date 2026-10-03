import Foundation
import Geraeteschnittstelle

/// Was die App zuletzt von einem Gerät gelesen hat. Der Anlagenbetrieb füllt ihn; die
/// Zusammenführung macht aus allen Ständen das Anlagenbild.
public struct Geraetestand: Sendable, Equatable {
    public var geraet: BekanntesGeraet
    /// Hat die letzte Abfrage geantwortet?
    public var erreichbar: Bool
    public var letzterKontakt: Date?
    public var fehler: String?
    public var verteiler: Verteilerzustand?
    public var heizgeraet: Heizgeraetezustand?
    /// Die letzte vollständige Antwort von `/api/state`, für die KI und für Felder, die das
    /// Modell nicht kennt
    public var rohzustand: JSONWert?
    /// `GET /api/config`; Kennwörter erscheinen dort nur als `pass_set`.
    public var konfiguration: JSONWert?
    /// Nur Verteiler: empfangene Bluetooth-Thermometer
    public var thermometer: [Thermometerliste.Funkthermometer]
    /// Nur Verteiler: Adressen, für die ein Thermometerschlüssel hinterlegt ist
    public var thermometerSchluessel: [String]
    /// Nur Verteiler: Gegenspannung der laufenden oder letzten Messfahrt, fortlaufend ab Punkt 0
    public var messreihe: [Double]
    /// Nur Heizungsgeräte
    public var ladungen: [Ladungseintrag]
    public var tage: [Tageseintrag]
    public var verlauf: Verlaufsantwort?
    /// `GET /api/peers`: die Geräte, die dieses Gerät per mDNS im Netz sieht
    public var nachbarn: [Nachbarliste.Nachbar]

    public init(
        geraet: BekanntesGeraet, erreichbar: Bool = false, letzterKontakt: Date? = nil, fehler: String? = nil,
        verteiler: Verteilerzustand? = nil, heizgeraet: Heizgeraetezustand? = nil, rohzustand: JSONWert? = nil,
        konfiguration: JSONWert? = nil,
        thermometer: [Thermometerliste.Funkthermometer] = [], thermometerSchluessel: [String] = [], messreihe: [Double] = [],
        ladungen: [Ladungseintrag] = [], tage: [Tageseintrag] = [], verlauf: Verlaufsantwort? = nil,
        nachbarn: [Nachbarliste.Nachbar] = []
    ) {
        self.geraet = geraet
        self.erreichbar = erreichbar
        self.letzterKontakt = letzterKontakt
        self.fehler = fehler
        self.verteiler = verteiler
        self.heizgeraet = heizgeraet
        self.rohzustand = rohzustand
        self.konfiguration = konfiguration
        self.thermometer = thermometer
        self.thermometerSchluessel = thermometerSchluessel
        self.messreihe = messreihe
        self.ladungen = ladungen
        self.tage = tage
        self.verlauf = verlauf
        self.nachbarn = nachbarn
    }

    /// Ort nach dem Gerät selbst, sonst nach dem Verzeichnis
    public var ort: String {
        let vomGeraet = verteiler?.geraet?.ort ?? heizgeraet?.geraet?.ort ?? konfiguration?["site"]?.alsText
        return vomGeraet.flatMap { $0.isEmpty ? nil : $0 } ?? geraet.ort
    }
}
