import Foundation
import FoundationModels
import Anlage
import Geraeteschnittstelle

/// Eingreifendes Werkzeug, das nie unmittelbar wirkt: Es legt einen Vorschlag an, den der Nutzer
/// übernimmt oder verwirft. Geprüft wird vorher gegen den Parameterkatalog.
public struct AenderungVorschlagenWerkzeug: Tool {
    public let name = "aenderung_vorschlagen"
    public let description = """
        Schlägt eine geänderte Einstellung vor. Wirkt nicht sofort: Der Nutzer sieht eine Karte \
        mit bisherigem und neuem Wert, Begründung und Wirkung und übernimmt oder verwirft sie; \
        vor der Übernahme sichert die App das Gerät, danach liest sie den Wert zurück. Nur \
        Einstellungen mit ki in einstellungen_lesen, dazu heizkreis.betriebsart und \
        kesselkreispumpe.betriebsart (auto, ein, aus) und circuits[].peers (versorgte \
        Verteiler, kommagetrennt). Speicherwerte trägt die App auf beiden Heizungsgeräten ein. \
        Mehrere Änderungen, die nur zusammen gelten, gehören in einen Vorschlag. Die Antwort \
        nennt Fehler, die vor einem neuen Versuch zu berichtigen sind.
        """

    @Generable
    public struct Wertangabe {
        @Guide(description: "Schlüssel wie buffer.voll_c, burner.delta_on_k, rooms[].p_band_k, circuits[].min_buffer_c, heizkreis.betriebsart")
        public var parameter: String
        @Guide(description: "Bei Werten je Raum der Raumname oder die Raumnummer, je Heizkreis dessen Nummer; sonst leer")
        public var eintrag: String?
        @Guide(description: "Neuer Wert als Zahl ohne Einheit, als Auswahlwert wie heat oder off, oder als Liste von Verteilern")
        public var wert: String
    }

    @Generable
    public struct Argumente {
        @Guide(description: "Titel der Karte, kurz, etwa „Speicher voll: 62 → 68 °C“")
        public var titel: String
        @Guide(description: "Gerät: Kennung oder Ort wie Erdgeschoss, Kessel, Pufferspeicher; leer, wenn eindeutig")
        public var geraet: String?
        @Guide(description: "Eine bis vier Änderungen, die zusammen gelten", .count(1...4))
        public var aenderungen: [Wertangabe]
        @Guide(description: "Begründung mit den Zahlen aus den Werkzeugen, zwei bis vier Sätze in der Sie-Form")
        public var begruendung: String
        @Guide(description: "Erwartete Wirkung und woran man sie erkennt, ein bis drei Sätze")
        public var wirkung: String
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        let ergebnis = Vorschlagsbau.aenderung(
            titel: arguments.titel, geraet: arguments.geraet,
            angaben: arguments.aenderungen.map { .init(parameter: $0.parameter, eintrag: $0.eintrag, wert: $0.wert) },
            begruendung: arguments.begruendung, wirkung: arguments.wirkung,
            staende: await zugriff.staende(), bild: await zugriff.bild())
        return await Self.melden(ergebnis, zugriff)
    }

    static func melden(_ ergebnis: Vorschlagsbau.Ergebnis, _ zugriff: any Anlagenzugriff) async -> String {
        switch ergebnis {
        case .abgelehnt(let fehler):
            return JSONWert.objekt(["angelegt": false, "fehler": .liste(fehler.map(JSONWert.text))]).kompakt
        case .vorschlag(let v):
            await zugriff.vorschlagAnlegen(v)
            let zeilen: [JSONWert] = v.ziele.map { z in
                ["geraet": .text(z.ort), "einstellung": .text(z.bezeichnung),
                 "bisher": .text(Zielwerte.anzeige(z.parameter, z.bisher)), "neu": .text(Zielwerte.anzeige(z.parameter, z.neu))]
            }
            return JSONWert.objekt([
                "angelegt": true, "vorschlag": .text(v.id), "titel": .text(v.titel),
                "aenderungen": .liste(v.aktion == nil ? zeilen : []),
                "hinweis": "Der Nutzer sieht den Vorschlag als Karte mit Übernehmen und Verwerfen. Wiederholen Sie die Werte nicht in voller Länge; verweisen Sie auf die Karte.",
            ]).kompakt
        }
    }
}

/// Eingreifendes Werkzeug für Aktionen: Messfahrt, Schutzfahrt, Fühlersuche,
/// Ladungsaufzeichnung, Relaisprüfung und Neustart nach einer Änderung, die ihn verlangt.
public struct AktionVorschlagenWerkzeug: Tool {
    public let name = "aktion_vorschlagen"
    public let description = """
        Schlägt eine Aktion vor, die der Nutzer auslöst: messfahrt (ein Kanal eines Verteilers \
        wird vermessen, etwa 90 Sekunden), schutzfahrt (die Ventile eines Verteilers fahren \
        einmal auf und zu), fuehlersuche (das Heizungsgerät sucht die Fühler am Bus neu), \
        ladungsaufzeichnung (die nächste Speicherladung wird im 5-Sekunden-Raster \
        aufgezeichnet), relaispruefung (das Relais eines Heizkreises oder der \
        Kesselkreispumpe wird unmittelbar abgefragt) und neustart (nur nach einer Änderung, die \
        erst mit dem Neustart wirkt). Wirkt nicht sofort; der Nutzer bestätigt auf einer Karte.
        """

    @Generable
    public struct Argumente {
        @Guide(description: "Die Aktion", .anyOf(["messfahrt", "schutzfahrt", "fuehlersuche", "ladungsaufzeichnung", "relaispruefung", "neustart"]))
        public var aktion: String
        @Guide(description: "Gerät: Kennung oder Ort wie Erdgeschoss oder Pufferspeicher")
        public var geraet: String
        @Guide(description: "Bei messfahrt die Kanalnummer, bei relaispruefung die Nummer des Heizkreises oder kkp; sonst leer")
        public var eintrag: String?
        @Guide(description: "Titel der Karte, kurz")
        public var titel: String
        @Guide(description: "Begründung mit den Zahlen aus den Werkzeugen, zwei bis vier Sätze in der Sie-Form")
        public var begruendung: String
        @Guide(description: "Was die Aktion bewirkt und worauf der Nutzer achten sollte, ein bis zwei Sätze")
        public var wirkung: String
    }

    let zugriff: any Anlagenzugriff

    public init(zugriff: any Anlagenzugriff) {
        self.zugriff = zugriff
    }

    @concurrent public func call(arguments: Argumente) async throws -> String {
        guard let aktion = Vorschlag.Aktion(rawValue: arguments.aktion) else {
            return JSONWert.objekt(["angelegt": false, "fehler": ["Unbekannte Aktion."]]).kompakt
        }
        let ergebnis = Vorschlagsbau.aktion(
            aktion, geraet: arguments.geraet, eintrag: arguments.eintrag, titel: arguments.titel,
            begruendung: arguments.begruendung, wirkung: arguments.wirkung,
            staende: await zugriff.staende(), bild: await zugriff.bild(), aenderungen: await zugriff.aenderungen())
        return await AenderungVorschlagenWerkzeug.melden(ergebnis, zugriff)
    }
}
