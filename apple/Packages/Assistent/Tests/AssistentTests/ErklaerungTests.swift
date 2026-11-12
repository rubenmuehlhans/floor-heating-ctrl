import Foundation
import Testing
import Anlage
import Diagnose
import Geraeteschnittstelle
@testable import Assistent

/// Der Betreiber erklärt einen Befund im Gespräch; danach fehlt er im Lagebericht, eine neue
/// Störung erscheint trotzdem.
@MainActor
@Suite("Erklärungen im Assistenten")
struct ErklaerungTests {
    let beginn = Date(timeIntervalSince1970: 1_790_056_800)
    let alle: Set<Befund.Schwere> = [.stoerung, .warnung, .hinweis]

    func minuten(_ m: Double) -> Date { beginn.addingTimeInterval(m * 60) }

    func relais(_ ort: String = "Heizkreis 1, Heizkreis 2") -> Befund {
        Befund(id: "relais", schwere: .stoerung, titel: "Relais der Pumpen nicht erreichbar",
               text: "Die Relais antworten nicht.", ort: ort, quelle: "Heizungsgeräte")
    }

    let rueckstroemung = Befund(id: "geraet:backflow:Kesselkreis", schwere: .warnung, titel: "Warmes Wasser strömt in den Kesselrücklauf",
                                text: "Dreimal beobachtet.", ort: "Kesselkreis", quelle: "Kessel")

    func lage(_ g: Befundgedaechtnis, _ liste: Erklaerungsliste, jetzt: Date) -> Befundlage {
        let e = liste.einordnen(g.sichtbar, jetzt: jetzt)
        return Befundlage(offen: e.offen, erledigt: g.erledigte, erklaert: e.erklaert)
    }

    func json(_ text: String) throws -> JSONWert { try JSONWert.lesen(Data(text.utf8)) }

    @Test func werkzeugLegtKarteAn() async throws {
        let anlage = try Testanlage()
        await anlage.setzeBefunde(Befundlage(offen: [.init(befund: relais(), seit: beginn)], erledigt: []))
        let werkzeug = BefundErklaerenWerkzeug(zugriff: anlage)
        let antwort = try json(try await werkzeug.call(arguments: .init(
            befund: "relais", erklaerung: "Die Relais hängen hinter der Kesselregelung und sind nachts stromlos.",
            gueltigTage: 0, hoechstdauerStunden: 14)))
        #expect(antwort["angelegt"] == true)
        #expect(antwort["geltung"] == "bis auf Widerruf, höchstens 14 Stunden am Stück")
        let angelegt = await anlage.erklaerungen
        #expect(angelegt.count == 1)
        #expect(angelegt.first?.status == .offen, "wirkt erst nach der Bestätigung")
        #expect(angelegt.first?.orte == ["Heizkreis 1", "Heizkreis 2"])
        #expect(angelegt.first?.hoechstdauer == TimeInterval(14 * 3600))
        #expect(antwort["erklaerung"]?.alsText == angelegt.first?.id)

        let falsch = try json(try await werkzeug.call(arguments: .init(befund: "wlan:x", erklaerung: "egal", gueltigTage: 0, hoechstdauerStunden: 0)))
        #expect(falsch["angelegt"] == false)
        #expect(falsch["fehler"]?[0]?.alsText?.contains("relais") == true, "nennt die anstehenden Befunde")
        #expect(await anlage.erklaerungen.count == 1)
    }

    @Test func befristet() throws {
        let lage = Befundlage(offen: [.init(befund: relais())], erledigt: [])
        let e = try BefundErklaerenWerkzeug.bauen(.init(befund: " relais ", erklaerung: "Umbau", gueltigTage: 7, hoechstdauerStunden: 0),
                                                 lage: lage, jetzt: beginn).get()
        #expect(e.bis == beginn.addingTimeInterval(7 * 86_400))
        #expect(e.hoechstdauer == nil)
        #expect(throws: (any Error).self) {
            try BefundErklaerenWerkzeug.bauen(.init(befund: "relais", erklaerung: "  ", gueltigTage: 0, hoechstdauerStunden: 0), lage: lage, jetzt: beginn).get()
        }
    }

    @Test func karteImGespraech() {
        let ausgabe = #"{"angelegt":true,"erklaerung":"e1","titel":"Relais der Pumpen nicht erreichbar"}"#
        #expect(Darstellung.erklaerungskennung("befund_erklaeren", ausgabe) == "e1")
        #expect(Darstellung.erklaerungskennung("befund_erklaeren", #"{"angelegt":false}"#) == nil)
        #expect(Darstellung.beschreibung("befund_erklaeren", [:], ausgabe: ausgabe) == "Erklärung zum Merken angelegt")
    }

    /// Der ganze Ablauf: Erklärung → Befund fehlt im Lagebericht → neue Störung erscheint.
    @Test func ablauf() async throws {
        let gedaechtnis = Befundgedaechtnis(datei: nil)
        let liste = Erklaerungsliste(datei: nil)
        var jetzt = minuten(0)
        @discardableResult func abgleichen(_ befunde: [Befund]) -> Befundgedaechtnis.Ergebnis {
            gedaechtnis.abgleichen(befunde, jetzt: jetzt, vollstaendig: true, melden: alle) { b, seit in
                liste.deckung(b, seit: seit, jetzt: jetzt)?.gedeckt
            }
        }

        // Vorher: Die Relais sind eine Störung, der Bericht verlangt zu handeln.
        abgleichen([relais(), rueckstroemung])
        var bericht = Lagebericht.ausBefunden(lage(gedaechtnis, liste, jetzt: jetzt).offen.map(\.befund), stand: jetzt)
        #expect(bericht.zustand == .handeln)
        #expect(bericht.befunde.contains("relais"))

        // Der Betreiber erklärt die Relais; der Assistent legt die Karte an, er bestätigt.
        let anlage = try Testanlage()
        await anlage.setzeBefunde(lage(gedaechtnis, liste, jetzt: jetzt))
        _ = try await BefundErklaerenWerkzeug(zugriff: anlage).call(arguments: .init(
            befund: "relais", erklaerung: "Die Relais sind hinter der Kesselregelung nachts stromlos.", gueltigTage: 0, hoechstdauerStunden: 0))
        let karte = try #require(await anlage.erklaerungen.first)
        liste.anlegen(karte)
        #expect(lage(gedaechtnis, liste, jetzt: jetzt).erklaert.isEmpty, "vor dem Merken wirkt nichts")
        liste.merken(karte.id, jetzt: jetzt)

        // Danach: Die Relais fehlen im Bericht, die Rückströmung bleibt.
        jetzt = minuten(5)
        let still = abgleichen([relais(), rueckstroemung])
        #expect(still.mitteilungen.isEmpty)
        var l = lage(gedaechtnis, liste, jetzt: jetzt)
        bericht = Lagebericht.ausBefunden(l.offen.map(\.befund), stand: jetzt)
        #expect(bericht.zustand == .beobachten)
        #expect(!bericht.befunde.contains("relais"))
        #expect(bericht.hinweise.map(\.id) == ["geraet:backflow:Kesselkreis"])

        // Das Werkzeug befunde führt die Relais als erklärt, mit der Erklärung.
        await anlage.setzeBefunde(l)
        let befunde = try json(try await BefundeWerkzeug(zugriff: anlage).call(arguments: .init(erledigte: false)))
        #expect(befunde["offen"]?.alsListe?.compactMap { $0["id"]?.alsText } == ["geraet:backflow:Kesselkreis"])
        #expect(befunde["erklaert"]?[0]?["id"] == "relais")
        #expect(befunde["erklaert"]?[0]?["erklaerung"]?.alsText?.contains("Kesselregelung") == true)

        // Eine andere Störung erscheint trotz Erklärung.
        jetzt = minuten(10)
        let ausfall = Befund(id: "offline:heiz_kessel", schwere: .stoerung, titel: "Kessel nicht erreichbar", text: "", ort: "Kessel", quelle: "Prüfung der App")
        let neu = abgleichen([relais(), rueckstroemung, ausfall])
        #expect(neu.mitteilungen.map(\.id) == ["offline:heiz_kessel"])
        l = lage(gedaechtnis, liste, jetzt: jetzt)
        bericht = Lagebericht.ausBefunden(l.offen.map(\.befund), stand: jetzt)
        #expect(bericht.zustand == .handeln)
        #expect(Set(bericht.befunde) == ["offline:heiz_kessel", "geraet:backflow:Kesselkreis"])

        // Fällt zusätzlich das Relais der Kesselkreispumpe aus, ist das ein anderes Muster:
        // Der Befund kehrt in den Bericht zurück und meldet sich.
        jetzt = minuten(15)
        let mehr = abgleichen([relais("Heizkreis 1, Heizkreis 2, Kesselkreispumpe"), rueckstroemung])
        #expect(mehr.mitteilungen.map(\.id) == ["relais"])
        l = lage(gedaechtnis, liste, jetzt: jetzt)
        #expect(l.erklaert.isEmpty)
        bericht = Lagebericht.ausBefunden(l.offen.map(\.befund), stand: jetzt)
        #expect(bericht.zustand == .handeln)
        #expect(bericht.befunde.contains("relais"))
    }
}
