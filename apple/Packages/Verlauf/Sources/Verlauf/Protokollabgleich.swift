import Foundation

/// Übernahme der Fünfminutenmittel aus dem Protokoll des Leitstands.
///
/// Der Leitstand schreibt je Gerät und UTC-Tag eine Datei `<kennung>.5min.csv` im Raster der
/// App: Zeit am Beginn des Platzes, danach die Schlüssel des Katalogs. Die App holt jede Datei
/// ab der Stelle, bis zu der sie sie schon kennt, und übernimmt nur vollständige Zeilen. Eine
/// Zeile ohne Zeilenende, etwa nach einem Stromausfall am Leitstand, bleibt für den nächsten
/// Abgleich liegen.
public enum Protokollabgleich {
    /// Wie weit eine Datei übernommen ist
    public struct Dateistand: Codable, Sendable, Equatable {
        /// Bytes vollständiger Zeilen, einschließlich der Kopfzeile
        public var stelle: Int = 0
        /// Spalten nach der Zeit, aus der Kopfzeile
        public var spalten: [String] = []

        public init(stelle: Int = 0, spalten: [String] = []) {
            self.stelle = stelle
            self.spalten = spalten
        }
    }

    /// Ergebnis eines gelesenen Stücks
    public struct Stueck: Sendable, Equatable {
        public var zeilen: [(index: Int, werte: [String: Double])]
        public var stand: Dateistand
        /// Zeilen, die nicht zur Kopfzeile passten
        public var verworfen: Int

        public static func == (a: Stueck, b: Stueck) -> Bool {
            a.stand == b.stand && a.verworfen == b.verworfen && a.zeilen.count == b.zeilen.count
                && zip(a.zeilen, b.zeilen).allSatisfy { $0.index == $1.index && $0.werte == $1.werte }
        }
    }

    /// Liest `daten`, die ab `stand.stelle` der Datei beginnen.
    public static func lesen(_ daten: Data, stand: Dateistand) -> Stueck {
        var neu = stand
        var zeilen: [(index: Int, werte: [String: Double])] = []
        var verworfen = 0
        // Nur bis zum letzten Zeilenende; der Rest ist eine angefangene Zeile.
        guard let letztes = daten.lastIndex(of: UInt8(ascii: "\n")) else {
            return Stueck(zeilen: [], stand: stand, verworfen: 0)
        }
        let vollstaendig = daten[daten.startIndex...letztes]
        neu.stelle = stand.stelle + vollstaendig.count
        let text = String(decoding: vollstaendig, as: UTF8.self)
        for roh in text.split(separator: "\n", omittingEmptySubsequences: true) {
            let zeile = roh.hasSuffix("\r") ? roh.dropLast() : roh
            let felder = zeile.split(separator: ",", omittingEmptySubsequences: false).map(String.init)
            guard let erstes = felder.first else { continue }
            if erstes == "zeit" {
                neu.spalten = Array(felder.dropFirst())
                continue
            }
            guard felder.count == neu.spalten.count + 1, let zeit = iso(erstes) else {
                verworfen += 1
                continue
            }
            var werte: [String: Double] = [:]
            for (i, name) in neu.spalten.enumerated() {
                if let w = Double(felder[i + 1]), w.isFinite { werte[name] = w }
            }
            if !werte.isEmpty {
                zeilen.append((Raster.index(zeit), werte))
            }
        }
        return Stueck(zeilen: zeilen, stand: neu, verworfen: verworfen)
    }

    /// `2026-09-23T05:35:00Z`
    public static func iso(_ s: String) -> Date? {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        return formatter.date(from: s)
    }

    /// `2026-09-23` als Beginn des UTC-Tages
    public static func tagesbeginn(_ tag: String) -> Date? {
        iso(tag + "T00:00:00Z")
    }

    /// Der UTC-Tag eines Zeitpunkts als `JJJJ-MM-TT`
    public static func tag(_ zeit: Date) -> String {
        var kalender = Calendar(identifier: .gregorian)
        kalender.timeZone = TimeZone(identifier: "UTC")!
        let k = kalender.dateComponents([.year, .month, .day], from: zeit)
        return String(format: "%04d-%02d-%02d", k.year ?? 0, k.month ?? 0, k.day ?? 0)
    }
}

/// Wie weit der Verlauf eines Leitstands übernommen ist, gespeichert als JSON neben dem
/// Verlauf. Ein Tag gilt als abgeschlossen, wenn er vorbei war, als seine Dateien zuletzt ganz
/// gelesen wurden; er wird danach nicht mehr gefragt.
public struct Abgleichstand: Codable, Sendable, Equatable {
    /// Tag → Datei → Stand
    public var dateien: [String: [String: Protokollabgleich.Dateistand]] = [:]
    public var abgeschlossen: Set<String> = []
    public var zuletzt: Date?
    /// Plätze, die beim letzten Abgleich übernommen wurden
    public var uebernommen = 0

    public init() {}

    public static func laden(_ datei: URL) -> Abgleichstand {
        guard let daten = try? Data(contentsOf: datei),
              let stand = try? decoder.decode(Abgleichstand.self, from: daten) else { return Abgleichstand() }
        return stand
    }

    public func sichern(_ datei: URL) throws {
        try FileManager.default.createDirectory(at: datei.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Self.encoder.encode(self).write(to: datei, options: .atomic)
    }

    private static var encoder: JSONEncoder {
        let e = JSONEncoder()
        e.dateEncodingStrategy = .iso8601
        e.outputFormatting = [.sortedKeys]
        return e
    }

    private static var decoder: JSONDecoder {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }
}
