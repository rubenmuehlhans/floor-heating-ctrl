import Foundation
import Verlauf

/// Ordnet die Reihen eines Verlaufsauszugs für die Anzeige: Gruppe, Bezeichnung, Reihenfolge.
public enum Verlaufsgliederung {
    public enum Gruppe: String, CaseIterable, Sendable, Hashable {
        case waermeerzeugung = "Wärmeerzeugung"
        case heizkreise = "Heizkreise"
        case aussen = "Außen"
        case raeume = "Räume"
        case verteilervorlauf = "Vorlauf an den Verteilern"
    }

    static let waermeerzeugung = ["fuehler.abgas", "fuehler.kessel_vl", "fuehler.kessel_rl", "fuehler.puffer", "fuehler.puffer_unten"]

    /// Die Gruppe einer Temperaturreihe im Anlagenverlauf. Sollwerte, Stellungen, Feuchte und
    /// Kanäle gehören in die Raumansicht; Anteile wie Brenner und Füllstand in das zweite Diagramm.
    public static func gruppe(_ r: Verlaufsauszug.Reihe) -> Gruppe? {
        let k = r.schluessel
        if waermeerzeugung.contains(k) { return .waermeerzeugung }
        if k.hasPrefix("fuehler.hk") { return .heizkreise }
        if k == "aussen" || k == "fuehler.aussen" { return .aussen }
        if k.hasPrefix("raum."), k.hasSuffix(".ist") { return .raeume }
        if k.hasPrefix("vorlauf.") { return .verteilervorlauf }
        return nil
    }

    /// Reihen einer Gruppe in fester Reihenfolge: Wärmeerzeugung vom Abgas zum Speicher,
    /// Heizkreise nach Nummer, Räume nach Etage und Raumnummer.
    public static func reihen(_ gruppe: Gruppe, in auszug: Verlaufsauszug) -> [Verlaufsauszug.Reihe] {
        auszug.reihen.filter { self.gruppe($0) == gruppe }.sorted { a, b in
            if gruppe == .waermeerzeugung {
                return (waermeerzeugung.firstIndex(of: a.schluessel) ?? 99) < (waermeerzeugung.firstIndex(of: b.schluessel) ?? 99)
            }
            return (a.ort, nummer(a.schluessel), a.schluessel) < (b.ort, nummer(b.schluessel), b.schluessel)
        }
    }

    /// Bezeichnung im Diagramm. Fühler heißen wie im Parameterkatalog; Räume und Verteilerfühler
    /// tragen ihre Etage, sobald mehrere Geräte solche Reihen haben.
    public static func bezeichnung(_ r: Verlaufsauszug.Reihe, in auszug: Verlaufsauszug) -> String {
        if r.schluessel.hasPrefix("fuehler.") {
            let name = Parameterkatalog.rollenname(String(r.schluessel.dropFirst("fuehler.".count)))
            // Dieselbe Rolle von zwei Geräten, etwa nach einem Gerätetausch
            let gleich = auszug.reihen.filter { $0.schluessel == r.schluessel }
            guard gleich.count > 1 else { return name }
            return Set(gleich.map(\.ort)).count == gleich.count ? "\(name) · \(r.ort)" : "\(name) · \(r.ort) (\(r.geraet))"
        }
        let gleichartig = auszug.reihen.filter { gruppe($0) == gruppe(r) }
        if Set(gleichartig.map(\.geraet)).count > 1, !r.ort.isEmpty {
            let doppelt = gleichartig.filter { $0.bezeichnung == r.bezeichnung && $0.ort == r.ort }.count > 1
            return doppelt ? "\(r.bezeichnung) · \(r.ort) (\(r.geraet))" : "\(r.bezeichnung) · \(r.ort)"
        }
        return r.bezeichnung
    }

    private static func nummer(_ schluessel: String) -> Int {
        schluessel.split(separator: ".").dropFirst().first.flatMap { Int($0) } ?? 0
    }
}

extension Verlaufsauszug {
    /// Der Beispielverlauf in der Form des gespeicherten Verlaufs, damit dieselben Diagramme
    /// auch ohne Geräte etwas zeigen.
    public static func beispiel(_ bild: Anlagenbild) -> Verlaufsauszug {
        let v = bild.verlauf
        let kessel = bild.geraete.first { $0.art == .kessel }
        let speicher = bild.geraete.first { $0.art == .speicher }
        let aussen = bild.etagen.first { $0.traegtAussenfuehler }
        var reihen: [Reihe] = []
        func hinzu(_ quelle: Kurzverlauf.Reihe, geraet: String?, ort: String, _ schluessel: String, _ name: String) {
            guard let geraet, let werte = v.werte[quelle] else { return }
            reihen.append(Reihe(geraet: geraet, ort: ort, schluessel: schluessel, bezeichnung: name, werte: werte))
        }
        hinzu(.abgas, geraet: kessel?.id, ort: kessel?.ort ?? "", "fuehler.abgas", "Abgas")
        hinzu(.kesselVorlauf, geraet: kessel?.id, ort: kessel?.ort ?? "", "fuehler.kessel_vl", "Kessel Vorlauf")
        hinzu(.kesselRuecklauf, geraet: kessel?.id, ort: kessel?.ort ?? "", "fuehler.kessel_rl", "Kessel Rücklauf")
        hinzu(.brenner, geraet: kessel?.id, ort: kessel?.ort ?? "", "brenner", "Brenner")
        hinzu(.speicher, geraet: speicher?.id, ort: speicher?.ort ?? "", "fuehler.puffer", "Pufferspeicher")
        hinzu(.ladung, geraet: speicher?.id, ort: speicher?.ort ?? "", "fuellstand", "Füllstand Speicher")
        hinzu(.hk1Vorlauf, geraet: speicher?.id, ort: speicher?.ort ?? "", "fuehler.hk1_vl", "Heizkreis 1 Vorlauf")
        hinzu(.hk1Ruecklauf, geraet: speicher?.id, ort: speicher?.ort ?? "", "fuehler.hk1_rl", "Heizkreis 1 Rücklauf")
        hinzu(.hk2Vorlauf, geraet: speicher?.id, ort: speicher?.ort ?? "", "fuehler.hk2_vl", "Heizkreis 2 Vorlauf")
        hinzu(.hk2Ruecklauf, geraet: speicher?.id, ort: speicher?.ort ?? "", "fuehler.hk2_rl", "Heizkreis 2 Rücklauf")
        hinzu(.aussen, geraet: aussen?.id, ort: aussen?.name ?? "", "aussen", "Außen")
        for (quelle, name) in [(Kurzverlauf.Reihe.wohnzimmer, "Wohnzimmer"), (.bad, "Bad"), (.buero, "Büro"), (.kueche, "Küche")] {
            for etage in bild.etagen {
                guard let raum = etage.raeume.first(where: { $0.name == name }) else { continue }
                hinzu(quelle, geraet: etage.id, ort: etage.name, Messgroesse.raum(raum.nummer, "ist"), name)
            }
        }
        let anzahl = v.werte.values.map(\.count).max() ?? 0
        return Verlaufsauszug(beginn: v.beginn, schritt: v.schritt, anzahl: anzahl, reihen: reihen)
    }
}
