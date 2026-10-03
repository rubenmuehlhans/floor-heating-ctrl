import Foundation
import Anlage
import Geraeteschnittstelle

/// Werte, die die KI vorschlagen darf, ohne dass sie im Parameterkatalog stehen: die
/// Betriebsarten der Pumpen, die nur als Befehl gehen, und die versorgten Verteiler eines
/// Heizkreises, eine Liste.
public enum Sonderparameter: String, CaseIterable, Sendable {
    case heizkreisBetriebsart = "heizung/heizkreis.betriebsart"
    case kesselkreispumpeBetriebsart = "heizung/kesselkreispumpe.betriebsart"
    case versorgteVerteiler = "heizung/circuits[].peers"

    public var schluessel: String { String(rawValue.dropFirst("heizung/".count)) }

    public var bezeichnung: String {
        switch self {
        case .heizkreisBetriebsart: "Betriebsart der Heizkreispumpe"
        case .kesselkreispumpeBetriebsart: "Betriebsart der Kesselkreispumpe"
        case .versorgteVerteiler: "Versorgte Verteiler"
        }
    }

    static let betriebsarten: [String: String] = ["auto": "Automatik", "ein": "dauernd ein", "aus": "aus"]
}

/// Wie ein Wert geschrieben wird
public enum Schreibart: Sendable, Equatable {
    /// `PUT /api/config` mit der Teilangabe nach `Schreibweg`
    case konfiguration
    /// `POST /api/room/{id}/target`
    case sollwert
    /// `POST /api/room/{id}/mode`
    case raumbetrieb
    /// `POST /api/circuit/{id}/mode`
    case heizkreisbetrieb
    /// `POST /api/boilerpump/{modus}`
    case kesselkreispumpenbetrieb

    public static func fuer(_ parameter: String) -> Schreibart {
        switch parameter {
        case "verteiler/rooms[].target_c": .sollwert
        case "verteiler/rooms[].mode": .raumbetrieb
        case Sonderparameter.heizkreisBetriebsart.rawValue: .heizkreisbetrieb
        case Sonderparameter.kesselkreispumpeBetriebsart.rawValue: .kesselkreispumpenbetrieb
        default: .konfiguration
        }
    }
}

/// Lesen, Anzeigen und Schreiben der Werte eines Vorschlags
public enum Zielwerte {
    /// Der Wert, der jetzt am Gerät gilt
    public static func aktuell(_ ziel: Vorschlag.Ziel, _ stand: Geraetestand) -> JSONWert? {
        let nummer = ziel.eintrag?.alsGanzzahl
        switch ziel.parameter {
        case Sonderparameter.heizkreisBetriebsart.rawValue:
            return stand.heizgeraet?.heizkreise?.first { $0.id == nummer }?.betriebsart.map(JSONWert.text)
        case Sonderparameter.kesselkreispumpeBetriebsart.rawValue:
            return stand.heizgeraet?.kesselkreispumpe?.betriebsart.map(JSONWert.text)
        case Sonderparameter.versorgteVerteiler.rawValue:
            return stand.konfiguration?["circuits"]?.alsListe?.first { $0["id"]?.alsGanzzahl == nummer }?["peers"] ?? .liste([])
        // Sollwert und Betriebsart ändern sich auch am Gerät selbst; der Zustand ist frischer
        // als die Konfiguration, die die App nur alle fünf Minuten liest.
        case "verteiler/rooms[].target_c", "verteiler/rooms[].mode":
            let raum = stand.verteiler?.raeume?.first { $0.id == nummer }
            if ziel.parameter.hasSuffix("target_c"), let soll = raum?.sollC { return .zahl(soll) }
            if ziel.parameter.hasSuffix("mode"), let art = raum?.betriebsart { return .text(art) }
            guard let p = Parameterkatalog.parameter(ziel.parameter), let k = stand.konfiguration else { return nil }
            return wert(p, eintrag: ziel.eintrag, in: k)
        default:
            guard let p = Parameterkatalog.parameter(ziel.parameter), let k = stand.konfiguration else { return nil }
            return wert(p, eintrag: ziel.eintrag, in: k)
        }
    }

    /// Wert eines Parameters in einer Konfiguration, bei Listen im Eintrag mit `id`
    public static func wert(_ p: Parameter, eintrag: JSONWert?, in k: JSONWert) -> JSONWert? {
        let lesen = { (e: JSONWert) in p.pfad.reduce(Optional(e)) { acc, t in Int(t).map { acc?[$0] } ?? acc?[t] } }
        guard let liste = p.ort.liste else { return lesen(k) }
        guard let e = k[liste]?.alsListe?.first(where: { $0[p.ort.schluessel] == eintrag }) else { return nil }
        return lesen(e)
    }

    /// Die Teilangabe für `PUT /api/config` aus Zielen desselben Geräts; ohne solche Ziele `nil`.
    public static func teil(_ ziele: [Vorschlag.Ziel], neu: Bool, konfiguration k: JSONWert) -> JSONWert? {
        let katalog = ziele.filter { Schreibart.fuer($0.parameter) == .konfiguration && $0.parameter != Sonderparameter.versorgteVerteiler.rawValue }
        let aenderungen = katalog.compactMap { z in
            Parameterkatalog.parameter(z.parameter).map { Wertaenderung($0, eintrag: z.eintrag, wert: neu ? z.neu : z.bisher) }
        }
        var teil: JSONWert? = aenderungen.isEmpty ? Optional.none : Schreibweg.teil(aenderungen, konfiguration: k)
        let kreise = ziele.filter { $0.parameter == Sonderparameter.versorgteVerteiler.rawValue }
        if !kreise.isEmpty {
            // Wie bei den Heizkreisen üblich: alle Kennungen, geänderte Felder
            var liste: [JSONWert] = (teil?["circuits"]?.alsListe) ?? (k["circuits"]?.alsListe ?? []).compactMap { c in c["id"].map { ["id": $0] } }
            for z in kreise {
                guard let i = liste.firstIndex(where: { $0["id"] == z.eintrag }) else { continue }
                liste[i]["peers"] = neu ? z.neu : z.bisher
            }
            var gesamt = teil ?? [:]
            gesamt["circuits"] = .liste(liste)
            teil = gesamt
        }
        return teil
    }

    /// Wert zur Anzeige: Zahl mit Einheit, Bezeichnung einer Auswahl, Orte statt Kennungen
    public static func anzeige(_ parameter: String, _ wert: JSONWert, orte: [String: String] = [:]) -> String {
        switch parameter {
        case Sonderparameter.heizkreisBetriebsart.rawValue, Sonderparameter.kesselkreispumpeBetriebsart.rawValue:
            return wert.alsText.map { Sonderparameter.betriebsarten[$0] ?? $0 } ?? "–"
        case Sonderparameter.versorgteVerteiler.rawValue:
            let namen = (wert.alsListe ?? []).compactMap(\.alsText).map { orte[$0] ?? $0 }
            return namen.isEmpty ? "keiner" : namen.joined(separator: ", ")
        default:
            guard let p = Parameterkatalog.parameter(parameter) else { return wert.kompakt }
            return anzeige(p, wert)
        }
    }

    public static func anzeige(_ p: Parameter, _ wert: JSONWert) -> String {
        let deutsch = Locale(identifier: "de_DE")
        let einheit = p.einheit.isEmpty ? "" : "\u{00A0}\(p.einheit)"
        switch p.art {
        case .zahl(_, _, let stellen):
            guard let x = wert.alsZahl else { return "–" }
            return x.formatted(.number.precision(.fractionLength(stellen)).locale(deutsch)) + einheit
        case .ganzzahl:
            guard let x = wert.alsZahl else { return "–" }
            return Int(x.rounded()).formatted(.number.locale(deutsch).grouping(.never)) + einheit
        case .millisekunden:
            guard let x = wert.alsZahl else { return "–" }
            return (x / 1000).formatted(.number.precision(.fractionLength(0...1)).locale(deutsch)) + einheit
        case .schalter:
            return wert.alsBool.map { $0 ? "ein" : "aus" } ?? "–"
        case .auswahl(let wahl):
            return wahl.first { $0.wert == wert }?.bezeichnung ?? wert.kompakt
        case .text:
            return wert.alsText ?? "–"
        case .kennwort:
            return "••••"
        }
    }

    /// Der Parameter zu einer Angabe der KI: Kennung (`heizung/buffer.voll_c`) oder Schlüssel
    /// (`buffer.voll_c`, `rooms[].p_band_k`), bei Schlüsseln beider Gerätearten nach `art`.
    public static func parameter(_ angabe: String, art: Geraeteart?) -> Parameter? {
        let a = angabe.trimmingCharacters(in: .whitespaces)
        if let p = Parameterkatalog.parameter(a) { return p }
        let passend = Parameterkatalog.alle.filter { $0.schluessel == a }
        if let art { return passend.first { $0.geraet == art } }
        // Auf beiden Gerätearten, etwa der Schutztermin: zuerst der Verteiler; ohne Gerät fragt
        // die Prüfung dann nach, welches gemeint ist.
        return passend.first
    }

    public static func sonderparameter(_ angabe: String) -> Sonderparameter? {
        let a = angabe.trimmingCharacters(in: .whitespaces)
        return Sonderparameter.allCases.first { $0.rawValue == a || $0.schluessel == a }
    }
}

/// Findet Geräte, Räume und Heizkreise nach dem, was die KI angibt: Kennung, Ort oder Name.
public enum Aufloesung {
    static func vereinfacht(_ s: String) -> String {
        s.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: Locale(identifier: "de_DE"))
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// Geräte nach Kennung oder Ort; „Kessel“ und „Speicher“ passen auf die Rolle des
    /// Heizungsgeräts, „Verteiler“ und „Heizung“ auf die Geräteart.
    public static func geraete(_ angabe: String?, in staende: [Geraetestand], bild: Anlagenbild) -> [Geraetestand] {
        guard let angabe, !angabe.trimmingCharacters(in: .whitespaces).isEmpty else { return staende }
        let a = vereinfacht(angabe)
        if let genau = staende.first(where: { vereinfacht($0.geraet.id) == a }) { return [genau] }
        let nachOrt = staende.filter { vereinfacht($0.ort) == a || vereinfacht($0.geraet.ort) == a }
        if !nachOrt.isEmpty { return nachOrt }
        let rolle: Geraet.Art? = switch a {
        case "kessel", "kesselgerat", "brenner": .kessel
        case "speicher", "pufferspeicher", "puffer", "speichergerat": .speicher
        default: nil
        }
        if let rolle {
            let ids = Set(bild.geraete.filter { $0.art == rolle }.map(\.id))
            return staende.filter { ids.contains($0.geraet.id) }
        }
        switch a {
        case "verteiler", "alle verteiler": return staende.filter { $0.geraet.art == .verteiler }
        case "heizung", "heizungsgerat", "heizungsgerate": return staende.filter { $0.geraet.art == .heizung }
        default: break
        }
        // „Verteiler Erdgeschoss“, „EG“ als Anfang des Orts
        return staende.filter { s in
            let ort = vereinfacht(s.ort)
            return !ort.isEmpty && (a.hasSuffix(ort) || ort.hasPrefix(a))
        }
    }

    /// Raumnummer auf einem Verteiler nach Nummer oder Name
    public static func raum(_ angabe: String, auf stand: Geraetestand) -> (nummer: Int, name: String)? {
        let raeume: [(Int, String)] = (stand.konfiguration?["rooms"]?.alsListe ?? []).compactMap { r in
            guard let id = r["id"]?.alsGanzzahl else { return nil }
            return (id, r["name"]?.alsText ?? "Raum \(id)")
        }
        let a = vereinfacht(angabe)
        if let n = Int(a.replacingOccurrences(of: "raum ", with: "")), let r = raeume.first(where: { $0.0 == n }) { return r }
        return raeume.first { vereinfacht($0.1) == a } ?? raeume.first { vereinfacht($0.1).hasPrefix(a) }
    }

    /// Räume der ganzen Anlage mit diesem Namen, falls kein Verteiler genannt ist
    public static func raeume(_ angabe: String, in staende: [Geraetestand]) -> [(stand: Geraetestand, nummer: Int, name: String)] {
        staende.filter { $0.geraet.art == .verteiler }.compactMap { s in
            raum(angabe, auf: s).map { (s, $0.nummer, $0.name) }
        }
    }

    /// Heizkreisnummer aus „1“, „Heizkreis 2“, „HK2“ oder dem Namen
    public static func heizkreis(_ angabe: String, auf stand: Geraetestand) -> (nummer: Int, name: String)? {
        let kreise: [(Int, String)] = (stand.konfiguration?["circuits"]?.alsListe ?? []).compactMap { c in
            guard let id = c["id"]?.alsGanzzahl else { return nil }
            return (id, c["name"]?.alsText ?? "Heizkreis \(id)")
        }
        let a = vereinfacht(angabe)
        let ziffern = a.filter(\.isNumber)
        if let n = Int(ziffern), a.count - ziffern.count <= 10, let k = kreise.first(where: { $0.0 == n }) { return k }
        return kreise.first { vereinfacht($0.1) == a }
    }

    /// Kennungen der Verteiler aus Orten oder Kennungen, durch Komma getrennt
    public static func verteilerkennungen(_ angabe: String, in staende: [Geraetestand]) -> (kennungen: [String], unbekannt: [String]) {
        var kennungen: [String] = []
        var unbekannt: [String] = []
        let teile = angabe.split(whereSeparator: { ",;".contains($0) }).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
        for t in teile where !["keiner", "keine", "-", "–"].contains(vereinfacht(t)) {
            let a = vereinfacht(t)
            if a.hasPrefix("fbh_") {
                kennungen.append(a)
                continue
            }
            let treffer = staende.filter { $0.geraet.art == .verteiler && (vereinfacht($0.ort) == a || vereinfacht($0.geraet.ort) == a) }
            if let s = treffer.first { kennungen.append(s.geraet.id) } else { unbekannt.append(t) }
        }
        return (Array(NSOrderedSet(array: kennungen)) as? [String] ?? kennungen, unbekannt)
    }
}
