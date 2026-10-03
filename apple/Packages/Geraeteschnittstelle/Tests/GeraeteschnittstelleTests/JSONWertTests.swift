import Foundation
import Testing
@testable import Geraeteschnittstelle

struct JSONWertTests {
    @Test func hinUndZurueck() throws {
        let wert: JSONWert = ["rooms": [["id": 1, "target_c": 21.5, "mode": "heat"]], "ok": true, "leer": nil]
        let zurueck = try JSONWert.lesen(try wert.daten())
        #expect(zurueck == wert)
        #expect(zurueck["rooms"]?[0]?["target_c"]?.alsZahl == 21.5)
        #expect(zurueck["rooms"]?[0]?["id"]?.alsGanzzahl == 1)
    }

    @Test func ganzeZahlenOhneNachkommastelle() throws {
        let wert: JSONWert = ["a": 5, "b": 0.5]
        let text = String(decoding: try wert.daten(), as: UTF8.self)
        #expect(text == #"{"a":5,"b":0.5}"#)
    }

    @Test func wahrheitswerteBleibenWahrheitswerte() throws {
        let wert = try JSONWert.lesen(Data(#"{"a":true,"b":1}"#.utf8))
        #expect(wert["a"] == .bool(true))
        #expect(wert["b"] == .zahl(1))
    }

    /// Sicherungen tragen die Schlüssel der verschlüsselten Thermometer im Klartext.
    @Test func thermometerschluesselErreichenDieKINie() throws {
        let sicherung = try JSONWert.lesen(Data(#"{"site":"Erdgeschoss","ble_keys":[{"mac":"C0:FF:EE:12:34:56","bindkey":"00112233445566778899aabbccddeeff"}]}"#.utf8))
        let text = sicherung.ohneZugangsdaten().kompakt
        #expect(!text.contains("bindkey"))
        #expect(!text.contains("00112233"))
        #expect(text.contains("C0:FF:EE:12:34:56"))
    }

    @Test(arguments: ["attrappe/verteiler-config.json", "attrappe/speicher-config.json"])
    func zugangsdatenErreichenDieKINie(_ datei: String) throws {
        let roh = try JSONWert.lesen(try Fixture.daten(datei))
        let bereinigt = roh.ohneZugangsdaten()
        let text = bereinigt.kompakt
        for schluessel in JSONWert.zugangsschluessel {
            #expect(!text.contains("\"\(schluessel)\":"), "\(schluessel) steht noch drin")
        }
        #expect(text.contains("\"pass_set\":"))
        // Alles andere bleibt: die Konfiguration ist ohne Zugangsdaten sonst unverändert lesbar.
        #expect(bereinigt["site"] == roh["site"])
        #expect(bereinigt["timezone"] == roh["timezone"])
    }
}
