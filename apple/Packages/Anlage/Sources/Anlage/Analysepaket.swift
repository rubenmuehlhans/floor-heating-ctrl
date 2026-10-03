import Foundation
import Geraeteschnittstelle
import Verlauf

/// Der Verlauf eines Zeitraums als Ordner für Auswertungen außerhalb der App: je Gerät eine
/// CSV-Datei im gewählten Raster, die Ereignisse, der Katalog der Messgrößen und eine
/// Beschreibung der Anlage. Die Spalten folgen dem Katalog, wie in den Dateien des Leitstands;
/// Zeiten in UTC am Beginn des Rasters, Anteile 0–1.
///
/// Das Paket enthält Gerätekennungen, Orts- und Raumnamen, aber keine Adressen und keine
/// Zugangsdaten.
public enum Analysepaket {
    public struct Inhalt: Sendable {
        public var dateien: [String: Data]
        /// Zeilen je Gerätedatei, ohne Kopfzeile
        public var zeilen: [String: Int]
    }

    public static func inhalt(auszug a: Verlaufsauszug, ereignisse: [Ereignis], bild: Anlagenbild,
                              katalog: String?, von: Date, bis: Date, erstellt: Date = .now) -> Inhalt {
        var dateien: [String: Data] = [:]
        var zeilen: [String: Int] = [:]
        let nachGeraet = Dictionary(grouping: a.reihen, by: \.geraet)
        for (geraet, reihen) in nachGeraet {
            // raum.2 vor raum.10
            let spalten = reihen.sorted { $0.schluessel.localizedStandardCompare($1.schluessel) == .orderedAscending }
            var text = "zeit," + spalten.map(\.schluessel).joined(separator: ",") + "\n"
            var n = 0
            for i in 0..<a.anzahl {
                let werte = spalten.map { i < $0.werte.count ? $0.werte[i] : nil }
                guard werte.contains(where: { $0 != nil }) else { continue }
                text += iso(a.beginn.addingTimeInterval(Double(i) * a.schritt)) + ","
                    + werte.map { $0.map(zahl) ?? "" }.joined(separator: ",") + "\n"
                n += 1
            }
            dateien["\(dateiname(geraet)).csv"] = Data(text.utf8)
            zeilen[geraet] = n
        }

        var e = "zeit,geraet,art,titel,einzelheiten,felder\n"
        for x in ereignisse {
            let felder = (try? JSONWert.objekt(x.felder).daten()).map { String(decoding: $0, as: UTF8.self) } ?? "{}"
            let einzelheiten = Ereignistext.einzelheiten(x, befundtitel: Zusammenfuehrung.geraetebefundTitel)
            e += [iso(x.zeit), x.geraet, x.art, Ereignistext.titel(x), einzelheiten, felder]
                .map { feld($0.replacingOccurrences(of: "\u{00A0}", with: " ")) }.joined(separator: ",") + "\n"
        }
        dateien["ereignisse.csv"] = Data(e.utf8)

        if let katalog { dateien["katalog-messgroessen.md"] = Data(katalog.utf8) }
        if let beschreibung = try? JSONWert.objekt(anlage(bild, reihen: a.reihen)).daten() {
            dateien["anlage.json"] = beschreibung
        }
        dateien["LIESMICH.txt"] = Data(liesmich(a, geraete: nachGeraet.keys.sorted(), zeilen: zeilen,
                                                ereignisse: ereignisse.count, von: von, bis: bis, erstellt: erstellt).utf8)
        return Inhalt(dateien: dateien, zeilen: zeilen)
    }

    /// Schreibt den Inhalt in den Ordner `ordner`; er wird bei Bedarf angelegt.
    public static func schreiben(_ inhalt: Inhalt, nach ordner: URL) throws {
        try FileManager.default.createDirectory(at: ordner, withIntermediateDirectories: true)
        for (name, daten) in inhalt.dateien {
            try daten.write(to: ordner.appending(path: name), options: .atomic)
        }
    }

    /// Packt einen Ordner als ZIP; das System erzeugt es beim koordinierten Lesen zum Hochladen.
    public static func zip(_ ordner: URL, nach ziel: URL) throws {
        var fehler: NSError?
        var innen: Error?
        NSFileCoordinator().coordinate(readingItemAt: ordner, options: .forUploading, error: &fehler) { zip in
            do {
                try? FileManager.default.removeItem(at: ziel)
                try FileManager.default.copyItem(at: zip, to: ziel)
            } catch {
                innen = error
            }
        }
        if let fehler { throw fehler }
        if let innen { throw innen }
    }

    // MARK: Teile

    static func anlage(_ b: Anlagenbild, reihen: [Verlaufsauszug.Reihe]) -> [String: JSONWert] {
        let geraete = Set(reihen.map(\.geraet))
        // Verteiler stehen je nach Stand schon unter den Geräten; jede Kennung nur einmal
        var liste: [JSONWert] = []
        var gesehen: Set<String> = []
        for (id, ort, art) in b.geraete.map({ ($0.id, $0.ort, "\($0.art)") }) + b.etagen.map({ ($0.id, $0.name, "verteiler") })
        where gesehen.insert(id).inserted {
            liste.append(["kennung": .text(id), "ort": .text(ort), "art": .text(art), "im_paket": .bool(geraete.contains(id))])
        }
        return [
            "hinweis": "Beschreibung der Anlage zum Zeitpunkt der Erstellung. Schlüssel der Spalten siehe katalog-messgroessen.md.",
            "geraete": .liste(liste),
            "raeume": .liste(b.etagen.flatMap { e in
                e.raeume.map { r in
                    ["verteiler": .text(e.id), "etage": .text(e.name), "nummer": .zahl(Double(r.nummer)), "name": .text(r.name),
                     "spalten": .text("raum.\(r.nummer).ist, .soll, .stellung, .feuchte in \(e.id).csv")]
                }
            }),
            "heizkreise": .liste(b.heizkreise.map { k in
                ["nummer": .zahl(Double(k.nummer)), "name": .text(k.name),
                 "vorlauf": .text(Messgroesse.fuehler(k.vorlaufRolle)), "ruecklauf": .text(Messgroesse.fuehler(k.ruecklaufRolle)),
                 "pumpe": .text("pumpe.\(k.nummer)"), "versorgt": .liste(k.versorgteVerteiler.map { .text($0) })]
            }),
        ]
    }

    static func liesmich(_ a: Verlaufsauszug, geraete: [String], zeilen: [String: Int], ereignisse: Int,
                         von: Date, bis: Date, erstellt: Date) -> String {
        let raster = a.schritt >= 3600 ? "\(Int(a.schritt / 3600)) h" : "\(Int(a.schritt / 60)) min"
        var t = """
            Analysepaket der Heizungsanlage
            Erstellt: \(iso(erstellt))
            Zeitraum: \(iso(von)) bis \(iso(bis)), Raster \(raster)

            Inhalt
            - <kennung>.csv: je Gerät die Mittelwerte je Raster. Erste Spalte zeit in UTC (ISO 8601) am
              Beginn des Rasters, danach die Schlüssel aus katalog-messgroessen.md. Leere Zelle: kein Wert.
              Temperaturen in °C, Anteile (Brenner, Pumpen, Ventilstellung, Füllstand) von 0 bis 1.
            - ereignisse.csv: Neustarts, Ausfälle, Brenner- und Pumpenwechsel, Sollwerte, Befunde aus dem
              Protokoll des Leitstands; die Spalte felder enthält die Angaben als JSON.
            - anlage.json: Geräte, Räume und Heizkreise mit den Schlüsseln ihrer Spalten.
            - katalog-messgroessen.md: Bedeutung und Einheit jedes Schlüssels.

            Das Paket enthält Gerätekennungen sowie Orts- und Raumnamen und ist nicht zur
            Veröffentlichung bestimmt. Adressen und Zugangsdaten enthält es nicht.

            Dateien
            """
        for g in geraete { t += "\n- \(dateiname(g)).csv: \(zeilen[g] ?? 0) Zeilen" }
        t += "\n- ereignisse.csv: \(ereignisse) Ereignisse\n"
        return t
    }

    static func dateiname(_ geraet: String) -> String {
        String(geraet.map { $0.isLetter || $0.isNumber || $0 == "_" || $0 == "-" ? $0 : "_" })
    }

    static func iso(_ d: Date) -> String {
        d.formatted(Date.ISO8601FormatStyle(timeZone: TimeZone(identifier: "UTC")!))
    }

    /// Punkt als Dezimalzeichen, höchstens drei Nachkommastellen, wie im Protokoll des Leitstands
    static func zahl(_ x: Double) -> String {
        guard x.isFinite else { return "" }
        var s = String(format: "%.3f", x)
        while s.contains("."), s.hasSuffix("0") { s.removeLast() }
        if s.hasSuffix(".") { s.removeLast() }
        return s == "-0" ? "0" : s
    }

    /// CSV-Feld, bei Bedarf in Anführungszeichen
    static func feld(_ s: String) -> String {
        s.contains(where: { $0 == "," || $0 == "\"" || $0 == "\n" }) ? "\"" + s.replacingOccurrences(of: "\"", with: "\"\"") + "\"" : s
    }
}
