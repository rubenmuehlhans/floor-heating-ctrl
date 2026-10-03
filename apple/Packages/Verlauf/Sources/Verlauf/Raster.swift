import Foundation

/// Zeitraster des Verlaufs: Plätze zu fünf Minuten, gezählt in UTC seit 1970. Ein Messblock
/// fasst die 288 Plätze eines UTC-Tages; so verschiebt die Umstellung auf Sommerzeit keinen
/// Platz, und ein Tag hat immer gleich viele.
public enum Raster {
    public static let schritt: TimeInterval = 300
    public static let plaetzeJeTag = 288

    /// Fortlaufende Nummer des Platzes, in den `zeit` fällt
    public static func index(_ zeit: Date) -> Int {
        Int((zeit.timeIntervalSince1970 / schritt).rounded(.down))
    }

    /// Beginn des Platzes `index`
    public static func beginn(_ index: Int) -> Date {
        Date(timeIntervalSince1970: Double(index) * schritt)
    }

    /// Tag als fortlaufende Nummer seit 1970 (UTC)
    public static func tag(_ index: Int) -> Int {
        Int((Double(index) / Double(plaetzeJeTag)).rounded(.down))
    }

    /// Platz innerhalb seines Tages, 0 bis 287
    public static func platz(_ index: Int) -> Int {
        index - tag(index) * plaetzeJeTag
    }
}

/// Die Werte eines Tages für ein Gerät: je Platz eine Zeile, je Messgröße eine Spalte. Jeder Wert
/// liegt als Int16 in Hundertsteln vor, wie Float16 zwei Byte, aber über den ganzen Bereich gleich
/// genau: 0,01 K bei Temperaturen bis ±327 °C, 1 % bei Anteilen. `Int16.min` steht für „kein Wert“.
struct Tagesmatrix: Sendable, Equatable {
    static let fehlt = Int16.min
    static let faktor = 100.0

    private(set) var spalten: [String]
    private var zellen: [Int16]

    init(spalten: [String] = []) {
        self.spalten = spalten
        zellen = [Int16](repeating: Self.fehlt, count: spalten.count * Raster.plaetzeJeTag)
    }

    /// Aus der gespeicherten Form; passt die Länge nicht zu den Spalten, bleibt der Tag leer.
    init(spalten: [String], daten: Data) {
        self.spalten = spalten
        let erwartet = spalten.count * Raster.plaetzeJeTag
        var werte = [Int16](repeating: Self.fehlt, count: erwartet)
        if daten.count == erwartet * 2 {
            werte.withUnsafeMutableBytes { ziel in
                daten.withUnsafeBytes { quelle in ziel.copyMemory(from: quelle) }
            }
            // Gespeichert wird in Little Endian; auf allen Zielgeräten ist das die eigene Ordnung.
            werte = werte.map { Int16(littleEndian: $0) }
        }
        zellen = werte
    }

    var daten: Data {
        zellen.map(\.littleEndian).withUnsafeBytes { Data($0) }
    }

    func wert(platz: Int, spalte: Int) -> Double? {
        let roh = zellen[platz * spalten.count + spalte]
        return roh == Self.fehlt ? nil : Double(roh) / Self.faktor
    }

    func wert(platz: Int, _ schluessel: String) -> Double? {
        spalten.firstIndex(of: schluessel).flatMap { wert(platz: platz, spalte: $0) }
    }

    /// Setzt einen Wert; eine neue Messgröße bekommt eine neue Spalte.
    mutating func setzen(platz: Int, _ schluessel: String, _ wert: Double?, nurLuecken: Bool = false) {
        let spalte = spaltenindex(schluessel)
        let i = platz * spalten.count + spalte
        if nurLuecken, zellen[i] != Self.fehlt { return }
        zellen[i] = wert.map(Self.kodiert) ?? Self.fehlt
    }

    static func kodiert(_ wert: Double) -> Int16 {
        guard wert.isFinite else { return fehlt }
        let roh = (wert * faktor).rounded()
        return Int16(min(max(roh, Double(Int16.min + 1)), Double(Int16.max)))
    }

    private mutating func spaltenindex(_ schluessel: String) -> Int {
        if let i = spalten.firstIndex(of: schluessel) { return i }
        // Neue Spalte am Ende jeder Zeile: Die Matrix wird einmal umgepackt.
        let alt = spalten.count
        var neu = [Int16](repeating: Self.fehlt, count: (alt + 1) * Raster.plaetzeJeTag)
        for platz in 0..<Raster.plaetzeJeTag {
            for spalte in 0..<alt {
                neu[platz * (alt + 1) + spalte] = zellen[platz * alt + spalte]
            }
        }
        spalten.append(schluessel)
        zellen = neu
        return alt
    }
}
