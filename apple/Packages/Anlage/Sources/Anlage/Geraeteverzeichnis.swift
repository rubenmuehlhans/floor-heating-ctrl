import Foundation
import Geraeteschnittstelle
import Observation

/// Ein Gerät der Anlage, wie es die App kennt.
public struct BekanntesGeraet: Codable, Sendable, Hashable, Identifiable {
    /// Kennung der Firmware, `fbh_…` oder `heiz_…`
    public var id: String
    public var art: Geraeteart
    public var ort: String
    /// Zuletzt bekannte Adresse; die Suche hält sie aktuell.
    public var adresse: URL
    public var firmware: String?
    public var aufgenommen: Date
    public var zuletztGesehen: Date?

    public init(id: String, art: Geraeteart, ort: String, adresse: URL, firmware: String? = nil,
                aufgenommen: Date = .now, zuletztGesehen: Date? = nil) {
        self.id = id
        self.art = art
        self.ort = ort
        self.adresse = adresse
        self.firmware = firmware
        self.aufgenommen = aufgenommen
        self.zuletztGesehen = zuletztGesehen
    }

    /// Kessel und Speicher sind dasselbe Gerät; den Unterschied macht der Ort.
    public var bezeichnung: String {
        switch art {
        case .verteiler: "Verteiler \(ort)"
        case .heizung: ort
        }
    }
}

/// Ein Leitstand, wie die App ihn kennt. Er regelt nichts und steht deshalb neben den Geräten
/// der Anlage, nicht unter ihnen: ohne `Geraeteart`, ohne Parameter, ohne Sicherung.
public struct BekannterLeitstand: Codable, Sendable, Hashable, Identifiable {
    /// Kennung der Firmware, `lst_…`
    public var id: String
    public var ort: String
    public var adresse: URL
    public var firmware: String?
    public var aufgenommen: Date
    /// Zuletzt von der Suche im Netz angetroffen; fehlt in Dateien älterer Fassungen.
    public var zuletztGesehen: Date?

    public init(id: String, ort: String, adresse: URL, firmware: String? = nil, aufgenommen: Date = .now,
                zuletztGesehen: Date? = nil) {
        self.id = id
        self.ort = ort
        self.adresse = adresse
        self.firmware = firmware
        self.aufgenommen = aufgenommen
        self.zuletztGesehen = zuletztGesehen
    }
}

/// Die Geräte der Anlage. Die Einrichtung füllt das Verzeichnis; von hier aus fragt die App die
/// Geräte ab. Gespeichert wird es als JSON unter Application Support.
@MainActor
@Observable
public final class Geraeteverzeichnis {
    public private(set) var geraete: [BekanntesGeraet] = []
    /// Leitstände liegen in einer eigenen Datei neben `geraete.json`; deren Aufbau bleibt so, wie
    /// ältere Fassungen der App ihn lesen.
    public private(set) var leitstaende: [BekannterLeitstand] = []
    private let datei: URL?
    private let leitstandDatei: URL?

    /// `datei` nil hält das Verzeichnis nur im Speicher, etwa für Vorschauen und Tests; ebenso
    /// `leitstandDatei` für die Leitstände.
    public init(datei: URL?, leitstandDatei: URL? = nil) {
        self.datei = datei
        self.leitstandDatei = leitstandDatei
        if let datei, let daten = try? Data(contentsOf: datei) {
            geraete = (try? Self.decoder.decode([BekanntesGeraet].self, from: daten)) ?? []
        }
        if let leitstandDatei, let daten = try? Data(contentsOf: leitstandDatei) {
            leitstaende = (try? Self.decoder.decode([BekannterLeitstand].self, from: daten)) ?? []
        }
    }

    public static func standard() -> Geraeteverzeichnis {
        let ordner = URL.applicationSupportDirectory.appending(path: "Heizung", directoryHint: .isDirectory)
        try? FileManager.default.createDirectory(at: ordner, withIntermediateDirectories: true)
        return Geraeteverzeichnis(datei: ordner.appending(path: "geraete.json"),
                                  leitstandDatei: ordner.appending(path: "leitstaende.json"))
    }

    public var verteiler: [BekanntesGeraet] { geraete.filter { $0.art == .verteiler } }
    public var heizgeraete: [BekanntesGeraet] { geraete.filter { $0.art == .heizung } }

    public func kennt(_ id: String) -> Bool {
        geraete.contains { $0.id == id }
    }

    /// Nimmt ein Gerät auf oder aktualisiert es, erkannt an seiner Kennung.
    public func aufnehmen(_ geraet: BekanntesGeraet) {
        if let i = geraete.firstIndex(where: { $0.id == geraet.id }) {
            var neu = geraet
            neu.aufgenommen = geraete[i].aufgenommen
            geraete[i] = neu
        } else {
            geraete.append(geraet)
        }
        geraete.sort { ($0.art.rawValue, $0.ort) < ($1.art.rawValue, $1.ort) }
        speichern()
    }

    public func entfernen(_ id: String) {
        geraete.removeAll { $0.id == id }
        speichern()
    }

    /// Ort und Firmware, wie das Gerät sie selbst meldet. Ändert jemand den Ort an der
    /// Weboberfläche oder spielt eine neue Firmware ein, folgt das Verzeichnis.
    public func gemeldet(_ id: String, ort: String?, firmware: String?) {
        guard let i = geraete.firstIndex(where: { $0.id == id }) else { return }
        var g = geraete[i]
        if let ort, !ort.isEmpty { g.ort = ort }
        if let firmware, !firmware.isEmpty { g.firmware = firmware }
        guard g != geraete[i] else { return }
        geraete[i] = g
        geraete.sort { ($0.art.rawValue, $0.ort) < ($1.art.rawValue, $1.ort) }
        speichern()
    }

    /// Die Suche hat ein Gerät im Netz angetroffen. Hat es eine neue Adresse bekommen, etwa nach
    /// einem Neustart des Routers, gilt von nun an diese. Liefert, ob sich die Adresse geändert hat.
    @discardableResult
    public func gesehen(_ id: String, unter adresse: URL, am zeitpunkt: Date = .now) -> Bool {
        guard let i = geraete.firstIndex(where: { $0.id == id }) else { return false }
        let neu = !Self.gleicheAdresse(geraete[i].adresse, adresse)
        if neu { geraete[i].adresse = adresse }
        geraete[i].zuletztGesehen = zeitpunkt
        speichern()
        return neu
    }

    /// Ordnet die Treffer einer Suche den bekannten Geräten und Leitständen zu, erkannt an der
    /// Kennung aus dem TXT-Eintrag. Unbekannte Kennungen bleiben unberücksichtigt; aufgenommen wird
    /// nur in der Einrichtung. Liefert die Kennungen, deren Adresse sich geändert hat.
    @discardableResult
    public func nachfuehren(_ treffer: [String: URL], am zeitpunkt: Date = .now) -> Set<String> {
        var geaendert: Set<String> = []
        for (id, adresse) in treffer {
            if kennt(id), gesehen(id, unter: adresse, am: zeitpunkt) { geaendert.insert(id) }
            if kenntLeitstand(id), leitstandGesehen(id, unter: adresse, am: zeitpunkt) { geaendert.insert(id) }
        }
        return geaendert
    }

    /// Dieselbe Adresse, auch wenn eine der beiden den Port 80 ausschreibt: Die Suche liefert ihn
    /// immer mit, die Einrichtung und die Eingabe von Hand meist nicht.
    public nonisolated static func gleicheAdresse(_ a: URL, _ b: URL) -> Bool {
        func teile(_ u: URL) -> (String, String, Int) {
            let schema = u.scheme?.lowercased() ?? "http"
            return (schema, u.host(percentEncoded: false)?.lowercased() ?? "", u.port ?? (schema == "https" ? 443 : 80))
        }
        return teile(a) == teile(b)
    }

    // MARK: Leitstände

    public func kenntLeitstand(_ id: String) -> Bool {
        leitstaende.contains { $0.id == id }
    }

    public func leitstandAufnehmen(_ leitstand: BekannterLeitstand) {
        if let i = leitstaende.firstIndex(where: { $0.id == leitstand.id }) {
            var neu = leitstand
            neu.aufgenommen = leitstaende[i].aufgenommen
            leitstaende[i] = neu
        } else {
            leitstaende.append(leitstand)
        }
        leitstaende.sort { $0.ort < $1.ort }
        leitstaendeSpeichern()
    }

    public func leitstandEntfernen(_ id: String) {
        leitstaende.removeAll { $0.id == id }
        leitstaendeSpeichern()
    }

    /// Ort und Firmware, wie der Leitstand sie selbst meldet
    public func leitstandGemeldet(_ id: String, ort: String?, firmware: String?) {
        guard let i = leitstaende.firstIndex(where: { $0.id == id }) else { return }
        var l = leitstaende[i]
        if let ort, !ort.isEmpty { l.ort = ort }
        if let firmware, !firmware.isEmpty { l.firmware = firmware }
        guard l != leitstaende[i] else { return }
        leitstaende[i] = l
        leitstaendeSpeichern()
    }

    /// Wie `gesehen(_:unter:am:)` für einen Leitstand
    @discardableResult
    public func leitstandGesehen(_ id: String, unter adresse: URL, am zeitpunkt: Date = .now) -> Bool {
        guard let i = leitstaende.firstIndex(where: { $0.id == id }) else { return false }
        let neu = !Self.gleicheAdresse(leitstaende[i].adresse, adresse)
        if neu { leitstaende[i].adresse = adresse }
        leitstaende[i].zuletztGesehen = zeitpunkt
        leitstaendeSpeichern()
        return neu
    }

    private func leitstaendeSpeichern() {
        guard let d = leitstandDatei, let daten = try? Self.encoder.encode(leitstaende) else { return }
        try? daten.write(to: d, options: .atomic)
    }

    private static var encoder: JSONEncoder {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return e
    }

    private func speichern() {
        guard let datei, let daten = try? Self.encoder.encode(geraete) else { return }
        try? daten.write(to: datei, options: .atomic)
    }

    private static var decoder: JSONDecoder {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }
}
