import Foundation
import Testing
import Anlage
@testable import Diagnose

@MainActor
@Suite("Erklärungen des Betreibers")
struct ErklaerungenTests {
    let beginn = Date(timeIntervalSince1970: 1_790_056_800)
    let alle: Set<Befund.Schwere> = [.stoerung, .warnung, .hinweis]

    func minuten(_ m: Double) -> Date { beginn.addingTimeInterval(m * 60) }

    func relais(_ ort: String = "Heizkreis 1, Heizkreis 2", _ schwere: Befund.Schwere = .stoerung) -> Befund {
        Befund(id: "relais", schwere: schwere, titel: "Relais der Pumpen nicht erreichbar", text: "Die Relais antworten nicht.",
               ort: ort, quelle: "Heizungsgeräte")
    }

    func gemerkt(_ b: Befund, hoechstdauer: TimeInterval? = nil, bis: Date? = nil) -> Erklaerungsliste {
        let liste = Erklaerungsliste(datei: nil)
        let e = Erklaerung(befund: b, text: "Die Relais hängen hinter der Kesselregelung und sind nachts stromlos.",
                           erstellt: beginn, bis: bis, hoechstdauer: hoechstdauer)
        liste.anlegen(e)
        liste.merken(e.id, jetzt: beginn)
        return liste
    }

    /// Erst die Bestätigung macht eine Erklärung wirksam.
    @Test func offeneErklaerungWirktNicht() {
        let liste = Erklaerungsliste(datei: nil)
        liste.anlegen(Erklaerung(befund: relais(), text: "gewollt", erstellt: beginn))
        #expect(liste.einordnen([relais()], jetzt: beginn).offen.count == 1)
        #expect(liste.deckung(relais(), seit: nil, jetzt: beginn) == nil)
    }

    @Test func deckungNachOrtSchwereUndDauer() {
        let e = Erklaerung(befund: relais(), text: "gewollt", erstellt: beginn, hoechstdauer: 14 * 3600)
        // Weniger Orte als beim Erklären: weiter gedeckt.
        #expect(e.deckung(relais("Heizkreis 2"), seit: beginn, jetzt: minuten(10)) == .gedeckt)
        #expect(e.deckung(relais("Heizkreis 1, Heizkreis 2, Kesselkreispumpe"), seit: beginn, jetzt: minuten(10))
                == .weitereOrte(["Kesselkreispumpe"]))
        let leichter = Erklaerung(befund: relais("Heizkreis 1", .warnung), text: "", erstellt: beginn)
        #expect(leichter.deckung(relais("Heizkreis 1", .stoerung), seit: nil, jetzt: beginn) == .schwerer(.stoerung))
        #expect(e.deckung(relais(), seit: beginn, jetzt: minuten(14 * 60 + 1)) == .zuLange)
        let befristet = Erklaerung(befund: relais(), text: "", erstellt: beginn, bis: minuten(60))
        #expect(befristet.deckung(relais(), seit: nil, jetzt: minuten(60)) == .abgelaufen)
        #expect(e.deckung(Befund(id: "wlan:x", schwere: .hinweis, titel: "", text: "", ort: "", quelle: ""), seit: nil, jetzt: beginn) == nil)
    }

    /// Eine neue Erklärung zum selben Befund ersetzt die alte; Löschen widerruft sie.
    @Test func ersetzenUndLoeschen() {
        let liste = gemerkt(relais("Heizkreis 1"))
        let neu = Erklaerung(befund: relais(), text: "beide Kreise", erstellt: minuten(5))
        liste.anlegen(neu)
        liste.merken(neu.id, jetzt: minuten(6))
        #expect(liste.gemerkte.map(\.id) == [neu.id])
        liste.loeschen(neu.id)
        #expect(liste.gemerkte.isEmpty)
        #expect(liste.einordnen([relais()], jetzt: minuten(7)).offen.count == 1)
    }

    /// Erklärter Befund: keine Mitteilung, frühere wird zurückgenommen. Kommt ein Ort hinzu,
    /// wird erneut gemeldet, auch bei gleicher Schwere.
    @Test func mitteilungen() {
        let g = Befundgedaechtnis(datei: nil)
        let erste = g.abgleichen([relais()], jetzt: minuten(0), vollstaendig: true, melden: alle)
        #expect(erste.mitteilungen.map(\.id) == ["relais"])

        let liste = gemerkt(relais())
        let deckung = { (b: Befund, seit: Date) in liste.deckung(b, seit: seit, jetzt: minuten(1))?.gedeckt }
        let erklaert = g.abgleichen([relais()], jetzt: minuten(1), vollstaendig: true, melden: alle, erklaert: deckung)
        #expect(erklaert.mitteilungen.isEmpty)
        #expect(erklaert.erledigt == ["relais"], "die frühere Mitteilung verschwindet")
        #expect(erklaert.sichtbar.count == 1, "das Gedächtnis kennt den Befund weiter")

        let weiter = g.abgleichen([relais()], jetzt: minuten(2), vollstaendig: true, melden: alle, erklaert: deckung)
        #expect(weiter.mitteilungen.isEmpty && weiter.erledigt.isEmpty)

        let mehr = relais("Heizkreis 1, Heizkreis 2, Kesselkreispumpe")
        let neu = g.abgleichen([mehr], jetzt: minuten(3), vollstaendig: true, melden: alle, erklaert: deckung)
        #expect(neu.mitteilungen.map(\.id) == ["relais"])
        #expect(neu.mitteilungen.first?.still == false)
    }

    /// Wird eine Erklärung gelöscht, kehrt der Befund zurück, ohne erneut zu melden.
    @Test func geloeschteErklaerungMeldetNicht() {
        let g = Befundgedaechtnis(datei: nil)
        let liste = gemerkt(relais())
        let deckung = { (b: Befund, seit: Date) in liste.deckung(b, seit: seit, jetzt: minuten(1))?.gedeckt }
        g.abgleichen([relais()], jetzt: minuten(0), vollstaendig: true, melden: alle, erklaert: deckung)
        liste.loeschen(liste.gemerkte[0].id)
        let danach = g.abgleichen([relais()], jetzt: minuten(1), vollstaendig: true, melden: alle, erklaert: deckung)
        #expect(danach.mitteilungen.isEmpty)
        #expect(g.eintraege["relais"]?.erklaert == nil)
    }

    /// Steht der Befund länger an als die Höchstdauer, meldet die App: etwa ein Relais, das nicht
    /// nur nachts stromlos, sondern defekt ist.
    @Test func hoechstdauer() {
        let g = Befundgedaechtnis(datei: nil)
        let liste = gemerkt(relais(), hoechstdauer: 12 * 3600)
        var jetzt = minuten(0)
        let deckung = { (b: Befund, seit: Date) in liste.deckung(b, seit: seit, jetzt: jetzt)?.gedeckt }
        #expect(g.abgleichen([relais()], jetzt: jetzt, vollstaendig: true, melden: alle, erklaert: deckung).mitteilungen.isEmpty)
        jetzt = minuten(11 * 60)
        #expect(g.abgleichen([relais()], jetzt: jetzt, vollstaendig: true, melden: alle, erklaert: deckung).mitteilungen.isEmpty)
        jetzt = minuten(12 * 60 + 1)
        #expect(g.abgleichen([relais()], jetzt: jetzt, vollstaendig: true, melden: alle, erklaert: deckung).mitteilungen.map(\.id) == ["relais"])
        let stand = g.sichtbar[0]
        #expect(liste.einordnen([stand], jetzt: jetzt).offen.count == 1)
    }

    @Test func gespeichert() throws {
        let datei = FileManager.default.temporaryDirectory.appending(path: "erklaerungen-\(UUID().uuidString).json")
        defer { try? FileManager.default.removeItem(at: datei) }
        let liste = Erklaerungsliste(datei: datei)
        let e = Erklaerung(befund: relais(), text: "gewollt", erstellt: beginn, hoechstdauer: 3600)
        liste.anlegen(e)
        liste.merken(e.id, jetzt: beginn)
        let wieder = Erklaerungsliste(datei: datei)
        #expect(wieder.gemerkte.first?.orte == ["Heizkreis 1", "Heizkreis 2"])
        #expect(wieder.gemerkte.first?.hoechstdauer == 3600)
        #expect(wieder.gemerkte.first?.schwere == .stoerung)
    }
}
