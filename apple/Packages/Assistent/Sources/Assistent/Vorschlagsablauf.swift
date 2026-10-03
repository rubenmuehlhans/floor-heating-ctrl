import Foundation
import Anlage
import Diagnose
import Geraeteschnittstelle

/// Übernahme und Rücknahme eines Vorschlags.
///
/// Übernahme: Die App liest jedes betroffene Gerät frisch und prüft, dass der Wert noch der
/// bisherige ist; ein inzwischen am Gerät geänderter Wert soll nicht stillschweigend
/// überschrieben werden. Dann sichert sie jedes Gerät, schreibt, liest zurück und vergleicht.
/// Scheitert das zweite Gerät, nimmt sie das erste zurück, damit beide Heizungsgeräte beim
/// Speicher nicht auseinanderlaufen. Zuletzt Änderungsprotokoll und Kennzahlen der sieben Tage
/// davor für die Wirkungskontrolle.
///
/// Rücknahme: stellt den bisherigen Wert her, sofern am Gerät noch der übernommene steht.
/// Scheitert sie, bleibt der Vorschlag übernommen, und `ergebnis` sagt, warum; „Rückgängig“
/// lässt sich dann erneut versuchen.
public enum Vorschlagsablauf {
    /// So lange nach der Übernahme vergleicht die Wirkungskontrolle.
    public static let kontrollzeitraum: TimeInterval = 7 * 86_400

    public static func uebernehmen(_ v: Vorschlag, mit a: any Vorschlagsausfuehrung, jetzt: Date = .now) async -> Vorschlag {
        var v = v
        if let aktion = v.aktion {
            guard let ziel = v.ziele.first else { return v }
            do {
                v.ergebnis = try await a.ausfuehren(aktion, ziel)
                v.status = .uebernommen(jetzt)
                await a.protokollieren([Aenderung(
                    id: UUID().uuidString, zeit: jetzt, geraet: ziel.ort, parameter: ziel.bezeichnung, bisher: "–",
                    neu: "ausgelöst", ausloeser: "Vorschlag der KI, bestätigt", geraetekennung: ziel.geraet,
                    parameterkennung: ziel.parameter, vorschlag: v.id)])
            } catch {
                v.status = .gescheitert(error.localizedDescription)
            }
            return v
        }

        let geraete = Array(NSOrderedSet(array: v.ziele.map(\.geraet))) as? [String] ?? []
        // Stand vorher: Weicht ein Wert schon ab, bleibt alles, wie es ist.
        for id in geraete {
            do {
                let stand = try await a.frischerStand(id)
                for z in v.ziele where z.geraet == id {
                    let jetzt = Zielwerte.aktuell(z, stand)
                    if let jetzt, !Vorschlagsbau.gleichwertig(jetzt, z.bisher) {
                        v.status = .gescheitert("Am Gerät \(z.ort) steht „\(z.bezeichnung)“ inzwischen auf \(Zielwerte.anzeige(z.parameter, jetzt)), nicht mehr auf \(Zielwerte.anzeige(z.parameter, z.bisher)). Nichts wurde geändert; fragen Sie die KI neu.")
                        return v
                    }
                }
            } catch {
                v.status = .gescheitert("\(ortsname(id, v)) ist nicht erreichbar: \(error.localizedDescription) Nichts wurde geändert.")
                return v
            }
        }
        let vorher = await a.kennzahlen(von: jetzt.addingTimeInterval(-kontrollzeitraum), bis: jetzt)

        var geschrieben: [String] = []
        for id in geraete {
            let ziele = v.ziele.filter { $0.geraet == id }
            do {
                if let sicherung = try await a.sichern(id) { v.sicherungen[id] = sicherung }
                try await a.schreiben(ziele, neu: true)
                geschrieben.append(id)
            } catch {
                var meldung = "\(ortsname(id, v)): \(error.localizedDescription)"
                for zurueck in geschrieben.reversed() {
                    do {
                        try await a.schreiben(v.ziele.filter { $0.geraet == zurueck }, neu: false)
                        meldung += " \(ortsname(zurueck, v)) wurde auf den bisherigen Wert zurückgesetzt."
                    } catch {
                        meldung += " \(ortsname(zurueck, v)) ließ sich nicht zurücksetzen und trägt den neuen Wert; die Sicherung liegt vor."
                    }
                }
                v.status = .gescheitert(meldung)
                return v
            }
        }
        v.status = .uebernommen(jetzt)
        v.kontrolle = Wirkungskontrolle(faellig: jetzt.addingTimeInterval(kontrollzeitraum), vorher: vorher.vergleichswerte)
        await a.protokollieren(v.ziele.map { z in
            Aenderung(id: UUID().uuidString, zeit: jetzt, geraet: z.ort, parameter: z.bezeichnung,
                      bisher: Zielwerte.anzeige(z.parameter, z.bisher), neu: Zielwerte.anzeige(z.parameter, z.neu),
                      ausloeser: "Vorschlag der KI, bestätigt", geraetekennung: z.geraet, parameterkennung: z.parameter, vorschlag: v.id)
        })
        return v
    }

    public static func zuruecknehmen(_ v: Vorschlag, mit a: any Vorschlagsausfuehrung, jetzt: Date = .now) async -> Vorschlag {
        var v = v
        guard v.aktion == nil, case .uebernommen = v.status else { return v }
        let geraete = Array(NSOrderedSet(array: v.ziele.map(\.geraet))) as? [String] ?? []
        for id in geraete {
            do {
                let stand = try await a.frischerStand(id)
                for z in v.ziele where z.geraet == id {
                    if let jetzt = Zielwerte.aktuell(z, stand), !Vorschlagsbau.gleichwertig(jetzt, z.neu) {
                        v.ergebnis = "Am Gerät \(z.ort) steht „\(z.bezeichnung)“ inzwischen auf \(Zielwerte.anzeige(z.parameter, jetzt)). Die Rücknahme würde diese spätere Änderung überschreiben; ändern Sie den Wert bei Bedarf von Hand."
                        return v
                    }
                }
                try await a.schreiben(v.ziele.filter { $0.geraet == id }, neu: false)
            } catch {
                v.ergebnis = "Rücknahme an \(ortsname(id, v)) gescheitert: \(error.localizedDescription)"
                return v
            }
        }
        v.ergebnis = nil
        v.status = .zurueckgenommen
        v.kontrolle = nil
        await a.protokollieren(v.ziele.map { z in
            Aenderung(id: UUID().uuidString, zeit: jetzt, geraet: z.ort, parameter: z.bezeichnung,
                      bisher: Zielwerte.anzeige(z.parameter, z.neu), neu: Zielwerte.anzeige(z.parameter, z.bisher),
                      ausloeser: "Rücknahme eines Vorschlags", geraetekennung: z.geraet, parameterkennung: z.parameter, vorschlag: v.id)
        })
        return v
    }

    /// Trägt die Kennzahlen nach dem Kontrollzeitraum ein, sobald er verstrichen ist.
    public static func wirkungPruefen(_ v: Vorschlag, mit a: any Anlagenzugriff, jetzt: Date = .now) async -> Vorschlag {
        var v = v
        guard case .uebernommen(let seit) = v.status, var k = v.kontrolle, k.ausgewertet == nil, jetzt >= k.faellig else { return v }
        let nachher = await a.kennzahlen(von: seit, bis: min(jetzt, seit.addingTimeInterval(kontrollzeitraum)))
        k.nachher = nachher.vergleichswerte
        k.ausgewertet = jetzt
        v.kontrolle = k
        return v
    }

    static func ortsname(_ id: String, _ v: Vorschlag) -> String {
        v.ziele.first { $0.geraet == id }?.ort ?? id
    }
}
