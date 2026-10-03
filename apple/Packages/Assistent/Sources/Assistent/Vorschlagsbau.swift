import Foundation
import Anlage
import Geraeteschnittstelle

/// Baut aus den Angaben der KI einen Vorschlag und prüft ihn vorher, wie es das Formular und die
/// Firmware täten: bekannte, für die KI freigegebene Einstellung, Wert im Bereich und auf das
/// Raster gerundet, gegenseitige Bedingungen wie „voll über leer“, ein eindeutiges Gerät. Was
/// nicht passt, geht als Meldung an die KI zurück, damit sie den Vorschlag berichtigen kann.
public enum Vorschlagsbau {
    public struct Angabe: Sendable, Hashable {
        public var parameter: String
        public var eintrag: String?
        public var wert: String

        public init(parameter: String, eintrag: String? = nil, wert: String) {
            self.parameter = parameter
            self.eintrag = eintrag
            self.wert = wert
        }
    }

    public enum Ergebnis: Sendable {
        case vorschlag(Vorschlag)
        case abgelehnt([String])
    }

    // MARK: - Einstellungen

    public static func aenderung(
        titel: String, geraet: String?, angaben: [Angabe], begruendung: String, wirkung: String,
        staende: [Geraetestand], bild: Anlagenbild, jetzt: Date = .now
    ) -> Ergebnis {
        var fehler: [String] = []
        var ziele: [Vorschlag.Ziel] = []
        let orte = Dictionary(staende.map { ($0.geraet.id, $0.ort) }, uniquingKeysWith: { a, _ in a })
        guard !angaben.isEmpty else { return .abgelehnt(["Der Vorschlag nennt keine Änderung."]) }
        if angaben.count > 4 { fehler.append("Höchstens vier Änderungen je Vorschlag; teilen Sie ihn auf.") }

        for angabe in angaben {
            switch Self.ziele(angabe, geraet: geraet, staende: staende, bild: bild, orte: orte) {
            case .success(let neu):
                for z in neu where ziele.contains(where: { $0.geraet == z.geraet && $0.parameter == z.parameter && $0.eintrag == z.eintrag }) {
                    fehler.append("„\(z.bezeichnung)“ ist doppelt genannt.")
                }
                ziele += neu
            case .failure(let f):
                fehler += f.meldungen
            }
        }
        // Gegenseitige Bedingungen je Gerät, wie sie die Firmware prüft. Nur neue Verstöße
        // zählen; was schon vorher nicht stimmte, soll den Vorschlag nicht blockieren.
        for id in Set(ziele.map(\.geraet)).sorted() {
            guard let s = staende.first(where: { $0.geraet.id == id }), let k = s.konfiguration,
                  let teil = Zielwerte.teil(ziele.filter { $0.geraet == id }, neu: true, konfiguration: k) else { continue }
            let vorher = Set(Schreibweg.pruefen(k, s.geraet.art))
            for f in Schreibweg.pruefen(Schreibweg.zusammengefuehrt(k, teil), s.geraet.art) where !vorher.contains(f) {
                fehler.append("\(s.ort): \(f)")
            }
        }
        if begruendung.trimmingCharacters(in: .whitespaces).isEmpty { fehler.append("Die Begründung fehlt.") }
        guard fehler.isEmpty, !ziele.isEmpty else { return .abgelehnt(fehler) }

        let einzeln = Dictionary(grouping: ziele, by: { "\($0.parameter)|\($0.eintrag?.kompakt ?? "")" })
        let gleich = einzeln.count == 1 && Set(ziele.map(\.bisher)).count == 1
        let erstes = ziele[0]
        let bisher = gleich ? Zielwerte.anzeige(erstes.parameter, erstes.bisher, orte: orte)
            : ziele.map { "\($0.bezeichnung): \(Zielwerte.anzeige($0.parameter, $0.bisher, orte: orte))" }.joined(separator: "\n")
        let neu = gleich ? Zielwerte.anzeige(erstes.parameter, erstes.neu, orte: orte)
            : ziele.map { "\($0.bezeichnung): \(Zielwerte.anzeige($0.parameter, $0.neu, orte: orte))" }.joined(separator: "\n")
        let bezeichnungen = Array(NSOrderedSet(array: ziele.map(\.bezeichnung))) as? [String] ?? []
        let geraete = Array(NSOrderedSet(array: ziele.map(\.ort))) as? [String] ?? []
        let kopf = titel.trimmingCharacters(in: .whitespacesAndNewlines)
        return .vorschlag(Vorschlag(
            id: "v-" + UUID().uuidString.prefix(8).lowercased(),
            titel: kopf.isEmpty ? "\(bezeichnungen.joined(separator: ", ")): \(bisher) → \(neu)" : kopf,
            geraete: geraete, parameter: bezeichnungen.joined(separator: ", "), bisher: bisher, neu: neu,
            begruendung: begruendung.trimmingCharacters(in: .whitespacesAndNewlines),
            wirkung: wirkung.trimmingCharacters(in: .whitespacesAndNewlines),
            ziele: ziele, erstellt: jetzt))
    }

    struct Meldungen: Error {
        var meldungen: [String]
        init(_ m: String) { meldungen = [m] }
        init(_ m: [String]) { meldungen = m }
    }

    static func bekannteGeraete(_ staende: [Geraetestand]) -> String {
        staende.map { "\($0.ort) (\($0.geraet.id))" }.joined(separator: ", ")
    }

    /// Die Ziele einer Angabe: ein Gerät, bei anlagenweiten Speicherwerten alle Heizungsgeräte.
    static func ziele(_ a: Angabe, geraet: String?, staende: [Geraetestand], bild: Anlagenbild, orte: [String: String]) -> Result<[Vorschlag.Ziel], Meldungen> {
        let vorgabe = geraet.map { Aufloesung.geraete($0, in: staende, bild: bild) }
        if let geraet, vorgabe?.isEmpty == true {
            return .failure(Meldungen("Kein Gerät „\(geraet)“. Bekannt sind: \(bekannteGeraete(staende))."))
        }
        let arten = Set((vorgabe ?? []).map(\.geraet.art))
        let sonder = Zielwerte.sonderparameter(a.parameter)
        let p = sonder == nil ? Zielwerte.parameter(a.parameter, art: arten.count == 1 ? arten.first : nil) : nil
        guard sonder != nil || p != nil else {
            return .failure(Meldungen("Unbekannte Einstellung „\(a.parameter)“. Die Schlüssel stehen in einstellungen_lesen unter parameter."))
        }
        if let p, !p.kiFreigegeben {
            return .failure(Meldungen("„\(p.bezeichnung)“ ist für Vorschläge der KI nicht freigegeben. Nennen Sie den Punkt stattdessen als Hinweis in Ihrer Antwort."))
        }
        let parameterID = sonder?.rawValue ?? p?.id ?? ""
        let bezeichnung = sonder?.bezeichnung ?? p?.bezeichnung ?? a.parameter
        let zielart: Geraeteart = sonder != nil ? .heizung : p?.geraet ?? .heizung
        let kandidaten = (vorgabe ?? staende).filter { $0.geraet.art == zielart }
        if vorgabe != nil, kandidaten.isEmpty {
            return .failure(Meldungen("„\(bezeichnung)“ gehört zu \(zielart == .verteiler ? "einem Verteiler" : "einem Heizungsgerät"), nicht zu „\(geraet ?? "")“."))
        }

        // Listeneinträge: Raum oder Heizkreis
        var eintraege: [(stand: Geraetestand, eintrag: JSONWert?, name: String?)] = []
        let brauchtKreis = p?.ort == .heizkreis || sonder == .heizkreisBetriebsart || sonder == .versorgteVerteiler
        if p?.ort == .raum {
            guard let raum = a.eintrag, !raum.isEmpty else {
                return .failure(Meldungen("„\(bezeichnung)“ gilt je Raum; geben Sie den Raum als eintrag an."))
            }
            let treffer = vorgabe == nil ? Aufloesung.raeume(raum, in: kandidaten)
                : kandidaten.compactMap { s in Aufloesung.raum(raum, auf: s).map { (stand: s, nummer: $0.nummer, name: $0.name) } }
            if treffer.isEmpty { return .failure(Meldungen("Kein Raum „\(raum)“ auf \(kandidaten.map(\.ort).joined(separator: ", ")).")) }
            if treffer.count > 1 {
                return .failure(Meldungen("Einen Raum „\(raum)“ gibt es auf mehreren Verteilern: \(treffer.map(\.stand.ort).joined(separator: ", ")). Geben Sie das Gerät an."))
            }
            eintraege = [(treffer[0].stand, .zahl(Double(treffer[0].nummer)), "\(treffer[0].name) (\(treffer[0].stand.ort))")]
        } else if brauchtKreis {
            guard let kreis = a.eintrag, !kreis.isEmpty else {
                return .failure(Meldungen("„\(bezeichnung)“ gilt je Heizkreis; geben Sie die Nummer des Heizkreises als eintrag an."))
            }
            let treffer = kandidaten.compactMap { s in Aufloesung.heizkreis(kreis, auf: s).map { (s, $0) } }
            guard let t = treffer.first else { return .failure(Meldungen("Keinen Heizkreis „\(kreis)“ gefunden.")) }
            eintraege = [(t.0, .zahl(Double(t.1.nummer)), t.1.name)]
        } else if sonder == .kesselkreispumpeBetriebsart {
            guard let s = kandidaten.first(where: { $0.heizgeraet?.kesselkreispumpe?.aktiv == true }) else {
                return .failure(Meldungen("Keines der Heizungsgeräte hat eine Kesselkreispumpe eingerichtet."))
            }
            eintraege = [(s, nil, nil)]
        } else if let p, p.anlagenweit {
            // Beide Heizungsgeräte schätzen denselben Speicher: gleiche Werte auf beiden.
            let mitWert = staende.filter { $0.geraet.art == .heizung && $0.konfiguration.flatMap { Zielwerte.wert(p, eintrag: nil, in: $0) } != nil }
            eintraege = (mitWert.isEmpty ? kandidaten : mitWert).map { (stand: $0, eintrag: nil, name: nil) }
        } else if let p {
            let gewaehlt = zielgeraete(p, kandidaten: kandidaten, vorgegeben: vorgabe != nil, bild: bild)
            if gewaehlt.isEmpty {
                return .failure(Meldungen("„\(bezeichnung)“ gibt es auf mehreren Geräten (\(kandidaten.map(\.ort).joined(separator: ", "))). Geben Sie das Gerät an."))
            }
            eintraege = gewaehlt.map { (stand: $0, eintrag: nil, name: nil) }
        }

        var fehler: [String] = []
        var ziele: [Vorschlag.Ziel] = []
        for e in eintraege {
            var ziel = Vorschlag.Ziel(geraet: e.stand.geraet.id, parameter: parameterID, eintrag: e.eintrag, bisher: .null, neu: .null,
                                      ort: e.stand.ort, bezeichnung: e.name.map { "\(bezeichnung) · \($0)" } ?? bezeichnung)
            if Schreibart.fuer(parameterID) == .konfiguration || parameterID.hasPrefix("verteiler/rooms"), e.stand.konfiguration == nil {
                fehler.append("Die Einstellungen von \(e.stand.ort) sind noch nicht gelesen; versuchen Sie es gleich noch einmal.")
                continue
            }
            guard let bisher = Zielwerte.aktuell(ziel, e.stand) else {
                fehler.append("\(e.stand.ort) hat keinen Wert für „\(ziel.bezeichnung)“.")
                continue
            }
            let neu: JSONWert
            switch wert(a.wert, p: p, sonder: sonder, staende: staende) {
            case .success(let w): neu = w
            case .failure(let f):
                fehler += f.meldungen
                continue
            }
            if gleichwertig(bisher, neu) {
                fehler.append("„\(ziel.bezeichnung)“ steht auf \(e.stand.ort) bereits auf \(Zielwerte.anzeige(parameterID, bisher, orte: orte)).")
                continue
            }
            if sonder == .versorgteVerteiler {
                // Ein Verteiler gehört zu höchstens einem Heizkreis.
                let belegt = (e.stand.konfiguration?["circuits"]?.alsListe ?? []).filter { $0["id"] != e.eintrag }
                    .flatMap { c in (c["peers"]?.alsListe ?? []).compactMap(\.alsText).map { ($0, c["name"]?.alsText ?? "ein anderer Heizkreis") } }
                for id in (neu.alsListe ?? []).compactMap(\.alsText) {
                    if let anderer = belegt.first(where: { $0.0 == id }) {
                        fehler.append("\(orte[id] ?? id) wird schon von \(anderer.1) versorgt.")
                    }
                }
            }
            ziel.bisher = bisher
            ziel.neu = neu
            ziele.append(ziel)
        }
        return fehler.isEmpty ? .success(ziele) : .failure(Meldungen(fehler))
    }

    /// Das Gerät einer Einstellung, die auf jedem Gerät der Art steht: das genannte, sonst das,
    /// auf dem sie wirkt – Brenner am Kessel, Kesselkreispumpe dort, wo sie eingerichtet ist,
    /// Bedarfsabfrage und Schutzlauf am Speicher mit den Heizkreisen.
    static func zielgeraete(_ p: Parameter, kandidaten: [Geraetestand], vorgegeben: Bool, bild: Anlagenbild) -> [Geraetestand] {
        if kandidaten.count <= 1 || vorgegeben {
            // „Verteiler“ als Angabe meint alle; ein Ort meint einen.
            return kandidaten
        }
        guard p.geraet == .heizung else { return [] }
        let rolle = { (art: Geraet.Art) in Set(bild.geraete.filter { $0.art == art }.map(\.id)) }
        let passend: [Geraetestand] = switch p.gruppe {
        case "brenner": kandidaten.filter { rolle(.kessel).contains($0.geraet.id) }
        case "kkp": kandidaten.filter { $0.konfiguration?["boiler_pump"]?["enabled"]?.alsBool == true }
        case "bedarf", "zeit": kandidaten.filter { !($0.konfiguration?["circuits"]?.alsListe ?? []).isEmpty }
        default: []
        }
        return passend.count == 1 ? passend : []
    }

    static func gleichwertig(_ a: JSONWert, _ b: JSONWert) -> Bool {
        if let x = a.alsZahl, let y = b.alsZahl { return abs(x - y) < 1e-6 }
        if let x = a.alsListe?.compactMap(\.alsText), let y = b.alsListe?.compactMap(\.alsText) { return Set(x) == Set(y) }
        return a == b
    }

    /// Der Wert aus dem Text der KI: Zahl mit Komma oder Punkt, auf das Raster gerundet und im
    /// Bereich; Auswahl nach Wert oder Bezeichnung; Schalter als ein/aus.
    static func wert(_ text: String, p: Parameter?, sonder: Sonderparameter?, staende: [Geraetestand]) -> Result<JSONWert, Meldungen> {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let einfach = Aufloesung.vereinfacht(t)
        switch sonder {
        case .heizkreisBetriebsart, .kesselkreispumpeBetriebsart:
            let modus = ["auto": "auto", "automatik": "auto", "ein": "ein", "an": "ein", "dauernd ein": "ein", "aus": "aus"][einfach]
            return modus.map { .success(.text($0)) } ?? .failure(Meldungen("Die Betriebsart ist auto, ein oder aus, nicht „\(t)“."))
        case .versorgteVerteiler:
            let (kennungen, unbekannt) = Aufloesung.verteilerkennungen(t, in: staende)
            if !unbekannt.isEmpty {
                return .failure(Meldungen("Unbekannte Verteiler: \(unbekannt.joined(separator: ", ")). Bekannt sind: \(staende.filter { $0.geraet.art == .verteiler }.map(\.ort).joined(separator: ", "))."))
            }
            if kennungen.count > 4 { return .failure(Meldungen("Ein Heizkreis versorgt höchstens vier Verteiler.")) }
            return .success(.liste(kennungen.map(JSONWert.text)))
        case nil:
            break
        }
        guard let p else { return .failure(Meldungen("Unbekannte Einstellung.")) }
        let deutsch = Locale(identifier: "de_DE")
        let f = { (x: Double) in x.formatted(.number.precision(.fractionLength(0...2)).locale(deutsch)) }
        let einheit = p.einheit.isEmpty ? "" : "\u{00A0}\(p.einheit)"
        switch p.art {
        case .zahl(let bereich, let schritt, let stellen):
            guard let x = zahl(t) else { return .failure(Meldungen("„\(t)“ ist keine Zahl für „\(p.bezeichnung)“.")) }
            guard bereich.contains(x) else {
                return .failure(Meldungen("\(p.bezeichnung) muss zwischen \(f(bereich.lowerBound)) und \(f(bereich.upperBound))\(einheit) liegen."))
            }
            let faktor = pow(10, Double(stellen))
            let gerundet = (((x / schritt).rounded() * schritt) * faktor).rounded() / faktor
            return .success(.zahl(min(bereich.upperBound, max(bereich.lowerBound, gerundet))))
        case .ganzzahl(let bereich, let schritt):
            guard let x = zahl(t) else { return .failure(Meldungen("„\(t)“ ist keine Zahl für „\(p.bezeichnung)“.")) }
            let n = Int((x / Double(schritt)).rounded()) * schritt
            guard bereich.contains(n) else {
                return .failure(Meldungen("\(p.bezeichnung) muss zwischen \(bereich.lowerBound) und \(bereich.upperBound)\(einheit) liegen."))
            }
            return .success(.zahl(Double(n)))
        case .auswahl(let wahl):
            if let w = wahl.first(where: { Aufloesung.vereinfacht($0.bezeichnung) == einfach || Aufloesung.vereinfacht($0.wert.alsText ?? $0.wert.kompakt) == einfach }) {
                return .success(w.wert)
            }
            return .failure(Meldungen("Für „\(p.bezeichnung)“ gelten: \(wahl.map { "\($0.wert.alsText ?? $0.wert.kompakt) (\($0.bezeichnung))" }.joined(separator: ", "))."))
        case .schalter:
            if ["ein", "an", "ja", "true", "1"].contains(einfach) { return .success(.bool(true)) }
            if ["aus", "nein", "false", "0"].contains(einfach) { return .success(.bool(false)) }
            return .failure(Meldungen("„\(p.bezeichnung)“ ist ein Schalter: ein oder aus."))
        case .millisekunden, .text, .kennwort:
            return .failure(Meldungen("„\(p.bezeichnung)“ ist für Vorschläge der KI nicht freigegeben."))
        }
    }

    /// „68“, „68,5“, „68.5 °C“, „-2“
    static func zahl(_ t: String) -> Double? {
        let erlaubt = t.replacingOccurrences(of: ",", with: ".").replacingOccurrences(of: "−", with: "-")
        let teil = erlaubt.prefix { "0123456789.-+".contains($0) || $0 == " " }.replacingOccurrences(of: " ", with: "")
        return Double(teil)
    }

    // MARK: - Aktionen

    public static func aktion(
        _ aktion: Vorschlag.Aktion, geraet: String, eintrag: String?, titel: String, begruendung: String, wirkung: String,
        staende: [Geraetestand], bild: Anlagenbild, aenderungen: [Aenderung], jetzt: Date = .now
    ) -> Ergebnis {
        let treffer = Aufloesung.geraete(geraet, in: staende, bild: bild)
        let art: Geraeteart = [.messfahrt, .schutzfahrt].contains(aktion) ? .verteiler : aktion == .neustart ? (treffer.first?.geraet.art ?? .verteiler) : .heizung
        var passend = treffer.filter { $0.geraet.art == art }
        if aktion == .relaispruefung, passend.count > 1 {
            passend = passend.filter { !($0.konfiguration?["circuits"]?.alsListe ?? []).isEmpty || $0.konfiguration?["boiler_pump"]?["enabled"]?.alsBool == true }
        }
        if aktion == .ladungsaufzeichnung, passend.count > 1 {
            let kessel = Set(bild.geraete.filter { $0.art == .kessel }.map(\.id))
            passend = passend.filter { kessel.contains($0.geraet.id) }
        }
        guard passend.count == 1, let s = passend.first else {
            let welche = art == .verteiler ? "einen Verteiler" : "ein Heizungsgerät"
            return .abgelehnt([passend.isEmpty
                ? "„\(aktion.bezeichnung)“ braucht \(welche); „\(geraet)“ passt nicht. Bekannt sind: \(bekannteGeraete(staende))."
                : "„\(geraet)“ meint mehrere Geräte; nennen Sie genau eines."])
        }
        var ziel = Vorschlag.Ziel(geraet: s.geraet.id, parameter: "aktion.\(aktion.rawValue)", bisher: .null, neu: .null,
                                  ort: s.ort, bezeichnung: aktion.bezeichnung)
        var neu = s.ort
        switch aktion {
        case .messfahrt:
            guard let n = eintrag.flatMap({ Int($0.filter(\.isNumber)) }), (1...11).contains(n) else {
                return .abgelehnt(["Die Messfahrt braucht die Kanalnummer 1 bis 11 als eintrag."])
            }
            let raum = (s.konfiguration?["rooms"]?.alsListe ?? []).first { ($0["channels"]?.alsListe ?? []).contains(.zahl(Double(n))) }
            guard let raum else { return .abgelehnt(["Kanal \(n) auf \(s.ort) gehört zu keinem Raum; eine Messfahrt bringt dort nichts."]) }
            if s.verteiler?.messfahrt?.zustand == "running" {
                return .abgelehnt(["Auf \(s.ort) läuft bereits eine Messfahrt; je Verteiler nur eine zur Zeit."])
            }
            ziel.eintrag = .zahl(Double(n))
            ziel.bezeichnung = "Messfahrt · Kanal \(n)"
            neu = "Kanal \(n) (\(raum["name"]?.alsText ?? "Raum")), \(s.ort)"
        case .schutzfahrt:
            if s.verteiler?.schutzfahrt?.laeuft == true { return .abgelehnt(["Auf \(s.ort) läuft die Schutzfahrt schon."]) }
        case .ladungsaufzeichnung:
            let z = s.heizgeraet?.aufzeichnung?.zustand ?? "aus"
            if z == "scharf" || z == "laeuft" { return .abgelehnt(["Die Ladungsaufzeichnung auf \(s.ort) ist schon \(z == "laeuft" ? "in Gang" : "scharf geschaltet")."]) }
        case .relaispruefung:
            let angabe = eintrag ?? ""
            if Aufloesung.vereinfacht(angabe).contains("kessel") || Aufloesung.vereinfacht(angabe) == "kkp" {
                ziel.eintrag = "kkp"
                ziel.bezeichnung = "Relaisprüfung · Kesselkreispumpe"
                neu = "Relais der Kesselkreispumpe"
            } else if let k = Aufloesung.heizkreis(angabe, auf: s) {
                ziel.eintrag = .zahl(Double(k.nummer))
                ziel.bezeichnung = "Relaisprüfung · \(k.name)"
                neu = "Relais von \(k.name)"
            } else {
                return .abgelehnt(["Die Relaisprüfung braucht den Heizkreis oder kkp als eintrag."])
            }
        case .neustart:
            let grenze = jetzt.addingTimeInterval(-7 * 86_400)
            let noetig = aenderungen.contains { a in
                a.geraetekennung == s.geraet.id && a.zeit > grenze
                    && a.parameterkennung.flatMap(Parameterkatalog.parameter)?.wirkung == .neustart
            }
            guard noetig else {
                return .abgelehnt(["Einen Neustart schlägt die KI nur nach einer Änderung vor, die erst mit dem Neustart wirkt; für \(s.ort) ist in den letzten sieben Tagen keine verzeichnet."])
            }
        case .fuehlersuche:
            break
        }
        if begruendung.trimmingCharacters(in: .whitespaces).isEmpty { return .abgelehnt(["Die Begründung fehlt."]) }
        let kopf = titel.trimmingCharacters(in: .whitespacesAndNewlines)
        return .vorschlag(Vorschlag(
            id: "v-" + UUID().uuidString.prefix(8).lowercased(),
            titel: kopf.isEmpty ? "\(aktion.bezeichnung): \(neu)" : kopf,
            geraete: [s.ort], parameter: aktion.bezeichnung, bisher: "–", neu: neu,
            begruendung: begruendung.trimmingCharacters(in: .whitespacesAndNewlines),
            wirkung: wirkung.trimmingCharacters(in: .whitespacesAndNewlines),
            ziele: [ziel], aktion: aktion, erstellt: jetzt))
    }
}
