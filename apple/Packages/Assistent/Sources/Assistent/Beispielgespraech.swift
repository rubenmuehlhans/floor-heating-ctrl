import Foundation
import Anlage

// Gestellte Inhalte für das Klickmodell. Zahlen und Befunde folgen den Beispieldaten der
// Anlage; die Antworten zeigen, wie die KI sie verwenden soll: Ergebnis zuerst, Belege aus den
// Werkzeugen, Eingriffe nur als Vorschlag.

extension Lagebericht {
    public static let beispiel = Lagebericht(
        zustand: .beobachten,
        kurztext: "Die Anlage arbeitet. Zwei Punkte sollten Sie prüfen lassen, ein Wert lässt sich verbessern.",
        hinweise: [
            Hinweis(
                id: "h-relais", schwere: .stoerung,
                text: "Die Relais der drei Pumpen antworten nicht. Die Pumpen folgen der Steuerung deshalb nicht.",
                bezug: "relais"
            ),
            Hinweis(
                id: "h-backflow", schwere: .warnung,
                text: "Warmes Wasser strömt ohne Brenner in den Kesselrücklauf, dreimal in 24 Stunden. Lassen Sie die Schwerkraftbremse prüfen.",
                bezug: "backflow"
            ),
            Hinweis(
                id: "h-voll", schwere: .hinweis,
                text: "„Speicher voll“ steht auf 62 °C, die Ladungen enden bei 68,5 bis 72,3 °C. Vorschlag: 68 °C.",
                bezug: "voll_68"
            ),
        ],
        erstellt: Beispielzeit.datum("2026-08-28T17:52:00"),
        modell: "Claude Opus 5"
    )
}

extension Vorschlag {
    public static let speicherVoll = Vorschlag(
        id: "voll_68",
        titel: "Speicher voll: 62 → 68 °C",
        geraete: ["Kessel", "Pufferspeicher"],
        parameter: "buffer.voll_c",
        bisher: "62,0 °C",
        neu: "68,0 °C",
        begruendung: "Die Ladekalibrierung misst als Höchstwert 68,9 °C, die letzten beiden Ladungen endeten bei 68,5 und 72,3 °C. Oberhalb von 62 °C zeigt der Ladezustand deshalb keinen Unterschied mehr.",
        wirkung: "Der Ladezustand bildet den oberen Bereich des Speichers ab. Die Warnung „Warmwasserreserve knapp“ bleibt unverändert, sie hängt an der Warngrenze von 40 °C. Der Wert wird auf beiden Heizungsgeräten gleich eingetragen, wie es das Handbuch verlangt."
    )

    public static let messfahrten = Vorschlag(
        id: "messfahrten",
        titel: "Messfahrt für 5 Kanäle",
        geraete: ["Keller", "Erdgeschoss"],
        parameter: "aktion.messfahrt",
        bisher: "nicht kalibriert",
        neu: "Messfahrt nacheinander",
        begruendung: "Keller 3 und Erdgeschoss 4, 6, 8, 10 sind Räumen zugeordnet, aber nie vermessen worden. Sie erkennen die Endlage nicht und fahren bei jeder Bewegung bis zur Maximallaufzeit.",
        wirkung: "Je Kanal dauert die Fahrt etwa 90 Sekunden; es fährt immer nur ein Kanal je Messgruppe. Die ermittelten Fahrzeiten und Schwellen legt die App Ihnen einzeln zur Übernahme vor."
    )

    public static let beispiele: [Vorschlag] = [.speicherVoll, .messfahrten]
}

extension Gespraech {
    public static let beispiel = Gespraech(
        id: "g1",
        titel: "Rückströmung und Ladezustand",
        beitraege: [
            Beitrag(
                id: "b1", rolle: .nutzer,
                bausteine: [.text("Was bedeutet die Rückströmung am Kessel, und muss ich etwas tun?")],
                zeit: Beispielzeit.datum("2026-08-28T17:55:10")
            ),
            Beitrag(
                id: "b2", rolle: .assistent,
                bausteine: [
                    .werkzeug(Werkzeugaufruf(
                        id: "w1", name: "befunde",
                        beschreibung: "Befunde aller Geräte gelesen",
                        ergebnis: "Kessel: Rückströmung, 3 Ereignisse, zuletzt +3,2 K"
                    )),
                    .werkzeug(Werkzeugaufruf(
                        id: "w2", name: "verlauf",
                        beschreibung: "Kesselrücklauf, Speicher und Brenner der letzten 24 Stunden gelesen",
                        ergebnis: "Rücklauf steigt dreimal ohne Brenner und Pumpe, Speicher fällt je um 0,2 bis 0,6 K"
                    )),
                    .ueberlegung("Der Kesselrücklauf steigt, während Brenner und Kesselkreispumpe stehen. Die Wärme kann nur aus dem Speicher kommen. Das Handbuch nennt dafür eine fehlende oder undichte Schwerkraftbremse; die Firmware meldet den Befund aus genau diesem Muster."),
                    .text("Warmes Wasser aus dem Pufferspeicher strömt in den Kesselrücklauf, obwohl weder Brenner noch Kesselkreispumpe laufen. In den letzten 24 Stunden kam das dreimal vor, zuletzt mit einem Anstieg von 3,2 K.\n\nDie übliche Ursache ist eine fehlende oder undichte Schwerkraftbremse im Kesselkreis. Der Speicher gibt dann Wärme an den Kessel ab, die über den Schornstein verloren geht.\n\nEine Einstellung behebt das nicht. Lassen Sie die Schwerkraftbremse bei der nächsten Wartung prüfen."),
                ],
                zeit: Beispielzeit.datum("2026-08-28T17:55:31")
            ),
            Beitrag(
                id: "b3", rolle: .nutzer,
                bausteine: [.text("Warum zeigt der Speicher nur 8 % Ladung bei 53 °C?")],
                zeit: Beispielzeit.datum("2026-08-28T17:57:02")
            ),
            Beitrag(
                id: "b4", rolle: .assistent,
                bausteine: [
                    .werkzeug(Werkzeugaufruf(
                        id: "w3", name: "einstellungen_lesen",
                        beschreibung: "Speichereinstellungen beider Heizungsgeräte gelesen",
                        ergebnis: "voll 62 °C, leer 52,3 °C (gelernt), Warngrenze 40 °C"
                    )),
                    .werkzeug(Werkzeugaufruf(
                        id: "w4", name: "protokolle",
                        beschreibung: "Ladungsprotokoll gelesen",
                        ergebnis: "Ladungsende 68,5 und 72,3 °C, Kalibrierung: Höchstwert 68,9 °C"
                    )),
                    .ueberlegung("Der Ladezustand wird linear geschätzt: (53,0 − 52,3) / (62 − 52,3) ≈ 7 %. Rechnerisch stimmt die Anzeige. Auffällig ist „voll“: Die Ladungen enden 6 bis 10 K darüber."),
                    .text("Die Anzeige rechnet linear zwischen „leer“ und „voll“. Der gelernte Leerpunkt liegt bei 52,3 °C, „voll“ bei 62 °C; bei 53 °C ergibt das rund 8 %.\n\nDer Leerpunkt ist plausibel, er wurde aus dem Temperaturabfall nach Ladungen gelernt. Der Wert für „voll“ ist dagegen zu niedrig: Die Ladekalibrierung misst als Höchstwert 68,9 °C. Oberhalb von 62 °C zeigt die Anzeige deshalb immer 100 %."),
                    .vorschlag("voll_68"),
                ],
                zeit: Beispielzeit.datum("2026-08-28T17:57:40")
            ),
            Beitrag(
                id: "b5", rolle: .nutzer,
                bausteine: [
                    .bild("Ölstandsanzeige am Tank"),
                    .text("Wie viel Öl ist noch im Tank?"),
                ],
                zeit: Beispielzeit.datum("2026-08-28T18:01:15")
            ),
            Beitrag(
                id: "b6", rolle: .assistent,
                bausteine: [
                    .werkzeug(Werkzeugaufruf(
                        id: "w5", name: "ablesung_speichern",
                        beschreibung: "Tankablesung mit Datum in der App gespeichert",
                        ergebnis: "3 100 l am 28.08.2026 18:01"
                    )),
                    .text("Die Anzeige steht bei etwa 3 100 Litern.\n\nMit einer zweiten Ablesung in einigen Wochen lässt sich der tatsächliche Düsendurchsatz bestimmen: Verbrauch zwischen beiden Ablesungen geteilt durch die Brennerlaufzeit im selben Zeitraum. Bisher rechnet der Kessel mit angenommenen 2,2 l/h. Ich erinnere Sie an die zweite Ablesung."),
                ],
                zeit: Beispielzeit.datum("2026-08-28T18:01:40")
            ),
        ]
    )
}

extension Aenderung {
    public static let beispiele: [Aenderung] = [
        Aenderung(
            id: "a1", zeit: Beispielzeit.datum("2026-08-24T19:12:00"), geraet: "Kessel",
            parameter: "Kesselkreispumpe · Ausschaltschwelle", bisher: "0,5 K", neu: "2,0 K",
            ausloeser: "von Hand"
        ),
        Aenderung(
            id: "a2", zeit: Beispielzeit.datum("2026-08-17T12:28:00"), geraet: "Kessel",
            parameter: "Kessel gereinigt", bisher: "–", neu: "17.08.2026",
            ausloeser: "von Hand"
        ),
    ]
}
