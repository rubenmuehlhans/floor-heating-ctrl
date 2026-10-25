import Foundation
import FoundationModels
import Testing
@testable import Sprachmodelle

struct StromTests {
    private func aktionen(_ strom: [String]) throws -> (aktionen: [Kanalaktion], zustand: Stromzustand) {
        var zustand = Stromzustand(praefix: "r")
        var aktionen: [Kanalaktion] = []
        let text = String(decoding: SSE.strom(strom), as: UTF8.self)
        for zeile in text.split(separator: "\n", omittingEmptySubsequences: false) {
            if let e = try ClaudeStrom.auswerten(zeile: String(zeile)) {
                aktionen += try zustand.verarbeiten(e)
            }
        }
        return (aktionen, zustand)
    }

    @Test func denkenUndWerkzeugaufruf() throws {
        let (a, z) = try aktionen(
            [SSE.beginn] + SSE.denken(0, "Ich lese den Zustand.", signatur: "EqQBCgIYAhIM")
                + [SSE.ping]
                + SSE.werkzeug(1, id: "toolu_1", name: "anlage_status", argumente: [#"{"bereich": "#, #""heizung"}"#])
                + SSE.ende("tool_use"))
        #expect(a == [
            .denken(eintrag: "r-0", text: "Ich lese den Zustand."),
            .signatur(eintrag: "r-0", signatur: "EqQBCgIYAhIM"),
            // Die Argumente gehen erst mit dem Ende des Blocks hinaus, als Ganzes.
            .werkzeug(eintrag: "r-werkzeuge", id: "toolu_1", name: "anlage_status", argumente: #"{"bereich": "heizung"}"#),
            // Eingabe samt Zwischenspeicher: 1200 + 800 + 40
            .verbrauch(eintrag: "r-werkzeuge", art: .werkzeuge, eingabe: 2040, cache: 800, ausgabe: 87),
        ])
        try z.abschluss()
    }

    @Test func textInTeilen() throws {
        let (a, z) = try aktionen([SSE.beginn] + SSE.text(0, ["Der Speicher ", "hat 53 °C."]) + SSE.ende("end_turn"))
        #expect(a.prefix(2) == [.text(eintrag: "r-0", text: "Der Speicher "), .text(eintrag: "r-0", text: "hat 53 °C.")])
        #expect(a.last == .verbrauch(eintrag: "r-0", art: .antwort, eingabe: 2040, cache: 800, ausgabe: 87))
        try z.abschluss()
    }

    @Test func werkzeugOhneParameterErhaeltLeeresObjekt() throws {
        let (a, _) = try aktionen([SSE.beginn] + SSE.werkzeug(0, id: "t", name: "befunde", argumente: []) + SSE.ende("tool_use"))
        #expect(a.first == .werkzeug(eintrag: "r-werkzeuge", id: "t", name: "befunde", argumente: "{}"))
    }

    /// Claude schreibt Zahlen, wo das Schema Text verlangt; FoundationModels würde den Zug mit
    /// einem `ToolCallError` abbrechen.
    @Test func argumenteNachDemSchema() throws {
        let schema = try JSON.lesen(#"""
            {"type":"object","$defs":{"Wertangabe":{"type":"object","properties":{"parameter":{"type":"string"},"eintrag":{"type":"string"},"wert":{"type":"string"}}}},
             "properties":{"aenderungen":{"type":"array","items":{"$ref":"#/$defs/Wertangabe"}},"stunden":{"type":"integer"},
                           "reihen":{"type":"array","items":{"type":"string"}},"erledigte":{"type":"boolean"},"titel":{"type":"string"}}}
            """#)
        var zustand = Stromzustand(praefix: "r", schemata: ["x": schema])
        let argumente = #"{"aenderungen":[{"parameter":"rooms[].p_band_k","eintrag":3,"wert":1.5},{"parameter":"circuits[].peers","eintrag":2,"wert":["Erdgeschoss","Keller"]}],"stunden":"24","reihen":"brenner","erledigte":"false","titel":"T","fremd":7}"#
        var aktionen: [Kanalaktion] = []
        for e in [ClaudeEreignis.blockBeginn(index: 0, block: .werkzeug(id: "t", name: "x")), .argumentDelta(index: 0, json: argumente), .blockEnde(index: 0)] {
            aktionen += try zustand.verarbeiten(e)
        }
        guard case .werkzeug(_, _, _, let text)? = aktionen.first else {
            Issue.record("kein Aufruf")
            return
        }
        #expect(try JSON.lesen(text) == JSON.lesen(#"""
            {"aenderungen":[{"parameter":"rooms[].p_band_k","eintrag":"3","wert":"1.5"},{"parameter":"circuits[].peers","eintrag":"2","wert":"Erdgeschoss, Keller"}],
             "stunden":24,"reihen":["brenner"],"erledigte":false,"titel":"T","fremd":7}
            """#))
    }

    @Test func unlesbareArgumenteBleibenUnveraendert() throws {
        var zustand = Stromzustand(praefix: "r", schemata: ["x": ["type": "object"]])
        _ = try zustand.verarbeiten(.blockBeginn(index: 0, block: .werkzeug(id: "t", name: "x")))
        _ = try zustand.verarbeiten(.argumentDelta(index: 0, json: #"{"a":"#))
        #expect(try zustand.verarbeiten(.blockEnde(index: 0)) == [.werkzeug(eintrag: "r-werkzeuge", id: "t", name: "x", argumente: #"{"a":"#)])
    }

    @Test func ablehnung() throws {
        let (_, z) = try aktionen([SSE.beginn] + SSE.ende("refusal", details: #"{"category":"cyber","explanation":"Abgelehnt."}"#))
        #expect(throws: LanguageModelError.self) { try z.abschluss() }
    }

    @Test func abgeschnittenerWerkzeugaufrufWirdNichtAusgefuehrt() throws {
        let (_, z) = try aktionen([SSE.beginn] + SSE.werkzeug(0, id: "t", name: "x", argumente: [#"{"a":"#], schliessen: false) + SSE.ende("max_tokens"))
        #expect(throws: ClaudeFehler.werkzeugaufrufAbgeschnitten) { try z.abschluss() }
    }

    @Test func abgeschnittenerTextIstVerwendbar() throws {
        let (_, z) = try aktionen([SSE.beginn] + SSE.text(0, ["Halb"]) + SSE.ende("max_tokens"))
        try z.abschluss()
    }

    @Test func abbruchOhneNachrichtenende() throws {
        let (_, z) = try aktionen([SSE.beginn] + SSE.text(0, ["Halb"]))
        #expect(throws: ClaudeFehler.unvollstaendig) { try z.abschluss() }
    }

    @Test func fehlerImStrom() {
        #expect(throws: ClaudeFehler.imStrom(art: "overloaded_error", meldung: "Overloaded")) {
            _ = try aktionen([SSE.beginn, #"{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}"#])
        }
    }

    /// Übernimmt das Ersatzmodell mitten in der Antwort, gehen Denkschritte und Werkzeugaufrufe
    /// davor nicht zurück; Text bleibt.
    @Test func wechselZumErsatzmodell() throws {
        let (a, z) = try aktionen(
            [SSE.beginn] + SSE.denken(0, "Erst", signatur: "s0") + SSE.text(1, ["Teil"])
                + SSE.werkzeug(2, id: "t", name: "anlage_status", argumente: [#"{"bereich":"alle"}"#])
                + [#"{"type":"content_block_start","index":3,"content_block":{"type":"fallback","from":{"model":"claude-opus-5"},"to":{"model":"claude-opus-4-8"}}}"#,
                   #"{"type":"content_block_stop","index":3}"#]
                + SSE.text(4, ["Weiter"]) + SSE.ende("end_turn"))
        #expect(a.contains(.denkenVerwerfen(eintrag: "r-0")))
        #expect(a.contains(.werkzeugEntfernen(eintrag: "r-werkzeuge", id: "t")))
        #expect(a.contains(.text(eintrag: "r-1", text: "Teil")))
        #expect(a.contains(.text(eintrag: "r-4", text: "Weiter")))
        try z.abschluss()
    }

    @Test func verborgenesDenken() throws {
        let (a, _) = try aktionen([SSE.beginn, #"{"type":"content_block_start","index":0,"content_block":{"type":"redacted_thinking","data":"EmwKAhgB"}}"#, #"{"type":"content_block_stop","index":0}"#] + SSE.text(1, ["Ok"]) + SSE.ende("end_turn"))
        #expect(a.first == .verborgenesDenken(eintrag: "r-0", daten: "EmwKAhgB"))
    }
}
