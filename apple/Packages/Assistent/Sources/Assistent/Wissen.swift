import Foundation

/// Das Wissen der KI: Handbuch und Konzepte aus `docs/`, die das Projekt als Ressource in die App
/// übernimmt.
///
/// Claude bekommt den vollständigen Text im zwischengespeicherten Teil der Anweisungen. Die
/// Apple-Modelle haben dafür zu wenig Kontext; sie fragen über `wissen_suchen` einzelne
/// Abschnitte ab. Die Suche ist bewusst schlicht: Begriffe als Teilwort, damit Zusammensetzungen
/// wie „Kesselkreispumpe“ auch auf „Pumpe“ passen, gewichtet nach Seltenheit und Überschrift.
public struct Wissen: Sendable {
    public struct Dokument: Sendable, Hashable {
        public var name: String
        public var titel: String
        public var text: String
    }

    public struct Abschnitt: Sendable, Hashable, Identifiable {
        public var id: String { "\(dokument)#\(nummer)" }
        public var dokument: String
        public var nummer: Int
        /// Überschriften von oben nach unten, etwa „Brenner und Pufferspeicher › Ladezustand“
        public var titel: String
        public var text: String
    }

    public let dokumente: [Dokument]
    public let abschnitte: [Abschnitt]

    /// Die Dateien in der Reihenfolge, in der sie in den Anweisungen stehen: erst das Handbuch.
    public static let dateien = ["handbuch.md", "konzept-waermeerzeuger.md", "konzept-auswertung.md"]

    public init(dokumente: [Dokument]) {
        self.dokumente = dokumente
        abschnitte = dokumente.flatMap(Self.gliedern)
    }

    /// Liest die Dokumente aus einem Ordner; fehlende Dateien entfallen.
    public init(ordner: URL) {
        let dokumente = Self.dateien.compactMap { name -> Dokument? in
            guard let text = try? String(contentsOf: ordner.appending(path: name), encoding: .utf8) else { return nil }
            let titel = text.split(separator: "\n").first { $0.hasPrefix("# ") }.map { String($0.dropFirst(2)) } ?? name
            return Dokument(name: name, titel: titel, text: text)
        }
        self.init(dokumente: dokumente)
    }

    /// Aus den Ressourcen der App, in die das Projekt die Dateien aus `docs/` übernimmt
    public static func ausBuendel(_ buendel: Bundle = .main) -> Wissen {
        Wissen(ordner: buendel.resourceURL ?? URL(filePath: "/nicht-vorhanden"))
    }

    public var leer: Bool { dokumente.isEmpty }

    /// Alle Dokumente für die Anweisungen, jeweils mit Kopf
    public var volltext: String {
        dokumente.map { "<dokument name=\"\($0.name)\">\n\($0.text.trimmingCharacters(in: .whitespacesAndNewlines))\n</dokument>" }
            .joined(separator: "\n\n")
    }

    // MARK: Gliederung

    static func gliedern(_ d: Dokument) -> [Abschnitt] {
        var abschnitte: [Abschnitt] = []
        var pfad: [String] = []
        var zeilen: [String] = []
        var imCode = false

        func abschliessen() {
            let text = zeilen.joined(separator: "\n").trimmingCharacters(in: .whitespacesAndNewlines)
            zeilen = []
            guard !text.isEmpty else { return }
            let titel = pfad.isEmpty ? d.titel : pfad.joined(separator: " › ")
            // Lange Abschnitte in Absatzgruppen von höchstens etwa 2 000 Zeichen
            var teil = ""
            for absatz in text.components(separatedBy: "\n\n") {
                if teil.count + absatz.count > 2000, !teil.isEmpty {
                    abschnitte.append(Abschnitt(dokument: d.name, nummer: abschnitte.count, titel: titel, text: teil))
                    teil = ""
                }
                teil += (teil.isEmpty ? "" : "\n\n") + absatz
            }
            if !teil.isEmpty { abschnitte.append(Abschnitt(dokument: d.name, nummer: abschnitte.count, titel: titel, text: teil)) }
        }

        for zeile in d.text.components(separatedBy: "\n") {
            if zeile.hasPrefix("```") { imCode.toggle() }
            let ebene = imCode ? 0 : zeile.prefix(while: { $0 == "#" }).count
            if ebene > 0, ebene <= 4, zeile.dropFirst(ebene).hasPrefix(" ") {
                abschliessen()
                let ueberschrift = zeile.dropFirst(ebene + 1).trimmingCharacters(in: .whitespaces)
                // Ebene 1 ist der Titel des Dokuments, Ebene 2 der oberste Abschnitt.
                let tiefe = max(0, ebene - 2)
                pfad = Array(pfad.prefix(tiefe))
                if ebene >= 2 { pfad.append(ueberschrift) }
                continue
            }
            zeilen.append(zeile)
        }
        abschliessen()
        return abschnitte
    }

    // MARK: Suche

    static let fuellwoerter: Set<String> = [
        "der", "die", "das", "den", "dem", "des", "ein", "eine", "einen", "einem", "einer", "und", "oder", "ist",
        "sind", "wird", "werden", "wie", "was", "wann", "warum", "wo", "mit", "von", "vom", "zum", "zur", "auf",
        "fur", "bei", "nach", "aus", "im", "in", "an", "am", "zu", "es", "sich", "nicht", "kein", "keine", "man",
        "ich", "sie", "welche", "welcher", "welches", "soll", "kann", "muss", "gibt", "uber",
    ]

    static func begriffe(_ anfrage: String) -> [String] {
        let einfach = anfrage.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: Locale(identifier: "de_DE"))
        let woerter = einfach.split { !$0.isLetter && !$0.isNumber && $0 != "_" }.map(String.init)
        return woerter.filter { $0.count >= 3 && !fuellwoerter.contains($0) }.map(stamm)
    }

    /// Nimmt gängige Endungen ab: „Pumpen“ findet „Pumpe“ und „Kesselkreispumpe“.
    static func stamm(_ w: String) -> String {
        for endung in ["ungen", "en", "er", "es", "e", "n", "s"] where w.count - endung.count >= 4 && w.hasSuffix(endung) {
            return String(w.dropLast(endung.count))
        }
        return w
    }

    public func suchen(_ anfrage: String, hoechstens: Int = 3) -> [Abschnitt] {
        let begriffe = Self.begriffe(anfrage)
        guard !begriffe.isEmpty, !abschnitte.isEmpty else { return [] }
        let texte = abschnitte.map { ($0.titel + "\n" + $0.text).folding(options: [.caseInsensitive, .diacriticInsensitive], locale: Locale(identifier: "de_DE")) }
        let titel = abschnitte.map { $0.titel.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: Locale(identifier: "de_DE")) }
        let n = Double(abschnitte.count)
        var punkte = [Double](repeating: 0, count: abschnitte.count)
        for b in Set(begriffe) {
            let haeufigkeit = texte.map { $0.components(separatedBy: b).count - 1 }
            let vorkommen = Double(haeufigkeit.filter { $0 > 0 }.count)
            guard vorkommen > 0 else { continue }
            let gewicht = log(1 + n / vorkommen)
            for i in texte.indices where haeufigkeit[i] > 0 {
                // Häufigkeit gedämpft, Überschrift dreifach
                punkte[i] += gewicht * (1 + log(Double(haeufigkeit[i]))) + (titel[i].contains(b) ? 3 * gewicht : 0)
            }
        }
        return punkte.indices.filter { punkte[$0] > 0 }
            .sorted { punkte[$0] > punkte[$1] }
            .prefix(hoechstens)
            .map { abschnitte[$0] }
    }
}
