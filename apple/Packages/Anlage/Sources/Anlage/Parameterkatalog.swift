import Foundation
import Geraeteschnittstelle

/// Eine Einstellung eines Geräts mit allem, was Formular, Prüfung, Schreibweg und KI brauchen.
///
/// Bereich und Vorgabe folgen der Prüfung in `config_store.c` der jeweiligen Firmware
/// (Verteiler: `components/config_store`, Heizungsgerät: `apps/heatsource/components/config_store`),
/// Bezeichnungen und Hilfetexte der Weboberfläche und dem Handbuch.
public struct Parameter: Identifiable, Sendable, Hashable {
    /// Wo der Wert in der Konfiguration steht
    public enum Ort: String, Sendable, Hashable {
        /// oben oder in einer Gruppe wie `buffer`
        case geraet
        /// in `rooms[]`, gefunden über `id`
        case raum
        /// in `channels[]`, gefunden über `id`
        case kanal
        /// in `circuits[]`, gefunden über `id`
        case heizkreis
        /// in `probes[]`, gefunden über `rom`
        case fuehler

        public var liste: String? {
            switch self {
            case .geraet: nil
            case .raum: "rooms"
            case .kanal: "channels"
            case .heizkreis: "circuits"
            case .fuehler: "probes"
            }
        }

        public var schluessel: String { self == .fuehler ? "rom" : "id" }
    }

    public struct Wahl: Sendable, Hashable {
        public var wert: JSONWert
        public var bezeichnung: String

        public init(_ wert: JSONWert, _ bezeichnung: String) {
            self.wert = wert
            self.bezeichnung = bezeichnung
        }
    }

    public enum Art: Sendable, Hashable {
        case zahl(bereich: ClosedRange<Double>, schritt: Double, stellen: Int)
        case ganzzahl(bereich: ClosedRange<Int>, schritt: Int)
        /// Gespeichert in Millisekunden, angezeigt in Sekunden
        case millisekunden(bereich: ClosedRange<Double>, schritt: Double)
        case schalter
        case auswahl([Wahl])
        case text(laenge: Int)
        /// Nur schreibbar; leer lassen behält das gespeicherte, ein leerer Text löscht es.
        case kennwort(laenge: Int, mindestens: Int)
    }

    /// Wann eine Änderung wirkt
    public enum Wirkung: String, Sendable, Hashable {
        case sofort
        /// Das Gerät verbindet sich neu mit dem WLAN.
        case neuverbindung
        /// erst nach einem Neustart des Geräts
        case neustart
    }

    /// `verteiler/rooms[].p_band_k`
    public var id: String { "\(geraet == .verteiler ? "verteiler" : "heizung")/\(schluessel)" }
    public var geraet: Geraeteart
    public var ort: Ort
    /// Pfad in der Konfiguration oder im Listeneintrag; Zahlen stehen für Listenplätze.
    public var pfad: [String]
    public var gruppe: String
    public var abschnitt: String?
    public var bezeichnung: String
    public var hilfe: String
    public var einheit: String
    public var art: Art
    public var vorgabe: JSONWert
    public var wirkung: Wirkung
    /// Darf die KI eine Änderung dieses Werts vorschlagen?
    public var kiFreigegeben: Bool
    public var erweitert: Bool
    /// Gehört auf alle Heizungsgeräte gleich, weil beide denselben Speicher schätzen.
    public var anlagenweit: Bool

    /// `rooms[].p_band_k`, `buffer.voll_c`, `touch.thresholds[1]`
    public var schluessel: String {
        let teile = pfad.map { Int($0) != nil ? "[\($0)]" : $0 }
        let innen = teile.enumerated().map { i, t in i > 0 && !t.hasPrefix("[") ? ".\(t)" : t }.joined()
        return ort.liste.map { "\($0)[].\(innen)" } ?? innen
    }

    public var istKennwort: Bool {
        if case .kennwort = art { return true }
        return false
    }
}

// MARK: - Katalog

public enum Parameterkatalog {
    public static let alle: [Parameter] = verteiler + heizung

    public static func parameter(_ id: String) -> Parameter? {
        alle.first { $0.id == id }
    }

    public static func gruppe(_ gruppe: String, _ geraet: Geraeteart) -> [Parameter] {
        alle.filter { $0.gruppe == gruppe && $0.geraet == geraet }
    }

    static let wochentage: [Parameter.Wahl] = [
        .init(-1, "kein Termin"), .init(0, "Sonntag"), .init(1, "Montag"), .init(2, "Dienstag"),
        .init(3, "Mittwoch"), .init(4, "Donnerstag"), .init(5, "Freitag"), .init(6, "Samstag"),
    ]

    /// Rollen der Fühler am Heizungsgerät (`config_store.c`, Rollentabelle)
    public static let rollen: [Parameter.Wahl] = [
        .init("", "nicht zugeordnet"), .init("abgas", "Abgas"), .init("kessel_vl", "Kessel Vorlauf"),
        .init("kessel_rl", "Kessel Rücklauf"), .init("puffer", "Pufferspeicher"),
        .init("puffer_unten", "Pufferspeicher unten"),
        .init("hk1_vl", "Heizkreis 1 Vorlauf"), .init("hk1_rl", "Heizkreis 1 Rücklauf"),
        .init("hk2_vl", "Heizkreis 2 Vorlauf"), .init("hk2_rl", "Heizkreis 2 Rücklauf"),
        .init("hk3_vl", "Heizkreis 3 Vorlauf"), .init("hk3_rl", "Heizkreis 3 Rücklauf"),
        .init("hk4_vl", "Heizkreis 4 Vorlauf"), .init("hk4_rl", "Heizkreis 4 Rücklauf"),
        .init("aussen", "Außentemperatur"),
    ]

    public static func rollenname(_ rolle: String) -> String {
        rollen.first { $0.wert.alsText == rolle }?.bezeichnung ?? rolle
    }

    // MARK: Bausteine

    private static func zahl(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, _ einheit: String, _ bereich: ClosedRange<Double>, schritt: Double, stellen: Int,
        vorgabe: Double, hilfe: String, ki: Bool = false, erweitert: Bool = false, wirkung: Parameter.Wirkung = .sofort,
        anlagenweit: Bool = false
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: einheit, art: .zahl(bereich: bereich, schritt: schritt, stellen: stellen),
                  vorgabe: .zahl(vorgabe), wirkung: wirkung, kiFreigegeben: ki, erweitert: erweitert, anlagenweit: anlagenweit)
    }

    private static func ganz(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, _ einheit: String, _ bereich: ClosedRange<Int>, schritt: Int = 1,
        vorgabe: Int, hilfe: String, ki: Bool = false, erweitert: Bool = false, wirkung: Parameter.Wirkung = .sofort,
        anlagenweit: Bool = false
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: einheit, art: .ganzzahl(bereich: bereich, schritt: schritt),
                  vorgabe: .zahl(Double(vorgabe)), wirkung: wirkung, kiFreigegeben: ki, erweitert: erweitert, anlagenweit: anlagenweit)
    }

    private static func ms(
        _ pfad: String, _ bezeichnung: String, _ bereich: ClosedRange<Double>, vorgabe: Int, hilfe: String
    ) -> Parameter {
        Parameter(geraet: .verteiler, ort: .kanal, pfad: [pfad], gruppe: "kanal", abschnitt: nil, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: "s", art: .millisekunden(bereich: bereich, schritt: 0.5),
                  vorgabe: .zahl(Double(vorgabe)), wirkung: .sofort, kiFreigegeben: false, erweitert: false, anlagenweit: false)
    }

    private static func schalter(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, vorgabe: Bool, hilfe: String, ki: Bool = false, erweitert: Bool = false,
        wirkung: Parameter.Wirkung = .sofort, anlagenweit: Bool = false
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: "", art: .schalter, vorgabe: .bool(vorgabe), wirkung: wirkung,
                  kiFreigegeben: ki, erweitert: erweitert, anlagenweit: anlagenweit)
    }

    private static func auswahl(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, _ wahl: [Parameter.Wahl], vorgabe: JSONWert, hilfe: String, ki: Bool = false,
        erweitert: Bool = false
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: "", art: .auswahl(wahl), vorgabe: vorgabe, wirkung: .sofort,
                  kiFreigegeben: ki, erweitert: erweitert, anlagenweit: false)
    }

    private static func text(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, laenge: Int, vorgabe: String = "", hilfe: String, erweitert: Bool = false,
        wirkung: Parameter.Wirkung = .sofort
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: "", art: .text(laenge: laenge), vorgabe: .text(vorgabe), wirkung: wirkung,
                  kiFreigegeben: false, erweitert: erweitert, anlagenweit: false)
    }

    private static func kennwort(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, laenge: Int, mindestens: Int = 0, vorgabe: String = "", hilfe: String,
        wirkung: Parameter.Wirkung = .sofort
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: "", art: .kennwort(laenge: laenge, mindestens: mindestens), vorgabe: .text(vorgabe),
                  wirkung: wirkung, kiFreigegeben: false, erweitert: false, anlagenweit: false)
    }

    // MARK: Gemeinsam

    private static func netz(_ g: Geraeteart, hostname: String) -> [Parameter] {
        [
            text(g, .geraet, ["wifi", "ssid"], "netz", "WLAN-Name", laenge: 32,
                 hilfe: "Das Heimnetz, mit dem sich das Gerät verbindet.", wirkung: .neuverbindung),
            kennwort(g, .geraet, ["wifi", "pass"], "netz", "WLAN-Kennwort", laenge: 63,
                     hilfe: "Leer lassen, um das gespeicherte Kennwort zu behalten.", wirkung: .neuverbindung),
            text(g, .geraet, ["wifi", "hostname"], "netz", "Gerätename im Netz", laenge: 31, vorgabe: hostname,
                 hilfe: "Das Gerät meldet sich unter diesem Namen beim Router. Den Namen mit „.local“ trägt es erst nach einem Neustart.",
                 wirkung: .neuverbindung),
            kennwort(g, .geraet, ["wifi", "ap_pass"], "netz", "Kennwort des Zugangspunkts", laenge: 63, mindestens: 8,
                     vorgabe: "fussboden",
                     hilfe: "Gilt für das eigene WLAN, das das Gerät zur Einrichtung öffnet. Mindestens acht Zeichen; es wirkt beim nächsten Öffnen des Zugangspunkts.",
                     wirkung: .neustart),
        ]
    }

    private static func mqtt(_ g: Geraeteart, praefix: String) -> [Parameter] {
        [
            schalter(g, .geraet, ["mqtt", "enabled"], "mqtt", "MQTT verwenden", vorgabe: false,
                     hilfe: "Für Home Assistant. Die Entitäten meldet das Gerät selbst an.", wirkung: .neustart),
            text(g, .geraet, ["mqtt", "uri"], "mqtt", "Broker", laenge: 127,
                 hilfe: "Etwa mqtt://192.168.1.10:1883. Ohne Broker lässt sich MQTT nicht einschalten.", wirkung: .neustart),
            text(g, .geraet, ["mqtt", "user"], "mqtt", "Benutzer", laenge: 31, hilfe: "Anmeldung am Broker.", wirkung: .neustart),
            kennwort(g, .geraet, ["mqtt", "pass"], "mqtt", "Kennwort", laenge: 63,
                     hilfe: "Leer lassen, um das gespeicherte Kennwort zu behalten.", wirkung: .neustart),
            text(g, .geraet, ["mqtt", "prefix"], "mqtt", "Themenpräfix", laenge: 31, vorgabe: praefix,
                 hilfe: "Vorsilbe aller Themen dieses Geräts.", wirkung: .neustart),
        ]
    }

    private static func zeit(_ g: Geraeteart, gruppe: String, neustartStunde: Int, schutz: String) -> [Parameter] {
        [
            text(g, .geraet, ["timezone"], gruppe, abschnitt: "Zeit und Neustart", "Zeitzone", laenge: 47,
                 vorgabe: "CET-1CEST,M3.5.0,M10.5.0/3",
                 hilfe: "POSIX-Angabe; die Vorgabe gilt für Mitteleuropa. Sie bestimmt, wann ein Tag im Protokoll endet und wann Termine fallen.",
                 erweitert: true, wirkung: .neustart),
            ganz(g, .geraet, ["reboot_hour"], gruppe, abschnitt: "Zeit und Neustart", "Täglicher Neustart, Stunde", "Uhr", -1...23,
                 vorgabe: neustartStunde,
                 hilfe: "−1 schaltet den täglichen Neustart ab. Er ist zulässig, aber nicht nötig; er entfällt, solange \(g == .verteiler ? "ein Ventil fährt" : "eine Pumpe in einer Mindestlaufzeit steht").",
                 erweitert: true, wirkung: .neustart),
            ganz(g, .geraet, ["reboot_minute"], gruppe, abschnitt: "Zeit und Neustart", "Täglicher Neustart, Minute", "min", 0...59,
                 vorgabe: 0, hilfe: "Minute des täglichen Neustarts.", erweitert: true, wirkung: .neustart),
            auswahl(g, .geraet, ["seize_weekday"], gruppe, abschnitt: schutz, "Wochentag", wochentage, vorgabe: 6,
                    hilfe: g == .verteiler
                        ? "Im Sommer stehen die Ventile über Monate geschlossen. Zum Termin fährt jeder Kreis einmal auf Anschlag auf, wieder zu und zurück. Übergangen wird, wer seit der letzten Fahrt ohnehin gefahren ist, von Hand gehalten wird oder gerade vermessen wird."
                        : "Im Sommer stehen die Umwälzpumpen über Monate still. Zum Termin läuft jede Pumpe eines Heizkreises drei Minuten; übergangen wird, wer am selben Tag ohnehin gelaufen ist.",
                    ki: true),
            ganz(g, .geraet, ["seize_hour"], gruppe, abschnitt: schutz, "Stunde", "Uhr", 0...23, vorgabe: 11,
                 hilfe: "Uhrzeit des Termins, Ortszeit.", ki: true),
        ]
    }

    // MARK: Verteiler

    public static let verteiler: [Parameter] = {
        let v = Geraeteart.verteiler
        return [
            // Regelung je Raum
            zahl(v, .raum, ["target_c"], "raum", "Sollwert", "°C", 5...35, schritt: 0.5, stellen: 1, vorgabe: 20,
                 hilfe: "Solltemperatur des Raums.", ki: true),
            auswahl(v, .raum, ["mode"], "raum", "Betriebsart", [.init("heat", "Regelung aktiv"), .init("off", "ausgeschaltet")],
                    vorgabe: "heat", hilfe: "Ausgeschaltet fährt der Verteiler die Ventile des Raums zu.", ki: true),
            zahl(v, .raum, ["p_band_k"], "raum", "Proportionalband", "K", 0.2...10, schritt: 0.1, stellen: 1, vorgabe: 1.0,
                 hilfe: "Abstand zum Sollwert, über den die Ventile fahren: am Sollwert halb offen, ein Band darunter ganz offen, eines darüber ganz zu.",
                 ki: true),
            ganz(v, .raum, ["interval_s"], "raum", "Prüfintervall", "s", 5...3600, schritt: 5, vorgabe: 30,
                 hilfe: "Abstand zwischen zwei Regelläufen des Raums.", ki: true, erweitert: true),
            zahl(v, .raum, ["step"], "raum", "Rasterung", "", 0.01...0.5, schritt: 0.01, stellen: 2, vorgabe: 0.1,
                 hilfe: "Die Zielstellung wird auf dieses Raster gerundet; kleinere Werte bewegen die Ventile öfter.",
                 ki: true, erweitert: true),
            zahl(v, .raum, ["min_delta"], "raum", "Mindeständerung", "", 0...0.5, schritt: 0.01, stellen: 2, vorgabe: 0.01,
                 hilfe: "Kleinere Änderungen der Zielstellung lösen keine Fahrt aus.", ki: true, erweitert: true),

            // Kanäle
            ms("open_ms", "Fahrzeit öffnen", 1...300, vorgabe: 39000,
               hilfe: "Zeit von ganz zu bis ganz offen. Kalibrierte Werte stammen aus einer Messfahrt."),
            ms("close_ms", "Fahrzeit schließen", 1...300, vorgabe: 40000, hilfe: "Zeit von ganz offen bis ganz zu."),
            ms("max_ms", "Maximallaufzeit", 1...600, vorgabe: 45000,
               hilfe: "Spätestens dann schaltet der Verteiler den Motor ab. Mindestens so lang wie die längere Fahrzeit."),
            ms("blank_ms", "Sperrzeit", 0...30, vorgabe: 2000,
               hilfe: "So lange nach dem Anlaufen wird die Gegenspannung nicht ausgewertet; 0 wirkt wie 2 s."),
            ganz(v, .kanal, ["bemf_mv"], "kanal", "Auslöseschwelle", "mV", 10...2200, vorgabe: 190,
                 hilfe: "Steigt die Gegenspannung über diesen Wert, gilt die Endlage als erreicht."),
            ganz(v, .kanal, ["bemf_hyst_mv"], "kanal", "Hysterese", "mV", 0...2200, vorgabe: 30,
                 hilfe: "Höchstens so groß wie die Auslöseschwelle."),

            // Betrieb
            text(v, .geraet, ["site"], "betrieb", "Bezeichnung der Anlage", laenge: 31,
                 hilfe: "Steht in der Kopfzeile und im Gerätenamen für Home Assistant und unterscheidet die Verteiler im Haus. Im Netz erscheint der neue Name nach einem Neustart."),
            ganz(v, .geraet, ["sensor_timeout_s"], "betrieb", "Messwert veraltet nach", "s", 60...43200, schritt: 60, vorgabe: 900,
                 hilfe: "Älter als diese Zeit gilt ein Messwert als ungültig; der Raum wird dann nicht geregelt, die Ventile bleiben stehen.",
                 ki: true),
            ganz(v, .geraet, ["display_brightness"], "betrieb", "Helligkeit der Anzeige", "%", 0...100, vorgabe: 2,
                 hilfe: "Helligkeit der Anzeige am Gehäuse.", erweitert: true),

            // Tasten
            schalter(v, .geraet, ["touch", "enabled"], "tasten", "Tasten am Gerät verwenden", vorgabe: true,
                     hilfe: "Die drei Berührflächen am Gehäuse: Sollwert senken, Sollwert erhöhen, Raum wählen.", wirkung: .neustart),
            ganz(v, .geraet, ["touch", "thresholds", "0"], "tasten", "Schwelle „Sollwert senken“", "", 0...65535, vorgabe: 1000,
                 hilfe: "Eine Taste gilt als berührt, sobald ihr Rohwert unter die Schwelle fällt. Setzen Sie die Schwelle etwa mittig zwischen Ruhe- und Berührwert."),
            ganz(v, .geraet, ["touch", "thresholds", "1"], "tasten", "Schwelle „Sollwert erhöhen“", "", 0...65535, vorgabe: 870,
                 hilfe: "Wie oben, für die mittlere Fläche."),
            ganz(v, .geraet, ["touch", "thresholds", "2"], "tasten", "Schwelle „Raum wählen“", "", 0...65535, vorgabe: 1000,
                 hilfe: "Wie oben, für die dritte Fläche."),
        ]
        + zeit(v, gruppe: "zeit", neustartStunde: 10, schutz: "Schutzfahrt")
        + netz(v, hostname: "floor-heating")
        + mqtt(v, praefix: "fbh")
    }()

    // MARK: Heizungsgerät

    public static let heizung: [Parameter] = {
        let h = Geraeteart.heizung
        let gpio = "Nicht geeignet sind GPIO 6 bis 11 (Flash-Speicher), 34 bis 39 (nur Eingang), 1 und 3 (serielle Schnittstelle) sowie 12 (Flash-Spannung beim Start). −1 heißt: nicht belegt. Der Bus braucht einen Widerstand von 4,7 kΩ nach 3,3 V."
        return [
            text(h, .geraet, ["site"], "geraet", "Bezeichnung", laenge: 31,
                 hilfe: "Steht in der Kopfzeile und macht das Gerät für die anderen Platinen im Haus auffindbar."),

            // 1-Wire-Bus
            ganz(h, .geraet, ["onewire_pin", "0"], "bus", "Bus 1 (GPIO)", "", -1...33, vorgabe: 13, hilfe: gpio),
            ganz(h, .geraet, ["onewire_pin", "1"], "bus", "Bus 2 (GPIO)", "", -1...33, vorgabe: -1, hilfe: gpio),
            ganz(h, .geraet, ["poll_s"], "bus", "Abtastabstand", "s", 1...600, vorgabe: 10,
                 hilfe: "Abstand zwischen zwei Messrunden aller Fühler."),

            // Bedarfsabfrage
            ganz(h, .geraet, ["demand_poll_s"], "bedarf", "Abfrage alle", "s", 1...300, vorgabe: 5,
                 hilfe: "Wie oft das Gerät die versorgten Verteiler nach Wärmebedarf fragt.", ki: true),
            ganz(h, .geraet, ["demand_timeout_s"], "bedarf", "Zeitgrenze", "s", 10...3600, schritt: 10, vorgabe: 180,
                 hilfe: "Antwortet ein Verteiler, der schon einmal geantwortet hat, länger nicht, gilt sein Kreis als mit Bedarf: die Pumpe läuft im Zweifel.",
                 ki: true),

            // Brennererkennung
            zahl(h, .geraet, ["burner", "delta_on_k"], "brenner", "Einschaltschwelle", "K", 1...100, schritt: 0.5, stellen: 1, vorgabe: 12,
                 hilfe: "Abgas über der Bezugslinie, ab dem der Brenner als angelaufen gilt. Muss über der Ausschaltschwelle liegen.",
                 ki: true),
            zahl(h, .geraet, ["burner", "delta_off_k"], "brenner", "Ausschaltschwelle", "K", 0...99, schritt: 0.5, stellen: 1, vorgabe: 6,
                 hilfe: "Muss unter der Einschaltschwelle liegen.", ki: true),
            zahl(h, .geraet, ["burner", "swing_k"], "brenner", "Ausschlag", "K", 1...60, schritt: 0.5, stellen: 1, vorgabe: 6,
                 hilfe: "Anstieg über den Tiefstwert beim Anlaufen und Abfall unter den Höchstwert der Fahrt, an dem das Ende erkannt wird, auch bei warmem Kessel.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["burner", "on_hold_s"], "brenner", "Haltezeit an", "s", 0...3600, schritt: 10, vorgabe: 60,
                 hilfe: "So lange muss die Einschaltbedingung anstehen.", ki: true, erweitert: true),
            ganz(h, .geraet, ["burner", "off_hold_s"], "brenner", "Haltezeit aus", "s", 0...3600, schritt: 10, vorgabe: 300,
                 hilfe: "So lange muss die Ausschaltbedingung anstehen.", ki: true, erweitert: true),
            zahl(h, .geraet, ["burner", "duese_l_h"], "brenner", "Düsendurchsatz", "l/h", 0...20, schritt: 0.05, stellen: 2, vorgabe: 2.2,
                 hilfe: "Grundlage der Verbrauchsschätzung. Zweimal im Jahr am Tank abgelesen, wird aus der Annahme eine Messung.",
                 ki: true),

            // Pufferspeicher
            zahl(h, .geraet, ["buffer", "leer_c"], "speicher", abschnitt: "Leer und voll", "Leer bei", "°C", 15...90, schritt: 0.5, stellen: 1,
                 vorgabe: 35,
                 hilfe: "Die Temperatur, bei der der Kessel von sich aus nachheizt: der Nullpunkt der Anzeige. Mit „Leerpunkt selbst nachmessen“ zieht das Gerät den Wert nach.",
                 ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "voll_c"], "speicher", abschnitt: "Leer und voll", "Voll bei", "°C", 20...95, schritt: 0.5, stellen: 1,
                 vorgabe: 62, hilfe: "Speichertemperatur, die als 100 % gilt. Muss über „leer“ liegen.", ki: true, anlagenweit: true),
            schalter(h, .geraet, ["buffer", "leer_lernen"], "speicher", abschnitt: "Leer und voll", "Leerpunkt selbst nachmessen",
                     vorgabe: true,
                     hilfe: "Beim Anlaufen des Brenners nach echtem Verbrauch misst das Gerät den Leerpunkt selbst.",
                     ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "lern_drop_k"], "speicher", abschnitt: "Leer und voll", "Nachmessen ab Abfall", "K", 0.5...30,
                 schritt: 0.5, stellen: 1, vorgabe: 3,
                 hilfe: "Ein Brennerstart zählt nur, wenn der Speicher vorher mindestens so weit gefallen ist.",
                 ki: true, erweitert: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "warn_c"], "speicher", abschnitt: "Warmwasser", "Warnung Warmwasser", "°C", 20...80, schritt: 1,
                 stellen: 0, vorgabe: 40, hilfe: "Darunter meldet das Gerät „Warmwasserreserve knapp“.", ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "volumen_l"], "speicher", abschnitt: "Warmwasser", "Speicherinhalt", "l", 0...20000, schritt: 50,
                 stellen: 0, vorgabe: 0, hilfe: "Dient nur der Umrechnung von Zapfungen in Kilowattstunden; ohne Inhalt zählt das Gerät in Kelvin.",
                 ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "zapf_drop_k"], "speicher", abschnitt: "Warmwasser", "Zapfung ab Einbruch", "K", 0.2...40,
                 schritt: 0.1, stellen: 1, vorgabe: 2,
                 hilfe: "Eine Zapfung lässt den Speicher steil einbrechen, der Stillstand langsam absinken; getrennt werden beide an dieser Schwelle.",
                 ki: true, erweitert: true, anlagenweit: true),
            ganz(h, .geraet, ["buffer", "zapf_win_s"], "speicher", abschnitt: "Warmwasser", "im Fenster", "s", 60...7200, schritt: 60,
                 vorgabe: 900, hilfe: "Zeitfenster, in dem der Einbruch liegen muss.", ki: true, erweitert: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "spread_full_k"], "speicher", abschnitt: "Wann eine Ladung fertig ist", "Spreizung „voll“", "K",
                 1...40, schritt: 0.5, stellen: 1, vorgabe: 8,
                 hilfe: "Nähert sich der Kesselrücklauf dem Vorlauf bis auf diesen Abstand, nimmt der Speicher keine Wärme mehr auf.",
                 ki: true, erweitert: true, anlagenweit: true),
            ganz(h, .geraet, ["buffer", "spread_hold_s"], "speicher", abschnitt: "Wann eine Ladung fertig ist", "Haltezeit", "s",
                 0...3600, schritt: 10, vorgabe: 300, hilfe: "So lange muss die Spreizung unterschritten sein.",
                 ki: true, erweitert: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "kessel_hot_c"], "speicher", abschnitt: "Wann eine Ladung fertig ist", "Vorlauf gilt als heiß ab",
                 "°C", 20...95, schritt: 1, stellen: 0, vorgabe: 60,
                 hilfe: "Verlangt einen heißen Vorlauf: beim Anfahren aus dem kalten Kessel liegen Vor- und Rücklauf ebenfalls dicht beieinander.",
                 ki: true, erweitert: true, anlagenweit: true),

            // Kesselkreispumpe
            schalter(h, .geraet, ["boiler_pump", "enabled"], "kkp", "Kesselkreispumpe vorhanden", vorgabe: false,
                     hilfe: "Die Pumpe zwischen Kessel und Speicher. Braucht Kesselvor- und -rücklauf an diesem Gerät."),
            zahl(h, .geraet, ["boiler_pump", "on_k"], "kkp", abschnitt: "Schaltpunkte", "Ein ab Spreizung", "K", 0.1...20, schritt: 0.1,
                 stellen: 1, vorgabe: 3, hilfe: "Kesselvorlauf über dem Rücklauf aus dem Speicher, ab dem die Pumpe läuft.", ki: true),
            zahl(h, .geraet, ["boiler_pump", "off_k"], "kkp", abschnitt: "Schaltpunkte", "Aus unter", "K", -20...19.9, schritt: 0.1,
                 stellen: 1, vorgabe: 2,
                 hilfe: "Muss unter der Einschaltschwelle liegen; dazwischen bleibt es, wie es war.", ki: true),
            ganz(h, .geraet, ["boiler_pump", "hold_s"], "kkp", abschnitt: "Schaltpunkte", "Haltezeit", "s", 0...3600, schritt: 10,
                 vorgabe: 120, hilfe: "So lange muss eine Bedingung anstehen, bevor die Pumpe umschaltet.", ki: true, erweitert: true),
            ganz(h, .geraet, ["boiler_pump", "min_run_s"], "kkp", abschnitt: "Schaltpunkte", "Mindestlaufzeit", "s", 0...3600, schritt: 10,
                 vorgabe: 180, hilfe: "Kürzer läuft die Pumpe nicht.", ki: true, erweitert: true),
            ganz(h, .geraet, ["boiler_pump", "min_pause_s"], "kkp", abschnitt: "Schaltpunkte", "Mindestpause", "s", 0...3600, schritt: 10,
                 vorgabe: 180, hilfe: "Kürzer steht die Pumpe nicht.", ki: true, erweitert: true),
            zahl(h, .geraet, ["boiler_pump", "emergency_c"], "kkp", abschnitt: "Schaltpunkte", "Notgrenze", "°C", 60...110, schritt: 1,
                 stellen: 0, vorgabe: 85,
                 hilfe: "Über diesem Kesselvorlauf läuft die Pumpe in jedem Fall: ein klemmender Fühler darf die Wärmeabfuhr nicht verhindern. Nicht für Vorschläge der KI freigegeben.",
                 erweitert: true),
            text(h, .geraet, ["boiler_pump", "topic"], "kkp", abschnitt: "Relais", "Relais-Thema (MQTT)", laenge: 47,
                 hilfe: "Geschaltet wird über MQTT, sobald ein Broker eingerichtet ist, sonst unmittelbar über die Adresse des Relais."),
            text(h, .geraet, ["boiler_pump", "host"], "kkp", abschnitt: "Relais", "Relais-Adresse (HTTP)", laenge: 47,
                 hilfe: "Adresse des Tasmota-Relais im Heimnetz."),
            ganz(h, .geraet, ["boiler_pump", "relay"], "kkp", abschnitt: "Relais", "Relaisnummer", "", 1...8, vorgabe: 1,
                 hilfe: "Kanal des Relais, an dem die Pumpe hängt."),
            text(h, .geraet, ["boiler_pump", "user"], "kkp", abschnitt: "Relais", "Benutzer am Relais", laenge: 23,
                 hilfe: "Nur, wenn das Relais eine Anmeldung verlangt."),
            kennwort(h, .geraet, ["boiler_pump", "pass"], "kkp", abschnitt: "Relais", "Kennwort am Relais", laenge: 47,
                     hilfe: "Leer lassen, um das gespeicherte Kennwort zu behalten."),

            // Heizkreise
            text(h, .heizkreis, ["name"], "heizkreis", "Name", laenge: 31, hilfe: "Anzeigename des Heizkreises."),
            ganz(h, .heizkreis, ["overrun_s"], "heizkreis", abschnitt: "Pumpenlogik", "Nachlauf", "s", 0...3600, schritt: 10, vorgabe: 300,
                 hilfe: "So lange läuft die Pumpe nach dem letzten Bedarf weiter.", ki: true),
            ganz(h, .heizkreis, ["min_run_s"], "heizkreis", abschnitt: "Pumpenlogik", "Mindestlaufzeit", "s", 0...3600, schritt: 10,
                 vorgabe: 180, hilfe: "Kürzer läuft die Pumpe nicht.", ki: true, erweitert: true),
            ganz(h, .heizkreis, ["min_pause_s"], "heizkreis", abschnitt: "Pumpenlogik", "Mindestpause", "s", 0...3600, schritt: 10,
                 vorgabe: 180, hilfe: "Kürzer steht die Pumpe nicht.", ki: true, erweitert: true),
            zahl(h, .heizkreis, ["min_buffer_c"], "heizkreis", abschnitt: "Pumpenlogik", "Speicher mindestens", "°C", 0...90, schritt: 1,
                 stellen: 0, vorgabe: 40,
                 hilfe: "Darunter bleibt die Pumpe aus. Liegt bewusst über der nötigen Vorlauftemperatur, weil der Mischer nur herunterregeln kann.",
                 ki: true),
            zahl(h, .heizkreis, ["frost_c"], "heizkreis", abschnitt: "Pumpenlogik", "Frostgrenze", "°C", -10...20, schritt: 1, stellen: 0,
                 vorgabe: 6,
                 hilfe: "Unterschreitet ein Raum diesen Wert, läuft die Pumpe auch auf „Aus“. Nicht für Vorschläge der KI freigegeben.",
                 erweitert: true),
            text(h, .heizkreis, ["pump", "topic"], "heizkreis", abschnitt: "Relais", "Relais-Thema (MQTT)", laenge: 47,
                 hilfe: "Mit Broker meldet das Relais jede Änderung von selbst, auch eine von Hand am Gerät."),
            text(h, .heizkreis, ["pump", "host"], "heizkreis", abschnitt: "Relais", "Relais-Adresse (HTTP)", laenge: 47,
                 hilfe: "Ohne Broker geht der Befehl unmittelbar an die Weboberfläche des Relais."),
            ganz(h, .heizkreis, ["pump", "relay"], "heizkreis", abschnitt: "Relais", "Relaisnummer", "", 1...8, vorgabe: 1,
                 hilfe: "Kanal des Relais, an dem die Pumpe hängt."),
            text(h, .heizkreis, ["pump", "user"], "heizkreis", abschnitt: "Relais", "Benutzer am Relais", laenge: 23,
                 hilfe: "Nur, wenn das Relais eine Anmeldung verlangt."),
            kennwort(h, .heizkreis, ["pump", "pass"], "heizkreis", abschnitt: "Relais", "Kennwort am Relais", laenge: 47,
                     hilfe: "Leer lassen, um das gespeicherte Kennwort zu behalten."),

            // Fühler
            auswahl(h, .fuehler, ["role"], "fuehler", "Rolle", rollen, vorgabe: "",
                    hilfe: "Jede Rolle nur einmal. Nach der Rolle richten sich Brennererkennung, Ladung und Heizkreise."),
            text(h, .fuehler, ["name"], "fuehler", "Name", laenge: 31, hilfe: "Leer übernimmt das Gerät den Namen der Rolle."),
            zahl(h, .fuehler, ["offset_k"], "fuehler", "Korrektur", "K", -20...20, schritt: 0.1, stellen: 1, vorgabe: 0,
                 hilfe: "Wird zum Messwert addiert, etwa wenn ein Anlegefühler schlecht am Rohr anliegt."),
        ]
        + zeit(h, gruppe: "zeit", neustartStunde: -1, schutz: "Schutzlauf der Pumpen")
        + netz(h, hostname: "heizung")
        + mqtt(h, praefix: "heiz")
    }()
}
