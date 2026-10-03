import Foundation
import SwiftData

/// Ein Tag eines Geräts. Die Werte liegen gepackt vor (siehe `Tagesmatrix`), damit ein Jahr
/// Aufzeichnung aller Geräte bei einigen zehn Megabyte bleibt.
@Model
final class Messblock {
    /// „<Gerät>|<Tag>“
    @Attribute(.unique) var kennung: String
    var geraet: String
    var tag: Int
    /// Ort, wie ihn das Gerät zuletzt an diesem Tag meldete
    var ort: String
    var spalten: [String]
    var werte: Data
    var bezeichnungen: [String: String]

    init(geraet: String, tag: Int, ort: String) {
        kennung = Self.kennung(geraet, tag)
        self.geraet = geraet
        self.tag = tag
        self.ort = ort
        spalten = []
        werte = Data()
        bezeichnungen = [:]
    }

    static func kennung(_ geraet: String, _ tag: Int) -> String { "\(geraet)|\(tag)" }

    var matrix: Tagesmatrix {
        get { Tagesmatrix(spalten: spalten, daten: werte) }
        set {
            spalten = newValue.spalten
            werte = newValue.daten
        }
    }
}

/// Mittelwerte eines Platzes für ein Gerät, bereit zum Schreiben
public struct Messzeile: Sendable, Equatable {
    public var geraet: String
    public var ort: String
    /// Fortlaufender Platz, siehe `Raster.index`
    public var index: Int
    public var werte: [String: Double]
    public var bezeichnungen: [String: String]

    public init(geraet: String, ort: String, index: Int, werte: [String: Double], bezeichnungen: [String: String]) {
        self.geraet = geraet
        self.ort = ort
        self.index = index
        self.werte = werte
        self.bezeichnungen = bezeichnungen
    }
}

public struct Messpunkt: Sendable, Equatable {
    public var zeit: Date
    public var wert: Double

    public init(zeit: Date, wert: Double) {
        self.zeit = zeit
        self.wert = wert
    }
}

/// Umfang des gespeicherten Verlaufs
public struct Verlaufsumfang: Sendable, Equatable {
    public var bloecke: Int
    public var geraete: Int
    /// Tage mit mindestens einem Messwert, gleich von welchem Gerät
    public var tage: Int
    /// Erster und letzter Platz mit einem Messwert
    public var von: Date?
    public var bis: Date?
    /// Summe der gepackten Werte in Byte
    public var groesse: Int
}

/// Der Verlauf aller Geräte, lokal in SwiftData. Alle Zugriffe laufen über diesen Actor.
@ModelActor
public actor Verlaufsspeicher {
    /// `datei` nil hält den Verlauf nur im Speicher, etwa für Tests und Vorschauen.
    public static func oeffnen(datei: URL?) throws -> Verlaufsspeicher {
        let schema = Schema([Messblock.self, Ereignisblock.self])
        let konfiguration: ModelConfiguration
        if let datei {
            try FileManager.default.createDirectory(at: datei.deletingLastPathComponent(), withIntermediateDirectories: true)
            konfiguration = ModelConfiguration(schema: schema, url: datei)
        } else {
            konfiguration = ModelConfiguration(schema: schema, isStoredInMemoryOnly: true)
        }
        return Verlaufsspeicher(modelContainer: try ModelContainer(for: schema, configurations: konfiguration))
    }

    /// Schreibt Mittelwerte je Platz. `nurLuecken` lässt vorhandene Werte stehen; so ergänzen
    /// Geräteverlauf und Mitschnitt die eigene Aufzeichnung, ohne sie zu überschreiben.
    public func schreiben(_ zeilen: [Messzeile], nurLuecken: Bool = false) throws {
        guard !zeilen.isEmpty else { return }
        var bloecke: [String: Messblock] = [:]
        var matrizen: [String: Tagesmatrix] = [:]
        for z in zeilen {
            let tag = Raster.tag(z.index)
            let kennung = Messblock.kennung(z.geraet, tag)
            let block: Messblock
            if let b = bloecke[kennung] {
                block = b
            } else {
                block = try messblock(z.geraet, tag, ort: z.ort)
                bloecke[kennung] = block
                matrizen[kennung] = block.matrix
            }
            if !z.ort.isEmpty { block.ort = z.ort }
            var m = matrizen[kennung] ?? block.matrix
            for (schluessel, wert) in z.werte {
                m.setzen(platz: Raster.platz(z.index), schluessel, wert, nurLuecken: nurLuecken)
            }
            matrizen[kennung] = m
            for (schluessel, name) in z.bezeichnungen where !name.isEmpty {
                if !nurLuecken || block.bezeichnungen[schluessel] == nil {
                    block.bezeichnungen[schluessel] = name
                }
            }
        }
        for (kennung, m) in matrizen {
            bloecke[kennung]?.matrix = m
        }
        try modelContext.save()
    }

    /// Füllt Lücken aus einem Verlauf mit beliebigem Raster: Punkte desselben Platzes werden
    /// gemittelt.
    public func auffuellen(geraet: String, ort: String, reihen: [String: [Messpunkt]], bezeichnungen: [String: String]) throws {
        var summen: [Int: [String: (Double, Int)]] = [:]
        for (schluessel, punkte) in reihen {
            for p in punkte where p.wert.isFinite {
                let i = Raster.index(p.zeit)
                let bisher = summen[i]?[schluessel] ?? (0, 0)
                summen[i, default: [:]][schluessel] = (bisher.0 + p.wert, bisher.1 + 1)
            }
        }
        let zeilen = summen.keys.sorted().map { i in
            Messzeile(geraet: geraet, ort: ort, index: i,
                      werte: summen[i]!.mapValues { $0.0 / Double($0.1) },
                      bezeichnungen: bezeichnungen)
        }
        try schreiben(zeilen, nurLuecken: true)
    }

    /// Mittelwerte im Raster `schritt` (ein Vielfaches von fünf Minuten) zwischen `von` und `bis`.
    /// `geraet` und `schluessel` schränken ein, was gelesen wird.
    public func auszug(von: Date, bis: Date, schritt: TimeInterval, geraet: String? = nil,
                       schluessel: Set<String>? = nil) throws -> Verlaufsauszug {
        let schritt = max(Raster.schritt, (schritt / Raster.schritt).rounded() * Raster.schritt)
        let beginn = Date(timeIntervalSince1970: (von.timeIntervalSince1970 / schritt).rounded(.down) * schritt)
        let anzahl = max(1, Int(((bis.timeIntervalSince(beginn)) / schritt).rounded(.up)))
        let erster = Raster.index(beginn)
        let letzter = Raster.index(bis)
        let tagVon = Raster.tag(erster)
        let tagBis = Raster.tag(letzter)
        var beschreibung = FetchDescriptor<Messblock>(predicate: #Predicate { $0.tag >= tagVon && $0.tag <= tagBis })
        beschreibung.sortBy = [SortDescriptor(\.tag)]
        var bloecke = try modelContext.fetch(beschreibung)
        if let geraet { bloecke = bloecke.filter { $0.geraet == geraet } }

        struct Sammler {
            var ort = ""
            var bezeichnung = ""
            var summen: [Double]
            var anzahl: [Int]
        }
        var reihen: [String: Sammler] = [:]
        let plaetzeJeSchritt = Int(schritt / Raster.schritt)
        for block in bloecke {
            let m = block.matrix
            let basis = block.tag * Raster.plaetzeJeTag
            for (spalte, name) in m.spalten.enumerated() {
                if let schluessel, !schluessel.contains(name) { continue }
                let id = "\(block.geraet)/\(name)"
                var s = reihen[id] ?? Sammler(summen: [Double](repeating: 0, count: anzahl), anzahl: [Int](repeating: 0, count: anzahl))
                for platz in 0..<Raster.plaetzeJeTag {
                    let index = basis + platz
                    guard index >= erster, index <= letzter, let wert = m.wert(platz: platz, spalte: spalte) else { continue }
                    let fach = (index - erster) / plaetzeJeSchritt
                    guard fach < anzahl else { continue }
                    s.summen[fach] += wert
                    s.anzahl[fach] += 1
                }
                // Jüngere Blöcke kommen später und bestimmen Ort und Bezeichnung.
                s.ort = block.ort
                s.bezeichnung = block.bezeichnungen[name] ?? s.bezeichnung
                reihen[id] = s
            }
        }
        let liste = reihen.compactMap { id, s -> Verlaufsauszug.Reihe? in
            guard s.anzahl.contains(where: { $0 > 0 }) else { return nil }
            let teile = id.split(separator: "/", maxSplits: 1).map(String.init)
            return Verlaufsauszug.Reihe(
                geraet: teile[0], ort: s.ort, schluessel: teile[1],
                bezeichnung: s.bezeichnung.isEmpty ? teile[1] : s.bezeichnung,
                werte: zip(s.summen, s.anzahl).map { $1 > 0 ? $0 / Double($1) : nil })
        }
        return Verlaufsauszug(beginn: beginn, schritt: schritt, anzahl: anzahl,
                              reihen: liste.sorted { ($0.ort, $0.schluessel) < ($1.ort, $1.schluessel) })
    }

    public func umfang() throws -> Verlaufsumfang {
        let bloecke = try modelContext.fetch(FetchDescriptor<Messblock>())
        var erster: Int?
        var letzter: Int?
        var tage: Set<Int> = []
        for block in bloecke {
            let m = block.matrix
            let belegt = (0..<Raster.plaetzeJeTag).filter { platz in
                (0..<m.spalten.count).contains { m.wert(platz: platz, spalte: $0) != nil }
            }
            guard let a = belegt.first, let b = belegt.last else { continue }
            tage.insert(block.tag)
            let basis = block.tag * Raster.plaetzeJeTag
            erster = min(erster ?? .max, basis + a)
            letzter = max(letzter ?? .min, basis + b)
        }
        return Verlaufsumfang(
            bloecke: bloecke.count,
            geraete: Set(bloecke.map(\.geraet)).count,
            tage: tage.count,
            von: erster.map(Raster.beginn),
            bis: letzter.map { Raster.beginn($0 + 1) },
            groesse: bloecke.reduce(0) { $0 + $1.werte.count })
    }

    /// Verwirft den gesamten Verlauf aller Geräte samt Ereignissen.
    public func loeschen() throws {
        try modelContext.delete(model: Messblock.self)
        try modelContext.delete(model: Ereignisblock.self)
        try modelContext.save()
    }

    private func messblock(_ geraet: String, _ tag: Int, ort: String) throws -> Messblock {
        let kennung = Messblock.kennung(geraet, tag)
        var beschreibung = FetchDescriptor<Messblock>(predicate: #Predicate { $0.kennung == kennung })
        beschreibung.fetchLimit = 1
        if let vorhanden = try modelContext.fetch(beschreibung).first { return vorhanden }
        let neu = Messblock(geraet: geraet, tag: tag, ort: ort)
        modelContext.insert(neu)
        return neu
    }
}

/// Ein Ausschnitt des Verlaufs in festem Raster, fertig für Diagramme
public struct Verlaufsauszug: Sendable, Equatable {
    public var beginn: Date
    public var schritt: TimeInterval
    public var anzahl: Int
    public var reihen: [Reihe]

    public init(beginn: Date, schritt: TimeInterval, anzahl: Int, reihen: [Reihe]) {
        self.beginn = beginn
        self.schritt = schritt
        self.anzahl = anzahl
        self.reihen = reihen
    }

    public struct Reihe: Identifiable, Sendable, Hashable {
        public var id: String { "\(geraet)/\(schluessel)" }
        public var geraet: String
        public var ort: String
        public var schluessel: String
        public var bezeichnung: String
        public var werte: [Double?]

        public init(geraet: String, ort: String, schluessel: String, bezeichnung: String, werte: [Double?]) {
            self.geraet = geraet
            self.ort = ort
            self.schluessel = schluessel
            self.bezeichnung = bezeichnung
            self.werte = werte
        }

        public var einheit: Messgroesse.Einheit { Messgroesse.einheit(schluessel) }
    }

    public struct Punkt: Identifiable, Sendable, Hashable {
        public var id: Date { zeit }
        public var zeit: Date
        public var wert: Double

        public init(zeit: Date, wert: Double) {
            self.zeit = zeit
            self.wert = wert
        }
    }

    public var ende: Date { beginn.addingTimeInterval(Double(anzahl) * schritt) }
    public var leer: Bool { reihen.isEmpty }

    public func reihe(_ geraet: String, _ schluessel: String) -> Reihe? {
        reihen.first { $0.geraet == geraet && $0.schluessel == schluessel }
    }

    /// Zusammenhängende Stücke einer Reihe. Eine Linie über eine Lücke hinweg täuschte Werte vor,
    /// die niemand gemessen hat; Diagramme zeichnen deshalb jedes Stück für sich.
    public func abschnitte(_ reihe: Reihe) -> [[Punkt]] {
        var stuecke: [[Punkt]] = []
        var laufend: [Punkt] = []
        for (i, wert) in reihe.werte.enumerated() {
            if let wert {
                laufend.append(Punkt(zeit: beginn.addingTimeInterval((Double(i) + 0.5) * schritt), wert: wert))
            } else if !laufend.isEmpty {
                stuecke.append(laufend)
                laufend = []
            }
        }
        if !laufend.isEmpty { stuecke.append(laufend) }
        return stuecke
    }

    /// Punkte einer Reihe, jeweils in der Mitte ihres Rasters
    public func punkte(_ reihe: Reihe) -> [Punkt] {
        reihe.werte.enumerated().compactMap { i, wert in
            wert.map { Punkt(zeit: beginn.addingTimeInterval((Double(i) + 0.5) * schritt), wert: $0) }
        }
    }

    public static func leer(von: Date, bis: Date) -> Verlaufsauszug {
        Verlaufsauszug(beginn: von, schritt: max(Raster.schritt, bis.timeIntervalSince(von)), anzahl: 1, reihen: [])
    }
}
