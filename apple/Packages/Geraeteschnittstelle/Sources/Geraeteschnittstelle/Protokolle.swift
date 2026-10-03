import Foundation

/// Ein Satz aus `GET /api/log/charges`: eine abgeschlossene Ladung des Pufferspeichers.
public struct Ladungseintrag: Sendable, Equatable {
    public var beginn: Date
    public var dauerS: Int
    public var brennerS: Int
    public var starts: Int
    public var pufferVorherC: Double?
    public var pufferNachherC: Double?
    public var kesselVorlaufMaxC: Double?
    public var abgasMaxC: Double?
    public var aussenMittelC: Double?
    /// Schätzung aus Laufzeit und Düsendurchsatz
    public var liter: Double
}

/// Ein Satz aus `GET /api/log/days`: Laufzeit, Starts und Heizgradtage eines Kalendertags.
public struct Tageseintrag: Sendable, Equatable {
    /// Kalendertag in der Zeitzone des Geräts
    public var datum: DateComponents
    public var laufzeitS: Int
    public var starts: Int
    public var liter: Double
    public var heizgradtage: Double?
    public var aussenMinC: Double?
    public var aussenMaxC: Double?
}

/// Liest die CSV-Protokolle des Heizungsgeräts.
///
/// Die Spalten werden über die Kopfzeile gefunden, nicht über ihre Stellung. Leere Felder stehen
/// für fehlende Messwerte. Zu beachten: Die Firmware schreibt Werte zwischen −0,9 und −0,1 ohne
/// Vorzeichen (`%d.%d` aus Zehnteln); sie kommen hier positiv an, und das lässt sich aus der Datei
/// allein nicht erkennen.
public enum Protokolle {
    public static func ladungen(_ csv: String) throws -> [Ladungseintrag] {
        let t = try Tabelle(csv, pflicht: ["beginn", "dauer_s", "brenner_s", "starts", "liter"])
        return try t.zeilen.map { z in
            Ladungseintrag(
                beginn: Date(timeIntervalSince1970: try t.zahl(z, "beginn")),
                dauerS: Int(try t.zahl(z, "dauer_s")),
                brennerS: Int(try t.zahl(z, "brenner_s")),
                starts: Int(try t.zahl(z, "starts")),
                pufferVorherC: t.wert(z, "puffer_start"),
                pufferNachherC: t.wert(z, "puffer_ende"),
                kesselVorlaufMaxC: t.wert(z, "kessel_vl_max"),
                abgasMaxC: t.wert(z, "abgas_max"),
                aussenMittelC: t.wert(z, "aussen_mittel"),
                liter: try t.zahl(z, "liter")
            )
        }
    }

    public static func tage(_ csv: String) throws -> [Tageseintrag] {
        let t = try Tabelle(csv, pflicht: ["datum", "laufzeit_s", "starts", "liter"])
        return try t.zeilen.map { z in
            let teile = t.text(z, "datum").split(separator: "-").compactMap { Int($0) }
            guard teile.count == 3 else {
                throw Geraetefehler.unerwarteteAntwort("Datum „\(t.text(z, "datum"))“ im Tagesprotokoll")
            }
            return Tageseintrag(
                datum: DateComponents(year: teile[0], month: teile[1], day: teile[2]),
                laufzeitS: Int(try t.zahl(z, "laufzeit_s")),
                starts: Int(try t.zahl(z, "starts")),
                liter: try t.zahl(z, "liter"),
                heizgradtage: t.wert(z, "heizgradtage"),
                aussenMinC: t.wert(z, "aussen_min"),
                aussenMaxC: t.wert(z, "aussen_max")
            )
        }
    }

    /// Beliebige CSV mit Kopfzeile, etwa die Ladungsaufzeichnung im Fünf-Sekunden-Raster.
    public static func tabelle(_ csv: String) throws -> (kopf: [String], zeilen: [[Double?]]) {
        let t = try Tabelle(csv, pflicht: [])
        return (t.kopf, t.zeilen.map { z in z.map { Double($0) } })
    }
}

private struct Tabelle {
    let kopf: [String]
    let zeilen: [[String]]
    let spalte: [String: Int]

    init(_ csv: String, pflicht: [String]) throws {
        let alle = csv.split(whereSeparator: \.isNewline).map { String($0) }
        guard let erste = alle.first else {
            throw Geraetefehler.unerwarteteAntwort("leeres Protokoll")
        }
        let kopf = erste.split(separator: ",", omittingEmptySubsequences: false).map {
            $0.trimmingCharacters(in: .whitespaces)
        }
        let spalte = Dictionary(kopf.enumerated().map { ($1, $0) }, uniquingKeysWith: { erste, _ in erste })
        if let fehlt = pflicht.first(where: { spalte[$0] == nil }) {
            throw Geraetefehler.unerwarteteAntwort("Spalte \(fehlt) fehlt")
        }
        self.kopf = kopf
        self.spalte = spalte
        zeilen = alle.dropFirst().map { zeile in
            zeile.split(separator: ",", omittingEmptySubsequences: false).map {
                $0.trimmingCharacters(in: .whitespaces)
            }
        }
    }

    func text(_ zeile: [String], _ name: String) -> String {
        guard let i = spalte[name], zeile.indices.contains(i) else { return "" }
        return zeile[i]
    }

    func wert(_ zeile: [String], _ name: String) -> Double? {
        Double(text(zeile, name))
    }

    func zahl(_ zeile: [String], _ name: String) throws -> Double {
        guard let z = wert(zeile, name) else {
            throw Geraetefehler.unerwarteteAntwort("Feld \(name) ist leer oder keine Zahl")
        }
        return z
    }
}
