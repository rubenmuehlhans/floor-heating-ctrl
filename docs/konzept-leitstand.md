# Konzept: Leitstand für Funk, Protokoll, Anzeige und HomeKit

Ein eigenes Gerät in der Nähe von Kessel und Speicher übernimmt vier Aufgaben, für die in den
Regelgeräten kein Platz ist: Es empfängt Funkthermometer, protokolliert alle Messwerte der Anlage
lückenlos auf eine SD-Karte, zeigt den Zustand der Anlage auf dem eigenen Bildschirm an und macht
die Räume in Apple Home bedienbar. Die Apps auf Mac, iPhone und iPad beziehen ihren Verlauf
künftig vom Leitstand. Damit liegt auf jedem Apple-Gerät derselbe vollständige Verlauf vor, auch
als Grundlage für Auswertungen mit dem Assistenten.

Das Papier erweitert den Entwurf einer reinen Funkbrücke (AtomS3 Lite) um Protokoll und Anzeige.

## Ausgangslage

### Wie heute aufgezeichnet wird

| Quelle | Inhalt | Raster | Vorrat |
|---|---|---|---|
| App, je Apple-Gerät | Fühler, Räume, Ventile, Pumpen, Brenner | 5 min | unbegrenzt, aber nur für die Zeit, in der die App abfragt |
| Heizungsgeräte, Verlauf | eigene Fühler | 2 min | 24 h im Arbeitsspeicher |
| Heizungsgeräte, scharfe Aufzeichnung | eigene Fühler | 5 s | 2 h 13 min im Arbeitsspeicher, nur auf Anforderung |
| Heizungsgeräte, Ladungs- und Tagesprotokoll | Kennzahlen je Ladung und Tag | je Ladung, je Tag | 64 Ladungen und 365 Tage im NVS |
| Verteiler | — | — | nur Momentwerte |
| Mitschnitt am Rechner (`messungen/mitschnitt.py`) | vollständiger Zustand aller Geräte | 30 s | solange der Rechner wach ist |

### Was daran fehlt

- **Keine Quelle ist lückenlos.** Die App zeichnet nur auf, während sie abfragt. Der Verlauf der
  Heizungsgeräte ergänzt 24 Stunden, und das nur für deren eigene Fühler. Räume und Ventile haben
  außerhalb der App-Laufzeit keinen Verlauf.
- **Jedes Apple-Gerät hat einen anderen Verlauf.** Die App speichert nur lokal; die Einstellungen
  sagen es: „Den Verlauf speichert die App nur auf diesem Gerät".
- **Der Mitschnitt am Rechner ist unvollständig.** Er lief vom 25. bis 28. August, 2 Tage und
  19 Stunden im Takt von 30 s. Er enthält 24 Lücken von mehr als zehn Minuten, die längste
  4,1 Stunden, dazu 349 Abtastungen (6,2 %), in denen alle fünf Geräte zugleich nicht erreichbar
  waren; in diesen Fällen hatte der Rechner selbst keine Verbindung. Von den im Zeitraum zu
  erwartenden Abtastungen liegen rund zwei Drittel vor.
- **Ereignisse werden nirgends festgehalten**: Neustarts, Verbindungsausfälle, Versionswechsel,
  Befunde, Sollwertänderungen. Für die zuletzt nicht aufgeklärten Neustarts fehlten genau diese
  Angaben: freier Speicher und Funksignal in den Minuten davor.

### Warum ein eigenes Gerät

- **Funk in Kesselnähe.** Das Außenthermometer, ein Climate-Sat von camperSense, sitzt außer
  Reichweite der Verteiler. Die Heizungsgeräte können kein Bluetooth aufnehmen: Mit dem
  Bluetooth-Controller überschreitet ihre Firmware den Datenspeicher um 29 848 Byte.
- **Aufzeichnung unabhängig von Rechnern und Apps.** Ein Rechner geht in den Ruhezustand, eine
  App läuft nicht dauernd.
- **Keine zusätzliche Last in den Regelgeräten.** Protokoll, Anzeige und HomeKit laufen auf
  einem Gerät, das nichts regelt.

## Aufgaben und Abgrenzung

| Aufgabe | Inhalt |
|---|---|
| Funk | BTHome- und Xiaomi-Thermometer empfangen, verschlüsselte mit hinterlegtem Schlüssel; Außentemperatur an die Heizungsgeräte liefern |
| Protokoll | alle Geräte in festem Takt abfragen; Messwerte, Ereignisse und vollständige Zustände auf die SD-Karte schreiben |
| Anzeige | Zustand der Anlage auf dem eigenen Bildschirm |
| HomeKit | je Raum ein Thermostat, dazu Fühler für Außen und Speicher |

Der Leitstand regelt nichts. Er liest, zeichnet auf und reicht weiter. Schreibend greift er nur
über HomeKit auf Sollwert und Betriebsart der Räume zu, über dieselben Endpunkte wie die App.

Ein Ausfall des Leitstands berührt die Regelung nicht. Das ergibt sich aus der Firmware der
Heizungsgeräte: Die Pumpensteuerung fragt nur die Verteiler ab, die einem Heizkreis zugeordnet
sind (`poll_peers` in `apps/heatsource/main/app_pumps.c`), und die Außentemperatur geht dort in
keine Regelung ein, sie wird aufgezeichnet. Fällt der Leitstand aus, übernehmen die
Heizungsgeräte die Außentemperatur wie bisher von einem Verteiler mit Außenfühler, sofern einer
zugeordnet ist.

## Gerät

### Das vorhandene Gerät

Am 23. September über USB ausgelesen, ausschließlich lesend:

| Merkmal | Befund |
|---|---|
| Modell | M5Stack Core (Basic oder Gray); M5GFX erkennt „M5Stack", keine Berührungsfläche |
| Prozessor | ESP32-D0WDQ6-V3, Revision 3.1, zwei Kerne |
| Flash | 16 MB |
| PSRAM | keiner („PSRAM chip not found") |
| Echtzeituhr | keine |
| Bedienung | drei Tasten unter dem Bildschirm |
| Anzeige | 320 × 240 über SPI; der microSD-Steckplatz liegt am selben Bus |
| Aufgespielt | camperSense-Panel 0.8.10 mit ESP-IDF 5.3.2, M5GFX und NimBLE |

Die Panel-Firmware meldet beim Start den freien internen Speicher auf genau diesem Gerät:

| Zeitpunkt | frei |
|---|---|
| nach dem Start | 165 596 Byte |
| nach Aufbau der Oberfläche | 130 972 Byte |
| nach Start von Bluetooth mit Beobachter-, Zentral- und Peripherierolle | 45 444 Byte |

Zum Vergleich die Verteiler mit WLAN, Bluetooth als reinem Beobachter, Webserver und kleiner
Anzeige, gemessen im Mitschnitt vom August: im Median 53 KB frei, im Tiefstwert 38 KB.

### Bewertung

- **Funk, Protokoll und Anzeige** sind auf dem vorhandenen Gerät voraussichtlich tragbar. Die
  Verteiler zeigen, dass WLAN, Bluetooth-Beobachter und Webserver ohne PSRAM rund 50 KB frei
  lassen. SD-Karte, Abfrage der Geräte und die größere Anzeige kosten nach Schätzung 25 bis
  35 KB. Das ist knapp; Etappe 1 misst es, bevor darauf aufgebaut wird.
- **HomeKit zusätzlich** ist ohne PSRAM nicht tragbar. Kopplung und verschlüsselte Sitzungen
  brauchen Speicher, der nach den übrigen Aufgaben nicht mehr frei ist.
- **Ohne Echtzeituhr** hat das Gerät nach einem Stromausfall keine Uhrzeit, bis das Netz sie
  liefert. Das Protokoll berücksichtigt das (siehe Zeitbasis).
- **Ohne PSRAM** fehlt der Speicher für eine Kompression auf dem Gerät; gzip braucht mit den
  üblichen Einstellungen rund 256 KB. Das Dateiformat ist deshalb so gewählt, dass es auch
  unkomprimiert klein bleibt.

### Zielgerät

Für alle vier Aufgaben ist ein Gerät mit PSRAM nötig. Entschieden ist der **M5Stack Core2**
(Herstellerangaben):

| Merkmal | Core2 |
|---|---|
| Prozessor | ESP32-D0WDQ6-V3, zwei Kerne, 240 MHz, derselbe Baustein wie am vorhandenen Gerät |
| Speicher | 16 MB Flash, 8 MB PSRAM |
| Anzeige | 320 × 240, kapazitive Berührungsfläche mit drei Tastenfeldern darunter |
| Echtzeituhr | BM8563 |
| Stromversorgung | AXP192, in neueren Fassungen AXP2101; Akku 500 mAh |
| microSD | am Bus der Anzeige |

Der Akku überbrückt einen Stromausfall, bis die Dateien geschlossen sind; die Echtzeituhr hält
die Zeit über den Ausfall. Da der Core2 denselben Prozessor trägt, läuft dieselbe Firmware ohne
eigenen Bauauftrag; M5Unified erkennt das Gerät beim Start und bedient Leistungsbaustein,
Berührungsfelder und Echtzeituhr. Das vorhandene Gerät dient bis zur Beschaffung als Entwicklungs-
und Übergangsgerät.

## Protokoll

### Grundsätze

1. **Vollständig.** Jedes Gerät wird in festem Takt abgefragt, unabhängig davon, ob eine App
   läuft. Aufgezeichnet werden alle Messgrößen des Katalogs, dazu Ereignisse und in größerem
   Abstand der vollständige Zustand jedes Geräts.
2. **Einheitlich.** Ein Katalog der Messgrößen für Leitstand und App, eine Zeitbasis (UTC), ein
   Dateiformat je Inhalt.
3. **Offen lesbar.** CSV und JSON Lines, dokumentiert und ohne eigenes Programm lesbar: in einer
   Tabellenkalkulation, in Python, in KI-Werkzeugen.
4. **Maßgeblich ist die Karte.** Die Apps halten Abschriften. Fehlt dort etwas, holt die App es
   beim Leitstand nach.

### Abtastung

| Gerät | Endpunkt | Takt |
|---|---|---|
| Heizungsgeräte | `/api/state` | 30 s; Ziel sind 10 s, der Messtakt der 1-Wire-Fühler |
| Verteiler | `/api/state` mit `If-None-Match`, dazu `/api/demand` | 30 s |
| eigene Funkthermometer | Empfang | Mittelwert über 30 s |
| Leitstand selbst | — | 30 s |

Die Verteiler geben mit `/api/state` einen Änderungszähler als ETag aus; ein unveränderter
Zustand kostet eine Antwort ohne Inhalt. Zusammen sind das rund 0,3 Abfragen je Sekunde, mit
10 s für die Heizungsgeräte 0,4. Ob die
Heizungsgeräte diese Last ohne Einbußen tragen, prüft Etappe 2 am Tiefstwert ihres freien
Speichers und ihrem Neustartgrund. Im Mitschnitt lag der Tiefstwert bei 24,7 KB am Kessel und
21,5 KB am Speicher. Bis dahin bleibt es bei 30 s: Am 23. September starteten Kessel und Speicher
gegen 6 Uhr ohne bekannten Grund neu, nachdem der Leitstand sie gut vier Stunden im
Zehnsekundentakt abgefragt hatte. Ein Zusammenhang ist nicht belegt, aber auch nicht
ausgeschlossen. Tragen die Geräte 10 s nicht, wird der Vollzustand nur alle 30 s abgefragt und
im Zehnsekundentakt nur `/api/measurements`.

### Messgrößen

Grundlage ist der Katalog, den die App bereits verwendet (`Messgroesse` in
`apple/Packages/Verlauf/Sources/Verlauf/Abtastung.swift`). Die Schlüssel bleiben unverändert,
damit App und Leitstand in dieselben Reihen schreiben. Ergänzt wird, was für die Auswertung
fehlt.

| Schlüssel | Gerät | Bedeutung | Einheit |
|---|---|---|---|
| `raum.<n>.ist`, `.soll`, `.stellung`, `.feuchte` | Verteiler | wie bisher | °C, °C, Anteil, % |
| `raum.<n>.betrieb` | Verteiler | Betriebsart | Kennzahl |
| `raum.<n>.batterie` | Verteiler | Batterie des Raumthermometers | % |
| `kanal.<n>.stellung`, `vorlauf.<i>`, `aussen` | Verteiler | wie bisher | Anteil, °C, °C |
| `bedarf`, `raeume.rufend`, `kanaele.offen` | Verteiler | Bedarfsmeldung an die Heizungsgeräte | 0/1, Anzahl, Anzahl |
| `fuehler.<rolle>`, `brenner`, `fuellstand`, `pumpe.<id>`, `kkp` | Heizungsgerät | wie bisher | °C, Anteil |
| `brenner.starts`, `brenner.laufzeit`, `brenner.oel` | Heizungsgerät | Stände des laufenden Tages | Anzahl, s, l (Schätzung) |
| `abgas.bezug` | Heizungsgerät | Bezugslinie der Brennererkennung | °C |
| `ladung.phase`, `ladung.spreizung` | Heizungsgerät | Ladephase, Spreizung am Kessel | Kennzahl, K |
| `kreis.<id>.bedarf`, `kreis.<id>.veraltet` | Heizungsgerät | Bedarf je Kreis; Bedarfsquelle antwortet nicht mehr | 0/1 |
| `befunde` | alle | offene Befunde | Anzahl |
| `funk.<adresse>.temp`, `.feuchte`, `.batterie`, `.rssi` | Leitstand | eigene Funkthermometer | °C, %, %, dBm |
| `geraet.heap`, `geraet.rssi`, `geraet.laufzeit` | alle | Zustand des Geräts | Byte, dBm, s |

Die Kennzahlen für Betriebsart und Ladephase legt der Katalog fest, der als eigenes Dokument
neben dem Handbuch entsteht.

Der Katalog wird zweimal umgesetzt, in C im Leitstand und in Swift in der App. Damit beide
Umsetzungen gleich bleiben, prüfen beide gegen dieselbe Vorlage: einen anonymisierten Ausschnitt
aus dem Mitschnitt vom August als Eingabe und die erwarteten Messwerte als Ergebnis.

### Dateien auf der Karte

Je Tag, gezählt in UTC, ein Verzeichnis:

```
/protokoll/2026/2026-09-23/
    geraete.json                Kennung, Ort, Art und Version je Gerät
    <kennung>.csv               Messwerte im Takt der Abtastung
    <kennung>.5min.csv          Mittelwerte je fünf Minuten
    ereignisse.jsonl            Ereignisse aller Geräte
    zustaende.jsonl             vollständige Zustände, alle 15 Minuten und bei Versionswechsel
    protokolle/<kennung>.csv    Ladungs- und Tagesprotokoll, einmal täglich übernommen
```

- **Messwerte:** CSV nach RFC 4180, Komma als Trennzeichen, Punkt als Dezimalzeichen, UTF-8.
  Erste Spalte `zeit` in ISO 8601 (UTC), danach die Schlüssel des Katalogs. Ein fehlender Wert
  bleibt leer. Kommt im Laufe des Tages eine Messgröße hinzu, beginnt eine neue Datei
  `<kennung>.2.csv` mit erweiterter Kopfzeile.
- **Fünfminutenmittel:** dasselbe Format im Raster der App, Plätze zu 300 s in UTC. Ein
  Schaltzustand wird zum Anteil: Lief der Brenner drei von fünf Minuten, steht dort 0,6. Diese
  Dateien sind die Grundlage für den Abgleich mit den Apps.
- **Zustände:** im Format von `messungen/verlauf.jsonl`, je Zeile `epoch` und der Zustand eines
  Geräts unter seiner Kennung. Der vorhandene Mitschnittimport der App liest sie ohne Änderung.
  Sie sind verlustfrei; auch Felder, die der Katalog nicht kennt, bleiben erhalten.
- **Ereignisse:** je Zeile ein JSON-Objekt mit `zeit`, `geraet`, `art` und den Einzelheiten.

Umfang, hochgerechnet aus dem Mitschnitt:

| Inhalt | je Tag |
|---|---|
| Messwerte der drei Verteiler, je rund 45 Spalten, Takt 30 s | 2,5 MB |
| Messwerte der Heizungsgeräte, je rund 15 Spalten, Takt 10 s | 2,0 MB |
| Zustände der fünf Geräte, alle 15 Minuten | 1,6 MB |
| Fünfminutenmittel, Ereignisse, Funk | 0,5 MB |
| zusammen | rund 7 MB, 2,5 GB im Jahr |

Eine Karte mit 16 GB fasst damit mehr als fünf Jahre. Der vollständige Zustand im Takt von 30 s,
wie im bisherigen Mitschnitt, ergäbe 50 MB am Tag; mit gzip wären es 1,6 MB, aber die Kompression
braucht PSRAM. Auf dem Zielgerät lassen sich abgeschlossene Tage nachträglich komprimieren;
notwendig ist das nicht.

### Schreiben und Stromausfall

- Neue Zeilen werden im Arbeitsspeicher gesammelt und alle 60 Sekunden angehängt: Datei öffnen,
  schreiben, schließen. Zwischen zwei Schreibvorgängen ist keine Datei geöffnet.
- Ein Stromausfall kostet höchstens die Daten der letzten Minute. Eine unvollständige letzte
  Zeile wird beim Lesen übergangen; die Leseprogramme von Leitstand und App prüfen die
  Spaltenzahl.
- Auf dem Zielgerät meldet der Leistungsbaustein den Wegfall der Versorgung. Der Leitstand schreibt dann
  sofort und setzt das Schreiben aus, bis die Versorgung wieder anliegt.
- Beim Start prüft der Leitstand die Karte: Einhängen, freier Platz, Schreibprobe. Fehler
  erscheinen auf der Anzeige und als Befund.
- Unterschreitet der freie Platz 10 %, werden zuerst die ältesten Zustände gelöscht, danach die
  ältesten Messwerte im Takt der Abtastung. Fünfminutenmittel und Ereignisse bleiben erhalten;
  zusammen belegen sie weniger als 150 MB im Jahr.
- Karte: Ausführung für Dauerbetrieb („High Endurance" oder Industrieausführung).

### Zeitbasis

Alle Zeiten in UTC. Ohne gültige Zeit wird nicht in die Dateien geschrieben.

- Zeitquelle ist NTP: die öffentlichen Server wie bisher, zusätzlich der Router, sofern er Zeit
  anbietet. Damit ist die Zeit auch bei gestörter Internetverbindung verfügbar.
- Auf dem vorhandenen Gerät ohne Echtzeituhr werden Abtastungen vor der ersten
  Zeitsynchronisation mit ihrer Laufzeit im Arbeitsspeicher gehalten, höchstens 30 Minuten, und
  nach der Synchronisation mit der dann bekannten Zeit geschrieben. Dauert es länger, vermerkt
  das Ereignisprotokoll die Lücke.
- Auf dem Zielgerät hält die Echtzeituhr die Zeit über den Ausfall; NTP stellt sie nach.

### Ereignisse

| Art | Erkannt an |
|---|---|
| `neustart` | Laufzeit des Geräts kleiner als bei der vorigen Abfrage; dazu der Neustartgrund |
| `nicht_erreichbar`, `erreichbar` | drei Abfragen in Folge ohne Antwort; bei Wiederkehr die Dauer |
| `version` | andere Firmwareversion |
| `sollwert`, `betriebsart` | Änderung je Raum, mit altem und neuem Wert |
| `brenner`, `pumpe` | Ein- und Ausschalten, mit Dauer des vorigen Zustands |
| `befund` | Befund neu oder erledigt, mit Kennung und Text |
| `funk` | Thermometer verloren oder wieder empfangen, Schlüssel falsch |
| `leitstand` | Start, Zeit gültig, Karte eingehängt oder voll, Schreibfehler, Abgleich durch eine App |

Den Neustartgrund melden Verteiler und Heizungsgeräte heute nicht. Beide Firmwares erhalten dafür
ein Feld `reset_reason` in `/api/state` (Etappe 2).

## Abruf und Abgleich

### Schnittstelle des Leitstands

| Endpunkt | Inhalt |
|---|---|
| `GET /api/state` | eigener Zustand mit Abschnitt `log`: Karte, freier Platz, heute geschrieben, letzte Schreibung, Zeit gültig |
| `GET /api/log/days` | Tage mit ihren Dateien und Größen |
| `GET /log/<tag>/<datei>` | eine Datei; mit `Range` ab einer Stelle, da alle Dateien nur wachsen |
| `GET /api/log/series` | ausgewählte Messgrößen eines Zeitraums in wählbarem Raster, für Diagramme und den Assistenten |
| `GET /api/log/events` | Ereignisse eines Zeitraums |
| `GET /api/ble`, `POST /api/ble/key` | Funkthermometer und Schlüssel, wie am Verteiler |
| `GET /api/demand` | Außentemperatur mit Alter (`outdoor_c`, `outdoor_age_s`) für die Heizungsgeräte |

Der Leitstand meldet sich per mDNS unter `_fbhctrl._tcp` mit der neuen Rolle `station`. Die
Heizungsgeräte übernehmen die Außentemperatur bevorzugt von dieser Rolle und erst danach wie
bisher von einem Verteiler. Das ist eine zusätzliche Bedingung in `aussen_holen`
(`apps/heatsource/main/app_remote.c`).

### Abgleich in der App

- Die App führt den Leitstand als eigene Geräteart.
- Beim Verbinden holt sie die Fünfminutenmittel aller Tage, die ihr fehlen, und vom laufenden Tag
  den Teil ab ihrer letzten Stelle. Sie schreibt sie in den vorhandenen Verlaufsspeicher. Werte
  des Leitstands haben Vorrang vor der eigenen Aufzeichnung, weil sie den ganzen Platz abdecken.
- Ereignisse kommen in eine eigene Ablage und erscheinen in den Verlaufsdiagrammen als
  Markierungen.
- Ist kein Leitstand erreichbar, zeichnet die App wie bisher selbst auf.
- Umfang: rund 0,3 MB Fünfminutenmittel je Tag, rund 110 MB je Jahr. Der erste Abgleich eines
  Jahres dauert wenige Minuten, jeder weitere Sekunden.

### Alle Geräte, auch außer Haus

- Im Heimnetz gleicht jedes Apple-Gerät unmittelbar mit dem Leitstand ab.
- Über VPN ist der Leitstand wie die übrigen Geräte mit einer von Hand eingegebenen Adresse
  erreichbar.
- Soll der Verlauf auch ohne Verbindung ins Heimnetz aktuell sein, ist ein Abgleich über iCloud
  nötig (Etappe 6): Ein dauernd laufender Mac gleicht mit dem Leitstand ab und legt die
  Tagesblöcke in der privaten iCloud-Datenbank ab, iPhone und iPad lesen sie von dort. Die
  heutige Ablage lässt sich dafür nicht unmittelbar spiegeln, weil CloudKit keine eindeutigen
  Attribute zulässt (`Messblock.kennung`). Vorgesehen ist ein eigener Abgleich der Tagesblöcke
  mit `CKSyncEngine`.

## Auswertung mit dem Assistenten

Der Assistent der App liest den Verlauf heute über das Werkzeug `verlauf`: bis 720 Stunden, im
Fünfminutenraster, aus der lokalen Ablage. Nach dem Abgleich enthält diese Ablage den
vollständigen Verlauf, und die vorhandenen Werkzeuge liefern ohne Änderung vollständige
Ergebnisse. Ergänzt werden:

| Werkzeug | Inhalt |
|---|---|
| `verlauf` | auch Zeiträume über 720 Stunden, dann in Tagesmitteln |
| `ereignisse` (neu) | Ereignisse eines Zeitraums, gefiltert nach Gerät und Art |
| `feinverlauf` (neu) | Messwerte im Takt der Abtastung für bis zu 24 Stunden, vom Leitstand abgerufen, nur im Heimnetz |

Damit lassen sich Fragen beantworten wie: Wie lange dauert eine Ladung bei welcher
Außentemperatur? Welcher Raum erreicht seinen Sollwert nicht, und seit wann? Wie verliefen freier
Speicher und Funksignal eines Geräts vor seinem Neustart?

Für Auswertungen außerhalb der App entsteht ein **Analysepaket**: ein ZIP-Archiv mit den
CSV-Dateien eines Zeitraums in wählbarem Raster, den Ereignissen, dem Katalog der Messgrößen mit
Einheiten und Bedeutung und einer Beschreibung der Anlage (Räume, Kreise, Rollen der Fühler). Es
wird in der App über „Teilen" oder auf der Weboberfläche des Leitstands erzeugt. Das Paket
enthält Gerätekennungen und Adressen und ist nicht zur Veröffentlichung bestimmt. Es ist zugleich
die Datengrundlage für Stufe 2 aus [konzept-auswertung.md](konzept-auswertung.md).

Die Daten verlassen das Heimnetz nur, wenn der Assistent ein Modell außerhalb des Geräts nutzt.
Dafür gilt die vorhandene Zustimmung in der App.

## Anzeige

Bedient wird mit drei Feldern unter dem Bildschirm: am vorhandenen Gerät die Tasten, am Core2
dieselben Felder auf der Berührungsfläche. A blättert zur vorigen Seite, C zur
nächsten, B schaltet innerhalb einer Seite um. B lang gedrückt schaltet die Anzeige aus.

| Seite | Inhalt |
|---|---|
| Anlage | Kessel mit Brenner, Vorlauf, Rücklauf und Abgas; Speicher mit Temperatur und Füllstand; beide Heizkreise mit Pumpe, Vorlauf und Rücklauf; Außentemperatur und Feuchte; Brennerlaufzeit und Starts des Tages; Zahl der offenen Befunde |
| Räume | alle Räume aller Verteiler mit Ist, Soll, Feuchte und Ventilstellung; Räume unter Soll hervorgehoben; B blättert |
| Verlauf | 24 Stunden: Speicher, Kesselvorlauf, Außen, Brennerlauf als Balken; B wechselt zu den Raumtemperaturen |
| Meldungen | offene Befunde aller Geräte, darunter die letzten Ereignisse mit Uhrzeit |
| Geräte | je Gerät Erreichbarkeit, WLAN-Signal, Laufzeit seit dem Neustart, freier Speicher, Version; Funkthermometer mit Batterie, Signal und Schlüsselzustand |
| Leitstand | Karte (belegt, frei, heute geschrieben, letzte Schreibung), Zeit, Netz, letzter Abgleich je App; auf dem Zielgerät der Kopplungscode für HomeKit |

Die Farben folgen dem dunklen Schema der App: Grund, Fläche, Tinte und Gedämpft für den Aufbau,
Wärme, Kälte, Gut, Warnung und Störung für Zustände. Die Anzeige dunkelt nach zwei Minuten ohne
Bedienung ab und ist nachts aus. Ein neuer Befund der Stufe Störung schaltet sie ein und zeigt
die Seite Meldungen.

Ohne PSRAM zeichnet die Anzeige unmittelbar in den Bildspeicher des Displays, ohne vollständigen
Zwischenspeicher. Messwerte werden alle 10 Sekunden aktualisiert, Diagramme nur bei einem neuen
Fünfminutenwert. Der 24-Stunden-Verlauf für die Anzeige liegt im Arbeitsspeicher (288 Plätze,
rund zwölf Reihen, 7 KB) und wird nach einem Neustart aus den Fünfminutendateien der Karte
wiederhergestellt.

Entwürfe der Seiten, mit Beispielwerten: [Anlage](entwuerfe/leitstand/1-anlage.svg),
[Räume](entwuerfe/leitstand/2-raeume.svg), [Verlauf](entwuerfe/leitstand/3-verlauf.svg),
[Meldungen](entwuerfe/leitstand/4-meldungen.svg), [Geräte](entwuerfe/leitstand/5-geraete.svg),
[Leitstand](entwuerfe/leitstand/6-leitstand.svg) und [HomeKit-Kopplung](entwuerfe/leitstand/7-kopplung.svg).

## HomeKit

- Eine HomeKit-Brücke mit je Raum einem Thermostat: aktuelle Temperatur, Solltemperatur von 5 bis
  35 °C in Schritten von 0,5 K, Betriebsart aus oder heizen, Heizzustand (Ventile offen) und
  Luftfeuchte, sofern das Raumthermometer sie liefert.
- Fühler für Außen (Temperatur und Feuchte), Speicher und Kesselvorlauf. Der Brenner optional
  als Kontaktsensor „Brenner läuft".
- Pumpen und Einstellungen der Anlage sind über HomeKit nicht erreichbar.
- Schreibweg: Sollwert und Betriebsart über `POST /api/room/<id>/…` am zuständigen Verteiler,
  mit einer Sammelverzögerung von 900 ms wie in der App, damit das Verschieben eines Reglers in
  Home nicht eine Folge von Befehlen auslöst.
- Die Werte stammen aus der Abfrage des Protokolls; Änderungen gehen als Benachrichtigung an Home.
- Kommt ein Raum hinzu oder entfällt einer, meldet der Leitstand die geänderte Konfiguration über
  die Konfigurationsnummer; eine neue Kopplung ist nicht nötig.
- Der Kopplungscode wird beim ersten Start auf dem Gerät erzeugt, im NVS abgelegt und von der
  Seite Leitstand aus (Taste B) als Zahl und QR-Code angezeigt. Es gibt keinen Aufkleber und keinen Code im
  Repository.
- Grundlage ist das öffentliche esp-homekit-sdk von Espressif (ESPRESSIF MIT License,
  ESP-IDF 5.x). Das Beispiel `bridge` übersetzt für den ESP32-S3 mit ESP-IDF 5.3.6. Eine
  MFi-Lizenz ist nur für Geräte nötig, die verkauft werden.
- Für Automationen und den Zugriff von unterwegs braucht Apple Home eine Steuerzentrale (Apple TV
  oder HomePod).

## Software

- Eigene Anwendung `apps/station` mit ESP-IDF 5.3.6, weil das HomeKit-SDK ESP-IDF 5.x
  voraussetzt. Gebaut wird nur für den ESP32; Core Basic und Core2 tragen denselben Baustein. M5GFX läuft mit ESP-IDF 5.3 auf dem vorhandenen Gerät, wie die Panel-Firmware
  zeigt. Verteiler und Heizungsgeräte bleiben auf ESP-IDF 6.0.2.
- Übernommene Komponenten: `netmgr`, `peers`, `atc_ble`, `cfgjson`, `config_store`,
  `captive_dns`. Sie übersetzen unter ESP-IDF 5.3.6 für den ESP32-S3 und für den ESP32. In den
  Vorgaben muss wie in den übrigen Anwendungen `CONFIG_LWIP_SNTP_MAX_SERVERS=3` stehen, sonst
  überschreitet die Liste der Zeitserver die Vorgabe. Für den ESP32 gelten zwei weitere Vorgaben:
  NimBLE wie im Verteiler, ohne Sicherheitsfunktionen und ohne Adressschutz, sonst fehlt beim
  Binden `ble_sm_alg_encrypt`; und Optimierung auf Geschwindigkeit (`-O2`), weil ESP-IDF 5.3.6 mit
  Größenoptimierung in `ble_hs_pvcy.c` an einer als Fehler behandelten Warnung abbricht.
- Neue Komponenten:
  - `plantpoll`: Abfrage der Geräte, Katalog, Erkennung der Ereignisse. Der Katalog als reines
    Rechenmodul, auf dem Rechner prüfbar wie `heatlogic`.
  - `plantlog`: Karte, Dateien, Schreibpuffer, Platzverwaltung, Lesen mit `Range`.
  - `station_ui`: Seiten der Anzeige mit M5GFX (MIT-Lizenz, aus der Komponentenverwaltung).
  - später `hap_bridge`: die HomeKit-Schicht.
- Schriften: M5GFX bringt keine Schrift mit Umlauten, Eszett und Gradzeichen mit.
  `tools/schriften_leitstand.swift` erzeugt sie aus Inter (SIL Open Font License) als
  Bitmapschriften; das Ergebnis liegt in `apps/station/main/schriften.h`, der Lizenztext daneben.
- CI: ein zweiter Bauauftrag mit ESP-IDF 5.3.6 für `apps/station`.

## Etappen

### Stand am 23. September

| Etappe | Stand |
|---|---|
| 0 | erledigt: Sicherung der Panel-Firmware zweimal gelesen, Prüfsummen gleich |
| 1 | in Arbeit: Die Leitstand-Firmware läuft auf dem vorhandenen Gerät, findet alle Geräte der Anlage, zeigt die Seiten Anlage und Leitstand und empfängt den Climate-Sat. Das Gerät am Kessel fragt den Leitstand seit dem 23. September zuerst nach der Außentemperatur, das Gerät am Pufferspeicher trägt noch 0.4.0. Die App führt den Leitstand ab Build 6, erprobt gegen die Attrappe. Offen: Schlüssel und Zuordnung des Außenfühlers, Aufnahme des Geräts in der App, 24-Stunden-Messung |
| 2 | in Arbeit: Protokoll auf der Karte seit 23. September am Leitstand (Messwerte, Fünfminutenmittel, Ereignisse, Zustände, `geraete.json`, Platzverwaltung, Abruf unter `/api/log/days` und `/log/…`, Reiter Protokoll). Katalog in [katalog-messgroessen.md](katalog-messgroessen.md), 137 Prüfungen am Rechner. Übernahme der Ladungs- und Tagesprotokolle, Ereignisse `funk`, `/api/log/series`, `/api/log/events`, Absturzspeicher über `/api/coredump`, Prüfwerkzeug `tools/protokoll_pruefen.py`. `reset_reason` und der vergrößerte Stapel für den Neustart seit dem 23. September auf allen Geräten. Kartenwechsel im Betrieb wird erkannt, die Abfrage ruht während eines Updates. Freier Speicher im Betrieb 35–37 KB, Tiefstwert 11–15 KB. Karte seit 23. September 64 GB, vom Leitstand als FAT32 formatiert. Dauerlast am 23. September bestanden: 1 GB geschrieben (0,40 MB/s) und zurückgelesen (0,53 MB/s) bei laufender Anzeige, Seitenwechseln alle 3 s und Bildschirmabzügen, ohne Schreibfehler, ohne abweichenden Block, ohne Neustart; freier Speicher dabei bis 28 Byte. Stromunterbrechungen: drei statt zwanzig, auf Entscheidung beendet; danach alle Dateien lesbar, kein Formatfehler, jede Lücke durch ein Startereignis belegt, die Karte ließ sich jedes Mal einhängen. Offen: sieben Tage |
| 3 | in Arbeit: Abgleich der App mit dem Protokoll seit 23. September, erprobt im Simulator gegen die Attrappe: alle Tage beim ersten Abgleich, danach nur neue Zeilen über `Range`, Wiederaufnahme über den gesicherten Stand je Datei, keine doppelten Plätze bei erneutem Abgleich. Stand unter Geräte › Leitstand › Verlauf. Ereignisablage je Tag, Markierungen und Ereignisliste in Heizung › Verlauf und im Raumverlauf; ein kürzer gewordener Tag (Karte getauscht oder formatiert) wird neu gelesen. In der App ab Build 7 (23. September); Verlauf in Tafeln und Heizkurve ab Build 8. Werkzeuge `ereignisse` und `feinverlauf`, `verlauf` bis ein Jahr, Analysepaket in den Einstellungen, ab Build 9. Abweichung: Das Analysepaket enthält keine Adressen, und die Weboberfläche des Leitstands erzeugt keines. Offen: Anzeige mit allen sechs Seiten, Prüfschritte am echten Leitstand |
| 4 und 5 | in Arbeit: Core2 am 24. September erwartet. Firmware vorbereitet: ein Abbild für Core Basic und Core2; HomeKit startet nur mit PSRAM. Echtzeituhr stellt die Zeit nach einem Ausfall, Ereignisse `versorgung_aus` und `versorgung_wieder`, Ladestand auf der Anzeige. HomeKit-Brücke mit esp-homekit-sdk (Stand Februar 2026, unter `third_party/`): Thermostat je Raum, Fühler für Außen, Speicher und Kesselvorlauf, Schreibweg mit 900 ms Sammelzeit, „Keine Antwort“ bei nicht erreichbarem Verteiler, Code und QR-Code auf der Anzeige (Taste B), Stand und Löschen der Kopplungen in App und Schnittstelle. Abweichungen: HAP auf Port 5556 neben der Weboberfläche; mDNS teilen `peers` und das SDK über einen Linker-Umweg (`st_hap_mdns.c`), das SDK bleibt unverändert; der Brenner als Kontaktsensor entfällt vorerst, weil „Kontakt offen“ für „Brenner läuft“ in Home missverständlich ist. Anzeige mit allen sechs Seiten und Berührung (Wischen, Tippen auf den Fuß) für den Core2; Verlauf der Anzeige im PSRAM, nach einem Neustart von der Karte gelesen; ein neuer Befund schaltet die Anzeige ein und zeigt die Meldungen. Am Core Basic weiterhin nur Anlage und Leitstand. Offen: Erprobung am Core2, alle Prüfschritte der Etappen 4 und 5 |

### Etappe 0: Vorbereitung

- Sicherung der aufgespielten Firmware: `esptool read_flash` über die vollen 16 MB, zweimal
  gelesen. Die Sicherung wird außerhalb des Repositorys abgelegt, weil der NVS-Bereich Schlüssel
  des camperSense-Panels enthält.
- SD-Karte für Dauerbetrieb, 16 GB, FAT32.

**Prüfschritte:** Prüfsummen beider Lesungen gleich. Karte am Rechner lesbar.

### Etappe 1: Grundgerät und Funk

- `apps/station` mit WLAN-Einrichtung, mDNS-Rolle `station`, Abfrage der Geräte ohne Protokoll,
  Funkempfang mit Schlüsselverwaltung, `/api/demand` mit Außentemperatur, Seite Anlage als erste
  Anzeige.
- Heizungsgeräte: Außentemperatur bevorzugt von der Rolle `station`.
- App: Geräteart Leitstand mit Funkthermometern und dem vorhandenen Schlüsselblatt.

**Prüfschritte:**
- Rechner: vorhandene Prüfungen ohne Fehler; Bau ohne Warnungen.
- Attrappe `tools/mock_station.py`: Die App zeigt den Leitstand im Simulator, ein Schlüssel lässt
  sich gegen die Attrappe eintragen.
- Gerät: Der Climate-Sat erscheint; mit dem über die App eingetragenen Schlüssel stehen seine
  Werte binnen einer Minute an.
- Anlage: Kessel und Speicher zeigen die Außentemperatur mit der Quelle „Leitstand" (App, Karte
  Anlage). Der Außenfühler am Verteiler Erdgeschoss wird vorher abgemeldet.
- Speicher: Tiefstwert des freien Speichers über 24 Stunden mit WLAN, Funk, Anzeige und Abfrage
  aller Geräte mindestens 30 KB. Liegt er darunter, wird das Zielgerät vor Etappe 2 beschafft.
- 24 Stunden ohne Neustart.

### Etappe 2: Protokoll

Abweichungen auf dem vorhandenen Gerät ohne PSRAM, festgestellt bei der Umsetzung:

- Jede Zeile wird sofort geschrieben statt eine Minute gesammelt; für den Sammelpuffer fehlt der
  Arbeitsspeicher. Ein Stromausfall kostet damit weniger, die Karte wird öfter beschrieben.
- Abfragen vor der ersten Zeitsynchronisation werden verworfen und gezählt, nicht 30 Minuten
  gehalten. Das Halten folgt mit dem Zielgerät.
- Die Verteiler werden je Takt zweimal abgefragt: erst `/api/demand`, dann `/api/state` mit
  ETag. Antwortet der Zustand mit 304, wiederholt die Zeile die letzten Werte, auch die des
  Bedarfs.

- Karte, Abtastung, Katalog, Messwerte, Fünfminutenmittel, Ereignisse, Zustände, Übernahme der
  Ladungs- und Tagesprotokolle, Zeitbasis, Platzverwaltung, Endpunkte unter `/api/log/`,
  Weboberfläche mit der Liste der Tage zum Herunterladen.
- Verteiler und Heizungsgeräte: `reset_reason` in `/api/state`.

**Prüfschritte:**
- Rechner: Katalog gegen die Vorlage aus dem Mitschnitt. CSV-Schreiber mit Grenzfällen: neue
  Spalte im Laufe des Tages, Tageswechsel in UTC, fehlende Werte, Rücksprung der Uhr.
- Gerät: 20 Stromunterbrechungen während des Schreibens. Danach sind alle Dateien lesbar, je
  Unterbrechung fehlen höchstens 60 s, das Dateisystem ist fehlerfrei.
- Gerät: Anzeige und Karte gleichzeitig unter Last, 1 GB schreiben bei laufender Anzeige: keine
  Bildfehler, keine Schreibfehler.
- Anlage, sieben Tage: je Gerät mindestens 99,5 % der erwarteten Abtastungen, gezählt gegen den
  Takt; jede Lücke mit einem Ereignis `nicht_erreichbar` belegt. Tiefstwert des freien Speichers
  der Heizungsgeräte nicht unter dem Wert aus dem Mitschnitt. Datenmenge je Tag im Rahmen der
  Hochrechnung.
- Abgleich der Umsetzungen: `zustaende.jsonl` eines Tages über den Mitschnittimport der App
  eingelesen ergibt dieselben Fünfminutenmittel wie die Dateien des Leitstands, auf 0,01 genau.

### Etappe 3: Abgleich, Anzeige, Auswertung

- App: Abgleich mit dem Leitstand, Ereignisablage und Markierungen im Verlauf, Umfang des
  Verlaufs in den Einstellungen, Werkzeuge `ereignisse` und `feinverlauf`, erweitertes `verlauf`,
  Analysepaket.
- Anzeige: alle sechs Seiten.

**Prüfschritte:**
- Simulator gegen die Attrappe mit Vorlagetagen: Abgleich, Wiederaufnahme nach Abbruch über
  `Range`, keine doppelten Plätze.
- Mac und iPhone zeigen für denselben Zeitraum dieselben Reihen; verglichen über eine Prüfsumme
  je Tagesblock.
- Leitstand ausgeschaltet: Die App zeigt den vorhandenen Verlauf und zeichnet selbst weiter auf;
  nach dem Wiedereinschalten wird die Lücke gefüllt.
- Assistent: Prüffragen gegen das Ereignisprotokoll, etwa Brennerstarts des Vortags oder der
  letzte Neustart eines Geräts.
- Analysepaket in einem externen Werkzeug geöffnet; Spalten und Einheiten entsprechen dem Katalog.
- Anzeige: alle Seiten mit Werten der Anlage, Abdunkeln, Seite Meldungen bei einem neuen Befund.

### Etappe 4: Zielgerät

- Umzug auf den Core2: Karte umstecken, Einstellungen über die Sicherung. Echtzeituhr, Meldung
  des Versorgungsausfalls, Berührungsfelder. Aufstellung in der Nähe des Kessels.

**Prüfschritte:** Zehn Stromunterbrechungen ohne Datenverlust, weil der Akku die Zeit bis zum
Schließen der Dateien überbrückt. Nach einem Ausfall ohne Netzverbindung stimmt die Zeit. Die Prüfschritte der
Etappen 1 bis 3 werden auf dem Zielgerät wiederholt.

### Etappe 5: HomeKit

- HomeKit-Schicht wie oben beschrieben, Kopplungscode auf der Anzeige.

**Prüfschritte:**
- Kopplung mit dem iPhone über den QR-Code der Anzeige; alle Räume erscheinen als Thermostate.
- Sollwert in Home geändert: Der Wert steht binnen zwei Sekunden am Verteiler und in der App;
  das Protokoll verzeichnet ein Ereignis `sollwert`.
- Betriebsart aus und heizen in beide Richtungen.
- Verteiler nicht erreichbar: Seine Räume zeigen in Home „Keine Antwort", keine veralteten Werte.
- Neustart des Leitstands: Die Kopplung bleibt, die Werte stehen binnen einer Minute wieder an.
- Sieben Tage mit gekoppeltem iPhone und Steuerzentrale: freier Speicher gleichbleibend,
  Protokoll so vollständig wie in Etappe 2.

### Etappe 6: Abgleich über iCloud (nach Entscheidung)

- Tagesblöcke in der privaten iCloud-Datenbank mit `CKSyncEngine`; ein dauernd laufender Mac mit
  der App in der Menüleiste gleicht mit dem Leitstand ab.

**Prüfschritte:** Ein iPhone außerhalb des Heimnetzes zeigt den Verlauf bis zum letzten Abgleich
des Macs, mit denselben Werten wie der Leitstand.

### Etappe 7: Abschluss

- Handbuch: Kapitel Leitstand mit Einrichtung, Karte, Dateiformat, Katalog und Analysepaket.
  Funktionsabgleich, CHANGELOG, README.

**Prüfschritte:** Das Handbuchkapitel am Gerät nachvollzogen. `tools/check_api.py` deckt die
Endpunkte des Leitstands ab.

## Offene Entscheidungen

1. Abgleich über iCloud (Etappe 6): ja oder nein.
2. Bedienung am Gerät: nur Anzeige (Vorschlag) oder auch Sollwerte.

Entschieden am 23. September: Die camperSense-Panel-Firmware auf dem vorhandenen Gerät darf nach
der Sicherung überschrieben werden; Zielgerät ist der Core2.

## Risiken

| Risiko | Folge | Gegenmaßnahme |
|---|---|---|
| Der Speicher des vorhandenen Geräts reicht nicht | Etappen 1 bis 3 nur eingeschränkt möglich | Messung in Etappe 1; sonst Zielgerät vorziehen |
| Anzeige und Karte am selben Bus | Bildfehler oder Schreibfehler | gemeinsame Bussperre; Lastprüfung in Etappe 2 |
| Karte fällt aus oder Dateisystem beschädigt | Protokoll auf der Karte verloren | Tagesdateien, keine offene Datei zwischen den Schreibvorgängen, Karte für Dauerbetrieb; Fünfminutenmittel liegen zusätzlich in den Apps |
| Zwei ESP-IDF-Stände im Repository | eine Änderung an einer Komponente bricht einen Stand | CI baut beide |
| Zusätzliche Abfragen | weniger freier Speicher in den Heizungsgeräten | Messung in Etappe 2, sonst Takt senken |
| Katalog in zwei Sprachen umgesetzt | App und Leitstand schreiben verschiedene Reihen | gemeinsame Vorlage in beiden Prüfungen |
| Verlauf lässt Rückschlüsse auf Anwesenheit zu | Einblick Dritter in Lebensgewohnheiten | Daten bleiben im Heimnetz und in der privaten iCloud; Analysepaket nur lokal |

## Was bewusst nicht vorgesehen ist

- **Keine Regelung im Leitstand.** Er meldet und zeichnet auf; die Regelung bleibt in den
  Verteilern und Heizungsgeräten.
- **Kein Dienst außerhalb des Hauses.** iCloud dient nur als Ablage der Apps.
- **Keine Datenbank auf dem Gerät.** Dateien genügen und bleiben ohne Programm lesbar.
- **Kein Bluetooth in den Heizungsgeräten.** Die vorbereitete Änderung bleibt zurückgestellt.
- **Kein Matter.**
