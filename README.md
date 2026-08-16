# Ventilsteuerung Fußbodenheizung

Diese Firmware steuert elf motorische Stellantriebe an einem Heizkreisverteiler
und regelt darüber die Raumtemperaturen einer Fußbodenheizung. Sie läuft auf
einem ESP32 auf einer selbst entworfenen Platine und richtet sich an alle, die
ihre Heizkreise mit eigener Hardware ansteuern wollen.

Räume und die Zuordnung der Heizkreise werden über eine Weboberfläche auf dem
Gerät eingerichtet. Die Raumtemperaturen empfängt das Gerät über Bluetooth,
die Anbindung an Home Assistant läuft über MQTT.

![Übersicht der Weboberfläche](docs/screenshots/uebersicht.png)

Das Gerät übernimmt vier Aufgaben:

- **Ventile fahren.** Elf Stellantriebe werden über H-Brücken bidirektional
  angesteuert. Die Endlage wird an der Gegenspannung des blockierenden Motors
  erkannt, weil die Antriebe keine Endschalter haben.
- **Räume regeln.** Jeder Raum hat einen Sollwert, ein Thermometer und beliebig
  viele Heizkreise; die Ventilstellung folgt der Regelabweichung proportional.
- **Bedienen.** Bedient wird das Gerät über die Weboberfläche sowie über die
  Anzeige und drei Tasten am Gehäuse.
- **Anbinden.** Die Entitäten für Home Assistant meldet das Gerät über
  MQTT-Discovery an; die Raumtemperaturen empfängt es über Bluetooth Low
  Energy.

Der Quelltextbestand enthält neben der Firmware auch die Fertigungsdaten der
Platine ([`board/`](board/)) und die Konstruktion des Gehäuses
([`case/`](case/)).

## Einordnung

Dies ist ein privates Bastelprojekt für die eigene Heizungsanlage. Es entsteht
in der Freizeit; eine laufende Betreuung, Unterstützung bei der Nachnutzung
oder Bearbeitung von Anfragen wird nicht zugesagt.

Das Projekt steht in keiner Verbindung zu einem Hersteller und implementiert
kein Funkprotokoll: Die Antriebe werden über eigene H-Brücken elektrisch
angesteuert, also durch Anlegen der Motorspannung. Verwendete
Produktbezeichnungen dienen allein der Angabe, welche Bauteile verbaut sind.

## Herkunft

Diese Firmware ist die Portierung eines ESPHome-Aufbaus auf das ESP-IDF. Jener
Aufbau geht seinerseits auf [floor-heating-controller][fhc] von nliaudat
zurück, eine ESPHome-Konfiguration für das Shield
[esp32_8ch_motor_shield][shield] desselben Urhebers. Von dort stammen der
Grundgedanke der Endlagenerkennung über die Gegenspannung, die Kanalauswahl
über Schieberegister und das Regelgesetz, das hier in
[`components/roomctrl`](components/roomctrl/) steht.

Die Vorlage steht unter der GPL-3.0. Dieser Quelltextbestand folgt ihr darin;
siehe [Lizenz](#lizenz).

[fhc]: https://github.com/nliaudat/floor-heating-controller
[shield]: https://github.com/nliaudat/esp32_8ch_motor_shield

## Inhalt

- [Herkunft](#herkunft)
- [Dokumentation](#dokumentation)
- [Voraussetzungen](#voraussetzungen)
- [Hardware](#hardware)
- [Übersetzen und Einspielen](#übersetzen-und-einspielen)
- [Erste Inbetriebnahme](#erste-inbetriebnahme)
- [Weboberfläche](#weboberfläche)
- [Home Assistant](#home-assistant)
- [Funktionsweise](#funktionsweise)
- [Vorgabewerte](#vorgabewerte)
- [Fehlersuche](#fehlersuche)
- [Aufbau des Quelltextbestands](#aufbau-des-quelltextbestands)
- [Entwicklung ohne Hardware](#entwicklung-ohne-hardware)
- [Prüfungen](#prüfungen)
- [Stand der Erprobung](#stand-der-erprobung)
- [Vorgängerkonfiguration](#vorgängerkonfiguration)
- [Offene Punkte](#offene-punkte)
- [Geplante Erweiterungen](#geplante-erweiterungen)
- [Fremdkomponenten](#fremdkomponenten)
- [Lizenz](#lizenz)
- [Marken](#marken)

## Dokumentation

| Dokument | Inhalt |
|---|---|
| [Benutzerhandbuch](docs/handbuch.md) | Bedienung, Einrichtung, Wartung und Fehlersuche — mit Aufnahmen beider Oberflächen |
| [Konzept: Wärmeerzeugung und Pumpensteuerung](docs/konzept-waermeerzeuger.md) | Messstellen, Brennererkennung, Ladezustand, Pumpenlogik |
| [Umbau auf zwei Anwendungen](docs/umbau-projektstruktur.md) | Aufteilung des Projekts und gemeinsame Komponenten |

Diese Seite beschreibt Hardware, Aufbau und Funktionsweise. Wer das Gerät bedienen
oder einrichten will, findet das im [Handbuch](docs/handbuch.md).

## Voraussetzungen

- **Steuergerät.** Benötigt werden die Steuerplatine aus [`board/`](board/)
  und ein darauf gesteckter ESP32-DevKitC mit WROOM-32.
- **Stellantriebe.** Vorausgesetzt werden bis zu elf motorische Stellantriebe
  ohne Endschalter am Heizkreisverteiler.
- **Raumtemperatur.** Je Raum wird ein Xiaomi-Thermometer mit ATC- oder
  pvvx-Firmware benötigt, das seine Messwerte als Rundruf sendet.
- **Netz.** Das Gerät benötigt ein WLAN-Netz. Für die Anbindung an Home
  Assistant kommt ein MQTT-Broker hinzu.
- **Vorlauffühler.** Bis zu acht DS18B20 am 1-Wire-Bus sind möglich, aber
  nicht erforderlich.
- **Entwicklungsumgebung.** Zum Übersetzen wird ESP-IDF v6.0.2 vorausgesetzt.
- **Arbeiten ohne Gerät.** Die Geräteattrappe benötigt Python 3, die
  Erzeugung der Aufnahmen zusätzlich Google Chrome und ImageMagick.

## Hardware

Verbaut sind motorische Stellantriebe vom Typ **HmIP-VDMOT**. Sie haben keine
Endschalter; die Endlage ist daran zu erkennen, dass der Motor am Anschlag
blockiert und die Spannung über dem Messwiderstand deutlich steigt.

Die Steuerplatine ist in EasyEDA selbst entworfen, von Hand geroutet und bei
**JLCPCB** gefertigt. Sie trägt
keinen eigenen Mikrocontroller: Ein **ESP32-DevKitC** wird auf Buchsenleisten
gesteckt und lässt sich damit ohne Löten tauschen. Fertigungsdaten, Gerber und
Stückliste liegen in [`board/`](board/), das Druckgehäuse in
[`case/`](case/).

| Baugruppe | Umsetzung |
|---|---|
| Stellantriebe | 11 × motorisch, ohne Endschalter (HmIP-VDMOT) |
| Anschluss der Antriebe | 11 × 4P4C-Buchse (RJ11) an der Platinenkante |
| Ansteuerung | 11 × L9110s H-Brücke, bidirektional |
| Auswahl des Kreises | 3 × SN74HC595 Schieberegister über SPI2, 22 der 24 Ausgänge belegt |
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
Kreisnummern 1 bis 11 und Gruppennummern 0 bis 5.

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

Daraus folgt die zentrale Betriebsregel: **Innerhalb einer Messgruppe fährt
immer nur ein Kreis.** Sonst liegt die Gegenspannung beider Motoren auf
demselben Eingang und die Endlage ließe sich keinem Antrieb mehr zuordnen.
Durchgesetzt wird das von der Gruppensperre in
[`apps/manifold/main/app_control.c`](apps/manifold/main/app_control.c).

## Übersetzen und Einspielen

Vorausgesetzt wird ESP-IDF v6.0.2. Das Repositorium enthält mehrere
Anwendungen, die sich `components/` teilen; gebaut wird deshalb mit `-C` aus
dem Verzeichnis der jeweiligen Anwendung. Die Verteiler-Firmware liegt unter
`apps/manifold`.

```bash
. ~/esp/esp-idf-6.0.2/export.sh && idf.py -C apps/manifold set-target esp32 && idf.py -C apps/manifold build
```

```bash
idf.py -C apps/manifold -p /dev/cu.usbserial-0001 flash monitor
```

Spätere Aktualisierungen laufen über die Weboberfläche (System → Firmware
aktualisieren) oder direkt:

```bash
curl -X POST --data-binary @apps/manifold/build/floor-heating-ctrl.bin http://<adresse>/api/ota
```

Die Firmware belegt rund 1,2 MB. Die Partitionstabelle sieht zwei
OTA-Bereiche zu je 1 966 080 Byte vor; rund 39 % bleiben frei. Eine frisch
eingespielte Firmware wird erst bestätigt, wenn Konfiguration, Ausgangsstufe,
Regelung und Weboberfläche angelaufen sind; andernfalls kehrt der Bootlader zur
vorherigen zurück.

## Erste Inbetriebnahme

1. Nach dem ersten Einspielen findet das Gerät kein WLAN und öffnet sofort
   einen Zugangspunkt `floor-heating-XXXX` mit dem Kennwort `fussboden`. Die
   vier Stellen sind die letzten beiden Bytes der MAC-Adresse.
2. Beim Verbinden öffnet sich das Einrichtungsportal von selbst (Captive
   Portal). Rufen Sie andernfalls `http://192.168.4.1` auf.
3. Wählen Sie das Netz aus, tragen Sie das Kennwort ein und speichern Sie. Der
   Zugangspunkt schließt sich, sobald die Verbindung steht und niemand mehr
   daran hängt.
4. Danach führt der Einrichtungsassistent in drei Schritten durch die Anlage:
   Bezeichnung der Etage, Räume mit ihren Heizkreisen, Thermometer je Raum. Er
   startet von selbst, solange keine Bezeichnung hinterlegt ist, und ist später
   über **System → Einrichtung erneut durchlaufen** wieder erreichbar. Die
   Thermometer erscheinen zur Auswahl, sobald ihr erster Rundruf empfangen
   wurde.
5. Starten Sie unter **Kreise** für jeden Kreis eine Messfahrt.
6. Lesen Sie unter **Sensoren** die Rohwerte der Tasten in Ruhe und bei
   Berührung ab und setzen Sie die Schwelle etwa mittig.
7. Tragen Sie unter **System** die MQTT-Verbindung ein, falls Home Assistant
   angebunden werden soll; ab Werk ist MQTT ausgeschaltet.

<img src="docs/screenshots/einrichtung.png" width="45%" alt="Einrichtungsportal">

## Weboberfläche

Räume, Kreiszuordnung, Thermometer und Regelparameter sind zur Laufzeit
änderbar. Die Weboberfläche liegt komprimiert im Programmabbild (rund 22 kB)
und lädt ohne Internetzugang.

| | |
|---|---|
| ![Räume](docs/screenshots/raeume.png) | ![Heizkreise](docs/screenshots/kreise.png) |
| **Räume** — Kreise zuordnen, Thermometer wählen, Regelparameter einstellen | **Kreise** — Zustand, Handsteuerung, Fahrzeiten und Schwellen |
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

## Home Assistant

Es gibt zwei Wege. Beide führen zu denselben Entitäten; sie schließen einander
nicht aus, sollten aber nicht gleichzeitig verwendet werden, weil sonst zwei
Wege dieselben Werte setzen.

### Eigene Integration

Unter [`custom_components/floor_heating/`](custom_components/floor_heating/)
liegt eine Integration, die unmittelbar mit dem Gerät spricht. Sie braucht
keinen MQTT-Broker und wird über HACS als benutzerdefiniertes Repository
hinzugefügt oder von Hand nach `config/custom_components/` kopiert. Eingerichtet
wird sie über **Einstellungen → Geräte & Dienste → Integration hinzufügen**;
gefragt wird nur nach der Adresse des Geräts.

Zusätzlich zu den Entitäten stellt sie fünf Dienste bereit, die sich in
Automatisierungen aufrufen lassen:

| Dienst | Wirkung |
|---|---|
| `floor_heating.calibrate` | Messfahrt für einen Kreis starten |
| `floor_heating.calibrate_accept` | Ergebnis der Messfahrt übernehmen |
| `floor_heating.calibrate_abort` | laufende Messfahrt abbrechen |
| `floor_heating.force` | Notfahrt auf, zu oder anhalten |
| `floor_heating.release` | Handbetrieb eines Kreises aufheben |

Der Zustand wird alle fünf Sekunden abgefragt. Da das Gerät seinen
Änderungszähler als ETag mitgibt, bleibt die Abfrage im Ruhezustand ohne
Inhalt (siehe [Abfragetakt](#abfragetakt)).

Ob Schnittstelle und Integration zueinander passen, prüft
`tools/check_api.py` ohne laufende Home-Assistant-Installation:

```bash
python3 tools/check_api.py 192.168.1.250
```

### MQTT-Discovery

Die Entitäten werden per MQTT-Discovery angemeldet und bei jeder
Konfigurationsänderung neu berechnet; entfallene Räume werden wieder entfernt.

| Typ | Umfang |
|---|---|
| `cover` | 11 Heizkreise mit Position, Öffnen/Schließen/Stopp |
| `climate` | je eingerichtetem Raum |
| `sensor`, je Raum | Temperatur, Luftfeuchte, Batterie des Thermometers, Zielstellung |
| `sensor`, Diagnose | 6 Messgruppen in mV, erkannte Vorlauffühler, Schaltschrankklima, Laufzeit, WLAN-Empfang, freier Speicher |
| `button` | Neustart, Regelung je Raum sofort auslösen |

Öffnen, Schließen und Stopp über die `cover`-Entitäten setzen den Halt des
Handbetriebs und bleiben damit stehen; eine gesetzte Position tut das nicht und
wird von der Regelung wieder überschrieben.

Die 22 einzelnen H-Brücken-Eingänge werden bewusst **nicht** exportiert, denn
direktes Schalten umginge Verriegelung und Gruppensperre.

## Funktionsweise

### Endlagenerkennung

Im Lauf zieht der Motor eine kleine Spannung über den Messwiderstand; erreicht
das Ventil den Anschlag, blockiert er und die Spannung steigt deutlich. Alle
sechs Messgruppen werden alle 50 ms abgetastet, ausgewertet wird ein gleitender
Median über fünf Werte. Erreicht er die Auslöseschwelle, gilt die Endlage als
erreicht und die Position wird auf 0 beziehungsweise 100 % gesetzt. Scharf ist
die Auswertung nur für die Messgruppe eines gerade fahrenden Kreises; die
Schwelle ist dann die des fahrenden Kreises.

Nach dem Anlauf gilt eine Sperrzeit von zwei Sekunden, in der die Meldung
verworfen wird, denn der Einschaltstrom sieht sonst wie ein Blockieren aus.
Bleibt die Endlage aus, beendet die Maximallaufzeit die Fahrt.

### Messfahrt zur Schwellwertermittlung

Die Auslöseschwelle hängt von Antrieb, Messwiderstand und Verkabelung ab.
Statt sie von Hand zu suchen, fährt die Messfahrt den Kreis einmal ganz zu und
einmal ganz auf und schreibt dabei den Spannungsverlauf mit. Das Blockieren
wird **relativ** zum gemessenen Fahrniveau erkannt; eine vorhandene Schwelle
geht nicht ein, sonst würde die Messfahrt ihr eigenes Ergebnis voraussetzen.

![Ergebnis einer Messfahrt](docs/screenshots/kalibrierung.png)

Braun ist die Schließfahrt, blau die Öffnungsfahrt, dazwischen liegen drei
Sekunden Ruhe. Die beiden Spitzen sind die Anschläge. Aus dem Verlauf ergeben
sich Fahrzeiten, Maximallaufzeit (längste Fahrzeit plus rund 15 %),
Auslöseschwelle (Mitte zwischen Fahrniveau und niedrigerer Spitze) und
Hysterese; übernommen werden sie erst nach Bestätigung.

Wird in einer Fahrtrichtung keine Endlage erkannt, bricht die Messfahrt mit
einem Hinweis auf Verdrahtung und Messwiderstand ab.

### Handsteuerung und Notfahrt

Jeder Kreis lässt sich von Hand fahren, **unabhängig davon, welche Stellung
die Steuerung ihm zuschreibt**. Das ist kein gewöhnlicher Fahrbefehl:
`valve_goto` prüft die geschätzte Stellung und täte nichts, wenn das Ventil
bereits als offen gilt; bei unbekannter Stellung führe es sogar erst eine
Referenzfahrt gegen die untere Endlage aus, bei „auf“ also zunächst zu. Für
den Notfall ist beides falsch.

`valve_force` fährt deshalb ohne Stellungsvergleich bis zum Anschlag. Beendet
wird die Fahrt nur durch die Endlage, die Maximallaufzeit oder einen Halt. Über
`/api/channel/all/cmd` gilt der Befehl für alle elf Kreise zugleich.

Danach bleibt der Kreis im **Handbetrieb**: Die Regelung fasst ihn nicht mehr
an, bis er ausdrücklich wieder freigegeben wird. Ohne diesen Halt wäre eine
von Hand erzwungene Stellung beim nächsten Regeldurchlauf nach spätestens
einem Prüfintervall wieder verworfen. Denselben Halt setzt ein Stoppbefehl:
Von Hand angehalten bleibt von Hand angehalten. Die Freigabe erfolgt über den
Befehl „Automatik“, einzeln oder für alle Kreise; der zugehörige Raum wird
sofort neu bewertet, statt bis zum nächsten Prüfintervall zu warten.

Ein Fahrbefehl auf eine bestimmte Stellung setzt den Halt **nicht** — er wird
von der Regelung nach spätestens einem Prüfintervall überschrieben.

Die Gruppensperre gilt weiter: Fährt in derselben Messgruppe bereits ein
anderer Kreis, wartet der Befehl, bis sie frei ist.

### Regelung

Die Ventilstellung folgt der Regelabweichung proportional, über ein Band von
±1 K (Vorgabe) von ganz zu bis ganz auf, gerastert auf Zehntel:

```text
diff = soll − ist
pos  = runden(((diff / band) + 1) / 2 / raster) × raster,   begrenzt auf 0 … 1
gefahren wird, wenn |aktuell − pos| > Mindeständerung
```

Band, Rasterung, Mindeständerung und Prüfintervall sind je Raum einstellbar.
Der erste Regeldurchlauf findet erst 50 Sekunden nach dem Start statt, damit
Messwerte und Netzwerk eingelaufen sind. Die Ventilstellungen werden höchstens
einmal je Minute in den NVS geschrieben und beim Start wiederhergestellt.

### Raumtemperatur

Der ESP32 hört die Raumthermometer selbst mit: Xiaomi-Thermometer mit ATC- oder
pvvx-Firmware senden ihre Messwerte offen als Rundruf. Es wird keine Verbindung
aufgebaut und nichts gesendet. Damit hängt die Regelung nicht am Netzwerk.

Bleibt ein Messwert länger als eingestellt aus (Vorgabe 900 s), setzt die
Regelung für diesen Raum aus und die Ventile bleiben stehen, statt mit einem
veralteten Wert weiterzuregeln. Dasselbe gilt, solange für einen Raum noch kein
Messwert eingegangen ist.

## Vorgabewerte

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

## Fehlersuche

Die folgenden Fälle ergeben sich aus dem beschriebenen Verhalten des Geräts.

| Beobachtung | Ursache | Abhilfe |
|---|---|---|
| Das Gerät verbindet sich nicht, ein Zugangspunkt `floor-heating-XXXX` erscheint | Es sind keine WLAN-Zugangsdaten hinterlegt | Über das Einrichtungsportal verbinden |
| Das Einrichtungsportal öffnet sich beim Verbinden nicht von selbst | Das Betriebssystem erkennt das Captive Portal nicht | `http://192.168.4.1` aufrufen |
| Ein Raum wird nicht geregelt, die Ventile stehen | Der Messwert ist älter als das eingestellte Höchstalter, oder es liegt noch keiner vor | Empfang unter **Sensoren** prüfen |
| Die Messfahrt bricht mit einem Hinweis ab | In einer Fahrtrichtung wurde keine Endlage erkannt | Verdrahtung und Messwiderstand prüfen |
| Ein Kreis folgt der Regelung nicht | Der Kreis steht im Handbetrieb | Befehl „Automatik“ für den Kreis oder für alle Kreise |
| Ein Fahrbefehl wird erst verzögert ausgeführt | In derselben Messgruppe fährt bereits ein anderer Kreis | Abwarten; die Gruppensperre gibt den Befehl frei |
| Die Tasten am Gehäuse lösen nicht aus | Die Tastenschwellen passen nicht zur Baugruppe | Rohwerte unter **Sensoren** ablesen, Schwelle mittig setzen |
| Nach einer Aktualisierung läuft wieder die vorherige Firmware | Die neue Fassung ist nicht vollständig angelaufen und wurde nicht bestätigt | Ursache über `idf.py monitor` suchen |

## Aufbau des Quelltextbestands

```text
apps/manifold/    Verteiler-Firmware
  main/
    main.c        Start der Bausteine
    app_control.c Ventilzustände, Gruppensperre, Raumregelung   ← Kern
    app_calib.c   Messfahrt und Auswertung der Schwellwerte
    app_web.c     HTTP-Server, JSON-Schnittstelle, Captive Portal, OTA
    app_mqtt.c    Home-Assistant-Discovery und Zustände
    app_ui.c      Anzeige und Tasten am Gerät
    www/            Oberfläche: kopf.html, rumpf.html und sources.txt
apps/heatsource/  Firmware der Heizungsgeräte (Kessel und Pufferspeicher)
  components/     eigene Konfiguration, verdrängt die gemeinsame
  main/           Fühlererfassung, Verlauf, Weboberfläche
www/stil.css      gemeinsames Gerüst beider Oberflächen: Farben, Schriften,
                  Kopfzeile, Reiter — die Bedienelemente bleiben je Anwendung eigen
cmake/embed_www   Bauregel: Oberfläche zusammenfügen, komprimieren, einbetten
components/       von allen Anwendungen gemeinsam genutzt
  hw_map/         Pin- und Gruppentabellen — einzige Stelle mit Hardwarewissen
  sr74hc595/      Schieberegister mit Verriegelung IA gegen IB
  valve/          Zustandsmaschine je Kreis (frei von IDF-Abhängigkeiten)
  bemf/           ADC-Abtastung, gleitender Median, Schwellwertauswertung
  roomctrl/       Regelgesetz (frei von IDF-Abhängigkeiten)
  schedule/       wöchentlicher Termin an der Uhr, für die Schutzfahrt
  config_store/   Konfiguration als JSON im NVS
  atc_ble/        NimBLE-Observer, ATC- und pvvx-Dekoder
  ssd1327/        Anzeigetreiber mit Bitmap-Schrift
  sensors_local/  DS18B20 und HDC1080
  onewire_temp/   mehrere DS18B20 an bis zu zwei Bussen, Sammelwandlung
  netmgr/         WLAN, SNTP, täglicher Neustart
  captive_dns/    Namensdienst des Einrichtungsportals
  i2cbus/         gemeinsamer I²C-Bus
board/            Fertigungsdaten der Platine: Gerber, DXF, Stückliste (eigene
                  README und eigene Lizenz)
case/             Druckgehäuse, mit CadQuery konstruiert (eigene README)
test/host/        Prüfungen ohne IDF und ohne Hardware
tools/            Geräteattrappe, Bildschirmaufnahmen, Prüfung der Schnittstelle
custom_components/
                  Integration für Home Assistant (über HACS einzubinden)
docs/             Konzepte und Aufnahmen dieser Seite
```

## Entwicklung ohne Hardware

Die Weboberfläche lässt sich ohne Gerät bedienen. `tools/mock_device.py`
liefert die echte Seite mit erfundenen Messwerten aus:

```bash
python3 tools/mock_device.py
```

Rufen Sie danach `http://localhost:8321` auf. Der Parameter `?theme=dark`
erzwingt ein Farbschema, `#kreise` und die übrigen Anker öffnen einen Bereich
direkt. Mit `/mock/ap?on=1` schaltet die Attrappe in den Zugangspunkt-Betrieb,
sodass sich das Einrichtungsportal ansehen lässt. Aus diesen Ansichten erzeugt
`tools/screenshots.sh` die Aufnahmen dieser Seite; vorausgesetzt werden Google
Chrome und ImageMagick.

Mit `--app` liefert die Attrappe die Oberfläche einer anderen Anwendung aus;
der Port richtet sich dann nach der Anwendung, sofern `--port` nichts anderes
vorgibt. Für die Heizungsgeräte gibt es eine eigene Attrappe:

```bash
python3 tools/mock_heatsource.py
```

Sie bildet ein Speicherboard nach; mit `--kessel` stattdessen ein Kesselboard,
mit `--leer` ein Gerät ohne angeschlossene Fühler. Aufrufbar unter
`http://localhost:8322`.

Die Aufnahmen dieser Seite und des Handbuchs entstehen gegen die beiden Attrappen:

```bash
tools/screenshots.sh        # Verteilerplatine, gegen mock_device.py
tools/screenshots_heat.sh   # Heizungsgerät, gegen mock_heatsource.py
```

Beide Skripte prüfen nebenbei, dass die Oberfläche in beiden Farbschemata und in der
schmalen Ansicht steht. Vorausgesetzt werden Google Chrome und ImageMagick.

## Prüfungen

Regelgesetz, Ventil-Zustandsmaschine und Hardwarezuordnung laufen ohne IDF und
ohne Hardware:

```bash
make -C test/host
```

Der Lauf umfasst 297 Prüfungen: Regelgesetz, Ventil-Zustandsmaschine, Hardwarezuordnung,
Pumpensteuerung, Bedarfsauswertung, Brennererkennung und Ladezustand.

Dazu prüft

```bash
python3 tools/check_www.py
```

dass die eingebetteten Oberflächen übersetzbar sind — sie werden dafür aus ihren Teilen
zusammengesetzt, geprüft wird also das, was auch im Gerät landet — der Skriptteil wird mit `node --check`
geprüft. Anlass war, dass beim Bearbeiten zweimal ein Block an der falschen Stelle gelandet ist:
einmal JavaScript im Stilblatt, einmal CSS im Skript. Im Quelltext fällt das nicht auf, im
Browser bleibt die Seite leer.

Gegen ein laufendes Gerät prüft

```bash
python3 tools/check_api.py 192.168.1.250
```

die Schnittstelle mit derselben Klasse, die auch die Home-Assistant-Integration benutzt. Beide
Gerätearten werden erkannt.

An der Hardware sind folgende Schritte vorgesehen:

1. **Schieberegister.** Schalten Sie alle 22 Ausgänge einzeln, messen Sie gegen
   die L9110s-Eingänge und prüfen Sie die Verriegelung: IA und IB dürfen nie
   gleichzeitig anliegen.
2. **Ventilfahrt.** Fahren Sie einen Kreis ganz zu und ganz auf und prüfen Sie
   Laufzeit und Abschaltung über die Endlage. Die Maximallaufzeit muss greifen,
   wenn die Endlage ausbleibt.
3. **Messfahrt.** Führen Sie eine Messfahrt eines Kreises durch und halten Sie
   den Verlauf gegen den Vorgabewert von 190 mV.
4. **Gruppensperre.** Lassen Sie Kreis 1 fahren und fordern Sie währenddessen
   Kreis 2 an; Kreis 2 darf erst danach fahren.
5. **Tasten.** Lesen Sie unter **Sensoren** die Rohwerte in Ruhe und bei
   Berührung ab und setzen Sie die Schwelle etwa mittig.
6. **MQTT.** Prüfen Sie, ob die Entitäten in Home Assistant erscheinen.
   Löschen Sie einen Raum und legen Sie ihn neu an; die Entitäten müssen
   verschwinden und wiederkommen.

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

## Vorgängerkonfiguration

Vorgänger war eine ESPHome-Konfiguration mit rund 1750 Zeilen YAML, in der die
Regel- und Endlagenlogik bereits vollständig in Lambda-Blöcken steckte. Räume
und Kreiszuordnung waren dort fest verdrahtet; jede Änderung erforderte
Neuübersetzung und Einspielen. Das war der Anlass für die Portierung.

### Bereinigte Unstimmigkeiten

Vier Unstimmigkeiten der ESPHome-Fassung sind dabei bereinigt:

1. **Endlagenzuordnung Kreis 9 bis 11.** Die Cover gaben `BEMF_4_sensor` an,
   gehören laut Verdrahtung aber zu Gruppe 5 beziehungsweise 6. Wirksam wurde
   die Endlage bisher nur über die `on_press`-Lambdas.
2. **Belegtprüfung bei Kreis 5.** Geprüft wurde Kreis 4; Kreis 5 teilt sich
   die Messgruppe jedoch mit Kreis 6.
3. **Kapazitive Tasten.** Die Raumauswahl zählte 0 bis 3, die Tasten verglichen
   gegen 10 bis 13 und lösten deshalb nie aus.
4. **Abbruch statt Überspringen.** War ein Kreis gesperrt, brach die Prüfung
   des gesamten Raums ab. Jetzt wird nur der betroffene Kreis übersprungen.

### Bewusst geändertes Verhalten

- **Schnellere Endlagenerkennung.** ESPHome bildete den Median über fünf Werte
  und meldete nur jeden fünften, also alle 1,25 s. Jetzt wird bei 50 ms
  Abtastung ein gleitender Median bei jedem Wert ausgewertet.
- **Ein Sollwert statt low/high.** Das Original mittelte ohnehin über beide.
- **Ventilstellungen im NVS.** Sie werden beim Start wiederhergestellt. Ist
  keine Stellung bekannt, fährt der Kreis beim ersten Regeleingriff einmal auf
  Anschlag.
- **Veralteter Messwert setzt die Regelung aus.** Das Original hätte
  stillschweigend mit dem letzten bekannten Wert weitergeregelt.
- **Raumtemperatur über BLE statt über Home Assistant.** Die Regelung hängt
  damit nicht mehr am Netzwerk.
- **Räume zur Laufzeit konfigurierbar.** Sie stehen nicht mehr fest im YAML.

## Offene Punkte

- **Tastenschwellen** (1000, 870, 1000) stammen aus der ESPHome-Fassung. Die
  Messdauer der neuen Treiberfassung weicht ab, sie sind nur ein Ausgangspunkt
  und unter **Sensoren** nachzustellen.
- **MAC-Adressen der Thermometer** ergeben sich beim ersten BLE-Empfang.
- **Matter und Thread** sind nicht umgesetzt. Thread scheidet auf dieser
  Platine aus, weil dem ESP32-WROOM-32 das 802.15.4-Funkteil fehlt. Matter über
  WLAN passt nicht in den verfügbaren Programmspeicher. Wird Matter gebraucht,
  reicht Home Assistant die vorhandenen MQTT-Entitäten über seine eigene
  Matter-Bridge weiter.

## Geplante Erweiterungen

Die Steuerung wird um den Heizungsbereich ergänzt: zwei weitere ESP32 erfassen die Temperaturen
an Ölkessel und Pufferspeicher und sollen die beiden Heizkreispumpen abschalten, solange kein
Ventil offen ist. Das Repository trägt dafür eine zweite Anwendung unter
[`apps/heatsource/`](apps/heatsource/), die sich die Komponenten mit der Verteiler-Firmware
teilt.

Umgesetzt sind Fühlererfassung mit Zuordnung über die Weboberfläche, Anlagenschema, Verlauf über
24 Stunden, Bedarfsabfrage bei den Verteilern, Pumpensteuerung über ein Tasmota-Relais (über MQTT
oder unmittelbar über HTTP), Brennererkennung mit Tagesstatistik und Verbrauchsschätzung,
Aufzeichnung einer Ladung als CSV sowie die Beurteilung des Ladezustands. Die Zahl der
Heizkreise ist nicht festgelegt; vorgesehen sind bis zu vier.

Erprobt ist bislang der Teil, für den keine Fühler nötig sind: Bedarfsabfrage, Pumpenlogik,
gegenseitiges Auffinden und die Oberfläche. Alles, was Messwerte braucht, wartet auf den Anschluss
der Fühler.

- [Konzept: Wärmeerzeugung, Pufferspeicher und Pumpensteuerung](docs/konzept-waermeerzeuger.md)
  — Messstellen, Brennererkennung, Ladezustand, Bedarfserkennung, Pumpenlogik, Schnittstellen
- [Umbau auf zwei Anwendungen](docs/umbau-projektstruktur.md)
  — Aufteilung des Projekts, gemeinsame Komponenten, Nachweis der Unverändertheit

## Fremdkomponenten

Grundlage der Firmware ist das ESP-IDF von Espressif; der Empfang der
Raumthermometer setzt auf NimBLE auf. Über die ESP-IDF-Registry werden zur
Bauzeit vier Komponenten eingebunden, die eigenen Lizenzen unterliegen und
nicht Teil dieses Quelltextbestands sind:

| Komponente | Lizenz |
|---|---|
| `espressif/cjson` | MIT |
| `espressif/mqtt` | Apache-2.0 |
| `espressif/onewire_bus` | Apache-2.0 |
| `espressif/ds18b20` | Apache-2.0 |

Die Raumthermometer senden mit der freien Fremdfirmware ATC beziehungsweise
pvvx. Das Gehäuse ist mit CadQuery konstruiert.

## Lizenz

| Teil | Lizenz |
|---|---|
| Firmware, Werkzeuge, Dokumentation, `case/` | GNU General Public License, Version 3 — [LICENSE](LICENSE) |
| Entwurfsdaten der Platine, `board/` | CERN Open Hardware Licence v2, Strongly Reciprocal — [board/LICENSE](board/LICENSE) |

Die GPL folgt der Vorlage, auf die dieses Projekt zurückgeht — siehe
[Herkunft](#herkunft). Für die Platine passt sie nicht: Ihre Rückgabepflicht
hängt am Weitergeben von Objektcode, und eine gefertigte Leiterplatte ist keine
Kopie der Entwurfszeichnung. Die CERN-OHL knüpft dieselbe Pflicht an das
Bereitstellen des Produkts. Die Haltung ist in beiden Fällen dieselbe: offen,
mit Rückgabe. Einzelheiten in [board/README.md](board/README.md).

Das Gehäuse unter `case/` ist ein CadQuery-Programm und bleibt deshalb bei der
GPL.

Die eingebundenen Fremdkomponenten stehen unter MIT und Apache-2.0. Beide
lassen sich mit der GPL-3.0 zusammenführen; die zusammengesetzte Firmware
unterliegt dann der GPL-3.0.

Die Software wird ohne jede Gewährleistung bereitgestellt. Sie steuert eine
Heizungsanlage: Wenn Sie sie einsetzen, tun Sie das auf eigenes Risiko. Gehen
Sie die [Prüfungen](#prüfungen) an der eigenen Hardware durch, bevor Sie die
Anlage unbeaufsichtigt laufen lassen.

## Marken

HmIP und Homematic IP sind Marken der eQ-3 AG. Sie werden hier ausschließlich
genannt, um die verbauten Stellantriebe zu bezeichnen. Es besteht keine
Verbindung zu eQ-3, und es wird weder eine Kompatibilität noch eine Freigabe
behauptet.
