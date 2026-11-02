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
        /// Gespeichert in Sekunden, angezeigt und vorgeschlagen in ganzen Minuten
        case minuten(bereich: ClosedRange<Int>, schritt: Int)
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

    /// Bereich und Schritt in Minuten, Vorgabe in Sekunden wie in der Konfiguration
    private static func minuten(
        _ g: Geraeteart, _ ort: Parameter.Ort, _ pfad: [String], _ gruppe: String, abschnitt: String? = nil,
        _ bezeichnung: String, _ bereich: ClosedRange<Int>, schritt: Int = 1, vorgabeSekunden: Int, hilfe: String,
        ki: Bool = false, erweitert: Bool = false
    ) -> Parameter {
        Parameter(geraet: g, ort: ort, pfad: pfad, gruppe: gruppe, abschnitt: abschnitt, bezeichnung: bezeichnung,
                  hilfe: hilfe, einheit: "min", art: .minuten(bereich: bereich, schritt: schritt),
                  vorgabe: .zahl(Double(vorgabeSekunden)), wirkung: .sofort, kiFreigegeben: ki, erweitert: erweitert,
                  anlagenweit: false)
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
            kennwort(g, .geraet, ["wifi", "ap_pass"], "netz", "Kennwort des Einrichtungszugangs", laenge: 63, mindestens: 8,
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
            ganz(g, .geraet, ["reboot_hour"], gruppe, abschnitt: "Zeit und Neustart", "Täglicher Neustart um", "Uhr", -1...23,
                 vorgabe: neustartStunde,
                 hilfe: g == .verteiler
                    ? "Nicht nötig; −1 schaltet ihn ab. Er entfällt für diesen Tag, wenn in dieser Minute ein Ventil fährt oder eine Messfahrt läuft."
                    : "Nicht nötig; −1 schaltet ihn ab. Er entfällt für diesen Tag, wenn in dieser Minute eine Heizkreispumpe in ihrer Mindestlaufzeit steht. Ein Neustart bricht den Nachlauf der Kesselkreispumpe und die laufende Ladung im Protokoll ab.",
                 erweitert: true, wirkung: .neustart),
            ganz(g, .geraet, ["reboot_minute"], gruppe, abschnitt: "Zeit und Neustart", "Täglicher Neustart, Minute", "min", 0...59,
                 vorgabe: 0, hilfe: "Minute des täglichen Neustarts.", erweitert: true, wirkung: .neustart),
            auswahl(g, .geraet, ["seize_weekday"], gruppe, abschnitt: schutz, "Wochentag", wochentage, vorgabe: 6,
                    hilfe: g == .verteiler
                        ? "Im Sommer stehen die Ventile über Monate geschlossen. Zum Termin fährt jeder Kreis auf (bis 92 %), auf Anschlag zu und zurück auf seine vorherige Stellung, damit er nicht festsitzt. Übergangen wird ein Kreis, der seit der letzten Schutzfahrt ohnehin gefahren ist, von Hand gehalten oder gerade vermessen wird; nach einem Neustart fahren alle."
                        : "Im Sommer stehen die Heizkreispumpen über Monate still. Zum Termin läuft jede drei Minuten, die in den letzten 24 Stunden nicht gelaufen ist und nicht auf Hand aus steht. Die Kesselkreispumpe hat keinen Schutzlauf.",
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
            zahl(v, .raum, ["target_c"], "raum", "Solltemperatur", "°C", 5...35, schritt: 0.5, stellen: 1, vorgabe: 20,
                 hilfe: "Die Raumtemperatur, auf die der Verteiler regelt. Am Sollwert stehen die Ventile halb offen; braucht der Raum mehr oder weniger Wärme, pendelt er sich etwas darunter oder darüber ein.",
                 ki: true),
            auswahl(v, .raum, ["mode"], "raum", "Betriebsart", [.init("heat", "Regelung aktiv"), .init("off", "ausgeschaltet")],
                    vorgabe: "heat",
                    hilfe: "„Ausgeschaltet“ fährt die Ventile des Raums zu; er meldet keinen Wärmebedarf. Einen eigenen Frostschutz hat der Verteiler nicht, den übernimmt das Heizungsgerät mit „Heizkreis › Frostschutz unter Raumtemperatur“.",
                    ki: true),
            zahl(v, .raum, ["p_band_k"], "raum", "Regelbereich", "K", 0.2...10, schritt: 0.1, stellen: 1, vorgabe: 1.0,
                 hilfe: "Wie weit die Raumtemperatur vom Sollwert abweichen darf, bis die Ventile ganz öffnen oder schließen: am Sollwert halb offen, etwa einen Regelbereich darunter offen, darüber zu. Kleiner: Die Ventile reagieren schärfer und fahren öfter, die Temperatur schwankt stärker. Größer: ruhiger, aber der Raum weicht weiter vom Sollwert ab.",
                 ki: true),
            ganz(v, .raum, ["interval_s"], "raum", "Regelung prüft alle", "s", 5...3600, schritt: 5, vorgabe: 30,
                 hilfe: "Abstand zwischen zwei Regelschritten des Raums. Kürzer: schnellere Reaktion auf neue Messwerte. Länger: Ein hier geänderter Sollwert wirkt später; über die Übersicht oder die Tasten geändert, wirkt er sofort.",
                 ki: true, erweitert: true),
            zahl(v, .raum, ["step"], "raum", "Stellschritt der Ventile", "", 0.01...0.5, schritt: 0.01, stellen: 2, vorgabe: 0.1,
                 hilfe: "Die Ventilstellung wird auf Schritte dieser Größe gerundet; erst wenn sich der gerundete Wert ändert, fährt das Ventil. 0,1 entspricht etwa 4 s Motorlauf. Kleiner: viele kleine Fahrten, mehr Verschleiß, die geschätzte Stellung wird ungenauer. Größer: wenige grobe Sprünge.",
                 ki: true, erweitert: true),
            zahl(v, .raum, ["min_delta"], "raum", "Kleinste Fahrt", "", 0...0.5, schritt: 0.01, stellen: 2, vorgabe: 0.01,
                 hilfe: "Kleinere Abweichungen zwischen Ziel und Stellung lösen keine Fahrt aus. Wirkt erst, wenn der Wert mindestens so groß ist wie der Stellschritt.",
                 ki: true, erweitert: true),

            // Kanäle
            ms("open_ms", "Fahrzeit öffnen", 1...300, vorgabe: 39000,
               hilfe: "Zeit des Antriebs von ganz zu bis ganz offen; daraus schätzt der Verteiler die Ventilstellung. Die Messfahrt trägt den Wert ein. Zu kurz: Das Ventil öffnet weniger weit als angezeigt, der Raum bleibt kühler. Zu lang: Es öffnet weiter als angezeigt."),
            ms("close_ms", "Fahrzeit schließen", 1...300, vorgabe: 40000,
               hilfe: "Zeit von ganz offen bis ganz zu. Zu kurz: Das Ventil bleibt vor dem Sitz stehen, es fließt weiter Wasser, obwohl 0 % angezeigt wird."),
            ms("max_ms", "Höchstlaufzeit bis Anschlag", 1...600, vorgabe: 45000,
               hilfe: "Spätestens dann schaltet der Verteiler den Motor ab, wenn er die Endlage nicht erkennt. Gilt nur für Fahrten bis zum Anschlag (Notfahrt, Schutzfahrt, Handsteuerung). Mindestens so lang wie die längere Fahrzeit."),
            ms("blank_ms", "Anlauf ohne Endlagenerkennung", 0...30, vorgabe: 2000,
               hilfe: "So lange nach dem Anlaufen wird die Endlage nicht ausgewertet, weil der Anlaufstrom sie sonst vortäuschen würde. 0 wirkt wie 2 s. Zu kurz: Fahrten enden sofort. Zu lang: Eine kurze Fahrt in den Anschlag bleibt unerkannt."),
            ganz(v, .kanal, ["bemf_mv"], "kanal", "Endlage erkannt ab", "mV", 10...2200, vorgabe: 190,
                 hilfe: "Steigt die Spannung am Messeingang über diesen Wert, gilt die Endlage als erreicht. Die Messfahrt trägt den Wert ein. Zu niedrig: Fahrten enden vor der Endlage, die angezeigte Stellung stimmt nicht. Zu hoch: Der Motor drückt bis zum Ende der Fahrzeit gegen den Anschlag. Wirksam bis etwa 1750 mV."),
            ganz(v, .kanal, ["bemf_hyst_mv"], "kanal", "Rückfall der Endlagenerkennung", "mV", 0...2200, vorgabe: 30,
                 hilfe: "Nach dem Auslösen muss die Spannung um diesen Betrag unter die Schwelle fallen, bevor die Endlage erneut erkannt wird. Höchstens so groß wie „Endlage erkannt ab“. Zu groß: Nach dem Anlaufen wird die Endlage nicht mehr erkannt."),

            // Betrieb
            text(v, .geraet, ["site"], "betrieb", "Bezeichnung", laenge: 31,
                 hilfe: "Steht in der Kopfzeile und im Gerätenamen für Home Assistant und unterscheidet die Verteiler im Haus. Im Netz erscheint der neue Name nach einem Neustart."),
            ganz(v, .geraet, ["sensor_timeout_s"], "betrieb", "Thermometer gilt als ausgefallen nach", "s", 60...43200, schritt: 60, vorgabe: 900,
                 hilfe: "Ist der letzte Messwert eines Raums älter, setzt dessen Regelung aus: Die Ventile bleiben stehen, der Raum meldet keinen Wärmebedarf. Höher: Bei ausgefallenem Thermometer wird länger nach einem alten Wert geregelt. Niedriger: Bei selten sendenden Thermometern setzt die Regelung öfter aus.",
                 ki: true),
            ganz(v, .geraet, ["display_brightness"], "betrieb", "Helligkeit der Anzeige", "%", 0...100, vorgabe: 2,
                 hilfe: "Helligkeit der Anzeige am Gehäuse; 0 ist praktisch dunkel.", erweitert: true),

            // Tasten
            schalter(v, .geraet, ["touch", "enabled"], "tasten", "Tasten am Gerät verwenden", vorgabe: true,
                     hilfe: "Die drei Berührflächen am Gehäuse: Sollwert senken, Sollwert erhöhen, Raum wählen.", wirkung: .neustart),
            ganz(v, .geraet, ["touch", "thresholds", "0"], "tasten", "Schwelle „Sollwert senken“", "", 0...65535, vorgabe: 1000,
                 hilfe: "Eine Taste gilt als berührt, sobald ihr Rohwert unter die Schwelle fällt. Lesen Sie unter „Sensoren“ Ruhe- und Berührwert ab und setzen Sie die Schwelle etwa mittig dazwischen."),
            ganz(v, .geraet, ["touch", "thresholds", "1"], "tasten", "Schwelle „Sollwert erhöhen“", "", 0...65535, vorgabe: 870,
                 hilfe: "Eine Taste gilt als berührt, sobald ihr Rohwert unter die Schwelle fällt. Lesen Sie unter „Sensoren“ Ruhe- und Berührwert ab und setzen Sie die Schwelle etwa mittig dazwischen."),
            ganz(v, .geraet, ["touch", "thresholds", "2"], "tasten", "Schwelle „Raum wählen“", "", 0...65535, vorgabe: 1000,
                 hilfe: "Eine Taste gilt als berührt, sobald ihr Rohwert unter die Schwelle fällt. Lesen Sie unter „Sensoren“ Ruhe- und Berührwert ab und setzen Sie die Schwelle etwa mittig dazwischen."),
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
            ganz(h, .geraet, ["poll_s"], "bus", "Fühler messen alle", "s", 1...600, vorgabe: 10,
                 hilfe: "Abstand zwischen zwei Messrunden aller Fühler am Bus. Feiner als in diesem Abstand können Brennererkennung, Pumpen und Notlauf nicht reagieren.",
                 erweitert: true),

            // Bedarfsabfrage
            ganz(h, .geraet, ["demand_poll_s"], "bedarf", "Bedarf abfragen alle", "s", 1...300, vorgabe: 5,
                 hilfe: "Wie oft das Gerät die versorgten Verteiler nach Wärmebedarf fragt. Höher: Pumpen starten und stoppen bis zu diesem Abstand später. Niedriger: mehr Netzlast.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["demand_timeout_s"], "bedarf", "Verteiler gilt als verstummt nach", "s", 10...3600, schritt: 10, vorgabe: 180,
                 hilfe: "Antwortet ein Verteiler, der schon einmal geantwortet hat, so lange nicht, gilt sein Kreis als mit Bedarf: Die Pumpe läuft im Zweifel. Niedriger: Kurze WLAN-Aussetzer lassen die Pumpe anlaufen. Höher: Ein Ausfall wird später bemerkt.",
                 ki: true, erweitert: true),

            // Brennererkennung
            zahl(h, .geraet, ["burner", "delta_on_k"], "brenner", "Start ab Abgas über Ruhewert", "K", 1...100, schritt: 0.5, stellen: 1, vorgabe: 12,
                 hilfe: "Der Brenner gilt als gestartet, wenn das Abgas so weit über seinem Ruhewert liegt, dem kältesten Wert der letzten 24 Stunden. Höher: Starts werden später oder bei kurzen Läufen gar nicht erkannt, Laufzeit und Verbrauch fallen zu niedrig aus. Niedriger: Ein warmes Rohr kann einen Start vortäuschen. Muss über „Ende ab Abgas über Ruhewert“ liegen.",
                 ki: true, erweitert: true),
            zahl(h, .geraet, ["burner", "delta_off_k"], "brenner", "Ende ab Abgas über Ruhewert", "K", 0...99, schritt: 0.5, stellen: 1, vorgabe: 6,
                 hilfe: "Der Brenner gilt als aus, wenn das Abgas nur noch so weit über dem Ruhewert liegt. Bei warmem Kessel greift meist vorher „Abfall und Anstieg des Abgases“. Nach einem Neustart gilt ein laufender Brenner als aus, wenn das Abgas darunter liegt. Muss unter „Start ab Abgas über Ruhewert“ liegen.",
                 ki: true, erweitert: true),
            zahl(h, .geraet, ["burner", "swing_k"], "brenner", "Abfall und Anstieg des Abgases", "K", 1...60, schritt: 0.5, stellen: 1, vorgabe: 6,
                 hilfe: "Der Brenner gilt als aus, sobald das Abgas um diesen Betrag unter den Höchstwert des Laufs fällt; ein neuer Start verlangt einen Anstieg um diesen Betrag über den Tiefstwert. So wird das Ende auch bei warmem Kessel erkannt. Höher: Das Ende wird später erkannt, Nachzündungen verschmelzen mit dem vorigen Lauf, Laufzeit und Verbrauch steigen. Niedriger: Messschwankungen beenden Läufe, es werden zu viele Starts gezählt.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["burner", "on_hold_s"], "brenner", "Brennerstart bestätigt nach", "s", 0...3600, schritt: 10, vorgabe: 60,
                 hilfe: "So lange muss die Startbedingung ununterbrochen anstehen, bevor der Brenner als gestartet gilt; erst ab dann zählt die Laufzeit, und erst dann schaltet die Brennererkennung die Kesselkreispumpe ein. Höher: Laufzeit und Verbrauch je Start um diesen Betrag zu niedrig, sehr kurze Läufe fehlen. Niedriger: Kurze Störungen zählen als Start.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["burner", "off_hold_s"], "brenner", "Brennerende bestätigt nach", "s", 0...3600, schritt: 10, vorgabe: 300,
                 hilfe: "So lange muss das Abgas ununterbrochen fallen, bevor der Brenner als aus gilt. Bis dahin zählt die Laufzeit weiter; erst danach beginnt der Nachlauf der Kesselkreispumpe. Höher: Laufzeit und Ölverbrauch je Lauf zu hoch, Nachzündungen werden nicht als eigene Starts erkannt, die Pumpe läuft entsprechend länger. Niedriger: Ein kurzer Einbruch des Abgases beendet den Lauf vorzeitig. Den Nachlauf der Pumpe stellen Sie unter „Kesselkreispumpe › Nachlauf nach Brennerende“ ein.",
                 ki: true, erweitert: true),
            zahl(h, .geraet, ["burner", "duese_l_h"], "brenner", "Düsendurchsatz", "l/h", 0...20, schritt: 0.05, stellen: 2, vorgabe: 2.2,
                 hilfe: "Ölmenge je Stunde Brennerlaufzeit; daraus schätzt das Gerät den Verbrauch. Auf die Regelung wirkt der Wert nicht. Tragen Sie den Wert der eingebauten Düse ein (Tabelle im Handbuch) oder, genauer, den aus zwei Tankablesungen ermittelten. Eine Änderung gilt ab dem laufenden Tag. 0 schaltet die Schätzung ab.",
                 ki: true),

            // Pufferspeicher
            zahl(h, .geraet, ["buffer", "leer_c"], "speicher", abschnitt: "Leer und voll", "Leer bei", "°C", 15...90, schritt: 0.5, stellen: 1,
                 vorgabe: 35,
                 hilfe: "Speichertemperatur, die als 0 % angezeigt wird: die Temperatur, bei der die Kesselregelung nachheizt. Schaltet nichts. Wann der Kessel nachheizt, entscheidet seine eigene Regelung; wann die Heizkreise abschalten, entscheidet „Heizkreis › Pumpe aus unter Speicher“. Ist „Leerpunkt selbst nachmessen“ eingeschaltet, führt das Gerät den Wert selbst nach, auch einen von Hand eingetragenen.",
                 ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "voll_c"], "speicher", abschnitt: "Leer und voll", "Voll bei", "°C", 20...95, schritt: 0.5, stellen: 1,
                 vorgabe: 62,
                 hilfe: "Speichertemperatur, die als 100 % angezeigt wird. Tragen Sie die Temperatur ein, die der Speicher am Ende einer Ladung tatsächlich erreicht (Ladungsprotokoll). Zu niedrig: Die Anzeige steht lange auf 100 %. Wann eine Ladung fertig ist, bestimmt dieser Wert nicht. Muss über „Leer bei“ liegen.",
                 ki: true, anlagenweit: true),
            schalter(h, .geraet, ["buffer", "leer_lernen"], "speicher", abschnitt: "Leer und voll", "Leerpunkt selbst nachmessen",
                     vorgabe: true,
                     hilfe: "Startet der Brenner, nachdem der Speicher durch Verbrauch gefallen ist, nimmt das Gerät die Speichertemperatur in diesem Moment als Messpunkt und führt „Leer bei“ schrittweise dorthin nach. Eingeschaltet lassen.",
                     ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "lern_drop_k"], "speicher", abschnitt: "Leer und voll", "Nachmessen nur nach Abfall um", "K", 0.5...30,
                 schritt: 0.5, stellen: 1, vorgabe: 3,
                 hilfe: "Ein Brennerstart zählt nur als Messpunkt, wenn der Speicher seit seinem Höchststand mindestens so weit gefallen ist. Niedriger: Starts beim Takten oder nach einer Zapfung verschieben den Leerpunkt nach oben. Höher: Es gibt seltener Messpunkte.",
                 ki: true, erweitert: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "warn_c"], "speicher", abschnitt: "Warmwasser", "Warnung „Warmwasser knapp“ unter", "°C", 20...80, schritt: 1,
                 stellen: 0, vorgabe: 40,
                 hilfe: "Fällt der Speicher darunter, meldet das Gerät „Warmwasserreserve knapp“. Nur eine Meldung: Die Heizkreise sperrt „Heizkreis › Pumpe aus unter Speicher“.",
                 ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "volumen_l"], "speicher", abschnitt: "Warmwasser", "Speicherinhalt", "l", 0...20000, schritt: 50,
                 stellen: 0, vorgabe: 0,
                 hilfe: "Rechnet Zapfungen und Ladungen von Kelvin in Kilowattstunden um; steuert nichts. Tragen Sie den Nenninhalt vom Typenschild ein. Bei 0 bleiben die Angaben in Kelvin.",
                 ki: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "zapf_drop_k"], "speicher", abschnitt: "Warmwasser", "Zapfung erkannt ab Abfall um", "K", 0.2...40,
                 schritt: 0.1, stellen: 1, vorgabe: 2,
                 hilfe: "Fällt der Speicher bei stehendem Brenner innerhalb des Zeitfensters mindestens so weit, zählt das als Warmwasserzapfung. Im Stillstand kühlt er an dieser Anlage um etwa 0,8 K je Stunde ab. Höher: Kleine Zapfungen fehlen. Niedriger: Stillstandsverlust zählt als Zapfung.",
                 ki: true, erweitert: true, anlagenweit: true),
            ganz(h, .geraet, ["buffer", "zapf_win_s"], "speicher", abschnitt: "Warmwasser", "Zeitfenster der Zapfung", "s", 60...7200, schritt: 60,
                 vorgabe: 900,
                 hilfe: "In diesem Zeitraum muss der Abfall liegen. Länger: Auch langsame Abfälle zählen; ab etwa zwei Stunden erreicht der Stillstandsverlust die Schwelle. Kürzer: Langsame Zapfungen fehlen.",
                 ki: true, erweitert: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "spread_full_k"], "speicher", abschnitt: "Wann eine Ladung fertig ist", "Ladung fertig unter Spreizung", "K",
                 1...40, schritt: 0.5, stellen: 1, vorgabe: 8,
                 hilfe: "Läuft der Brenner und liegt der Kesselrücklauf weniger als diesen Betrag unter dem Vorlauf, nimmt der Speicher keine Wärme mehr auf: Die Ladung gilt als fertig. Nur Anzeige und Protokoll; Brenner und Pumpe schaltet der Wert nicht. Höher: „geladen“ erscheint zu früh. Niedriger: spät oder nie.",
                 ki: true, erweitert: true, anlagenweit: true),
            ganz(h, .geraet, ["buffer", "spread_hold_s"], "speicher", abschnitt: "Wann eine Ladung fertig ist", "Ladung fertig bestätigt nach", "s",
                 0...3600, schritt: 10, vorgabe: 300,
                 hilfe: "So lange muss die Spreizung ununterbrochen unterschritten sein, bevor die Ladung als fertig gilt. Höher: „geladen“ erscheint später. Niedriger: Schon kurze Engstellen reichen.",
                 ki: true, erweitert: true, anlagenweit: true),
            zahl(h, .geraet, ["buffer", "kessel_hot_c"], "speicher", abschnitt: "Wann eine Ladung fertig ist", "Ladung fertig erst ab Vorlauf",
                 "°C", 20...95, schritt: 1, stellen: 0, vorgabe: 60,
                 hilfe: "Die Ladung gilt nur als fertig, wenn der Kesselvorlauf mindestens so warm ist, denn beim Anfahren aus dem kalten Kessel liegen Vor- und Rücklauf ebenfalls dicht beieinander. Endet der Brenner bei heißerem Vorlauf, gilt die Ladung ebenfalls als fertig. Nur ändern, wenn der Kessel regulär kühler lädt. Nicht zu verwechseln mit „Kesselkreispumpe › Notlauf ab Kesselvorlauf“.",
                 ki: true, erweitert: true, anlagenweit: true),

            // Kesselkreispumpe
            schalter(h, .geraet, ["boiler_pump", "enabled"], "kkp", "Kesselkreispumpe vorhanden", vorgabe: false,
                     hilfe: "Die Pumpe zwischen Kessel und Speicher. Braucht Kesselvor- und -rücklauf an diesem Gerät. Ausgeschaltet sendet das Gerät keine Befehle mehr; das Relais bleibt im letzten Zustand."),
            minuten(h, .geraet, ["boiler_pump", "overrun_s"], "kkp", abschnitt: "Schaltpunkte", "Nachlauf nach Brennerende", 0...120,
                    vorgabeSekunden: 1800,
                    hilfe: "So lange läuft die Pumpe nach dem erkannten Brennerende weiter, unabhängig von der Spreizung. Der Kessel bleibt dabei auf Speichertemperatur und zündet gleich nach, solange die Ladung angefordert ist, statt auszukühlen und später einzeln nachzuheizen. Höher: mehr Pumpenstrom und Wärmeverlust über den Kessel. 0: kein Nachlauf. Das Brennerende erkennt das Gerät erst nach „Brennererkennung › Brennerende bestätigt nach“.",
                    ki: true),
            zahl(h, .geraet, ["boiler_pump", "on_k"], "kkp", abschnitt: "Schaltpunkte", "Ein ab Kessel wärmer als Speicher um", "K", 0.1...20, schritt: 0.1,
                 stellen: 1, vorgabe: 3,
                 hilfe: "Steht die Pumpe, läuft sie an, sobald der Kesselvorlauf so viel wärmer ist als das Wasser, das aus dem Speicher zurückkäme. Gilt nur bei stehendem Brenner und nach dem Nachlauf. Höher: Restwärme im Kessel wird seltener abgeholt. Niedriger: Die Pumpe läuft öfter kurz an. Muss über „Aus unter Spreizung“ liegen.",
                 ki: true, erweitert: true),
            zahl(h, .geraet, ["boiler_pump", "off_k"], "kkp", abschnitt: "Schaltpunkte", "Aus unter Spreizung", "K", -20...19.9, schritt: 0.1,
                 stellen: 1, vorgabe: 2,
                 hilfe: "Läuft die Pumpe, schaltet sie ab, sobald der Kesselvorlauf weniger als diesen Betrag über dem Rücklauf liegt; dann gibt der Kessel keine Wärme mehr ab. Niedriger: Sie läuft lange weiter und fördert Speicherwasser durch den auskühlenden Kessel. Zwischen beiden Schwellen bleibt sie, wie sie ist.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["boiler_pump", "hold_s"], "kkp", abschnitt: "Schaltpunkte", "Schaltverzögerung", "s", 0...3600, schritt: 10,
                 vorgabe: 120,
                 hilfe: "So lange muss eine der beiden Schwellen ununterbrochen über- oder unterschritten sein, bevor die Pumpe schaltet. Nach dem Nachlauf läuft sie deshalb mindestens so lange weiter. Das ist nicht der Nachlauf; den stellen Sie unter „Nachlauf nach Brennerende“ ein.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["boiler_pump", "min_run_s"], "kkp", abschnitt: "Schaltpunkte", "Mindestlaufzeit", "s", 0...3600, schritt: 10,
                 vorgabe: 180,
                 hilfe: "Kürzer läuft die Pumpe nicht, wenn sie nach der Spreizung schaltet. Brenner, Nachlauf, Notlauf und Handbetrieb gehen vor.",
                 ki: true, erweitert: true),
            ganz(h, .geraet, ["boiler_pump", "min_pause_s"], "kkp", abschnitt: "Schaltpunkte", "Mindestpause", "s", 0...3600, schritt: 10,
                 vorgabe: 180,
                 hilfe: "Kürzer steht die Pumpe nicht, wenn sie nach der Spreizung schaltet. Brenner, Nachlauf, Notlauf und Handbetrieb gehen vor.",
                 ki: true, erweitert: true),
            zahl(h, .geraet, ["boiler_pump", "emergency_c"], "kkp", abschnitt: "Schaltpunkte", "Notlauf ab Kesselvorlauf", "°C", 60...110, schritt: 1,
                 stellen: 0, vorgabe: 85,
                 hilfe: "Darüber läuft die Pumpe in jedem Fall, auch gegen die Schwellen, damit ein falsch messender Fühler die Wärmeabfuhr nicht verhindert. Ersetzt nicht den Sicherheitstemperaturbegrenzer des Kessels. Niedriger, nahe der üblichen Vorlauftemperatur: Die Pumpe läuft bei jedem heißen Stillstand und kühlt den Speicher über den Kessel aus.",
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
            ganz(h, .heizkreis, ["overrun_s"], "heizkreis", abschnitt: "Pumpe", "Nachlauf nach Bedarfsende", "s", 0...3600, schritt: 10, vorgabe: 300,
                 hilfe: "So lange läuft die Pumpe weiter, nachdem kein versorgter Verteiler mehr Wärme anfordert, damit die schließenden Ventile noch durchströmt werden. Höher: mehr Pumpenstrom. Hat mit dem Nachlauf der Kesselkreispumpe nichts zu tun.",
                 ki: true, erweitert: true),
            ganz(h, .heizkreis, ["min_run_s"], "heizkreis", abschnitt: "Pumpe", "Mindestlaufzeit", "s", 0...3600, schritt: 10,
                 vorgabe: 180,
                 hilfe: "Kürzer läuft die Pumpe nicht; das verhindert Takten. Frostschutz, Handbetrieb und Schutzlauf gehen vor. Solange eine Pumpe in ihrer Mindestlaufzeit steht, entfällt der tägliche Neustart.",
                 ki: true, erweitert: true),
            ganz(h, .heizkreis, ["min_pause_s"], "heizkreis", abschnitt: "Pumpe", "Mindestpause", "s", 0...3600, schritt: 10,
                 vorgabe: 180, hilfe: "Kürzer steht die Pumpe nicht; das verhindert Takten. Frostschutz, Handbetrieb und Schutzlauf gehen vor.",
                 ki: true, erweitert: true),
            zahl(h, .heizkreis, ["min_buffer_c"], "heizkreis", abschnitt: "Pumpe", "Pumpe aus unter Speicher", "°C", 0...90, schritt: 1,
                 stellen: 0, vorgabe: 40,
                 hilfe: "Ist der Speicher kälter, bleibt die Pumpe aus, auch bei Wärmebedarf und im Nachlauf. Liegt bewusst über der nötigen Vorlauftemperatur, weil der Mischer nur herunterregeln kann. Höher: Zwischen diesem Wert und dem Nachheizen des Kessels bekommen die Räume keine Wärme. Niedriger: Die Räume werden mit zu kaltem Wasser versorgt.",
                 ki: true),
            zahl(h, .heizkreis, ["frost_c"], "heizkreis", abschnitt: "Pumpe", "Frostschutz unter Raumtemperatur", "°C", -10...20, schritt: 1, stellen: 0,
                 vorgabe: 6,
                 hilfe: "Fällt der kälteste Raum eines versorgten Verteilers darunter, auch ein ausgeschalteter, läuft die Pumpe in jedem Fall, auch auf „Hand aus“. Unabhängig davon läuft sie, wenn ihr Vorlauf unter 8 °C fällt. Höher: Die Pumpe läuft dauernd, sobald ein unbeheizter Raum kühl ist.",
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
                 hilfe: "Wird zu jedem Messwert addiert, etwa wenn ein Anlegefühler schlecht am Rohr anliegt. Am Kesselvor- und -rücklauf verschiebt die Korrektur alle Schwellen der Kesselkreispumpe und das Ladungsende, am Speicher die Anzeige und die Sperre der Heizkreise. Am Abgasfühler wirkt sie nicht auf die Brennererkennung. Nur ändern, wenn ein Fühler nachweislich falsch misst."),
        ]
        + zeit(h, gruppe: "zeit", neustartStunde: -1, schutz: "Schutzlauf der Pumpen")
        + netz(h, hostname: "heizung")
        + mqtt(h, praefix: "heiz")
    }()
}
