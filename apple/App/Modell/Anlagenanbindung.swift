import Foundation
import Anlage
import Assistent
import Diagnose
import Geraeteschnittstelle
import Verlauf

/// Die Anlage aus Sicht der KI: Die Werkzeuge lesen aus dem laufenden Betrieb, dem
/// Befundgedächtnis und dem gespeicherten Verlauf; übernommene Vorschläge schreibt der
/// Anlagenbetrieb, der jeden Wert zurückliest.
@MainActor
final class Anlagenanbindung: Vorschlagsausfuehrung {
    unowned let modell: AppModell

    init(modell: AppModell) {
        self.modell = modell
    }

    // MARK: Lesen

    func bild() async -> Anlagenbild { modell.anlage }

    func staende() async -> [Geraetestand] {
        modell.betrieb.staende.values.sorted { $0.geraet.id < $1.geraet.id }
    }

    func befundlage() async -> Befundlage {
        Befundlage(offen: modell.befundgedaechtnis.sichtbar, erledigt: modell.befundgedaechtnis.erledigte)
    }

    func verlauf(von: Date, bis: Date, schritt: TimeInterval, schluessel: Set<String>?) async -> Verlaufsauszug? {
        guard let speicher = modell.verlaufsspeicher else { return nil }
        await modell.verlaufSichern()
        return try? await speicher.auszug(von: von, bis: bis, schritt: schritt, schluessel: schluessel)
    }

    func aenderungen() async -> [Aenderung] { modell.ablage.aenderungen }

    func ereignisse(von: Date, bis: Date) async -> [Ereignis] {
        (try? await modell.verlaufsspeicher?.ereignisse(von: von, bis: bis)) ?? []
    }

    func leitstandKennung() async -> String? { modell.verzeichnis.leitstaende.first?.id }

    /// Vom ersten eingerichteten Leitstand; er rechnet aus seiner Karte.
    func feinverlauf(geraet: String, schluessel: [String], von: Date, bis: Date, raster: Int) async throws -> Leitstand.Protokollreihe {
        guard let e = modell.verzeichnis.leitstaende.first else { throw Feinverlaufsfehler.keinLeitstand }
        return try await Leitstand(adresse: e.adresse).reihe(geraet: geraet, schluessel: schluessel, von: von, bis: bis, raster: raster)
    }

    func vorschlagAnlegen(_ vorschlag: Vorschlag) async {
        var v = vorschlag
        v.gespraech = modell.gespraech.id
        modell.ablage.vorschlagSichern(v)
    }

    // MARK: Schreiben

    func sichern(_ geraet: String) async throws -> String? {
        try await modell.betrieb.sichern(geraet)?.id
    }

    func frischerStand(_ geraet: String) async throws -> Geraetestand {
        try await modell.betrieb.frischerStand(geraet)
    }

    func schreiben(_ ziele: [Vorschlag.Ziel], neu: Bool) async throws {
        guard let id = ziele.first?.geraet else { return }
        let betrieb = modell.betrieb
        if ziele.contains(where: { Schreibart.fuer($0.parameter) == .konfiguration }) {
            let k = try await betrieb.frischeKonfiguration(id)
            if let teil = Zielwerte.teil(ziele, neu: neu, konfiguration: k), let art = betrieb.staende[id]?.geraet.art {
                // Seit dem Vorschlag kann sich anderes geändert haben; neue Verstöße zählen.
                let vorher = Set(Schreibweg.pruefen(k, art))
                let fehler = Schreibweg.pruefen(Schreibweg.zusammengefuehrt(k, teil), art).filter { !vorher.contains($0) }
                if !fehler.isEmpty { throw AppModell.Einstellungsfehler.ungueltig(fehler) }
                try await betrieb.konfigurationAendern(geraet: id, teil)
            }
        }
        for z in ziele {
            let wert = neu ? z.neu : z.bisher
            let nummer = z.eintrag?.alsGanzzahl ?? 0
            switch Schreibart.fuer(z.parameter) {
            case .konfiguration:
                break
            case .sollwert:
                try await betrieb.sollwert(raum: "\(id)/\(nummer)", wert.alsZahl ?? 20)
            case .raumbetrieb:
                try await betrieb.betriebsart(raum: "\(id)/\(nummer)", heizen: wert.alsText != "off")
            case .heizkreisbetrieb:
                try await betrieb.heizkreis(nummer, Pumpenmodus(rawValue: wert.alsText ?? "auto") ?? .automatik)
            case .kesselkreispumpenbetrieb:
                try await betrieb.kesselkreispumpe(Pumpenmodus(rawValue: wert.alsText ?? "auto") ?? .automatik)
            }
        }
    }

    func ausfuehren(_ aktion: Vorschlag.Aktion, _ ziel: Vorschlag.Ziel) async throws -> String? {
        let betrieb = modell.betrieb
        let nummer = ziel.eintrag?.alsGanzzahl ?? 0
        switch aktion {
        case .messfahrt:
            try await betrieb.messfahrtStarten(etage: ziel.geraet, kanal: nummer)
            return "Die Messfahrt läuft. Die ermittelten Werte erscheinen unter Geräte › \(ziel.ort) › Kanal \(nummer) zur Übernahme."
        case .schutzfahrt:
            let n = try await betrieb.schutzfahrt(etage: ziel.geraet, .jetzt)
            return n == 0 ? "Nichts zu tun: Alle Kanäle sind seit der letzten Schutzfahrt gefahren." : "\(n) \(n == 1 ? "Kanal fährt" : "Kanäle fahren") nacheinander auf und zu."
        case .fuehlersuche:
            try await betrieb.fuehlerNeuSuchen(geraet: ziel.geraet)
            return "Das Gerät sucht die Fühler am Bus neu; neue Fühler erscheinen ohne Rolle."
        case .ladungsaufzeichnung:
            try await betrieb.aufzeichnung(geraet: ziel.geraet, .scharfSchalten)
            return "Scharf geschaltet: Die nächste Speicherladung wird aufgezeichnet."
        case .relaispruefung:
            let p = ziel.eintrag == "kkp" ? try await kesselkreispumpenrelais(ziel.geraet) : try await modell.relaisPruefen(nummer)
            return Self.beschreibung(p)
        case .neustart:
            try await betrieb.neustart(geraet: ziel.geraet)
            return "Das Gerät startet neu und ist nach etwa 20 Sekunden wieder erreichbar."
        }
    }

    private func kesselkreispumpenrelais(_ geraet: String) async throws -> Tasmota.Pruefung {
        guard let p = modell.betrieb.staende[geraet]?.konfiguration?["boiler_pump"],
              let adresse = Tasmota.adresse(p["host"]?.alsText ?? "") else { throw AppModell.Relaisfehler.keineAdresse }
        if p["pass_set"]?.alsBool == true { throw AppModell.Relaisfehler.mitKennwort }
        return try await Tasmota(adresse: adresse, benutzer: p["user"]?.alsText).pruefen(relais: p["relay"]?.alsGanzzahl ?? 1)
    }

    static func beschreibung(_ p: Tasmota.Pruefung) -> String {
        var teile = ["Das Relais antwortet, der Kanal ist \(p.ein == true ? "ein" : p.ein == false ? "aus" : "unbekannt")."]
        teile.append(p.einschaltzustand == 1 ? "Nach einem Stromausfall schaltet es ein." : "Nach einem Stromausfall schaltet es nicht von selbst ein (PowerOnState \(p.einschaltzustand.map(String.init) ?? "unbekannt")).")
        teile.append(p.regelAktiv && p.regelPasst ? "Die Ausfallregel ist eingerichtet." : "Die Ausfallregel fehlt oder passt nicht; sie lässt sich unter Heizung › Heizkreis einrichten.")
        return teile.joined(separator: " ")
    }

    func protokollieren(_ aenderungen: [Aenderung]) async {
        modell.ablage.protokollieren(aenderungen)
    }
}
