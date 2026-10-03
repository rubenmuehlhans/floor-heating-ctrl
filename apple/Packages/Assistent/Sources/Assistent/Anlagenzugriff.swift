import Foundation
import Anlage
import Diagnose
import Geraeteschnittstelle
import Verlauf

/// Was die Werkzeuge der KI von der App brauchen. Die App liefert es aus dem laufenden Betrieb:
/// Abfragen, Befundgedächtnis, gespeicherter Verlauf, Änderungsprotokoll. Die Tests liefern es aus
/// Aufnahmen. Die Werkzeuge fragen dafür keine Geräte selbst ab.
public protocol Anlagenzugriff: Sendable {
    /// Das zusammengeführte Bild der Anlage
    func bild() async -> Anlagenbild
    /// Die Stände der eingebundenen Geräte samt Konfiguration und Rohzustand
    func staende() async -> [Geraetestand]
    func befundlage() async -> Befundlage
    /// Mittelwerte im Raster `schritt` aus dem gespeicherten Verlauf; `nil` ohne Verlauf
    func verlauf(von: Date, bis: Date, schritt: TimeInterval, schluessel: Set<String>?) async -> Verlaufsauszug?
    /// Das Änderungsprotokoll, jüngste zuerst
    func aenderungen() async -> [Aenderung]
    /// Legt einen Vorschlag an; er erscheint als Karte im Gespräch und unter „Offene Vorschläge“.
    func vorschlagAnlegen(_ vorschlag: Vorschlag) async
    /// Ereignisse aus dem Protokoll des Leitstands, zeitlich geordnet; leer ohne Leitstand
    func ereignisse(von: Date, bis: Date) async -> [Ereignis]
    /// Messwerte im Takt der Abfrage oder in gröberem Raster, vom Leitstand berechnet. Wirft,
    /// wenn kein Leitstand eingerichtet oder erreichbar ist.
    func feinverlauf(geraet: String, schluessel: [String], von: Date, bis: Date, raster: Int) async throws -> Leitstand.Protokollreihe
    /// Kennung des Leitstands, `lst_…`; nil ohne Leitstand
    func leitstandKennung() async -> String?
}

/// Ohne Leitstand gibt es weder Ereignisse noch Messwerte im Takt der Abfrage.
public enum Feinverlaufsfehler: Error, LocalizedError {
    case keinLeitstand

    public var errorDescription: String? { "Es ist kein Leitstand eingerichtet." }
}

extension Anlagenzugriff {
    public func ereignisse(von: Date, bis: Date) async -> [Ereignis] { [] }

    public func leitstandKennung() async -> String? { nil }

    public func feinverlauf(geraet: String, schluessel: [String], von: Date, bis: Date, raster: Int) async throws -> Leitstand.Protokollreihe {
        throw Feinverlaufsfehler.keinLeitstand
    }
}

extension Anlagenzugriff {
    public func kennzahlen(von: Date, bis: Date) async -> Kennzahlen {
        let dauer = bis.timeIntervalSince(von)
        // Stundenmittel genügen für die Raumwerte; über 30 Tage sind das 720 Raster.
        let auszug = await verlauf(von: von, bis: bis, schritt: dauer > 3 * 86_400 ? 3600 : 900, schluessel: nil)
        return Kennzahlen.berechnen(bild: await bild(), auszug: auszug, von: von, bis: bis)
    }
}

/// Befunde mit dem, was das Befundgedächtnis über sie weiß
public struct Befundlage: Sendable {
    public var offen: [Befundgedaechtnis.Stand]
    /// Erledigte Befunde der letzten 30 Tage, jüngste zuerst
    public var erledigt: [Befundgedaechtnis.Eintrag]

    public init(offen: [Befundgedaechtnis.Stand], erledigt: [Befundgedaechtnis.Eintrag]) {
        self.offen = offen
        self.erledigt = erledigt
    }
}

/// Was für das Übernehmen und Zurücknehmen eines Vorschlags hinzukommt: sichern, schreiben,
/// Aktionen ausführen. Die App schreibt über den Anlagenbetrieb, der jeden Wert zurückliest.
public protocol Vorschlagsausfuehrung: Anlagenzugriff {
    /// Sichert ein Gerät und liefert den Namen der Sicherung
    func sichern(_ geraet: String) async throws -> String?
    /// Liest die Konfiguration eines Geräts frisch, damit ein inzwischen geänderter Wert auffällt
    func frischerStand(_ geraet: String) async throws -> Geraetestand
    /// Schreibt die Werte der Ziele eines Geräts und prüft, ob das Gerät sie übernommen hat.
    /// `neu` schreibt die neuen Werte, sonst die bisherigen.
    func schreiben(_ ziele: [Vorschlag.Ziel], neu: Bool) async throws
    /// Führt eine Aktion aus und liefert ein Ergebnis zur Anzeige
    func ausfuehren(_ aktion: Vorschlag.Aktion, _ ziel: Vorschlag.Ziel) async throws -> String?
    func protokollieren(_ aenderungen: [Aenderung]) async
}
