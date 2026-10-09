import Foundation

/// Zustand der ganzen Anlage, so wie ihn die Oberfläche zeigt.
///
/// Das Klickmodell füllt ihn mit Beispieldaten aus dem Mitschnitt, die App später aus den
/// Geräten. Die Ansichten kennen nur diesen Typ und nicht die JSON-Schnittstelle der Firmware.
public struct Anlagenbild: Sendable {
    public var stand: Date
    public var aussen: Aussenwerte?
    public var etagen: [Etage]
    /// `nil`, solange kein Heizungsgerät mit Abgasfühler eingebunden ist
    public var kessel: Kessel?
    /// `nil`, solange kein Heizungsgerät mit Pufferfühler eingebunden ist
    public var speicher: Speicher?
    public var heizkreise: [Heizkreis]
    /// `nil`, wenn keine Kesselkreispumpe eingerichtet ist
    public var kesselkreispumpe: Pumpe?
    public var befunde: [Befund]
    public var geraete: [Geraet]
    public var tage: [Tagessatz]
    public var ladungen: [Ladungssatz]
    /// Die letzten 24 Stunden aus dem Verlauf der Heizungsgeräte; in der Beispielanlage der
    /// Beispielverlauf. Den gespeicherten Verlauf liefert das Paket Verlauf.
    public var verlauf: Kurzverlauf
    public var verbrauchslinie: Verbrauchslinie?

    public init(
        stand: Date, aussen: Aussenwerte?, etagen: [Etage], kessel: Kessel?, speicher: Speicher?,
        heizkreise: [Heizkreis], kesselkreispumpe: Pumpe?, befunde: [Befund], geraete: [Geraet],
        tage: [Tagessatz], ladungen: [Ladungssatz], verlauf: Kurzverlauf, verbrauchslinie: Verbrauchslinie?
    ) {
        self.stand = stand
        self.aussen = aussen
        self.etagen = etagen
        self.kessel = kessel
        self.speicher = speicher
        self.heizkreise = heizkreise
        self.kesselkreispumpe = kesselkreispumpe
        self.befunde = befunde
        self.geraete = geraete
        self.tage = tage
        self.ladungen = ladungen
        self.verlauf = verlauf
        self.verbrauchslinie = verbrauchslinie
    }

    /// Bild ohne Geräte, solange die erste Abfrage noch aussteht
    public static var leer: Anlagenbild {
        Anlagenbild(
            stand: .now, aussen: nil, etagen: [], kessel: nil, speicher: nil, heizkreise: [], kesselkreispumpe: nil,
            befunde: [], geraete: [], tage: [], ladungen: [], verlauf: Kurzverlauf(beginn: .now, schritt: 600, werte: [:]),
            verbrauchslinie: nil)
    }

    public var raeume: [Raum] { etagen.flatMap(\.raeume) }

    public func raum(_ id: Raum.ID) -> Raum? {
        raeume.first { $0.id == id }
    }

    public func etage(_ id: Etage.ID) -> Etage? {
        etagen.first { $0.id == id }
    }

    public func geraet(_ id: Geraet.ID) -> Geraet? {
        geraete.first { $0.id == id }
    }

    /// Befunde, die schwerste zuerst.
    public var befundeNachSchwere: [Befund] {
        befunde.sorted { $0.schwere > $1.schwere }
    }

    public mutating func aendereRaum(_ id: Raum.ID, _ aenderung: (inout Raum) -> Void) {
        for e in etagen.indices {
            if let r = etagen[e].raeume.firstIndex(where: { $0.id == id }) {
                aenderung(&etagen[e].raeume[r])
            }
        }
    }

    public mutating func aendereEtage(_ id: Etage.ID, _ aenderung: (inout Etage) -> Void) {
        guard let e = etagen.firstIndex(where: { $0.id == id }) else { return }
        aenderung(&etagen[e])
    }

    public mutating func aendereKanal(etage: Etage.ID, nummer: Int, _ aenderung: (inout Kanal) -> Void) {
        guard let e = etagen.firstIndex(where: { $0.id == etage }),
              let k = etagen[e].kanaele.firstIndex(where: { $0.nummer == nummer }) else { return }
        aenderung(&etagen[e].kanaele[k])
    }
}

// MARK: - Außen

public struct Aussenwerte: Sendable, Hashable {
    public var temperatur: Double
    public var feuchte: Double?
    public var quelle: String
    public var alter: Int

    public init(temperatur: Double, feuchte: Double?, quelle: String, alter: Int) {
        self.temperatur = temperatur
        self.feuchte = feuchte
        self.quelle = quelle
        self.alter = alter
    }
}

// MARK: - Verteiler, Räume, Kanäle

public struct Etage: Identifiable, Sendable {
    /// Kennung des Verteilers, z. B. `fbh_3a91c4`.
    public var id: String
    public var name: String
    public var raeume: [Raum]
    public var kanaele: [Kanal]
    public var bedarf: Bool
    public var schutzfahrt: Schutzfahrt
    public var traegtAussenfuehler: Bool
    /// Laufende oder letzte Messfahrt dieses Verteilers
    public var messfahrt: Messfahrt?

    public init(
        id: String, name: String, raeume: [Raum], kanaele: [Kanal], bedarf: Bool,
        schutzfahrt: Schutzfahrt, traegtAussenfuehler: Bool, messfahrt: Messfahrt? = nil
    ) {
        self.id = id
        self.name = name
        self.raeume = raeume
        self.kanaele = kanaele
        self.bedarf = bedarf
        self.schutzfahrt = schutzfahrt
        self.traegtAussenfuehler = traegtAussenfuehler
        self.messfahrt = messfahrt
    }

    public var belegteKanaele: [Kanal] { kanaele.filter { $0.raum != nil } }
}

public struct Schutzfahrt: Sendable, Hashable {
    /// 0 = Sonntag … 6 = Samstag; `nil` = kein Termin.
    public var wochentag: Int?
    public var stunde: Int
    public var tageBis: Int?
    public var laeuft: Bool

    public init(wochentag: Int?, stunde: Int, tageBis: Int?, laeuft: Bool) {
        self.wochentag = wochentag
        self.stunde = stunde
        self.tageBis = tageBis
        self.laeuft = laeuft
    }
}

public struct Raum: Identifiable, Sendable, Hashable {
    public enum Betriebsart: String, Sendable, Hashable {
        case heizen, aus
    }

    /// Verteilerkennung und Raumnummer, z. B. `fbh_5d20e7/2`.
    public var id: String
    public var nummer: Int
    public var name: String
    public var etage: String
    public var betriebsart: Betriebsart
    public var soll: Double
    /// `nil`, solange kein gültiger Messwert vorliegt.
    public var ist: Double?
    public var feuchte: Double?
    public var batterie: Int?
    public var messwertAlter: Int?
    /// Name des zugeordneten Thermometers; `nil` = keines zugeordnet.
    public var thermometer: String?
    public var kanaele: [Int]
    /// Zielstellung der Ventile, 0 = zu, 1 = auf.
    public var zielstellung: Double
    public var naechstePruefung: Int
    public var regelung: Regelparameter

    public init(
        id: String, nummer: Int, name: String, etage: String, betriebsart: Betriebsart, soll: Double,
        ist: Double?, feuchte: Double?, batterie: Int?, messwertAlter: Int?, thermometer: String?,
        kanaele: [Int], zielstellung: Double, naechstePruefung: Int, regelung: Regelparameter
    ) {
        self.id = id
        self.nummer = nummer
        self.name = name
        self.etage = etage
        self.betriebsart = betriebsart
        self.soll = soll
        self.ist = ist
        self.feuchte = feuchte
        self.batterie = batterie
        self.messwertAlter = messwertAlter
        self.thermometer = thermometer
        self.kanaele = kanaele
        self.zielstellung = zielstellung
        self.naechstePruefung = naechstePruefung
        self.regelung = regelung
    }

    /// Zustand in derselben Abstufung wie die Weboberfläche.
    public var zustand: Raumzustand {
        if betriebsart == .aus { return .ausgeschaltet }
        if thermometer == nil { return .keinThermometer }
        guard let ist else { return .messwertVeraltet }
        return ist < soll - 0.05 ? .heizt : .sollErreicht
    }

    /// Zielstellung nach dem Regelgesetz der Firmware (`components/roomctrl`): 50 % am Sollwert,
    /// ganz auf ein Proportionalband darunter, ganz zu eines darüber, gerastert.
    public func berechneZielstellung() -> Double {
        guard betriebsart == .heizen, let ist else { return betriebsart == .aus ? 0 : zielstellung }
        let roh = ((soll - ist) / regelung.proportionalband + 1) / 2
        let gerastert = (roh / regelung.raster).rounded() * regelung.raster
        return min(max(gerastert, 0), 1)
    }
}

public enum Raumzustand: Sendable, Hashable {
    case ausgeschaltet, keinThermometer, messwertVeraltet, heizt, sollErreicht
}

public struct Regelparameter: Sendable, Hashable {
    public var proportionalband: Double
    public var pruefintervall: Int
    public var raster: Double
    public var mindestaenderung: Double

    public init(proportionalband: Double, pruefintervall: Int, raster: Double, mindestaenderung: Double) {
        self.proportionalband = proportionalband
        self.pruefintervall = pruefintervall
        self.raster = raster
        self.mindestaenderung = mindestaenderung
    }

    public static let vorgabe = Regelparameter(
        proportionalband: 1.0, pruefintervall: 30, raster: 0.1, mindestaenderung: 0.01
    )
}

public struct Kanal: Identifiable, Sendable, Hashable {
    public enum Bewegung: String, Sendable, Hashable {
        case steht, oeffnet, schliesst
    }

    public var id: Int { nummer }
    public var nummer: Int
    /// Name des Raums; `nil` = frei.
    public var raum: String?
    public var gruppe: Int
    public var stellung: Double
    public var bekannt: Bool
    public var kalibriert: Bool
    public var handbetrieb: Bool
    public var bewegung: Bewegung
    public var gegenspannung: Int
    public var fahrzeitAuf: Double
    public var fahrzeitZu: Double
    public var maximal: Double
    public var sperrzeit: Double
    public var schwelle: Int
    public var hysterese: Int
    /// Durch eine Messfahrt belegt; Befehle quittiert die Firmware dann, ohne sie auszuführen.
    public var belegt: Bool
    /// Ein Befehl wartet darauf, dass die Messgruppe frei wird.
    public var befehlWartet: Bool

    public init(
        nummer: Int, raum: String?, gruppe: Int, stellung: Double, bekannt: Bool, kalibriert: Bool,
        handbetrieb: Bool, bewegung: Bewegung, gegenspannung: Int, fahrzeitAuf: Double,
        fahrzeitZu: Double, maximal: Double, sperrzeit: Double, schwelle: Int, hysterese: Int,
        belegt: Bool = false, befehlWartet: Bool = false
    ) {
        self.nummer = nummer
        self.raum = raum
        self.gruppe = gruppe
        self.stellung = stellung
        self.bekannt = bekannt
        self.kalibriert = kalibriert
        self.handbetrieb = handbetrieb
        self.bewegung = bewegung
        self.gegenspannung = gegenspannung
        self.fahrzeitAuf = fahrzeitAuf
        self.fahrzeitZu = fahrzeitZu
        self.maximal = maximal
        self.sperrzeit = sperrzeit
        self.schwelle = schwelle
        self.hysterese = hysterese
        self.belegt = belegt
        self.befehlWartet = befehlWartet
    }
}

// MARK: - Wärmeerzeugung

public struct Kessel: Sendable {
    public var brennerLaeuft: Bool
    public var brennerSeit: Int
    public var abgas: Double?
    public var bezugslinie: Double?
    public var vorlauf: Double?
    public var ruecklauf: Double?
    public var laufzeitHeute: Int
    public var startsHeute: Int
    public var literHeute: Double
    public var laufzeitGestern: Int
    public var startsGestern: Int
    public var taktbetrieb: Bool
    /// Eingetragener Düsendurchsatz in l/h; 0 oder fehlend heißt: nicht eingetragen.
    public var duese: Double?
    public var abgasAbstand: AbgasAbstand

    public init(
        brennerLaeuft: Bool, brennerSeit: Int, abgas: Double?, bezugslinie: Double?, vorlauf: Double?,
        ruecklauf: Double?, laufzeitHeute: Int, startsHeute: Int, literHeute: Double,
        laufzeitGestern: Int, startsGestern: Int, taktbetrieb: Bool, duese: Double?,
        abgasAbstand: AbgasAbstand
    ) {
        self.brennerLaeuft = brennerLaeuft
        self.brennerSeit = brennerSeit
        self.abgas = abgas
        self.bezugslinie = bezugslinie
        self.vorlauf = vorlauf
        self.ruecklauf = ruecklauf
        self.laufzeitHeute = laufzeitHeute
        self.startsHeute = startsHeute
        self.literHeute = literHeute
        self.laufzeitGestern = laufzeitGestern
        self.startsGestern = startsGestern
        self.taktbetrieb = taktbetrieb
        self.duese = duese
        self.abgasAbstand = abgasAbstand
    }

    public var spreizung: Double? {
        guard let vorlauf, let ruecklauf else { return nil }
        return vorlauf - ruecklauf
    }
}

/// Abstand zwischen Abgas- und Kesselvorlauf-Höchstwert je Ladung, verglichen mit dem Stand
/// nach der letzten Reinigung.
public struct AbgasAbstand: Sendable, Hashable {
    /// Brauchbare Ladungen im Fenster, höchstens 50
    public var ladungen: Int
    public var reinigung: Date?
    /// Median der letzten Ladungen in K
    public var jetzt: Double?
    /// Median der ersten Ladungen nach der Reinigung in K
    public var bezug: Double?
    public var bezugLadungen: Int?
    /// Ladungen ohne brauchbare Höchstwerte
    public var uebersprungen: Int

    public init(ladungen: Int, reinigung: Date?, jetzt: Double?, bezug: Double?, bezugLadungen: Int? = nil, uebersprungen: Int = 0) {
        self.ladungen = ladungen
        self.reinigung = reinigung
        self.jetzt = jetzt
        self.bezug = bezug
        self.bezugLadungen = bezugLadungen
        self.uebersprungen = uebersprungen
    }

    /// Anstieg gegenüber dem sauberen Kessel; grob 20 K entsprechen einem Prozentpunkt Wirkungsgrad.
    public var veraenderung: Double? {
        guard let jetzt, let bezug else { return nil }
        return jetzt - bezug
    }
}

public struct Speicher: Sendable {
    public var temperatur: Double?
    /// Korrektur des Pufferfühlers in K
    public var korrektur: Double?
    public var ladung: Double?
    public var phase: String
    /// Sekunden seit Beginn der Phase
    public var phaseSeit: Int?
    public var warmwasserKnapp: Bool
    /// Grenzen aus der Konfiguration; fehlen sie, ist die Konfiguration noch nicht gelesen.
    public var voll: Double?
    public var leer: Double?
    public var leerGelernt: Bool
    public var warngrenze: Double?
    public var hoechstwertLadung: Double?
    public var zapfungenHeute: Int
    public var rueckstroemungen: Int
    /// Inhalt in Litern aus der Konfiguration
    public var volumen: Double?

    public init(
        temperatur: Double?, korrektur: Double? = nil, ladung: Double?, phase: String, phaseSeit: Int? = nil,
        warmwasserKnapp: Bool, voll: Double?, leer: Double?, leerGelernt: Bool, warngrenze: Double?,
        hoechstwertLadung: Double?, zapfungenHeute: Int, rueckstroemungen: Int, volumen: Double? = nil
    ) {
        self.temperatur = temperatur
        self.korrektur = korrektur
        self.ladung = ladung
        self.phase = phase
        self.phaseSeit = phaseSeit
        self.warmwasserKnapp = warmwasserKnapp
        self.voll = voll
        self.leer = leer
        self.leerGelernt = leerGelernt
        self.warngrenze = warngrenze
        self.hoechstwertLadung = hoechstwertLadung
        self.zapfungenHeute = zapfungenHeute
        self.rueckstroemungen = rueckstroemungen
        self.volumen = volumen
    }
}

public enum Pumpenbetriebsart: String, CaseIterable, Sendable, Hashable {
    case automatik = "Automatik"
    case ein = "Ein"
    case aus = "Aus"
}

public struct Relais: Sendable, Hashable {
    public var adresse: String
    public var kanal: Int
    public var weg: String
    public var erreichbar: Bool
    public var ein: Bool
    /// Ohne Verbindung, weil die vorgeschaltete Regelung (Kessel, Heliomat) den Pumpenausgang und
    /// damit das Relais stromlos geschaltet hat. Die Pumpe steht; ein Fehler ist das nicht.
    public var stromlos: Bool
    public var stromlosSeit: Int?

    public init(adresse: String, kanal: Int, weg: String, erreichbar: Bool, ein: Bool,
                stromlos: Bool = false, stromlosSeit: Int? = nil) {
        self.adresse = adresse
        self.kanal = kanal
        self.weg = weg
        self.erreichbar = erreichbar
        self.ein = ein
        self.stromlos = stromlos
        self.stromlosSeit = stromlosSeit
    }

    /// Nicht erreichbar, ohne dass die vorgeschaltete Regelung es erklärt
    public var gestoert: Bool { !erreichbar && !stromlos }
}

public struct Heizkreis: Identifiable, Sendable {
    public var id: Int { nummer }
    public var nummer: Int
    public var name: String
    public var betriebsart: Pumpenbetriebsart
    public var pumpeLaeuft: Bool
    public var grund: String
    public var bedarf: Bool
    public var vorlauf: Double?
    public var ruecklauf: Double?
    public var versorgteVerteiler: [String]
    public var relais: Relais
    public var nachlauf: Int
    public var mindestlaufzeit: Int
    public var mindestpause: Int
    public var mindestSpeicher: Double
    public var frostgrenze: Double
    /// Mindestens ein versorgter Verteiler antwortet nicht mehr (`stale`).
    public var bedarfVeraltet: Bool
    /// Keiner der versorgten Verteiler war je erreichbar (`any_seen` falsch bei zugeordneten Verteilern).
    public var keinVerteilerErreicht: Bool
    /// Rollen der Fühler für Vor- und Rücklauf, `vl_role`/`rl_role`, sonst `hk<n>_vl`/`hk<n>_rl`;
    /// im Verlauf stehen sie als `fuehler.<rolle>`.
    public var vorlaufRolle: String
    public var ruecklaufRolle: String
    /// Die Pumpe soll laufen, die vorgeschaltete Regelung gibt sie nicht frei.
    public var gesperrt: Bool

    public init(
        nummer: Int, name: String, betriebsart: Pumpenbetriebsart, pumpeLaeuft: Bool, grund: String,
        bedarf: Bool, vorlauf: Double?, ruecklauf: Double?, versorgteVerteiler: [String],
        relais: Relais, nachlauf: Int, mindestlaufzeit: Int, mindestpause: Int,
        mindestSpeicher: Double, frostgrenze: Double, bedarfVeraltet: Bool = false, keinVerteilerErreicht: Bool = false,
        vorlaufRolle: String? = nil, ruecklaufRolle: String? = nil, gesperrt: Bool = false
    ) {
        self.gesperrt = gesperrt
        self.vorlaufRolle = vorlaufRolle ?? "hk\(nummer)_vl"
        self.ruecklaufRolle = ruecklaufRolle ?? "hk\(nummer)_rl"
        self.bedarfVeraltet = bedarfVeraltet
        self.keinVerteilerErreicht = keinVerteilerErreicht
        self.nummer = nummer
        self.name = name
        self.betriebsart = betriebsart
        self.pumpeLaeuft = pumpeLaeuft
        self.grund = grund
        self.bedarf = bedarf
        self.vorlauf = vorlauf
        self.ruecklauf = ruecklauf
        self.versorgteVerteiler = versorgteVerteiler
        self.relais = relais
        self.nachlauf = nachlauf
        self.mindestlaufzeit = mindestlaufzeit
        self.mindestpause = mindestpause
        self.mindestSpeicher = mindestSpeicher
        self.frostgrenze = frostgrenze
    }

    public var spreizung: Double? {
        guard let vorlauf, let ruecklauf else { return nil }
        return vorlauf - ruecklauf
    }
}

public struct Pumpe: Sendable {
    public var name: String
    public var betriebsart: Pumpenbetriebsart
    public var laeuft: Bool
    public var grund: String
    public var relais: Relais
    public var einschaltschwelle: Double
    public var ausschaltschwelle: Double
    public var haltezeit: Int
    public var notgrenze: Double

    public init(
        name: String, betriebsart: Pumpenbetriebsart, laeuft: Bool, grund: String, relais: Relais,
        einschaltschwelle: Double, ausschaltschwelle: Double, haltezeit: Int, notgrenze: Double
    ) {
        self.name = name
        self.betriebsart = betriebsart
        self.laeuft = laeuft
        self.grund = grund
        self.relais = relais
        self.einschaltschwelle = einschaltschwelle
        self.ausschaltschwelle = ausschaltschwelle
        self.haltezeit = haltezeit
        self.notgrenze = notgrenze
    }
}

// MARK: - Befunde

public struct Befund: Identifiable, Sendable, Hashable {
    public enum Schwere: Int, Comparable, Sendable, Hashable, Codable {
        case hinweis, warnung, stoerung

        public static func < (a: Schwere, b: Schwere) -> Bool { a.rawValue < b.rawValue }
    }

    public var id: String
    public var schwere: Schwere
    public var titel: String
    public var text: String
    public var ort: String
    /// Woher der Befund stammt: ein Gerät oder die Prüfungen der App.
    public var quelle: String
    /// So lange muss der Befund ununterbrochen anstehen, bevor er erscheint; gegen kurzes
    /// Flattern, etwa eines Relais oder des WLAN.
    public var mindestdauer: TimeInterval

    public init(id: String, schwere: Schwere, titel: String, text: String, ort: String, quelle: String,
                mindestdauer: TimeInterval = 0) {
        self.id = id
        self.schwere = schwere
        self.titel = titel
        self.text = text
        self.ort = ort
        self.quelle = quelle
        self.mindestdauer = mindestdauer
    }
}

// MARK: - Geräte

public struct Geraet: Identifiable, Sendable {
    public enum Art: String, Sendable, Hashable {
        case verteiler, kessel, speicher, relais
    }

    public var id: String
    public var art: Art
    public var ort: String
    public var adresse: String
    public var hostname: String
    public var firmware: String
    public var signal: Int?
    public var laufzeit: Int
    public var erreichbar: Bool
    /// Freier Arbeitsspeicher in Byte, wie ihn die Kopfzeile der Weboberfläche zeigt
    public var freierSpeicher: Int?
    public var fuehler: [Fuehler]
    public var bus: Einwirebus?
    public var funkthermometer: [Funkthermometer]
    /// Adressen, für die am Verteiler ein Thermometerschlüssel liegt, auch außer Reichweite
    public var funkschluessel: [String]
    public var bordfuehler: Bordfuehler?
    public var tasten: [Taste]

    public init(
        id: String, art: Art, ort: String, adresse: String, hostname: String, firmware: String,
        signal: Int?, laufzeit: Int, erreichbar: Bool, freierSpeicher: Int? = nil, fuehler: [Fuehler] = [], bus: Einwirebus? = nil,
        funkthermometer: [Funkthermometer] = [], funkschluessel: [String] = [], bordfuehler: Bordfuehler? = nil, tasten: [Taste] = []
    ) {
        self.funkschluessel = funkschluessel
        self.id = id
        self.art = art
        self.ort = ort
        self.adresse = adresse
        self.hostname = hostname
        self.firmware = firmware
        self.signal = signal
        self.laufzeit = laufzeit
        self.erreichbar = erreichbar
        self.freierSpeicher = freierSpeicher
        self.fuehler = fuehler
        self.bus = bus
        self.funkthermometer = funkthermometer
        self.bordfuehler = bordfuehler
        self.tasten = tasten
    }
}

public struct Fuehler: Identifiable, Sendable, Hashable {
    public var id: String { rom }
    public var rom: String
    public var rolle: String
    public var rollenname: String
    public var wert: Double
    public var aenderung30s: Double
    public var korrektur: Double
    public var messungen: Int
    public var fehler: Int

    public init(
        rom: String, rolle: String, rollenname: String, wert: Double, aenderung30s: Double,
        korrektur: Double, messungen: Int, fehler: Int
    ) {
        self.rom = rom
        self.rolle = rolle
        self.rollenname = rollenname
        self.wert = wert
        self.aenderung30s = aenderung30s
        self.korrektur = korrektur
        self.messungen = messungen
        self.fehler = fehler
    }
}

public struct Einwirebus: Sendable, Hashable {
    public var anschluesse: [Int]
    public var gefunden: Int
    public var zugeordnet: Int
    public var rundeMillisekunden: Int
    public var abfrageSekunden: Int

    public init(anschluesse: [Int], gefunden: Int, zugeordnet: Int, rundeMillisekunden: Int, abfrageSekunden: Int) {
        self.anschluesse = anschluesse
        self.gefunden = gefunden
        self.zugeordnet = zugeordnet
        self.rundeMillisekunden = rundeMillisekunden
        self.abfrageSekunden = abfrageSekunden
    }
}

public struct Funkthermometer: Identifiable, Sendable, Hashable {
    public var id: String { mac }
    public var mac: String
    public var name: String
    /// `nil` bei einem verschlüsselt sendenden Thermometer ohne passenden Schlüssel
    public var temperatur: Double?
    public var feuchte: Double?
    public var batterie: Int?
    public var batterieMillivolt: Int
    public var signal: Int
    public var format: String
    /// Raumname, „Außenfühler“ oder `nil`.
    public var zuordnung: String?
    /// Nur BTHome: Das Thermometer sendet verschlüsselt, und so steht es mit dem Schlüssel.
    public var verschluesselt: Bool
    public var schluessel: Schluesselzustand?

    public init(
        mac: String, name: String, temperatur: Double?, feuchte: Double?, batterie: Int?,
        batterieMillivolt: Int, signal: Int, format: String, zuordnung: String?,
        verschluesselt: Bool = false, schluessel: Schluesselzustand? = nil
    ) {
        self.mac = mac
        self.name = name
        self.temperatur = temperatur
        self.feuchte = feuchte
        self.batterie = batterie
        self.batterieMillivolt = batterieMillivolt
        self.signal = signal
        self.format = format
        self.zuordnung = zuordnung
        self.verschluesselt = verschluesselt
        self.schluessel = schluessel
    }

    /// Formatname für die Anzeige
    public var formatbezeichnung: String {
        switch format {
        case "atc1441": "ATC"
        case "ruuvi": "RuuviTag"
        case "bthome": "BTHome"
        default: format
        }
    }

    /// Ein Schlüssel ist nötig, und es liegt kein passender vor.
    public var brauchtSchluessel: Bool {
        verschluesselt && schluessel != .passt
    }
}

/// Stand des Schlüssels bei einem verschlüsselt sendenden Thermometer
public enum Schluesselzustand: String, Sendable, Hashable {
    case passt = "ok"
    case fehlt = "missing"
    case falsch = "wrong"

    public var text: String {
        switch self {
        case .passt: "Schlüssel passt"
        case .fehlt: "Schlüssel fehlt"
        case .falsch: "Schlüssel falsch"
        }
    }
}

/// Eingabe eines Thermometerschlüssels, wie ihn etwa die camperSense-App anzeigt
public enum Funkschluessel {
    /// 32 Hexadezimalziffern in Kleinschreibung, oder `nil`. Leerzeichen, Zeilenumbrüche,
    /// Doppelpunkte und Bindestriche dazwischen werden übergangen.
    public static func normalisiert(_ text: String) -> String? {
        let ziffern = text.lowercased().filter { !" \n\t:-".contains($0) }
        guard ziffern.count == 32, ziffern.allSatisfy({ $0.isHexDigit }) else { return nil }
        return ziffern
    }

    /// Sendeadresse aus einer Eingabe: zwölf Hexadezimalziffern mit oder ohne Trennzeichen,
    /// zurück in der Schreibweise des Verteilers (`C0:FF:EE:12:34:56`), oder `nil`.
    public static func adresse(_ text: String) -> String? {
        let ziffern = text.uppercased().filter { !" \n\t:-".contains($0) }
        guard ziffern.count == 12, ziffern.allSatisfy({ $0.isHexDigit }) else { return nil }
        var paare: [String] = []
        var rest = Substring(ziffern)
        while !rest.isEmpty {
            paare.append(String(rest.prefix(2)))
            rest = rest.dropFirst(2)
        }
        return paare.joined(separator: ":")
    }

    /// Wie weit die Eingabe ist, für den Hinweis unter dem Eingabefeld
    public static func stand(_ text: String) -> String {
        let ziffern = text.lowercased().filter { !" \n\t:-".contains($0) }
        if ziffern.contains(where: { !$0.isHexDigit }) {
            return "Erlaubt sind nur die Ziffern 0 bis 9 und die Buchstaben a bis f."
        }
        if ziffern.count > 32 {
            return "\(ziffern.count) Zeichen, das sind \(ziffern.count - 32) zu viel."
        }
        return "\(ziffern.count) von 32 Zeichen"
    }
}

public struct Bordfuehler: Sendable, Hashable {
    public var gueltig: Bool
    public var temperatur: Double
    public var feuchte: Double
    public var vorlauffuehler: [Double]

    public init(gueltig: Bool, temperatur: Double, feuchte: Double, vorlauffuehler: [Double]) {
        self.gueltig = gueltig
        self.temperatur = temperatur
        self.feuchte = feuchte
        self.vorlauffuehler = vorlauffuehler
    }
}

public struct Taste: Identifiable, Sendable, Hashable {
    public var id: String { name }
    public var name: String
    public var rohwert: Int
    public var schwelle: Int

    public init(name: String, rohwert: Int, schwelle: Int) {
        self.name = name
        self.rohwert = rohwert
        self.schwelle = schwelle
    }
}

// MARK: - Protokolle

public struct Tagessatz: Identifiable, Sendable, Hashable {
    public var id: Date { datum }
    public var datum: Date
    public var laufzeit: Int
    public var starts: Int
    public var liter: Double
    public var heizgradtage: Double
    public var aussenMin: Double?
    public var aussenMax: Double?

    public init(
        datum: Date, laufzeit: Int, starts: Int, liter: Double, heizgradtage: Double,
        aussenMin: Double?, aussenMax: Double?
    ) {
        self.datum = datum
        self.laufzeit = laufzeit
        self.starts = starts
        self.liter = liter
        self.heizgradtage = heizgradtage
        self.aussenMin = aussenMin
        self.aussenMax = aussenMax
    }
}

public struct Ladungssatz: Identifiable, Sendable, Hashable {
    public var id: Date { beginn }
    public var beginn: Date
    public var dauer: Int
    public var brenner: Int
    public var starts: Int
    public var speicherVorher: Double
    public var speicherNachher: Double
    public var kesselVorlaufMax: Double
    public var abgasMax: Double
    public var aussenMittel: Double?
    public var liter: Double

    public init(
        beginn: Date, dauer: Int, brenner: Int, starts: Int, speicherVorher: Double,
        speicherNachher: Double, kesselVorlaufMax: Double, abgasMax: Double, aussenMittel: Double?,
        liter: Double
    ) {
        self.beginn = beginn
        self.dauer = dauer
        self.brenner = brenner
        self.starts = starts
        self.speicherVorher = speicherVorher
        self.speicherNachher = speicherNachher
        self.kesselVorlaufMax = kesselVorlaufMax
        self.abgasMax = abgasMax
        self.aussenMittel = aussenMittel
        self.liter = liter
    }
}

// MARK: - Verlauf und Auswertung

public struct Kurzverlauf: Sendable {
    public enum Reihe: String, CaseIterable, Sendable, Hashable {
        case abgas, kesselVorlauf, kesselRuecklauf, speicher
        case hk1Vorlauf, hk1Ruecklauf, hk2Vorlauf, hk2Ruecklauf
        case aussen, ladung, brenner
        case wohnzimmer, bad, buero, kueche

        public var bezeichnung: String {
            switch self {
            case .abgas: "Abgas"
            case .kesselVorlauf: "Kessel Vorlauf"
            case .kesselRuecklauf: "Kessel Rücklauf"
            case .speicher: "Speicher"
            case .hk1Vorlauf: "Heizkreis 1 Vorlauf"
            case .hk1Ruecklauf: "Heizkreis 1 Rücklauf"
            case .hk2Vorlauf: "Heizkreis 2 Vorlauf"
            case .hk2Ruecklauf: "Heizkreis 2 Rücklauf"
            case .aussen: "Außen"
            case .ladung: "Ladezustand"
            case .brenner: "Brenner"
            case .wohnzimmer: "Wohnzimmer"
            case .bad: "Bad"
            case .buero: "Büro"
            case .kueche: "Küche"
            }
        }
    }

    public struct Punkt: Identifiable, Sendable, Hashable {
        public var id: Date { zeit }
        public var zeit: Date
        public var wert: Double
    }

    public var beginn: Date
    public var schritt: TimeInterval
    public var werte: [Reihe: [Double?]]

    public init(beginn: Date, schritt: TimeInterval, werte: [Reihe: [Double?]]) {
        self.beginn = beginn
        self.schritt = schritt
        self.werte = werte
    }

    public func punkte(_ reihe: Reihe) -> [Punkt] {
        guard let liste = werte[reihe] else { return [] }
        return liste.enumerated().compactMap { i, wert in
            wert.map { Punkt(zeit: beginn.addingTimeInterval(Double(i) * schritt), wert: $0) }
        }
    }
}

public struct Messfahrt: Sendable {
    public enum Zustand: String, Sendable, Hashable {
        case laeuft, fertig, fehlgeschlagen
    }

    public var zustand: Zustand
    public var kanal: Int
    public var raum: String
    public var abtastung: TimeInterval
    /// Gegenspannung in Millivolt, eine Probe je `abtastung`.
    public var werte: [Int]
    /// Index der ersten Probe der Öffnungsfahrt; davor schließt der Antrieb. `nil`, solange er
    /// noch schließt.
    public var oeffnenAb: Int?
    /// Erst nach einer vollständigen Fahrt
    public var vorschlag: Kalibriervorschlag?
    /// Meldung der Firmware, etwa der Grund eines Abbruchs
    public var meldung: String?

    public init(
        zustand: Zustand = .fertig, kanal: Int, raum: String, abtastung: TimeInterval, werte: [Int],
        oeffnenAb: Int?, vorschlag: Kalibriervorschlag?, meldung: String? = nil
    ) {
        self.zustand = zustand
        self.kanal = kanal
        self.raum = raum
        self.abtastung = abtastung
        self.werte = werte
        self.oeffnenAb = oeffnenAb
        self.vorschlag = vorschlag
        self.meldung = meldung
    }
}

public struct Kalibriervorschlag: Sendable, Hashable {
    public var fahrzeitZu: Double
    public var fahrzeitAuf: Double
    public var maximal: Double
    public var schwelle: Int
    public var hysterese: Int

    public init(fahrzeitZu: Double, fahrzeitAuf: Double, maximal: Double, schwelle: Int, hysterese: Int) {
        self.fahrzeitZu = fahrzeitZu
        self.fahrzeitAuf = fahrzeitAuf
        self.maximal = maximal
        self.schwelle = schwelle
        self.hysterese = hysterese
    }
}

/// Brennerlaufzeit je Tag gegen Heizgradtage, mit der Ausgleichsgeraden der Firmware.
public struct Verbrauchslinie: Sendable {
    public struct Tag: Identifiable, Sendable, Hashable {
        public var id: Int { nummer }
        public var nummer: Int
        public var heizgradtage: Double
        public var laufzeitStunden: Double
        public var auffaellig: Bool

        public init(nummer: Int, heizgradtage: Double, laufzeitStunden: Double, auffaellig: Bool) {
            self.nummer = nummer
            self.heizgradtage = heizgradtage
            self.laufzeitStunden = laufzeitStunden
            self.auffaellig = auffaellig
        }
    }

    /// Der letzte abgeschlossene Tag, gemessen an der Linie
    public struct LetzterTag: Sendable, Hashable {
        public var heizgradtage: Double
        public var stunden: Double
        public var erwartet: Double?
        /// Abstand zur Linie in Streuungen; über 3 meldet das Gerät einen Befund.
        public var streuungen: Double?

        public init(heizgradtage: Double, stunden: Double, erwartet: Double?, streuungen: Double?) {
            self.heizgradtage = heizgradtage
            self.stunden = stunden
            self.erwartet = erwartet
            self.streuungen = streuungen
        }
    }

    /// `false`, solange die Firmware noch keine Linie bilden kann (Sommer, zu wenige Tage).
    public var gueltig: Bool
    public var erfassteTage: Int
    /// Tage ohne Außenwert, die nicht eingehen
    public var uebersprungen: Int
    public var grund: String?
    /// Stunden Brennerlauf je Heizgradtag
    public var steigung: Double
    /// Stunden je Tag ohne Heizbedarf, also für Warmwasser
    public var grundlast: Double
    /// Streuung der Tage um die Linie in Stunden
    public var streuung: Double?
    /// Anteil der Streuung, den die Linie erklärt
    public var bestimmtheit: Double?
    public var letzterTag: LetzterTag?
    public var tage: [Tag]

    public init(
        gueltig: Bool, erfassteTage: Int, uebersprungen: Int = 0, grund: String?, steigung: Double, grundlast: Double,
        streuung: Double? = nil, bestimmtheit: Double? = nil, letzterTag: LetzterTag? = nil, tage: [Tag]
    ) {
        self.gueltig = gueltig
        self.erfassteTage = erfassteTage
        self.uebersprungen = uebersprungen
        self.grund = grund
        self.steigung = steigung
        self.grundlast = grundlast
        self.streuung = streuung
        self.bestimmtheit = bestimmtheit
        self.letzterTag = letzterTag
        self.tage = tage
    }
}
