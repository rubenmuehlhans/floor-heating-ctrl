import Foundation
import Testing
import Anlage
import Diagnose
import Geraeteschnittstelle
import Verlauf
@testable import Assistent

@Suite("Lesende Werkzeuge")
struct WerkzeugTests {
    @Test func einstellungenOhneNetzUndRelaisadressen() async throws {
        let anlage = try Testanlage()
        let text = try await EinstellungenWerkzeug(zugriff: anlage).call(arguments: .init())
        #expect(!text.contains("Heimnetz"), "WLAN-Name")
        #expect(!text.contains("mqtt://"), "Broker")
        #expect(!text.contains("192.168.1.204"), "Relaisadresse")
        #expect(!text.contains("pumpe_hk1"), "Relais-Thema")
        #expect(!text.contains("A4:C1:38"), "Thermometer-MAC")
        let json = try JSONWert.lesen(Data(text.utf8))
        let speicher = try #require(json["geraete"]?.alsListe?.first { $0["geraet"] == "heiz_3f21ac" })
        #expect(speicher["werte"]?["buffer.voll_c"] == 62)
        #expect(speicher["heizkreise"]?[0]?["versorgt"]?.alsListe?.contains("Erdgeschoss") == true)
        #expect(json["parameter"]?["heizung/buffer.voll_c"]?["ki"] == true)
        #expect(json["parameter"]?["heizung/buffer.voll_c"]?["auf_allen_heizungsgeraeten_gleich"] == true)
        #expect(json["parameter"]?["heizung/circuits[].frost_c"]?["ki"] == nil)
        let eg = try #require(json["geraete"]?.alsListe?.first { $0["geraet"] == "fbh_a1b2c3" })
        #expect(eg["raeume"]?[2]?["thermometer"] == false, "Flur/WC ohne Thermometer")
    }

    @Test func einstellungenEinerGruppe() async throws {
        let text = try await EinstellungenWerkzeug(zugriff: try Testanlage()).call(arguments: .init(geraet: "Kessel", gruppe: "brenner", erlaeuterungen: true))
        let json = try JSONWert.lesen(Data(text.utf8))
        #expect(json["geraete"]?.alsListe?.count == 1)
        #expect(json["geraete"]?[0]?["werte"]?["burner.delta_on_k"] == 12)
        #expect(json["parameter"]?["heizung/burner.delta_on_k"]?["hilfe"]?.alsText?.isEmpty == false)
        #expect(json["parameter"]?["heizung/buffer.voll_c"] == nil)
    }

    @Test func befundeMitBeginn() async throws {
        let anlage = try Testanlage()
        let seit = Date(timeIntervalSince1970: 1_790_000_000)
        await anlage.setzeBefunde(Befundlage(
            offen: [.init(befund: Befund(id: "relais", schwere: .stoerung, titel: "Relais antwortet nicht", text: "Seit 3 h.", ort: "Heizkreis 1", quelle: "Prüfung der App"), seit: seit)],
            erledigt: []))
        let json = try JSONWert.lesen(Data(try await BefundeWerkzeug(zugriff: anlage).call(arguments: .init(erledigte: true)).utf8))
        #expect(json["offen"]?[0]?["schwere"] == "stoerung")
        #expect(json["offen"]?[0]?["seit"]?.alsText?.hasPrefix("2026-") == true)
        #expect(json["erledigt"] == [])
    }

    @Test func protokolle() async throws {
        let anlage = try Testanlage()
        #expect(try await ProtokolleWerkzeug(zugriff: anlage).call(arguments: .init(art: "aenderungen", anzahl: 5)) == "Über die App wurde noch nichts geändert.")
        await anlage.protokollieren([Aenderung(id: "1", zeit: .now, geraet: "Kessel", parameter: "Voll bei", bisher: "62,0 °C", neu: "68,0 °C", ausloeser: "Vorschlag der KI, bestätigt")])
        let t = try await ProtokolleWerkzeug(zugriff: anlage).call(arguments: .init(art: "aenderungen", anzahl: 5))
        #expect(t.contains(";Kessel;Voll bei;62,0 °C;68,0 °C;Vorschlag der KI, bestätigt"))
    }

    @Test func verlaufVerdichtet() async throws {
        #expect(VerlaufWerkzeug.schritt(stunden: 24, zeilen: 48) == 1800)
        #expect(VerlaufWerkzeug.schritt(stunden: 720, zeilen: 48) == 86_400)
        #expect(VerlaufWerkzeug.schritt(stunden: 1, zeilen: 48) == 300)

        let anlage = try Testanlage()
        let beginn = Date(timeIntervalSince1970: 1_790_000_000)
        await anlage.setzeVerlauf(Verlaufsauszug(beginn: beginn, schritt: 1800, anzahl: 4, reihen: [
            .init(geraet: "heiz_3f21ac", ort: "Pufferspeicher", schluessel: "fuehler.puffer", bezeichnung: "Pufferspeicher", werte: [52, 53.44, nil, 55]),
            .init(geraet: "heiz_9a1b2c", ort: "Kessel", schluessel: "brenner", bezeichnung: "Brenner", werte: [0, 0.5, 1, nil]),
            .init(geraet: "fbh_a1b2c3", ort: "Erdgeschoss", schluessel: "raum.1.ist", bezeichnung: "Küche", werte: [20.1, 20.2, 20.3, 20.4]),
        ]))
        let werkzeug = VerlaufWerkzeug(zugriff: anlage)
        let liste = try await werkzeug.call(arguments: .init(reihen: [], stunden: 2))
        #expect(liste.contains("heiz_3f21ac/fuehler.puffer | Pufferspeicher"))
        let t = try await werkzeug.call(arguments: .init(reihen: ["waermeerzeugung"], stunden: 2))
        #expect(t.contains("heiz_3f21ac/fuehler.puffer | Pufferspeicher | Pufferspeicher | °C | 52.0 | 55.0 | 53.5 | 55.0"))
        #expect(t.contains("heiz_9a1b2c/brenner | Brenner | Kessel | % | 0 | 100 | 50 | 100"))
        #expect(!t.contains("raum.1.ist"))
        #expect(t.contains(";53.4;50\n"))
        let nurEG = try await werkzeug.call(arguments: .init(reihen: ["raeume"], stunden: 2, geraet: "Erdgeschoss"))
        #expect(nurEG.contains("fbh_a1b2c3/raum.1.ist | Küche"))
    }

    @Test func verlaufUeberEinJahr() {
        // Über 30 Tage mindestens Tagesmittel, auch wenn die Zeilen feinere zuließen
        #expect(VerlaufWerkzeug.schritt(stunden: 900, zeilen: 120) == 86_400)
        #expect(VerlaufWerkzeug.schritt(stunden: 8760, zeilen: 48) == 7 * 86_400)
        #expect(VerlaufWerkzeug.schritt(stunden: 2160, zeilen: 48) == 2 * 86_400)
    }

    @Test func ereignisseMitZusammenfassung() async throws {
        let anlage = try Testanlage()
        let jetzt = Date.now
        func e(_ vor: Double, _ geraet: String, _ art: String, _ felder: [String: JSONWert] = [:]) -> Ereignis {
            Ereignis(zeit: jetzt.addingTimeInterval(-vor * 60), geraet: geraet, art: art, felder: felder)
        }
        await anlage.setzeEreignisse([
            e(300, "heiz_9a1b2c", "brenner", ["ein": true]),
            e(280, "heiz_9a1b2c", "brenner", ["ein": false, "dauer_vorher_s": 1200]),
            e(180, "heiz_9a1b2c", "brenner", ["ein": true, "dauer_vorher_s": 6000]),
            e(150, "heiz_9a1b2c", "brenner", ["ein": false, "dauer_vorher_s": 1800]),
            e(120, "heiz_3f21ac", "pumpe", ["pumpe": "1", "ein": true]),
            e(100, "heiz_3f21ac", "pumpe", ["pumpe": "kkp", "ein": true]),
            e(60, "heiz_3f21ac", "neustart", ["grund": "panic", "laufzeit_vorher_s": 86_400]),
            e(50, "heiz_3f21ac", "befund", ["code": "flow_swapped", "stand": "neu"]),
            e(40, "lst_c0ffee", "leitstand", ["was": "start", "grund": "power_on"]),
            e(4000, "heiz_9a1b2c", "brenner", ["ein": true]),
        ])
        let w = EreignisseWerkzeug(zugriff: anlage, hoechstens: 5)
        let alle = try await w.call(arguments: .init(stunden: 24, arten: []))
        #expect(alle.contains("9 Ereignisse"), "das Ereignis vor drei Tagen liegt außerhalb")
        #expect(alle.contains("brenner | Kessel (heiz_9a1b2c) | 4 | 2 Starts; Laufzeit 50 min in 2 beendeten Läufen, Mittel 25 min"))
        // Zwei Pumpen eines Geräts stehen getrennt.
        #expect(alle.contains("pumpe | Pufferspeicher (heiz_3f21ac) | 1 | 1 Starts; Pumpe 1"))
        #expect(alle.contains("pumpe | Pufferspeicher (heiz_3f21ac) | 1 | 1 Starts; Pumpe kkp"))
        #expect(alle.contains("neustart | Pufferspeicher (heiz_3f21ac) | 1 | 1× Absturz (panic)"))
        #expect(alle.contains("Neuer Befund: Vorlauf und Rücklauf sind vermutlich vertauscht"))
        #expect(alle.contains("4 ältere nicht aufgeführt"))
        let kessel = try await w.call(arguments: .init(stunden: 24, geraet: "Kessel", arten: ["brenner"]))
        #expect(kessel.contains("4 Ereignisse") && !kessel.contains("pumpe |"))
        let leitstand = try await w.call(arguments: .init(stunden: 24, geraet: "Leitstand", arten: []))
        #expect(leitstand.contains("1 Ereignisse") && leitstand.contains("Einschalten oder Stromausfall"))
        let keine = try await EreignisseWerkzeug(zugriff: try Testanlage()).call(arguments: .init(stunden: 24, arten: []))
        #expect(keine.contains("nur mit einem eingerichteten Leitstand"))
    }

    @Test func feinverlaufVomLeitstand() async throws {
        #expect(FeinverlaufWerkzeug.raster(sekunden: 3600, zeilen: 120) == 30)
        #expect(FeinverlaufWerkzeug.raster(sekunden: 86_400, zeilen: 120) == 900)
        #expect(FeinverlaufWerkzeug.raster(sekunden: 6 * 3600, zeilen: 120) == 180)
        let beginn = FeinverlaufWerkzeug.ortszeit("2026-09-21T14:00")
        #expect(beginn != nil)
        #expect(FeinverlaufWerkzeug.ortszeit("gestern") == nil)

        let anlage = try Testanlage()
        let w = FeinverlaufWerkzeug(zugriff: anlage)
        let ohne = try await w.call(arguments: .init(geraet: "Kessel", reihen: ["geraet.heap"], stunden: 1))
        #expect(ohne.contains("nur im Heimnetz"))
        let t = Date(timeIntervalSince1970: 1_790_142_600)
        await anlage.setzeFeinverlauf(.init(geraet: "heiz_9a1b2c", raster: 30, spalten: ["geraet.heap", "geraet.rssi"], zeilen: [
            .init(zeit: t, werte: [41_000, -61]),
            .init(zeit: t.addingTimeInterval(30), werte: [12_500, nil]),
            .init(zeit: t.addingTimeInterval(60), werte: [40_100, -64.5]),
        ]))
        let text = try await w.call(arguments: .init(geraet: "Kessel", reihen: ["geraet.heap", "geraet.rssi"], stunden: 1))
        #expect(text.contains("geraet.heap | 12500 | 41000 | 31200 | 40100 | 3"))
        #expect(text.contains("geraet.rssi | -64.50 | -61 | -62.75 | -64.50 | 2"))
        #expect(text.contains(";12500;\n"), "eine fehlende Zelle bleibt leer")
        let anfrage = try #require(await anlage.feinverlaufAnfragen.last)
        #expect(anfrage.geraet == "heiz_9a1b2c" && anfrage.raster == 30)
        _ = try await w.call(arguments: .init(geraet: "leitstand", reihen: ["geraet.heap"], stunden: 24))
        #expect(await anlage.feinverlaufAnfragen.last?.geraet == "lst_c0ffee")
        #expect(await anlage.feinverlaufAnfragen.last?.raster == 900)
        let leer = try await w.call(arguments: .init(geraet: "Kessel", reihen: [], stunden: 1))
        #expect(leer.contains("Keine Reihe angegeben"))
    }

    @Test func kennzahlenAusDemVerlauf() async throws {
        let anlage = try Testanlage()
        let bild = await anlage.bild()
        let etage = try #require(bild.etagen.first { $0.id == "fbh_a1b2c3" })
        let raum = try #require(etage.raeume.first)
        let auszug = Verlaufsauszug(beginn: .now.addingTimeInterval(-7200), schritt: 1800, anzahl: 4, reihen: [
            .init(geraet: etage.id, ort: etage.name, schluessel: Messgroesse.raum(raum.nummer, "ist"), bezeichnung: raum.name, werte: [19, 19.2, 20, 21.5]),
            .init(geraet: etage.id, ort: etage.name, schluessel: Messgroesse.raum(raum.nummer, "soll"), bezeichnung: raum.name, werte: [20, 20, 20, 20]),
        ])
        let k = Kennzahlen.berechnen(bild: bild, auszug: auszug, von: .now.addingTimeInterval(-7200), bis: .now)
        let r = try #require(k.raeume.first { $0.raum == raum.id })
        #expect(r.mittlereAbweichungK == -0.08)
        #expect(r.anteilZuKalt == 0.5)
        #expect(r.anteilZuWarm == 0.25)
        #expect(r.messpunkte == 4)
        let text = KennzahlenWerkzeug.ausgabe(k)
        #expect(text.contains("\"mittlereAbweichungK\":-0.08"))
    }
}

extension Testanlage {
    func setzeBefunde(_ b: Befundlage) { befundlageWert = b }
    func setzeVerlauf(_ v: Verlaufsauszug) { verlaufWert = v }
}

@Suite("Wissen")
struct WissenTests {
    let wissen = Wissen(ordner: Aufnahme.dokumente)

    @Test func dokumenteGeladen() {
        #expect(wissen.dokumente.map(\.name) == Wissen.dateien)
        #expect(wissen.abschnitte.count > 80)
        #expect(wissen.volltext.contains("<dokument name=\"handbuch.md\">"))
    }

    @Test func gliederungNachUeberschriften() {
        let d = Wissen.Dokument(name: "t.md", titel: "Titel", text: """
            # Titel
            Vorwort.
            ## Pumpen
            Allgemein.
            ### Kesselkreispumpe
            Die Kesselkreispumpe läuft ab 3 K.
            ## Speicher
            ```
            # kein Titel im Code
            ```
            """)
        let a = Wissen.gliedern(d)
        #expect(a.map(\.titel) == ["Titel", "Pumpen", "Pumpen › Kesselkreispumpe", "Speicher"])
        #expect(a[3].text.contains("# kein Titel im Code"))
    }

    @Test func sucheFindetZusammensetzungen() {
        #expect(Wissen.stamm("pumpen") == "pump")
        let treffer = wissen.suchen("Kesselkreispumpe")
        #expect(treffer.first?.titel.contains("Kesselkreispumpe") == true, "\(treffer.map(\.titel))")
        let rueck = wissen.suchen("Rückströmung Schwerkraftbremse")
        #expect(!rueck.isEmpty)
        #expect(wissen.suchen("und oder der").isEmpty)
    }

    @Test func werkzeugKuerzt() async throws {
        let t = try await WissenWerkzeug(wissen: wissen, laenge: 200).call(arguments: .init(anfrage: "Messfahrt Gegenspannung"))
        #expect(t.hasPrefix("## "))
        #expect(t.contains(" …"))
    }
}
