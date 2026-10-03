import Foundation

// Beispieldaten für das Klickmodell.
//
// Messwerte, Raumnamen, Kanalbelegung, Firmwarestände und Befunde stammen aus dem Mitschnitt
// vom 28.08.2026 18:04 (messungen/verlauf.jsonl) und der Ladungsaufzeichnung vom 19.08.
// Geräte- und Fühlerkennungen sind verfremdet. Abweichend vom Mitschnitt, in dem die meisten
// Räume für den Sommer ausgeschaltet waren, stehen sie hier auf „Heizen“, und das Bad hat
// 24 °C Soll, damit alle Zustände einmal vorkommen.

public enum Beispielzeit {
    static let zone = TimeZone(identifier: "Europe/Berlin") ?? .current

    /// Ortszeit ohne Zonenangabe, z. B. `2026-08-27T04:40:00`.
    public static func datum(_ text: String) -> Date {
        let teile = text.split(whereSeparator: { "-T:".contains($0) }).compactMap { Int($0) }
        var kalender = Calendar(identifier: .gregorian)
        kalender.timeZone = zone
        let komponenten = DateComponents(
            year: teile[0], month: teile[1], day: teile[2],
            hour: teile.count > 3 ? teile[3] : 0,
            minute: teile.count > 4 ? teile[4] : 0,
            second: teile.count > 5 ? teile[5] : 0
        )
        return kalender.date(from: komponenten) ?? Date(timeIntervalSince1970: 0)
    }
}

extension Anlagenbild {
    public static let beispiel = Anlagenbild(
        stand: Beispielzeit.datum("2026-08-28T18:04:05"),
        aussen: Aussenwerte(temperatur: 21.1, feuchte: 74.5, quelle: "RuuviTag am Verteiler Erdgeschoss", alter: 121),
        etagen: [Beispiel.keller, Beispiel.erdgeschoss, Beispiel.obergeschoss],
        kessel: Kessel(
            brennerLaeuft: false, brennerSeit: 103_769, abgas: 27.4, bezugslinie: 27.4,
            vorlauf: 27.9, ruecklauf: 29.7, laufzeitHeute: 0, startsHeute: 0, literHeute: 0,
            laufzeitGestern: 3430, startsGestern: 2, taktbetrieb: false, duese: 2.2,
            abgasAbstand: AbgasAbstand(
                ladungen: 5, reinigung: Date(timeIntervalSince1970: 1_786_966_055), jetzt: nil, bezug: nil
            )
        ),
        speicher: Speicher(
            temperatur: 53.0, korrektur: 2.6, ladung: 0.075, phase: "keine Ladung", phaseSeit: 103_729,
            warmwasserKnapp: false,
            voll: 62, leer: 52.3, leerGelernt: true, warngrenze: 40, hoechstwertLadung: 68.9,
            zapfungenHeute: 0, rueckstroemungen: 3
        ),
        heizkreise: [
            Heizkreis(
                nummer: 1, name: "Heizkreis 1", betriebsart: .automatik, pumpeLaeuft: true,
                grund: "Abnehmer vorhanden", bedarf: true, vorlauf: 29.4, ruecklauf: 34.1,
                versorgteVerteiler: ["Keller", "Erdgeschoss"],
                relais: Relais(adresse: "192.168.1.203", kanal: 1, weg: "HTTP", erreichbar: false, ein: false),
                nachlauf: 300, mindestlaufzeit: 180, mindestpause: 180, mindestSpeicher: 40, frostgrenze: 6
            ),
            Heizkreis(
                nummer: 2, name: "Heizkreis 2", betriebsart: .automatik, pumpeLaeuft: true,
                grund: "Abnehmer vorhanden", bedarf: true, vorlauf: 31.1, ruecklauf: 26.5,
                versorgteVerteiler: ["Obergeschoss"],
                relais: Relais(adresse: "192.168.1.170", kanal: 1, weg: "HTTP", erreichbar: false, ein: false),
                nachlauf: 300, mindestlaufzeit: 180, mindestpause: 180, mindestSpeicher: 40, frostgrenze: 6
            ),
        ],
        kesselkreispumpe: Pumpe(
            name: "Kesselkreispumpe", betriebsart: .automatik, laeuft: false,
            grund: "Rücklauf wärmer als Vorlauf",
            relais: Relais(adresse: "192.168.1.136", kanal: 1, weg: "HTTP", erreichbar: false, ein: false),
            einschaltschwelle: 3.0, ausschaltschwelle: 2.0, haltezeit: 120, notgrenze: 85
        ),
        befunde: Beispiel.befunde,
        geraete: Beispiel.geraete,
        tage: [
            Tagessatz(datum: Beispielzeit.datum("2026-08-28"), laufzeit: 0, starts: 0, liter: 0, heizgradtage: 0, aussenMin: 19.4, aussenMax: 26.2),
            Tagessatz(datum: Beispielzeit.datum("2026-08-27"), laufzeit: 3430, starts: 2, liter: 2.10, heizgradtage: 0, aussenMin: 14.8, aussenMax: 29.1),
            Tagessatz(datum: Beispielzeit.datum("2026-08-26"), laufzeit: 0, starts: 0, liter: 0, heizgradtage: 0, aussenMin: 13.8, aussenMax: 20.3),
            Tagessatz(datum: Beispielzeit.datum("2026-08-25"), laufzeit: 1647, starts: 1, liter: 1.01, heizgradtage: 0, aussenMin: 14.4, aussenMax: 14.6),
        ],
        ladungen: [
            Ladungssatz(
                beginn: Beispielzeit.datum("2026-08-27T04:45:00"), dauer: 4380, brenner: 3430, starts: 2,
                speicherVorher: 52.0, speicherNachher: 72.3, kesselVorlaufMax: 76.3, abgasMax: 84.1,
                aussenMittel: 15.0, liter: 2.10
            ),
            Ladungssatz(
                beginn: Beispielzeit.datum("2026-08-19T23:19:11"), dauer: 4320, brenner: 3175, starts: 1,
                speicherVorher: 51.4, speicherNachher: 68.5, kesselVorlaufMax: 82.9, abgasMax: 88.0,
                aussenMittel: nil, liter: 1.94
            ),
        ],
        verlauf: .beispiel,
        verbrauchslinie: Verbrauchslinie(
            gueltig: false, erfassteTage: 9,
            grund: "Die Heizgradtage streuen noch zu wenig. Die Linie entsteht ab 14 Tagen mit mindestens 3 K Spannweite, also in der Heizperiode.",
            steigung: 0, grundlast: 0, tage: []
        )
    )
}

extension Verbrauchslinie {
    /// Frei gewählte Werte, die zeigen, wie die Linie in der Heizperiode aussieht. Nicht gemessen.
    public static let winterbeispiel: Verbrauchslinie = {
        let heizgradtage: [Double] = [4.2, 5.1, 6.0, 6.8, 7.3, 7.9, 8.4, 8.8, 9.3, 9.9, 10.4, 10.8, 11.5, 12.1, 12.6, 13.0, 13.8, 14.4, 15.1, 15.9, 16.4, 17.2]
        let abweichung: [Double] = [0.2, -0.3, 0.1, 0.4, -0.2, 0.0, 0.3, -0.4, 2.9, 0.1, -0.1, 0.2, -0.3, 0.4, 0.0, -0.2, 0.3, -0.1, 0.2, -0.4, 0.1, 0.3]
        let steigung = 0.42
        let grundlast = 0.65
        let tage = heizgradtage.enumerated().map { i, g in
            Verbrauchslinie.Tag(
                nummer: i,
                heizgradtage: g,
                laufzeitStunden: grundlast + steigung * g + abweichung[i],
                auffaellig: abweichung[i] > 2
            )
        }
        return Verbrauchslinie(gueltig: true, erfassteTage: tage.count, grund: nil, steigung: steigung, grundlast: grundlast, tage: tage)
    }()
}

extension Messfahrt {
    /// Nachgebildete Messfahrt nach dem Ablauf in `app_calib.c`: schließen, drei Sekunden Pause,
    /// öffnen; an jeder Endlage steigt die Gegenspannung des blockierenden Motors an.
    public static let beispiel: Messfahrt = {
        let schritt = 0.1
        var werte: [Int] = []
        func fahrt(dauer: Double, grund: Double, anstieg: Double) {
            let proben = Int(dauer / schritt)
            for i in 0..<proben {
                let t = Double(i) * schritt
                let anlauf = t < 1.0 ? (1.0 - t) * 380 : 0
                let rauschen = sin(Double(werte.count) * 1.7) * 7 + sin(Double(werte.count) * 0.31) * 5
                let endlage = t > dauer - 0.8 ? anstieg : 0
                werte.append(Int(grund + anlauf + rauschen + endlage))
            }
        }
        fahrt(dauer: 37.8, grund: 152, anstieg: 285)
        werte.append(contentsOf: Array(repeating: 0, count: 30))
        let oeffnenAb = werte.count
        fahrt(dauer: 36.5, grund: 161, anstieg: 272)
        return Messfahrt(
            kanal: 4, raum: "Kinderzimmer EG", abtastung: schritt, werte: werte, oeffnenAb: oeffnenAb,
            vorschlag: Kalibriervorschlag(fahrzeitZu: 37.8, fahrzeitAuf: 36.5, maximal: 44.1, schwelle: 293, hysterese: 69)
        )
    }()
}

// MARK: - Verteiler

enum Beispiel {
    static func kanaele(_ belegung: [Int: String], kalibriert: Set<Int>, stellung: [Int: Double] = [:]) -> [Kanal] {
        (1...11).map { n in
            let k = kalibriert.contains(n)
            return Kanal(
                nummer: n, raum: belegung[n], gruppe: (n + 1) / 2, stellung: stellung[n] ?? 0,
                bekannt: true, kalibriert: k, handbetrieb: false, bewegung: .steht,
                gegenspannung: 0,
                fahrzeitAuf: k ? 36.0 + Double(n % 3) * 0.7 : 39,
                fahrzeitZu: k ? 37.2 + Double(n % 4) * 0.5 : 40,
                maximal: k ? 44.5 + Double(n % 2) : 45,
                sperrzeit: 2, schwelle: k ? 270 + (n * 7) % 40 : 190, hysterese: k ? 55 + (n * 3) % 20 : 30
            )
        }
    }

    static func raum(
        _ etage: String, _ geraet: String, _ nummer: Int, _ name: String, kanaele: [Int],
        betriebsart: Raum.Betriebsart = .heizen, soll: Double = 20, ist: Double?, feuchte: Double?,
        batterie: Int?, alter: Int?, thermometer: String?, zielstellung: Double = 0
    ) -> Raum {
        Raum(
            id: "\(geraet)/\(nummer)", nummer: nummer, name: name, etage: etage, betriebsart: betriebsart,
            soll: soll, ist: ist, feuchte: feuchte, batterie: batterie, messwertAlter: alter,
            thermometer: thermometer, kanaele: kanaele, zielstellung: zielstellung,
            naechstePruefung: 12 + nummer * 3, regelung: .vorgabe
        )
    }

    static let schutzfahrt = Schutzfahrt(wochentag: 6, stunde: 11, tageBis: 2, laeuft: false)

    static let keller = Etage(
        id: "fbh_3a91c4", name: "Keller",
        raeume: [
            raum("Keller", "fbh_3a91c4", 1, "Büro", kanaele: [1, 2], ist: 26.9, feuchte: 50.5, batterie: 93, alter: 520, thermometer: "ATC_4F2A19"),
            raum("Keller", "fbh_3a91c4", 2, "Waschraum", kanaele: [3], ist: nil, feuchte: nil, batterie: nil, alter: nil, thermometer: nil),
            raum("Keller", "fbh_3a91c4", 3, "Flur/WC", kanaele: [4], ist: nil, feuchte: nil, batterie: nil, alter: nil, thermometer: nil),
        ],
        kanaele: kanaele([1: "Büro", 2: "Büro", 3: "Waschraum", 4: "Flur/WC"], kalibriert: [1, 2, 4]),
        bedarf: false, schutzfahrt: schutzfahrt, traegtAussenfuehler: false
    )

    static let erdgeschoss: Etage = {
        var liste = kanaele(
            [1: "Küche", 2: "Küche", 3: "Küche", 4: "Kinderzimmer EG", 5: "Kinderzimmer EG", 6: "Flur", 7: "WC", 8: "Wohnzimmer", 9: "Wohnzimmer", 10: "Wohnzimmer"],
            kalibriert: [1, 2, 3, 5, 7, 9],
            stellung: [1: 0.4, 2: 0.4, 3: 0.4]
        )
        liste[9].handbetrieb = true
        liste[9].stellung = 1
        return Etage(
            id: "fbh_5d20e7", name: "Erdgeschoss",
            raeume: [
                raum("Erdgeschoss", "fbh_5d20e7", 1, "Küche", kanaele: [1, 2, 3], soll: 23.5, ist: 23.6, feuchte: 61, batterie: 84, alter: 3, thermometer: "ATC_61B0D3", zielstellung: 0.4),
                raum("Erdgeschoss", "fbh_5d20e7", 2, "Wohnzimmer", kanaele: [8, 9, 10], ist: 24.1, feuchte: 61, batterie: 0, alter: 6, thermometer: "ATC_2C9E47"),
                raum("Erdgeschoss", "fbh_5d20e7", 3, "Kinderzimmer EG", kanaele: [4, 5], ist: 23.1, feuchte: 63.4, batterie: 74, alter: 96, thermometer: "ATC_83A15F"),
                raum("Erdgeschoss", "fbh_5d20e7", 4, "Flur", kanaele: [6], betriebsart: .aus, ist: 23.4, feuchte: 62, batterie: 57, alter: 16, thermometer: "ATC_0D7C22"),
                raum("Erdgeschoss", "fbh_5d20e7", 5, "WC", kanaele: [7], ist: 23.1, feuchte: 63.5, batterie: 85, alter: 24, thermometer: "ATC_B4E918"),
            ],
            kanaele: liste, bedarf: true, schutzfahrt: schutzfahrt, traegtAussenfuehler: true,
            messfahrt: .beispiel
        )
    }()

    static let obergeschoss: Etage = {
        var liste = kanaele(
            [1: "Kinderzimmer", 2: "Kinderzimmer", 3: "Bad", 4: "Bad", 5: "Schlafzimmer", 7: "Schlafzimmer", 8: "Schlafzimmer"],
            kalibriert: [1, 2, 3, 4, 5, 7, 8],
            stellung: [3: 0.8, 4: 0.62]
        )
        liste[3].bewegung = .oeffnet
        liste[3].gegenspannung = 164
        liste[2].gegenspannung = 164
        return Etage(
            id: "fbh_7b44f1", name: "Obergeschoss",
            raeume: [
                raum("Obergeschoss", "fbh_7b44f1", 1, "Kinderzimmer", kanaele: [1, 2], ist: 23.1, feuchte: 59.9, batterie: 92, alter: 94, thermometer: "ATC_9F1E60"),
                raum("Obergeschoss", "fbh_7b44f1", 2, "Bad", kanaele: [3, 4], soll: 24, ist: 23.5, feuchte: 62, batterie: 60, alter: 14, thermometer: "ATC_47D2A8", zielstellung: 0.8),
                raum("Obergeschoss", "fbh_7b44f1", 3, "Schlafzimmer", kanaele: [5, 7, 8], soll: 18, ist: 23.4, feuchte: 62, batterie: 84, alter: 145, thermometer: "ATC_E1305B"),
            ],
            kanaele: liste, bedarf: true, schutzfahrt: schutzfahrt, traegtAussenfuehler: false
        )
    }()

    // MARK: Befunde

    static let befunde: [Befund] = [
        Befund(
            id: "relais", schwere: .stoerung, titel: "Relais der Pumpen nicht erreichbar",
            text: "Die Tasmota-Relais unter 192.168.1.203, .170 und .136 antworten nicht. Die Pumpen folgen der Steuerung deshalb nicht. Bleibt das Lebenszeichen aus, schaltet die Ausfallregel im Relais die Pumpe nach 15 Minuten ein.",
            ort: "Heizkreis 1, Heizkreis 2, Kesselkreispumpe", quelle: "Pufferspeicher, Kessel"
        ),
        Befund(
            id: "backflow", schwere: .warnung, titel: "Warmes Wasser strömt in den Kesselrücklauf",
            text: "Der Kesselrücklauf stieg dreimal ohne Brenner und ohne Pumpe, zuletzt um 3,2 K. Übliche Ursache ist eine fehlende oder undichte Schwerkraftbremse im Kesselkreis.",
            ort: "Kesselkreis", quelle: "Kessel"
        ),
        Befund(
            id: "hk1_vertauscht", schwere: .hinweis, titel: "Vor- und Rücklauf von Heizkreis 1 vermutlich vertauscht",
            text: "Der Vorlauf (29,4 °C) ist kälter als der Rücklauf (34,1 °C). Die Firmware prüft das erst bei laufender Pumpe; bis dahin fällt es nicht als Befund auf.",
            ort: "Heizkreis 1", quelle: "App"
        ),
        Befund(
            id: "voll_zu_niedrig", schwere: .hinweis, titel: "„Speicher voll“ liegt unter dem Ladungsende",
            text: "Eingestellt sind 62 °C, die Ladekalibrierung misst als Höchstwert 68,9 °C. Oberhalb von 62 °C unterscheidet der Ladezustand nicht mehr.",
            ort: "Pufferspeicher", quelle: "App"
        ),
        Befund(
            id: "batterie_wohnzimmer", schwere: .hinweis, titel: "Batterie des Thermometers im Wohnzimmer leer",
            text: "Das Thermometer meldet 0 %. Fällt es aus, wird das Wohnzimmer nicht mehr geregelt.",
            ort: "Wohnzimmer", quelle: "Erdgeschoss"
        ),
        Befund(
            id: "unkalibriert", schwere: .hinweis, titel: "5 belegte Kanäle ohne Messfahrt",
            text: "Keller 3; Erdgeschoss 4, 6, 8, 10. Ohne Messfahrt erkennen sie die Endlage nicht und laufen bis zur Maximallaufzeit.",
            ort: "Keller, Erdgeschoss", quelle: "App"
        ),
        Befund(
            id: "ohne_thermometer", schwere: .hinweis, titel: "2 Räume ohne Thermometer",
            text: "Waschraum und Flur/WC im Keller haben kein Thermometer und werden deshalb nicht geregelt.",
            ort: "Keller", quelle: "Keller"
        ),
        Befund(
            id: "bordfuehler_og", schwere: .hinweis, titel: "Bordfühler im Obergeschoss ungültig",
            text: "Der Fühler im Gehäuse meldet 99,99 % Feuchte. Er dient nur der Anzeige, die Regelung ist nicht betroffen.",
            ort: "Obergeschoss", quelle: "Obergeschoss"
        ),
    ]

    // MARK: Geräte

    static let firmwareVerteiler = "v0.3.0-9-g3bbec6f"
    static let firmwareHeizung = "v0.3.0-12-gd4827b4"

    static func tasten(_ roh: [Int]) -> [Taste] {
        [
            Taste(name: "Sollwert senken", rohwert: roh[0], schwelle: 1000),
            Taste(name: "Sollwert erhöhen", rohwert: roh[1], schwelle: 870),
            Taste(name: "Raum wählen", rohwert: roh[2], schwelle: 1000),
        ]
    }

    static func thermometer(_ mac: String, _ name: String, _ t: Double, _ f: Double, _ b: Int?, _ mv: Int, _ rssi: Int, _ zu: String?, format: String = "pvvx") -> Funkthermometer {
        Funkthermometer(mac: mac, name: name, temperatur: t, feuchte: f, batterie: b, batterieMillivolt: mv, signal: rssi, format: format, zuordnung: zu)
    }

    static let geraete: [Geraet] = [
        Geraet(
            id: "heiz_19c8a2", art: .kessel, ort: "Kessel", adresse: "192.168.1.179", hostname: "heizung-kessel",
            firmware: firmwareHeizung, signal: -58, laufzeit: 103_771, erreichbar: true,
            fuehler: [
                Fuehler(rom: "7101A4F5E26C3A28", rolle: "kessel_vl", rollenname: "Kessel Vorlauf", wert: 27.94, aenderung30s: 0, korrektur: 0, messungen: 10_378, fehler: 0),
                Fuehler(rom: "8401C2E79B1D5A28", rolle: "kessel_rl", rollenname: "Kessel Rücklauf", wert: 29.69, aenderung30s: 0, korrektur: 0, messungen: 10_378, fehler: 0),
                Fuehler(rom: "81014D7A0E93BC28", rolle: "abgas", rollenname: "Abgas", wert: 27.44, aenderung30s: 0, korrektur: 0, messungen: 10_378, fehler: 0),
            ],
            bus: Einwirebus(anschluesse: [13], gefunden: 3, zugeordnet: 3, rundeMillisekunden: 40, abfrageSekunden: 10)
        ),
        Geraet(
            id: "heiz_6e03b5", art: .speicher, ort: "Pufferspeicher", adresse: "192.168.1.51", hostname: "heizung",
            firmware: firmwareHeizung, signal: -45, laufzeit: 103_745, erreichbar: true,
            fuehler: [
                Fuehler(rom: "0E01D3A71C9F2B28", rolle: "puffer", rollenname: "Pufferspeicher", wert: 53.04, aenderung30s: 0, korrektur: 2.6, messungen: 10_375, fehler: 0),
                Fuehler(rom: "4201B85E03D7E428", rolle: "hk1_vl", rollenname: "Heizkreis 1 Vorlauf", wert: 29.44, aenderung30s: -0.06, korrektur: 0, messungen: 10_375, fehler: 0),
                Fuehler(rom: "7B01F0269A4C8D28", rolle: "hk1_rl", rollenname: "Heizkreis 1 Rücklauf", wert: 34.06, aenderung30s: -0.06, korrektur: 0, messungen: 10_375, fehler: 0),
                Fuehler(rom: "AB0167C3E5B81F28", rolle: "hk2_vl", rollenname: "Heizkreis 2 Vorlauf", wert: 31.12, aenderung30s: 0, korrektur: 0, messungen: 10_375, fehler: 0),
                Fuehler(rom: "D101395DA84F6C28", rolle: "hk2_rl", rollenname: "Heizkreis 2 Rücklauf", wert: 26.50, aenderung30s: 0, korrektur: 0, messungen: 10_375, fehler: 0),
            ],
            bus: Einwirebus(anschluesse: [13], gefunden: 5, zugeordnet: 5, rundeMillisekunden: 66, abfrageSekunden: 10)
        ),
        Geraet(
            id: "fbh_3a91c4", art: .verteiler, ort: "Keller", adresse: "192.168.1.250", hostname: "floor-heating-test",
            firmware: firmwareVerteiler, signal: -55, laufzeit: 28_989, erreichbar: true,
            funkthermometer: [
                thermometer("A4:C1:38:4F:2A:19", "ATC_4F2A19", 26.9, 50.5, 93, 2980, -71, "Büro"),
                thermometer("A4:C1:38:61:B0:D3", "ATC_61B0D3", 23.6, 61.0, 84, 2910, -88, nil),
            ],
            bordfuehler: Bordfuehler(gueltig: true, temperatur: 26.1, feuchte: 57.8, vorlauffuehler: [23.1, 23.1]),
            tasten: tasten([152, 148, 171])
        ),
        Geraet(
            id: "fbh_5d20e7", art: .verteiler, ort: "Erdgeschoss", adresse: "192.168.1.213", hostname: "floor-heating-erdgeschoss",
            firmware: firmwareVerteiler, signal: -42, laufzeit: 28_989, erreichbar: true,
            funkthermometer: [
                thermometer("A4:C1:38:61:B0:D3", "ATC_61B0D3", 23.6, 61.0, 84, 2910, -62, "Küche"),
                thermometer("A4:C1:38:2C:9E:47", "ATC_2C9E47", 24.1, 61.0, 0, 2210, -66, "Wohnzimmer"),
                thermometer("A4:C1:38:83:A1:5F", "ATC_83A15F", 23.1, 63.4, 74, 2860, -74, "Kinderzimmer EG"),
                thermometer("A4:C1:38:0D:7C:22", "ATC_0D7C22", 23.4, 62.0, 57, 2740, -69, "Flur"),
                thermometer("A4:C1:38:B4:E9:18", "ATC_B4E918", 23.1, 63.5, 85, 2920, -77, "WC"),
                thermometer("D6:3F:21:5A:3C:8E", "Ruuvi 5A3C", 21.1, 74.5, nil, 2394, -81, "Außenfühler", format: "ruuvi"),
            ],
            bordfuehler: Bordfuehler(gueltig: true, temperatur: 30.6, feuchte: 48.2, vorlauffuehler: []),
            tasten: tasten([163, 140, 178])
        ),
        Geraet(
            id: "fbh_7b44f1", art: .verteiler, ort: "Obergeschoss", adresse: "192.168.1.46", hostname: "floor-heating-obergeschoss",
            firmware: firmwareVerteiler, signal: -65, laufzeit: 28_990, erreichbar: true,
            funkthermometer: [
                thermometer("A4:C1:38:9F:1E:60", "ATC_9F1E60", 23.1, 59.9, 92, 2970, -64, "Kinderzimmer"),
                thermometer("A4:C1:38:47:D2:A8", "ATC_47D2A8", 23.5, 62.0, 60, 2770, -70, "Bad"),
                thermometer("A4:C1:38:E1:30:5B", "ATC_E1305B", 23.4, 62.0, 84, 2900, -73, "Schlafzimmer"),
            ],
            bordfuehler: Bordfuehler(gueltig: false, temperatur: 27.6, feuchte: 99.99, vorlauffuehler: []),
            tasten: tasten([158, 0, 166])
        ),
        Geraet(id: "relais_hk1", art: .relais, ort: "Relais Heizkreis 1", adresse: "192.168.1.203", hostname: "tasmota", firmware: "Tasmota", signal: nil, laufzeit: 0, erreichbar: false),
        Geraet(id: "relais_hk2", art: .relais, ort: "Relais Heizkreis 2", adresse: "192.168.1.170", hostname: "tasmota", firmware: "Tasmota", signal: nil, laufzeit: 0, erreichbar: false),
        Geraet(id: "relais_kkp", art: .relais, ort: "Relais Kesselkreispumpe", adresse: "192.168.1.136", hostname: "tasmota", firmware: "Tasmota", signal: nil, laufzeit: 0, erreichbar: false),
    ]
}
