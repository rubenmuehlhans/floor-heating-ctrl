import Foundation
import Geraeteschnittstelle

/// Bringt ein Gerät im Einrichtungsbetrieb ins Heimnetz – derselbe Ablauf wie in der
/// Weboberfläche der Geräte.
///
/// Ohne WLAN-Zugang öffnet ein Gerät einen eigenen Zugangspunkt (`floor-heating-XXXX`,
/// `heizung-XXXX` oder `leitstand-XXXX`, Adresse 192.168.4.1). Die App verbindet sich damit,
/// sucht die Netze in Reichweite, schreibt Ort und WLAN-Zugang und wartet, bis das Gerät im
/// Heimnetz ist. Der Zugangspunkt schließt erst, wenn niemand mehr daran hängt; so bleibt Zeit,
/// die neue Adresse abzulesen.
public struct Einbindung: Sendable {
    public static let zugangspunkt = URL(string: "http://192.168.4.1")!
    /// Werksvorgabe von `wifi.ap_pass` bei Verteiler und Heizungsgerät
    public static let werkskennwort = "fussboden"

    /// Kennwort des Zugangspunkts ab Werk; der Leitstand öffnet ein offenes Netz.
    public static func werkskennwort(_ art: Einbindungsart) -> String {
        art == .leitstand ? "" : werkskennwort
    }

    /// Was die App über ein Gerät im Einrichtungsbetrieb weiß.
    public struct Geraet: Sendable, Equatable {
        public var art: Einbindungsart
        public var id: String?
        public var ort: String?
        public var hostname: String?
        public var mqttPraefix: String?
        public var firmware: String? = nil
    }

    public let adresse: URL
    let verbindung: Geraeteverbindung
    let takt: Duration

    public init(adresse: URL = Einbindung.zugangspunkt, sitzung: URLSession = .geraete, takt: Duration = .milliseconds(700)) {
        self.adresse = adresse
        verbindung = Geraeteverbindung(basis: adresse, sitzung: sitzung)
        self.takt = takt
    }

    /// Präfix des Zugangspunkts ab Werk
    public static func praefix(_ art: Einbindungsart) -> String {
        switch art {
        case .verteiler: "floor-heating-"
        case .heizung: "heizung-"
        case .leitstand: "leitstand-"
        }
    }

    // MARK: Schritte

    /// Wartet, bis das Gerät unter der Einrichtungsadresse antwortet, und erkennt seine Art.
    public func erkennen(zeitgrenze: Duration = .seconds(30)) async throws -> Geraet {
        let ende = ContinuousClock.now + zeitgrenze
        while true {
            do {
                let zustand = try await verbindung.holen("/api/state", als: JSONWert.self)
                let konfiguration = try await verbindung.holen("/api/config", als: JSONWert.self)
                guard let art = Self.einbindungsart(zustand) else {
                    throw Einbindungsfehler.unbekanntesGeraet
                }
                return Geraet(
                    art: art,
                    id: zustand["device"]?["id"]?.alsText,
                    ort: konfiguration["site"]?.alsText.flatMap { $0.isEmpty ? nil : $0 },
                    hostname: konfiguration["wifi"]?["hostname"]?.alsText,
                    mqttPraefix: konfiguration["mqtt"]?["prefix"]?.alsText,
                    firmware: zustand["version"]?.alsText)
            } catch let fehler as Einbindungsfehler {
                throw fehler
            } catch {
                guard ContinuousClock.now < ende else { throw Einbindungsfehler.keinGeraet }
                try await Task.sleep(for: takt)
            }
        }
    }

    /// Suchlauf wie in der Weboberfläche: anstoßen, dann nachfragen. Fehlgeschlagene Abfragen
    /// zählen nicht, denn während des Suchlaufs ist der Zugangspunkt kurz stumm.
    public func netzeSuchen(versuche: Int = 30) async throws -> [Netzsuche.Netz] {
        try await verbindung.senden("/api/wifi/scan")
        for _ in 0..<versuche {
            try await Task.sleep(for: takt)
            guard let stand = try? await verbindung.holen("/api/wifi/scan", als: Netzsuche.self) else { continue }
            if stand.laeuft != true {
                return Self.geordnet(stand.netze ?? [])
            }
        }
        throw Einbindungsfehler.suchlaufAntwortetNicht
    }

    /// Schreibt Ort und WLAN-Zugang. Das Kennwort geht nur mit, wenn eines eingegeben ist –
    /// eine leere Zeichenkette würde das gespeicherte löschen.
    public func einrichten(_ geraet: Geraet, netz: String, kennwort: String, ort: String) async throws {
        let teil = Self.konfiguration(geraet, netz: netz, kennwort: kennwort, ort: ort)
        try await verbindung.senden("/api/config", methode: "PUT", json: teil)
    }

    /// Wartet, bis das Gerät im Heimnetz ist, und gibt seine Adresse dort zurück.
    public func imHeimnetz(_ art: Einbindungsart, zeitgrenze: Duration = .seconds(90)) async throws -> String {
        let ende = ContinuousClock.now + zeitgrenze
        var zuletzt = "keine Antwort"
        while ContinuousClock.now < ende {
            if let zustand = try? await verbindung.holen("/api/state", als: JSONWert.self) {
                let netz = zustand["net"]
                let verbunden = art == .verteiler ? netz?["connected"]?.alsBool : netz?["sta"]?.alsBool
                if verbunden == true, let ip = netz?["ip"]?.alsText, !ip.isEmpty, ip != "192.168.4.1" {
                    return ip
                }
                zuletzt = "noch nicht verbunden"
            }
            try await Task.sleep(for: .seconds(1))
        }
        throw Einbindungsfehler.verbindetNicht(zuletzt)
    }

    // MARK: Regeln

    /// Gerätename aus dem Ort, wie ihn die Weboberfläche bildet: klein, Umlaute ausgeschrieben,
    /// alles andere zu Bindestrichen.
    public static func kurzname(_ ort: String) -> String {
        var s = ort.lowercased()
        for (umlaut, ersatz) in [("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss")] {
            s = s.replacingOccurrences(of: umlaut, with: ersatz)
        }
        var ergebnis = ""
        var trenner = false
        for z in s.unicodeScalars {
            if ("a"..."z").contains(z) || ("0"..."9").contains(z) {
                if trenner && !ergebnis.isEmpty { ergebnis.append("-") }
                ergebnis.unicodeScalars.append(z)
                trenner = false
            } else {
                trenner = true
            }
        }
        return ergebnis.isEmpty ? "anlage" : ergebnis
    }

    /// Teilangabe für `PUT /api/config`. Gerätename und MQTT-Präfix leitet die App nur ab,
    /// solange die Werksvorgabe gilt – wie der Assistent der Weboberfläche. Beim Heizungsgerät
    /// ebenso, damit Kessel und Speicher im Netz nicht beide `heizung` heißen. Der Leitstand
    /// behält seinen Namen `leitstand`, unter dem ihn die Regelgeräte suchen; MQTT hat er nicht.
    public static func konfiguration(_ geraet: Geraet, netz: String, kennwort: String, ort: String) -> JSONWert {
        let s = kurzname(ort)
        var wifi: JSONWert = ["ssid": .text(netz)]
        if !kennwort.isEmpty {
            wifi["pass"] = .text(kennwort)
        }
        var teil: JSONWert = ["site": .text(ort.trimmingCharacters(in: .whitespaces))]
        let name: String, praefix: String
        switch geraet.art {
        case .verteiler: (name, praefix) = ("floor-heating", "fbh")
        case .heizung: (name, praefix) = ("heizung", "heiz")
        case .leitstand:
            teil["wifi"] = wifi
            return teil
        }
        if geraet.hostname == nil || geraet.hostname == name || geraet.hostname == "" {
            wifi["hostname"] = .text("\(name)-\(s)")
        }
        if geraet.mqttPraefix == nil || geraet.mqttPraefix == praefix || geraet.mqttPraefix == "" {
            teil["mqtt"] = ["prefix": .text("\(praefix)_\(s)")]
        }
        teil["wifi"] = wifi
        return teil
    }

    /// Ein Leitstand antwortet ebenfalls auf `/api/state`, ist aber kein Regelgerät; die App führt
    /// ihn in einer eigenen Liste.
    public static func istLeitstand(_ zustand: JSONWert) -> Bool {
        Leitstand.istLeitstand(rolle: zustand["device"]?["role"]?.alsText, kennung: zustand["device"]?["id"]?.alsText)
    }

    /// Art für die Einbindung: Regelgerät oder Leitstand
    public static func einbindungsart(_ zustand: JSONWert) -> Einbindungsart? {
        if istLeitstand(zustand) { return .leitstand }
        return art(zustand).map(Einbindungsart.init)
    }

    public static func art(_ zustand: JSONWert) -> Geraeteart? {
        if let rolle = zustand["device"]?["role"]?.alsText, let art = Geraeteart(rawValue: rolle) {
            return art
        }
        if let id = zustand["device"]?["id"]?.alsText, let art = Geraeteart(kennung: id) {
            return art
        }
        if zustand["rooms"] != nil { return .verteiler }
        if zustand["probes"] != nil { return .heizung }
        return nil
    }

    /// Stärkstes Signal zuerst, jedes Netz einmal, versteckte Netze ohne Namen nicht.
    static func geordnet(_ netze: [Netzsuche.Netz]) -> [Netzsuche.Netz] {
        var bester: [String: Netzsuche.Netz] = [:]
        for n in netze {
            guard let name = n.name, !name.isEmpty else { continue }
            if (bester[name]?.signal ?? .min) < (n.signal ?? .min) {
                bester[name] = n
            }
        }
        return bester.values.sorted { ($0.signal ?? .min) > ($1.signal ?? .min) }
    }
}

/// Was sich über den Zugangspunkt einbinden lässt: die beiden Regelgeräte und der Leitstand
public enum Einbindungsart: String, Sendable, Hashable, CaseIterable {
    case verteiler
    case heizung
    case leitstand

    public init(_ art: Geraeteart) {
        switch art {
        case .verteiler: self = .verteiler
        case .heizung: self = .heizung
        }
    }

    /// Das Regelgerät; beim Leitstand keines
    public var geraeteart: Geraeteart? {
        switch self {
        case .verteiler: .verteiler
        case .heizung: .heizung
        case .leitstand: nil
        }
    }

    public var bezeichnung: String {
        switch self {
        case .verteiler: "Verteiler"
        case .heizung: "Heizungsgerät"
        case .leitstand: "Leitstand"
        }
    }
}

public enum Einbindungsfehler: Error, Sendable, Equatable {
    case keinGeraet
    case unbekanntesGeraet
    case suchlaufAntwortetNicht
    case verbindetNicht(String)
}

extension Einbindungsfehler: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .keinGeraet:
            "Unter 192.168.4.1 antwortet kein Gerät. Ist dieses Gerät mit dem WLAN des Heizungsgeräts verbunden? Ein aktives VPN kann die Adresse überdecken; trennen Sie es für die Einrichtung."
        case .unbekanntesGeraet:
            "Das Gerät antwortet, ist aber weder Verteiler noch Heizungsgerät noch Leitstand."
        case .suchlaufAntwortetNicht:
            "Die Suche nach Netzen antwortet nicht. Bitte versuchen Sie es erneut."
        case .verbindetNicht:
            "Das Gerät kommt nicht ins Heimnetz. Meist stimmt das WLAN-Kennwort nicht, oder das Netz ist am Einbauort zu schwach."
        }
    }
}
