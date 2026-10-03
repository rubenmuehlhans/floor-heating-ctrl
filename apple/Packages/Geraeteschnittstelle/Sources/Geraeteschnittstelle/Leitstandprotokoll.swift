import Foundation

/// Das Protokoll auf der SD-Karte des Leitstands: je Tag in UTC ein Verzeichnis mit Messwerten,
/// Fünfminutenmitteln, Ereignissen und Zuständen. Aufbau und Spalten beschreibt
/// `docs/katalog-messgroessen.md`.
extension Leitstand {
    /// Eine Datei eines Tages mit ihrer Größe
    public struct Protokolldatei: Decodable, Sendable, Hashable {
        public var name: String
        public var byte: Int

        public init(name: String, byte: Int) {
            self.name = name
            self.byte = byte
        }

        /// `<kennung>.5min.csv` oder `<kennung>.<teil>.5min.csv`: die Kennung des Geräts
        public var mittelFuer: String? {
            guard name.hasSuffix(".5min.csv") else { return nil }
            return name.split(separator: ".").first.map(String.init)
        }
    }

    struct Tagesliste: Decodable {
        var tage: [String]
    }

    struct Dateiliste: Decodable {
        var tag: String
        var dateien: [Protokolldatei]
    }

    /// Ein Gerät, wie `geraete.json` eines Tages es nennt
    public struct Protokollgeraet: Decodable, Sendable, Hashable {
        public var kennung: String
        public var ort: String
        public var art: String
        public var version: String
    }

    struct Geraeteliste: Decodable {
        var geraete: [Protokollgeraet]
    }

    /// Tage mit Protokoll als `JJJJ-MM-TT`, aufsteigend
    public func protokollTage() async throws -> [String] {
        try await verbindung.holen("/api/log/days", als: Tagesliste.self).tage.sorted()
    }

    /// Die Dateien eines Tages
    public func protokollDateien(tag: String) async throws -> [Protokolldatei] {
        try await verbindung.holen("/api/log/days?tag=\(tag)", als: Dateiliste.self).dateien
    }

    /// Eine Datei ab Stelle `ab`; alle Dateien des Protokolls wachsen nur.
    public func protokollDatei(tag: String, name: String, ab: Int = 0) async throws -> Data {
        try await verbindung.holenAb("/log/\(tag)/\(name)", ab: ab, zeitlimit: 60)
    }

    /// Die Geräte eines Tages mit Ort und Firmware
    public func protokollGeraete(tag: String) async throws -> [Protokollgeraet] {
        let daten = try await protokollDatei(tag: tag, name: "geraete.json")
        return try JSONDecoder().decode(Geraeteliste.self, from: daten).geraete
    }

    /// Ereignisse eines Zeitraums, höchstens 31 Tage
    public func ereignisse(von: Date, bis: Date, geraet: String? = nil, art: String? = nil) async throws -> [JSONWert] {
        var pfad = "/api/log/events?von=\(Int(von.timeIntervalSince1970))&bis=\(Int(bis.timeIntervalSince1970))"
        if let geraet { pfad += "&geraet=\(geraet)" }
        if let art { pfad += "&art=\(art)" }
        return try await verbindung.holen(pfad, als: [JSONWert].self)
    }

    /// Mittel ausgewählter Messgrößen eines Geräts je Raster, vom Leitstand berechnet
    public struct Protokollreihe: Sendable, Equatable {
        public struct Zeile: Sendable, Equatable {
            /// Beginn des Rasters
            public var zeit: Date
            /// In der Reihenfolge von `spalten`; nil ohne Wert
            public var werte: [Double?]

            public init(zeit: Date, werte: [Double?]) {
                self.zeit = zeit
                self.werte = werte
            }
        }

        public var geraet: String
        public var raster: Int
        public var spalten: [String]
        public var zeilen: [Zeile]

        public init(geraet: String, raster: Int, spalten: [String], zeilen: [Zeile]) {
            self.geraet = geraet
            self.raster = raster
            self.spalten = spalten
            self.zeilen = zeilen
        }
    }

    /// Höchstens acht Schlüssel. Ab 300 s Raster aus den Fünfminutenmitteln, höchstens 31 Tage;
    /// darunter aus den Messwerten im Takt der Abfrage, höchstens zwei Tage.
    public func reihe(geraet: String, schluessel: [String], von: Date, bis: Date, raster: Int) async throws -> Protokollreihe {
        let liste = schluessel.prefix(8).joined(separator: ",")
            .addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed.subtracting(CharacterSet(charactersIn: "&=+"))) ?? ""
        let pfad = "/api/log/series?geraet=\(geraet)&schluessel=\(liste)&von=\(Int(von.timeIntervalSince1970))"
            + "&bis=\(Int(bis.timeIntervalSince1970))&raster=\(raster)"
        let j = try JSONWert.lesen(try await verbindung.holenRoh(pfad, zeitlimit: 60))
        let iso = ISO8601DateFormatter()
        let zeilen = (j["zeilen"]?.alsListe ?? []).compactMap { z -> Protokollreihe.Zeile? in
            guard let l = z.alsListe, let t = l.first?.alsText, let zeit = iso.date(from: t) else { return nil }
            return .init(zeit: zeit, werte: l.dropFirst().map(\.alsZahl))
        }
        return Protokollreihe(geraet: j["geraet"]?.alsText ?? geraet, raster: j["raster_s"]?.alsGanzzahl ?? raster,
                              spalten: (j["spalten"]?.alsListe ?? []).compactMap(\.alsText), zeilen: zeilen)
    }
}
