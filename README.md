# Ventilsteuerung Fußbodenheizung

Steuerung für elf motorische Stellantriebe an einem Heizkreisverteiler, auf
eigener Platine mit ESP32. Räume und Kreiszuordnung sind über eine
Weboberfläche auf dem Gerät einzurichten, die Anbindung an Home Assistant läuft
über MQTT, die Raumtemperaturen kommen direkt über Bluetooth.

![Übersicht der Weboberfläche](docs/screenshots/uebersicht.png)

Das Gerät übernimmt vier Aufgaben:

- **Ventile fahren.** Elf Stellantriebe werden über H-Brücken bidirektional
  angesteuert; die Endlage wird an der Gegenspannung des blockierenden Motors
  erkannt, weil die Antriebe keine Endschalter haben.
- **Räume regeln.** Je Raum ein Sollwert, ein Thermometer und beliebig viele
  Heizkreise; die Ventilstellung folgt der Regelabweichung proportional.
- **Bedienen.** Weboberfläche auf dem Gerät, dazu Anzeige und drei Tasten am
  Gehäuse.
- **Anbinden.** MQTT-Discovery für Home Assistant, Raumtemperaturen über
  Bluetooth Low Energy.

Der Quelltextbestand enthält neben der Firmware auch die Fertigungsdaten der
Platine ([`board/`](board/)) und die Konstruktion des Gehäuses
([`case/`](case/)).

## Einordnung

Ein privates Bastelprojekt für die eigene Heizungsanlage. Es steht in keiner
Verbindung zu einem Hersteller und implementiert kein Funkprotokoll: die
Antriebe werden über eigene H-Brücken elektrisch angesteuert, also durch
Anlegen der Motorspannung. Verwendete Produktbezeichnungen dienen allein der
Angabe, welche Bauteile verbaut sind.

## Hintergrund

Verbaut sind motorische Stellantriebe vom Typ **HmIP-VDMOT**. Sie haben keine
Endschalter: die Endlage ist daran zu erkennen, dass der Motor am Anschlag
blockiert und die Spannung über dem Messwiderstand deutlich steigt. Genau das
leistet diese Firmware, dazu die Raumregelung darüber.

Vorgänger war eine ESPHome-Konfiguration mit rund 1750 Zeilen YAML, in der die
Regel- und Endlagenlogik bereits vollständig in Lambda-Blöcken steckte. Räume
und Kanalzuordnung waren dort fest verdrahtet; jede Änderung erforderte
Neuübersetzung und Flashen. Das war der Anlass für die Portierung.

## Hardware

Die Steuerplatine ist selbst entworfen und bei **JLCPCB** gefertigt. Sie trägt
keinen eigenen Mikrocontroller: ein **ESP32-DevKitC** wird auf Buchsenleisten
gesteckt und lässt sich damit ohne Löten tauschen. Fertigungsdaten, Gerber und
Stückliste liegen in [`board/`](board/), das Druckgehäuse in
[`case/`](case/).

| Baugruppe | Umsetzung |
|---|---|
| Stellantriebe | 11 × motorisch, ohne Endschalter (HmIP-VDMOT) |
| Anschluss der Antriebe | 11 × 4P4C-Buchse (RJ11) an der Platinenkante |
| Ansteuerung | 11 × L9110s H-Brücke, bidirektional |
| Kanalauswahl | 3 × SN74HC595 Schieberegister über SPI2, 22 der 24 Ausgänge belegt |
| Endlagenerkennung | Messwiderstand je Messgruppe an ADC1, 6 Messgruppen, Dämpfung 6 dB |
| Anzeige | SSD1327, 128 × 128, 16 Graustufen, I²C |
| Bedienung am Gerät | 3 kapazitive Tasten: Sollwert −, Sollwert +, Raumwahl |
| Vorlauffühler | DS18B20 am 1-Wire-Bus, bis zu 8 Fühler, eigene 4P4C-Buchse |
| Schaltschrank-Klima | HDC1080, I²C |
| Stromversorgung | HLK-5M03, Netzanschluss über Klemme auf der Platine |
| Rechner | ESP32-DevKitC (WROOM-32) auf Sockel |

> Auf der Platine liegt Netzspannung. Der Netzbereich ist durch gefräste
> Trennschlitze abgegrenzt; Aufbau und Gehäuse sind in
> [`case/README.md`](case/README.md) beschrieben, einschließlich der dort
> genannten Einschränkungen zu Werkstoff und Zugentlastung.

### Anschlussbelegung

| Funktion | Pin |
|---|---|
| Schieberegister | DATA 16, CLK 5, LATCH 17, OE 18 |
| Gegenspannung, ADC1, Dämpfung 6 dB | 36, 39, 34, 35, 32, 33 |
| 1-Wire | 23 |
| I²C | SDA 21, SCL 22 |
| Tasten | 4 (−), 2 (+), 15 (Raumwahl) |

Die Zuordnung steht ausschließlich in
[`components/hw_map/`](components/hw_map/); alles andere arbeitet mit
Kanalnummern 1 bis 11 und Gruppennummern 0 bis 5.

### Messgruppen

Je zwei Heizkreise teilen sich einen Messeingang:

| Gruppe | ADC-Pin | Kreise |
|---|---|---|
| 1 | 36 | 1, 2 |
| 2 | 39 | 3, 4 |
| 3 | 34 | 5, 6 |
| 4 | 35 | 7, 8 |
| 5 | 32 | 9, 10 |
| 6 | 33 | 11 |

Daraus folgt die zentrale Betriebsregel: **innerhalb einer Messgruppe fährt
immer nur ein Kreis.** Sonst liegt die Gegenspannung beider Motoren auf
demselben Eingang und die Endlage ließe sich keinem Antrieb mehr zuordnen.
Durchgesetzt wird das von der Gruppensperre in
[`main/app_control.c`](main/app_control.c).

## Funktionsweise

### Endlagenerkennung

Die Antriebe haben keine Endschalter. Im Lauf zieht der Motor eine kleine
Spannung über den Messwiderstand; erreicht das Ventil den Anschlag, blockiert
er und die Spannung steigt deutlich. Alle sechs Messgruppen werden alle 50 ms
abgetastet, ausgewertet wird ein gleitender Median über fünf Werte. Erreicht er
die Auslöseschwelle, gilt die Endlage als erreicht und die Position wird auf 0
beziehungsweise 100 % gesetzt. Scharf ist die Auswertung nur für die Messgruppe
eines gerade fahrenden Kreises; die Schwelle ist dann die des fahrenden Kreises.

Nach dem Anlauf gilt eine Sperrzeit von zwei Sekunden, in der die Meldung
verworfen wird — der Einschaltstrom sieht sonst wie ein Blockieren aus. Bleibt
die Endlage aus, beendet die Maximallaufzeit die Fahrt.

### Automatische Kalibrierung

Die Auslöseschwelle hängt von Antrieb, Messwiderstand und Verkabelung ab. Statt
sie von Hand zu suchen, fährt die Messfahrt den Kreis einmal ganz zu und einmal
ganz auf und schreibt dabei den Spannungsverlauf mit. Das Blockieren wird
**relativ** zum gemessenen Fahrniveau erkannt — eine vorhandene Schwelle geht
nicht ein, sonst würde die Kalibrierung ihr eigenes Ergebnis voraussetzen.

![Ergebnis einer Messfahrt](docs/screenshots/kalibrierung.png)

Braun ist die Schließfahrt, blau die Öffnungsfahrt, dazwischen liegen drei
Sekunden Ruhe. Die beiden Spitzen sind die Anschläge. Aus dem Verlauf ergeben
sich Fahrzeiten, Maximallaufzeit (längste Fahrzeit plus rund 15 %),
Auslöseschwelle (Mitte zwischen Fahrniveau und niedrigerer Spitze) und
Hysterese; übernommen werden sie erst nach Bestätigung.

Wird in einer Fahrtrichtung keine Endlage erkannt, bricht die Messfahrt mit
einem Hinweis auf Verdrahtung und Messwiderstand ab.

### Handsteuerung und Notfahrt

Jeder Kreis lässt sich von Hand fahren, **unabhängig davon, welche Stellung die
Steuerung ihm zuschreibt**. Das ist kein gewöhnlicher Fahrbefehl: `valve_goto`
prüft die geschätzte Stellung und täte nichts, wenn das Ventil bereits als
offen gilt; bei unbekannter Stellung führe es sogar erst eine Referenzfahrt
gegen die untere Endlage aus — bei „auf" also zunächst zu. Für den Notfall ist
beides falsch.

`valve_force` fährt deshalb ohne Stellungsvergleich bis zum Anschlag. Beendet
wird die Fahrt nur durch die Endlage, die Maximallaufzeit oder einen Halt. Über
`/api/channel/all/cmd` gilt der Befehl für alle elf Kreise zugleich.

Danach bleibt der Kreis im **Handbetrieb**: die Regelung fasst ihn nicht mehr
an, bis er ausdrücklich wieder freigegeben wird. Ohne diesen Halt wäre eine von
Hand erzwungene Stellung beim nächsten Regeldurchlauf nach spätestens einem
Prüfintervall wieder verworfen. Denselben Halt setzt ein Stoppbefehl: von Hand
angehalten bleibt von Hand angehalten. Die Freigabe erfolgt über den Befehl
„Automatik", einzeln oder für alle Kreise; der zugehörige Raum wird sofort neu
bewertet, statt bis zum nächsten Prüfintervall zu warten.

Ein Fahrbefehl auf eine bestimmte Stellung setzt den Halt **nicht** — er wird
von der Regelung nach spätestens einem Prüfintervall überschrieben.

Die Gruppensperre gilt weiter: fährt in derselben Messgruppe bereits ein
anderer Kreis, wartet der Befehl, bis sie frei ist.

### Regelung

Die Ventilstellung folgt der Regelabweichung proportional, über ein Band von
±1 K (Vorgabe) von ganz zu bis ganz auf, gerastert auf Zehntel:

```
diff = soll − ist
pos  = runden(((diff / band) + 1) / 2 / raster) × raster,   begrenzt auf 0 … 1
gefahren wird, wenn |aktuell − pos| > Mindeständerung
```

Band, Rasterung, Mindeständerung und Prüfintervall sind je Raum einstellbar.
Der erste Regeldurchlauf findet erst 50 Sekunden nach dem Start statt, damit
Messwerte und Netzwerk eingelaufen sind. Die Ventilstellungen werden höchstens
einmal je Minute in den NVS geschrieben und beim Start wiederhergestellt.

### Raumtemperatur

Der ESP32 hört die Raumthermometer selbst mit — Xiaomi-Thermometer mit
ATC- oder pvvx-Firmware senden ihre Messwerte offen als Rundruf. Es wird keine
Verbindung aufgebaut und nichts gesendet. Damit hängt die Regelung nicht am
Netzwerk.

Bleibt ein Messwert länger als eingestellt aus (Vorgabe 900 s), setzt die
Regelung für diesen Raum aus und die Ventile bleiben stehen, statt mit einem
veralteten Wert weiterzuregeln. Dasselbe gilt, solange für einen Raum noch kein
Messwert eingegangen ist.

### Vorgabewerte

Bis zur ersten Messfahrt und vor der Einrichtung gelten diese Werte. Sie
stammen aus dem Betrieb der Vorgängerkonfiguration.

| Größe | Vorgabe |
|---|---|
| Öffnungszeit / Schließzeit je Kreis | 39 s / 40 s |
| Maximallaufzeit je Kreis | 45 s |
| Auslöseschwelle / Hysterese | 190 mV / 30 mV |
| Sperrzeit nach dem Anlauf | 2 s |
| Solltemperatur je Raum | 20 °C |
| Proportionalband | 1,0 K |
| Rasterung / Mindeständerung | 0,1 / 0,01 |
| Prüfintervall | 30 s |
| Höchstalter eines Raummesswerts | 900 s |
| Täglicher Neustart | 10:00 Uhr, Zeitzone `CET-1CEST,M3.5.0,M10.5.0/3` |
| Gerätename | `floor-heating` |
| Kennwort des Einrichtungs-Zugangspunkts | `fussboden` |
| MQTT | ausgeschaltet, Präfix `fbh` |
| Tastenschwellen | 1000, 870, 1000 |

Ein täglicher Neustart wird verschoben, solange ein Stellantrieb fährt.

## Oberfläche

Räume, Kreiszuordnung, Thermometer und Regelparameter sind zur Laufzeit
änderbar. Die Oberfläche liegt komprimiert im Programmabbild (rund 22 kB) und
lädt ohne Internetzugang.

| | |
|---|---|
| ![Räume](docs/screenshots/raeume.png) | ![Heizkreise](docs/screenshots/kreise.png) |
| **Räume** — Kreise zuordnen, Thermometer wählen, Regelparameter | **Kreise** — Zustand, Handbedienung, Fahrzeiten und Schwellen |
| ![Sensoren](docs/screenshots/sensoren.png) | ![System](docs/screenshots/system.png) |
| **Sensoren** — empfangene Thermometer, Vorlauffühler, Tastenabgleich | **System** — Netzwerk, MQTT, Betrieb, Firmware |

<img src="docs/screenshots/uebersicht-dark.png" width="49%" alt="Dunkles Farbschema">
<img src="docs/screenshots/mobil.png" width="24%" alt="Ansicht auf dem Telefon">

### Abfragetakt

Die Seite fragt den Gerätezustand nicht in festem Takt ab, sondern richtet sich
danach, was sich tatsächlich ändert:

| Lage | Takt |
|---|---|
| ein Ventil fährt oder eine Messfahrt läuft | 1 s |
| Ruhezustand | 6 s |
| Seite im Hintergrund | keine Abfrage |
| Messreihe während einer Messfahrt | 400 ms |
| Liste der empfangenen Thermometer | 30 s |

`/api/state` gibt den Änderungszähler als ETag mit. Fragt die Seite mit
demselben Wert erneut an und fährt gerade kein Ventil, antwortet das Gerät mit
304 und erspart sich den Aufbau der rund 4 kB großen Antwort. Restzeiten,
Messwertalter und Laufzeit rechnet die Seite selbst weiter.

Fährt ein Kreis 20 % der Zeit, ergibt das rund 20 Zustandsabfragen je Minute;
Inhalt liefern davon nur die während der Fahrt und die nach einer Änderung.

## Übersetzen und Flashen

Vorausgesetzt wird ESP-IDF v6.0.2.

```bash
. ~/esp/esp-idf-6.0.2/export.sh && idf.py set-target esp32 && idf.py build
```

```bash
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

Spätere Aktualisierungen laufen über die Weboberfläche (System → Firmware
aktualisieren) oder direkt:

```bash
curl -X POST --data-binary @build/floor-heating-ctrl.bin http://<adresse>/api/ota
```

Die Firmware belegt rund 1,2 MB. Die Partitionstabelle sieht zwei
OTA-Bereiche zu je 1 966 080 Byte vor; rund 39 % bleiben frei. Eine frisch
eingespielte Firmware wird erst bestätigt, wenn Konfiguration, Ausgangsstufe,
Regelung und Weboberfläche angelaufen sind; andernfalls kehrt der Bootlader zur
vorherigen zurück.

## Erste Inbetriebnahme

1. Nach dem Flashen findet das Gerät kein WLAN und öffnet sofort einen
   Zugangspunkt `floor-heating-XXXX` (Kennwort `fussboden`). Die vier Stellen
   sind die letzten beiden Bytes der MAC-Adresse.
2. Beim Verbinden öffnet sich die Einrichtungsseite von selbst (Captive
   Portal). Andernfalls `http://192.168.4.1` aufrufen.
3. Netz wählen, Kennwort eintragen, speichern. Der Zugangspunkt schließt sich,
   sobald die Verbindung steht und niemand mehr daran hängt.
4. Danach führt der Einrichtungsassistent in drei Schritten durch die Anlage:
   Bezeichnung der Etage, Räume mit ihren Heizkreisen, Thermometer je Raum. Er
   startet von selbst, solange keine Bezeichnung hinterlegt ist, und ist später
   über **System → Einrichtung erneut durchlaufen** wieder erreichbar. Die
   Thermometer erscheinen zur Auswahl, sobald ihr erster Rundruf empfangen
   wurde.
5. Unter **Kreise** für jeden Kreis eine Messfahrt starten.
6. Unter **Sensoren** die Rohwerte der Tasten in Ruhe und bei Berührung
   ablesen und die Schwelle etwa mittig setzen.
7. Unter **System** MQTT eintragen, falls Home Assistant angebunden werden
   soll; ab Werk ist es ausgeschaltet.

<img src="docs/screenshots/einrichtung.png" width="45%" alt="Einrichtungsportal">

## Home Assistant

Die Entities werden per MQTT-Discovery angemeldet und bei jeder
Konfigurationsänderung neu berechnet; entfallene Räume werden wieder entfernt.

| Typ | Umfang |
|---|---|
| `cover` | 11 Heizkreise mit Position, Öffnen/Schließen/Stopp |
| `climate` | je eingerichtetem Raum |
| `sensor`, je Raum | Temperatur, Luftfeuchte, Batterie des Thermometers, Zielstellung |
| `sensor`, Diagnose | 6 Messgruppen in mV, erkannte Vorlauffühler, Schaltschrankklima, Laufzeit, WLAN-Empfang, freier Speicher |
| `button` | Neustart, Regelung je Raum sofort auslösen |

Öffnen, Schließen und Stopp über die Cover-Entities setzen den Handbetrieb-Halt
und bleiben damit stehen; eine gesetzte Position tut das nicht und wird von der
Regelung wieder überschrieben.

Die 22 einzelnen H-Brücken-Eingänge werden bewusst **nicht** exportiert —
direktes Schalten umginge Verriegelung und Gruppensperre.

## Aufbau

```
main/
  main.c          Start der Bausteine
  app_control.c   Ventilzustände, Gruppensperre, Raumregelung   ← Kern
  app_calib.c     Automatische Kalibrierung der Endlagenerkennung
  app_web.c       HTTP-Server, JSON-Schnittstelle, Captive Portal, OTA
  app_mqtt.c      Home-Assistant-Discovery und Zustände
  app_ui.c        Anzeige und Tasten am Gerät
  www/index.html  Oberfläche, komprimiert ins Abbild gelegt
components/
  hw_map/         Pin- und Gruppentabellen — einzige Stelle mit Hardwarewissen
  sr74hc595/      Schieberegister mit Verriegelung IA gegen IB
  valve/          Zustandsmaschine je Kreis (frei von IDF-Abhängigkeiten)
  bemf/           ADC-Abtastung, gleitender Median, Schwellwertauswertung
  roomctrl/       Regelgesetz (frei von IDF-Abhängigkeiten)
  config_store/   Konfiguration als JSON im NVS
  atc_ble/        NimBLE-Observer, ATC- und pvvx-Dekoder
  ssd1327/        Anzeigetreiber mit Bitmap-Schrift
  sensors_local/  DS18B20 und HDC1080
  netmgr/         WLAN, SNTP, täglicher Neustart
  captive_dns/    Namensdienst des Einrichtungsportals
  i2cbus/         gemeinsamer I²C-Bus
board/            Fertigungsdaten der Platine: Gerber, DXF, Stückliste
case/             Druckgehäuse, mit CadQuery konstruiert (eigene README)
test/host/        Prüfungen ohne IDF und ohne Hardware
tools/            Geräteattrappe und Bildschirmaufnahmen
docs/screenshots/ Aufnahmen dieser Seite
```

## Entwicklung ohne Hardware

Die Oberfläche lässt sich ohne Gerät bedienen. `tools/mock_device.py` liefert
die echte Seite mit erfundenen Messwerten:

```bash
python3 tools/mock_device.py
```

Danach `http://localhost:8321` aufrufen. `?theme=dark` erzwingt ein
Farbschema, `#kreise` und die übrigen Anker öffnen einen Bereich direkt,
`/mock/ap?on=1` schaltet in den Zugangspunkt-Betrieb, um das Einrichtungsportal
zu sehen. `tools/screenshots.sh` erzeugt daraus die Aufnahmen dieser Seite;
vorausgesetzt werden Google Chrome und ImageMagick.

## Prüfen

Regelgesetz, Ventil-Zustandsmaschine und Hardwarezuordnung laufen ohne IDF und
ohne Hardware:

```bash
make -C test/host
```

Der Lauf umfasst 215 Prüfungen.

An der Hardware:

1. Schieberegister: alle 22 Ausgänge einzeln schalten, gegen die
   L9110s-Eingänge messen, Verriegelung prüfen (IA und IB nie gleichzeitig).
2. Einen Kreis ganz zu und ganz auf fahren; Laufzeit und Abschaltung über die
   Endlage prüfen. Die Maximallaufzeit muss greifen, wenn die Endlage ausbleibt.
3. Messfahrt eines Kreises, Verlauf gegen den Vorgabewert von 190 mV halten.
4. Gruppensperre: Kreis 1 fahren lassen und währenddessen Kreis 2 anfordern —
   Kreis 2 darf erst danach fahren.
5. Tasten: unter **Sensoren** die Rohwerte in Ruhe und bei Berührung ablesen,
   die Schwelle etwa mittig setzen.
6. MQTT: Entities erscheinen in Home Assistant; einen Raum löschen und neu
   anlegen — die Entities verschwinden und kommen wieder.

## Stand der Erprobung

Die Firmware läuft auf der bestückten Platine. Erprobt sind Start, BLE-Empfang
von neun Thermometern, Tastenauswertung, Weboberfläche, Captive Portal,
Aktualisierung über das Netz, Handsteuerung und drei Messfahrten.

Die Messfahrten liegen durchweg unter den Vorgabewerten:

| Kreis | auf | zu | Auslöseschwelle |
|---|---|---|---|
| 1 | 36,1 s | 38,1 s | 166 mV |
| 2 | 38,4 s | 39,5 s | 168 mV |
| 4 | 33,7 s | 35,2 s | 182 mV |
| Vorgabe | 39 s | 40 s | 190 mV |

Nicht erprobt sind der Dauerbetrieb der Regelung über längere Zeit sowie
Anzeige (SSD1327) und DS18B20-Fühler, weil beide nicht dauerhaft angeschlossen
sind.

## Abweichungen von der Vorgängerkonfiguration

Vier Unstimmigkeiten der ESPHome-Fassung sind dabei bereinigt:

1. **Endlagenzuordnung Kreis 9 bis 11.** Die Cover gaben `BEMF_4_sensor` an,
   gehören laut Verdrahtung aber zu Gruppe 5 beziehungsweise 6. Wirksam wurde
   die Endlage bisher nur über die `on_press`-Lambdas.
2. **Belegtprüfung bei Kreis 5.** Geprüft wurde Kreis 4; Kreis 5 teilt sich die
   Messgruppe jedoch mit Kreis 6.
3. **Touch-Tasten.** Die Raumauswahl zählte 0 bis 3, die Tasten verglichen
   gegen 10 bis 13 und lösten deshalb nie aus.
4. **Abbruch statt Überspringen.** War ein Kreis gesperrt, brach die Prüfung des
   gesamten Raums ab. Jetzt wird nur der betroffene Kreis übersprungen.

Bewusst geändertes Verhalten:

- **Schnellere Endlagenerkennung.** ESPHome bildete den Median über fünf Werte
  und meldete nur jeden fünften, also alle 1,25 s. Jetzt wird bei 50 ms
  Abtastung ein gleitender Median bei jedem Wert ausgewertet.
- **Ein Sollwert statt low/high.** Das Original mittelte ohnehin über beide.
- **Ventilstellungen im NVS**, beim Start wiederhergestellt. Ist keine Stellung
  bekannt, fährt der Kreis beim ersten Regeleingriff einmal auf Anschlag.
- **Veralteter Messwert setzt die Regelung aus.** Das Original hätte
  stillschweigend mit dem letzten bekannten Wert weitergeregelt.
- **Raumtemperatur über BLE statt über Home Assistant.**
- **Räume zur Laufzeit konfigurierbar** statt fest im YAML.

## Offene Punkte

- **Touch-Schwellen** (1000, 870, 1000) stammen aus der ESPHome-Fassung. Die
  Messdauer der neuen Treiberfassung weicht ab, sie sind nur ein Ausgangspunkt
  und unter **Sensoren** nachzustellen.
- **MAC-Adressen der Thermometer** ergeben sich beim ersten BLE-Empfang.
- **Matter und Thread** sind nicht umgesetzt. Thread scheidet auf dieser
  Platine aus, weil dem ESP32-WROOM-32 das 802.15.4-Funkteil fehlt. Matter über
  WLAN passt nicht in den verfügbaren Programmspeicher. Wird Matter gebraucht,
  reicht Home Assistant die vorhandenen MQTT-Entities über seine eigene
  Matter-Bridge weiter.

## Lizenz

Apache License 2.0, siehe [LICENSE](LICENSE) und [NOTICE](NOTICE).

Die Software wird ohne jede Gewährleistung bereitgestellt. Sie steuert eine
Heizungsanlage: wer sie einsetzt, tut das auf eigenes Risiko und sollte die
Prüfschritte oben an der eigenen Hardware durchgehen, bevor er sie unbeaufsichtigt
laufen lässt.

Über die ESP-IDF-Registry eingebundene Komponenten stehen unter eigenen
Lizenzen (cJSON unter MIT, die übrigen unter Apache-2.0) und sind nicht Teil
dieses Quelltextbestands.

## Marken

HmIP und Homematic IP sind Marken der eQ-3 AG. Sie werden hier ausschließlich
genannt, um die verbauten Stellantriebe zu bezeichnen. Es besteht keine
Verbindung zu eQ-3, und es wird weder eine Kompatibilität noch eine Freigabe
behauptet.
