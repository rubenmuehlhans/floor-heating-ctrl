# Funktionsabgleich Weboberflächen und App

Stand 23.09.2026, Etappe 8, dazu der Leitstand (Abschnitt 6). Grundlage sind die Weboberflächen der Firmware:

- Verteiler: `apps/manifold/main/www/rumpf.html`, Routen in `apps/manifold/main/app_web.c` (Z. 1115–1134), Konfiguration in `components/config_store/config_store.c`
- Heizungsgerät: `apps/heatsource/main/www/rumpf.html`, Routen in `apps/heatsource/main/app_web.c` (Z. 1396–1418), Konfiguration in `apps/heatsource/components/config_store/config_store.c`

Jede Funktion der Weboberflächen ist einer Stelle der App zugeordnet. Die Pfade der App sind als Folge der Seiten angegeben, etwa „Geräte › Verteiler › Kanal n“.

| Stand | Bedeutung |
|---|---|
| abgedeckt | Die App bietet die Funktion mit gleicher Wirkung. |
| anders | Die App bietet die Funktion, weicht aber bewusst ab; der Grund steht in der Spalte. |
| teilweise | Ein Teil fehlt; was fehlt, steht in der Spalte. |
| offen | Die App bietet die Funktion noch nicht. |

Für alle Befehle gilt: Die App liest nach jedem Befehl den Zustand zurück und meldet, wenn die Firmware einen Befehl quittiert, ohne ihn auszuführen. Vor jedem Schreiben der Konfiguration sichert sie das Gerät, sofern die letzte Sicherung älter als zehn Minuten ist, und vergleicht danach jeden geschriebenen Wert mit dem gelesenen.

## 1 Verteiler

### 1.1 Allgemein (rumpf.html Z. 235–250, 538–657, 1751–1855)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Kopfzeile: Ort, Adresse, Signal, Außentemperatur, Laufzeit, freier Speicher, Firmware | `GET /api/state` | Geräte › Verteiler, Abschnitt oben. Die Außentemperatur steht unter Heizung in der Karte „Anlage“ mit Quelle, Feuchte und Alter, dazu als Kennzahl in der Übersicht | abgedeckt |
| Anzeige „verbindet …“ und „keine Verbindung zum Gerät“ | `GET /api/state` | Geräteliste und Geräteseite zeigen „nicht erreichbar“; Befund in der Übersicht. Erst die zweite fehlgeschlagene Abfrage in Folge gilt als Ausfall. | abgedeckt |
| Verweise auf die Nachbargeräte | `GET /api/peers` | Alle Geräte stehen unter Geräte; der Verteiler selbst wird nicht nach Nachbarn gefragt. | anders |
| Abfrage 1 s bei Bewegung, sonst 6 s mit ETag, alle 60 s ohne ETag | `GET /api/state` | gleicher Takt; Konfiguration alle 5 min, Thermometer alle 30 s | abgedeckt |
| Speichern sendet die gesamte Konfiguration | `PUT /api/config` | Die App sendet nur die geänderte Gruppe; die Raumliste wird vor dem Schreiben frisch gelesen und vollständig gesendet. Die Weboberfläche setzt dabei zwischenzeitlich geänderte Sollwerte zurück (siehe 4.1). | anders |
| Kennwort: leeres Feld behält das gespeicherte | `PUT /api/config` | Formulare zeigen „gespeichert“ oder „nicht gesetzt“; ein leeres Feld wird nicht gesendet. | abgedeckt |

### 1.2 Einrichtungskarte im Zugangspunktbetrieb (Z. 253–261, 1825–1833)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| „Gerät einrichten“, Netzsuche, WLAN-Daten | `POST`/`GET /api/wifi/scan`, `PUT /api/config` | Geräte › Gerät hinzufügen: Einrichtungsassistent mit Beitritt zum Zugangspunkt, Netzsuche, Kennwort und Ort | abgedeckt |

### 1.3 Übersicht `#ov` (HTML Z. 263–271, JS Z. 661–831)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Verteilerbild: Balken je Kreis, gruppiert nach Raum, Füllung nach Stellung | `GET /api/state` | Räume › Karte „Verteiler <Etage>“ | abgedeckt |
| Raumkarte: Zustand, Temperatur, Abweichung, Feuchte, Batterie, Alter, Kreise, Zielstellung, nächste Prüfung | `GET /api/state` | Räume › Raumkarte; Räume › Raum | abgedeckt |
| Sollwert „−“, „+“ und Eingabefeld | `POST /api/room/{id}/target` | Räume › Stellrad in Schritten von 0,5 K. Die App begrenzt auf 5–35 °C; die Firmware quittiert Werte außerhalb ohne Wirkung. | abgedeckt |
| „Ausschalten“, „Einschalten“ | `POST /api/room/{id}/mode` | Räume › Raumkarte und Räume › Raum | abgedeckt |
| Hinweis „Noch kein Raum eingerichtet“ | – | Räume, je Etage | abgedeckt |

### 1.4 Räume `#rooms` (HTML Z. 273–284, JS Z. 835–924)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Name, Kreise (belegte gesperrt), Thermometer je Raum | `PUT /api/config` `rooms[]` | Geräte › Verteiler › Räume und Kanäle. Die App warnt, wenn ein Thermometer mehreren Räumen zugeordnet ist. | abgedeckt |
| Sollwert, Proportionalband, Prüfintervall, Rasterung, Mindeständerung, Betriebsart | `PUT /api/config` `rooms[]` | Räume › Raum › Regelparameter | abgedeckt |
| „Raum hinzufügen“, „Raum entfernen“ mit Rückfrage | `PUT /api/config` `rooms[]` | Geräte › Verteiler › Räume und Kanäle | abgedeckt |

### 1.5 Kreise `#chans` (HTML Z. 286–358, JS Z. 928–1235)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Handsteuerung: „Motor auf“, „Motor aus“, „Motor zu“ | `POST /api/channel/{n}/cmd` `open`/`stop`/`close` | Geräte › Verteiler › Kanal n › Handsteuerung. Zum Umkehren einer Fahrt hält die App zuerst an, weil die Firmware einen Befehl an einen fahrenden Kanal bis zum Ende der Fahrt zurückstellt. | abgedeckt |
| „Wieder an die Regelung übergeben“ | `…/cmd` `auto` | Kanal n › Handsteuerung | abgedeckt |
| „Alle auf“, „Alle zu“, „Alle aus“, „Alle an die Regelung“ mit Rückfrage | `POST /api/channel/all/cmd` | Geräte › Verteiler › Alle Kanäle. „Alle anhalten“ lehnt die App während einer Messfahrt ab, weil `stop` die Messfahrt abbricht. | anders |
| Tabelle der Kreise: Raum, Gruppe, Stellung, Zustände, Gegenspannung | `GET /api/state` | Geräte › Verteiler › Kanäle; Kanal n | abgedeckt |
| „%“: Zwischenstellung | `…/cmd` `position` | Kanal n › Zwischenstellung, Schritte von 5 % | abgedeckt |
| Fahrzeiten und Schwellen: Öffnen, Schließen, Maximal, Sperrzeit, Schwelle, Hysterese | `PUT /api/config` `channels[]` | Kanal n › Fahrzeiten und Schwellen bearbeiten. Gesendet wird nur der geänderte Kanal mit seiner Kennung. | abgedeckt |
| Messfahrt: Start mit Rückfrage, Abbrechen, Kurve, Ergebnis, „Werte übernehmen“, „Verwerfen“ | `POST /api/calib/{n}/start`, `/abort`, `/accept`, `/discard`; `GET /api/calib?from=` | Geräte › Verteiler › Messfahrt. Vor dem Übernehmen sichert die App; danach prüft sie `calibrated` in der Konfiguration und räumt erst dann mit „Verwerfen“ auf. | abgedeckt |

### 1.6 Sensoren `#sensors` (HTML Z. 360–404, JS Z. 1239–1320)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Raumthermometer: Temperatur, Feuchte, Batterie, Signal, Format, Zuordnung | `GET /api/ble` | Geräte › Verteiler › Sensoren | abgedeckt |
| Schlüssel verschlüsselter Thermometer (BTHome, etwa Climate-Sat): Stand je Gerät, eingeben, ersetzen, entfernen | `GET /api/ble` `key`, `keys`; `POST /api/ble/key` | Sensoren › Thermometer antippen; Schlüssel ohne empfangenes Gerät in eigenem Abschnitt. Die App prüft die Eingabe auf 32 Hexadezimalziffern und behält den Schlüssel nicht; an die KI gelangt er nicht (`bindkey` in der Schwärzung). | abgedeckt |
| Außenfühler wählen | `PUT /api/config` `outdoor_mac` | Sensoren › Außenfühler | abgedeckt |
| Fühler auf der Platine: 1-Wire-Vorlauffühler, Klima im Schaltschrank | `GET /api/state` | Sensoren › Fühler im Gehäuse | abgedeckt |
| Tasten: Rohwert und Schwelle | `GET /api/state` | Sensoren › Tasten am Gehäuse; System › Tasten am Gerät | abgedeckt |

### 1.7 System `#sys` (HTML Z. 406–519, JS Z. 1324–1538)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Netzwerk: „Netze suchen“, WLAN-Name, Kennwort, Gerätename, Kennwort des Zugangspunkts | `POST`/`GET /api/wifi/scan`, `PUT /api/config` `wifi` | Geräte › Verteiler › System › Netzwerk | abgedeckt |
| MQTT: Schalter, Broker, Benutzer, Kennwort, Themenpräfix | `PUT /api/config` `mqtt` | System › MQTT | abgedeckt |
| Betrieb: Bezeichnung, Messwert veraltet nach, Helligkeit | `PUT /api/config` | System › Betrieb | abgedeckt |
| Tasten verwenden, Schwellen der drei Tasten | `PUT /api/config` `touch` | System › Tasten am Gerät | abgedeckt |
| Zeitzone, täglicher Neustart | `PUT /api/config` | System › Zeit und Termine. Nach dem Speichern bietet die App den Neustart an, weil diese Werte erst danach wirken. | abgedeckt |
| „Einrichtung erneut durchlaufen“ | – | Geräte › Gerät hinzufügen; Räume unter Räume und Kanäle | abgedeckt |
| „Neu starten“ mit Rückfrage | `POST /api/system/restart` | Geräte › Verteiler › Neu starten | abgedeckt |
| „Auf Werksvorgabe zurücksetzen“ mit Rückfrage | `POST /api/system/factory` | Geräte › Verteiler › Auf Werksvorgabe zurücksetzen. Die Rückfrage nennt den Verlust des WLAN-Zugangs; die App sichert vorher und startet das Gerät danach neu. | anders |
| „Sicherung holen“ | `GET /api/config/backup` | System › Sicherung › Jetzt sichern; Ablage verschlüsselt in der App, Weitergabe als Datei | anders |
| „Sicherung einspielen“ | `POST /api/config/restore` | System › Sicherung › Einspielen oder Aus Datei einspielen. Die App verlangt den Kopf `backup.app` des passenden Gerätetyps schon vor dem Senden. | abgedeckt |
| Schutzfahrt: Zustand, Termin, „Jetzt fahren“, „Alle Kreise fahren“, „Abbrechen“ | `PUT /api/config` `seize_*`, `POST /api/system/seize`, `/seize-all`, `/seize-abort` | System › Zeit und Termine › Schutzfahrt | abgedeckt |
| Firmware übertragen | `POST /api/ota` | System › Firmware. Die App prüft den Projektnamen im Abbild, fragt vor dem Übertragen nach und sichert vorher. | anders |

### 1.8 Einrichtungsassistent (HTML Z. 522–534, JS Z. 1540–1749)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Etage mit abgeleitetem Gerätenamen und MQTT-Präfix | `PUT /api/config` | Einrichtungsassistent der App, Schritt „Neues Gerät“ | abgedeckt |
| Räume mit Kreisen | `PUT /api/config` `rooms[]` | Geräte › Verteiler › Räume und Kanäle | abgedeckt |
| „Übliche Aufteilung übernehmen“ | – | nicht übernommen; die Vorlage beschreibt nur eine bestimmte Etage | offen |
| Thermometer je Raum | `PUT /api/config` `rooms[].sensor_mac` | Räume und Kanäle | abgedeckt |

### 1.9 Raumverlauf

| Funktion | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Verlauf von Raumtemperatur, Sollwert und Ventilstellung | – | Räume › Raum › Verlauf, 24 Stunden und 7 Tage. Der Verteiler speichert keinen Verlauf; die Werte stammen aus der Aufzeichnung der App und aus importierten Mitschnitten. | zusätzlich |

### 1.10 Nur über die Schnittstelle

| Funktion | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Regelung eines Raums sofort auslösen | `POST /api/room/{id}/check` | Räume › Raum › Jetzt prüfen | abgedeckt |
| Wärmebedarf für das Heizungsgerät | `GET /api/demand` | nicht abgefragt; der Bedarf erscheint über das Heizungsgerät (1-Wire-Bus und Bedarfsabfrage) | anders |

## 2 Heizungsgerät

### 2.1 Allgemein (HTML Z. 229–244, JS Z. 676–772, 1620–1648, 1983–2307)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Kopfzeile: Adresse, Signal, Fühler zugeordnet, Leserunde, Laufzeit, Speicher, Firmware | `GET /api/state` | Geräte › Kessel bzw. Pufferspeicher; Fühler am Bus und System › 1-Wire-Bus zeigen die Messrunde | abgedeckt |
| Verweise auf die Nachbargeräte | `GET /api/peers` | Alle Geräte stehen unter Geräte. Die Nachbarn des Speichergeräts werden jede Minute gelesen und dienen der Auswahl der versorgten Verteiler. | anders |
| Abfrage alle 5 s | `GET /api/state` | gleicher Takt; Geräteverlauf alle 30 min, Protokolle bei neuem Stand, sonst alle 10 min | abgedeckt |
| Speichern je Karte als Teilangabe | `PUT /api/config` | gleiches Verfahren; die App prüft vorher wie die Firmware (Grenzen, doppelte Rollen, Abhängigkeiten der Fühlerrollen, GPIO) | abgedeckt |

### 2.2 Einrichtungskarte (Z. 247–252)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| „Gerät einrichten“, „Einrichtung starten“ | – | Geräte › Gerät hinzufügen | abgedeckt |

### 2.3 Übersicht `#ov` (HTML Z. 254–357, JS Z. 799–1274)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Auffälligkeiten (`findings`) | `GET /api/state` | Übersicht › Befunde, Texte wie die Weboberfläche, gleiche Befunde beider Geräte einmal | abgedeckt |
| Anlagenschema mit Messwerten; Werte des Nachbargeräts blass | `GET /api/state` | Heizung › Anlagenschema; welches Gerät einen Wert gemessen hat, zeigt die App nicht | teilweise |
| Kesselkreispumpe per Klick im Schema umschalten | `POST /api/boilerpump/{mode}` | Heizung › Kesselkreispumpe, Wahl Automatik, Ein, Aus | anders |
| Pufferspeicher: Phase, Füllstand, Spreizung, Hinweise | `GET /api/state` | Heizung › Pufferspeicher | abgedeckt |
| Speichereinstellungen: Leer und voll, Warmwasser, Ladungsende (11 Schlüssel) | `PUT /api/config` `buffer` | Heizung › Pufferspeicher › Speichereinstellungen. Die App schreibt die Werte an beide Heizungsgeräte, weil beide denselben Speicher bewerten. Die Karte der Weboberfläche ist unsichtbar, solange Füllstand und Spreizung fehlen; die App zeigt die Einstellungen immer. | anders |
| Brenner: Zustand, Abgas, Laufzeit, Starts, Öl heute | `GET /api/state` | Heizung › Brenner | abgedeckt |
| Brennererkennung: Ein ab, Aus unter, Ausschlag, Haltezeiten, Düsendurchsatz | `PUT /api/config` `burner` | Heizung › Brenner › Brennererkennung | abgedeckt |
| Messwerte je Fühler, Spreizungen | `GET /api/state` | Geräte › Heizungsgerät › Fühler am Bus; Heizung › Anlagenschema | abgedeckt |

### 2.4 Heizkreise `#circuits` (HTML Z. 359–426, JS Z. 1280–1550)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Kesselkreispumpe: Automatik, Ein, Aus | `POST /api/boilerpump/{auto\|ein\|aus}` | Heizung › Kesselkreispumpe | abgedeckt |
| Kesselkreispumpe: Zustand mit Grund, Schaltweg, Relaiszustand; Grund `burner` („der Brenner läuft“) ab der Fassung nach 0.4.0 | `GET /api/state` | Heizung › Kesselkreispumpe; Geräte › Pumpenrelais | abgedeckt |
| Kesselkreispumpe: vorhanden, Relais, Schaltpunkte, Zeiten, Notgrenze | `PUT /api/config` `boiler_pump` | Heizung › Kesselkreispumpe › Schaltpunkte | abgedeckt |
| Heizkreis: Automatik, Ein, Aus | `POST /api/circuit/{id}/mode` | Heizung › Karte des Heizkreises | abgedeckt |
| Heizkreis: Pumpe, Grund, Vor- und Rücklauf, Spreizung; Hinweise „Verteiler antwortet nicht“, „kein Verteiler erreicht“, „Relais folgt nicht“ | `GET /api/state` | Heizung › Karte des Heizkreises | abgedeckt |
| Bedarf je versorgtem Verteiler | `GET /api/state` `demand_sources` | Geräte › Pufferspeicher › System › Bedarfsabfrage; die Karte des Heizkreises nennt nur, ob Bedarf besteht | teilweise |
| Name, Relais, Nachlauf, Mindestlaufzeit, Mindestpause, Speicher mindestens, Frostgrenze | `PUT /api/config` `circuits[]` | Heizung › Heizkreis › Pumpenlogik und Relais. Gesendet werden alle Kennungen, damit kein Kreis wegfällt. | abgedeckt |
| Versorgte Verteiler | `PUT /api/config` `circuits[].peers` | Heizung › Heizkreis › Versorgte Verteiler. Zur Wahl stehen die eingebundenen Verteiler und die, die das Heizungsgerät sieht; gespeicherte Zuordnungen zu Verteilern, die gerade nicht antworten, bleiben erhalten. | anders |
| „Heizkreis löschen“ mit Rückfrage | `PUT /api/config` `circuits[]` | Heizung › Heizkreis › Heizkreis löschen | abgedeckt |
| „Heizkreis anlegen“ | `PUT /api/config` `circuits[]` | Geräte › Pufferspeicher › Heizkreis anlegen | abgedeckt |
| „Alle Heizkreise entfernen“ (ohne Pufferfühler, ohne Rückfrage) | `PUT /api/config` `circuits: []` | nicht übernommen; Löschen je Kreis mit Rückfrage | anders |
| Relais unmittelbar prüfen, Ausfallregel einrichten | Tasmota `/cm` | Heizung › Heizkreis › Relais; nur in der App | zusätzlich |

### 2.5 Fühler `#probes` (HTML Z. 428–441, JS Z. 1556–1618)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Tabelle: Kennung, Bus, Messwert, 30-s-Änderung, Rolle, Name, Korrektur, Fehler | `GET /api/state` | Geräte › Heizungsgerät › Fühler am Bus | abgedeckt |
| Rolle, Name und Korrektur zuordnen, „Zuordnung speichern“ | `PUT /api/config` `probes[]` | Fühler am Bus › Fühler. Die App sendet alle Fühler mit ihrer Kennung und prüft doppelte Rollen und die Abhängigkeiten von Heizkreisen und Kesselkreispumpe vor dem Schreiben. | abgedeckt |
| „Bus neu absuchen“ | `POST /api/probes/rescan` | Fühler am Bus › Bus neu absuchen | abgedeckt |

### 2.6 Verlauf `#hist` (HTML Z. 443–516, JS Z. 871–946, 1652–1829)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Verlauf 1 h, 6 h, 24 h; Reihen ein- und ausblenden | `GET /api/history?step=&max=` | Heizung › Verlauf mit 6 Stunden, 24 Stunden, 7 Tagen und 30 Tagen, je Gruppe eine Tafel mit eigener Skala (Kessel und Speicher, Abgas, Brenner und Speicher, je Heizkreis Vor- und Rücklauf mit Spreizung und Pumpenlauf, Räume, Vorlauf an den Verteilern) und gemeinsamer Zeitauswahl; die Außentemperatur je Heizkreis auf fester rechter Achse. Je Heizkreis zusätzlich Verlauf und Heizkurve (Vorlauf über Außentemperatur mit Ausgleichsgerade). Den Verlauf speichert die App selbst im 5-Minuten-Raster und übernimmt die Fünfminutenmittel des Leitstands, die Vorrang vor der eigenen Aufzeichnung haben; den 24-Stunden-Verlauf der Geräte übernimmt sie nur in Lücken und nur für die eigenen Fühler des Geräts. Auf dem Mac zusätzlich in eigenem Fenster. | anders |
| Ladung aufzeichnen: beim nächsten Brennerstart, sofort, beenden, abbrechen | `POST /api/record/arm`, `/start`, `/stop` | Heizung › Ladung aufzeichnen, je Heizungsgerät. Die App fragt nach, bevor eine vorhandene Aufzeichnung verworfen wird. | anders |
| Aufzeichnung als CSV, „Verwerfen“ | `GET /api/record`, `POST /api/record/discard` | Heizung › Ladung aufzeichnen; Verwerfen mit Rückfrage | anders |
| Verbrauchslinie | `GET /api/state` `trend` | Heizung › Verbrauchslinie | abgedeckt |
| Abgas-Vorlauf-Abstand | `GET /api/state` `flue` | Heizung › Auswertung | abgedeckt |
| „Kessel gereinigt – ab jetzt neu messen“ mit Rückfrage | `PUT /api/config` `burner.wartung_epoch` | Geräte › Kessel › Kessel gereinigt | abgedeckt |
| Ladungen und Tage als CSV | `GET /api/log/charges`, `GET /api/log/days` | Heizung › Ladungs- und Tagesprotokoll | abgedeckt |
| – | – | Heizung › Auswertung › Wärmepumpen-Check: Heizlast aus der Verbrauchslinie, Kalibrierung über Tankablesungen, Vorlauf je Heizkreis bei Normaußentemperatur, Räume bei Kälte | zusätzlich |

### 2.7 System `#sys` (HTML Z. 518–657, JS Z. 1767–1861, 2131–2250)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Sicherung holen und einspielen | `GET /api/config/backup`, `POST /api/config/restore` | System › Sicherung | anders (wie 1.7) |
| Bezeichnung | `PUT /api/config` `site` | System › Bezeichnung | abgedeckt |
| Schutzlauf der Pumpen: Wochentag, Stunde | `PUT /api/config` `seize_*` | System › Zeit und Termine | abgedeckt |
| Zeitzone, täglicher Neustart | `PUT /api/config` | System › Zeit und Termine | abgedeckt |
| Bedarfsabfrage: Abfrage alle, Zeitgrenze | `PUT /api/config` `demand_*` | Geräte › Pufferspeicher › System › Bedarfsabfrage, mit den Bedarfsquellen; am Kesselgerät ausgeblendet, weil es keine Heizkreise führt | anders |
| 1-Wire-Bus: Anschlüsse, Abtastabstand | `PUT /api/config` `onewire_pin`, `poll_s` | System › 1-Wire-Bus, GPIO-Prüfung wie die Firmware | abgedeckt |
| Netzwerk: „Netze suchen“, WLAN, Gerätename, Kennwort des Zugangspunkts | `POST`/`GET /api/wifi/scan`, `PUT /api/config` `wifi` | System › Netzwerk | abgedeckt |
| MQTT | `PUT /api/config` `mqtt` | System › MQTT | abgedeckt |
| Firmware einspielen | `POST /api/ota` | System › Firmware | anders (wie 1.7) |
| „Neu starten“, „Auf Werksvorgabe zurücksetzen“ | `POST /api/system/restart`, `/factory` | Geräteseite unten | anders (wie 1.7) |

### 2.8 Einrichtungsassistent (HTML Z. 660–671, JS Z. 1865–1979)

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Bezeichnung mit Vorschlägen Kessel, Pufferspeicher, Heizungsraum | `PUT /api/config` `site` | Einrichtungsassistent der App, Schritt „Neues Gerät“ | abgedeckt |
| 1-Wire-Anschluss | `PUT /api/config` `onewire_pin` | System › 1-Wire-Bus | abgedeckt |
| Fühler zuordnen | `PUT /api/config` `probes[]` | Fühler am Bus. Anders als der Assistent der Weboberfläche behält die App Korrekturen, eigene Namen und nicht zugeordnete Fühler. | anders |

### 2.9 Nur über die Schnittstelle

| Funktion | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Protokolle und Tageszähler löschen | `POST /api/system/clear-logs` | Heizung › Ladungs- und Tagesprotokoll › Protokolle löschen, mit Rückfrage | zusätzlich |
| Messwerte für das Nachbargerät | `GET /api/measurements` | nicht benötigt | – |

## 3 Prüfungen und Mitteilungen der App

Die Weboberflächen zeigen die Befunde der Heizungsgeräte (`findings`), jedes Gerät nur seine
eigenen. Die App übernimmt sie und ergänzt Prüfungen, die kein einzelnes Gerät sehen kann. Ein
Befund erscheint, sobald er seine Mindestdauer ununterbrochen ansteht; die App merkt sich, seit
wann er ansteht, und meldet ihn einmal als Mitteilung. Ist er erledigt, nimmt sie die Mitteilung
zurück. Kehrt er binnen einer Stunde zurück, erscheint die Mitteilung still wieder. Beim ersten
Start meldet die App die vorhandenen Befunde nicht, sie stehen in der Übersicht.

| Prüfung | Schwere | Mindestdauer |
|---|---|---|
| Gerät nicht erreichbar (zwei Abfragen in Folge ohne Antwort) | Störung | – |
| Relais der Pumpen nicht erreichbar | Störung | 10 min |
| Uhrzeit eines Geräts ungültig | Warnung | 10 min |
| Schwaches WLAN (ab −80 dBm Hinweis, ab −87 dBm Warnung) | Hinweis, Warnung | 15 min |
| Unterschiedliche Firmware an Geräten derselben Art | Hinweis | – |
| Raum im Heizbetrieb ohne Thermometer | Warnung | – |
| Messwert eines Raums veraltet | Warnung | 5 min |
| Batterie eines Thermometers bei höchstens 15 % (höchstens 5 %: Warnung) | Hinweis, Warnung | – |
| Außenfühler ohne Messwert; Batterie des Außenfühlers unter 2,4 V | Hinweis | 60 min; – |
| Klima- oder Vorlauffühler auf der Platine ohne Messwert | Hinweis | 10 min |
| Belegte Kanäle ohne Messfahrt | Hinweis | – |
| Kanal im Handbetrieb | Hinweis | 12 h |
| Heizkreis ohne versorgten Verteiler; Verteiler ohne Heizkreis | Warnung | – |
| Pumpe folgt dem Schaltbefehl nicht (`mismatch`) | Warnung | 5 min |
| Heizkreis erreicht keinen Verteiler; ein Verteiler antwortet nicht | Warnung | 5 min |
| Speichergrenzen der beiden Heizungsgeräte weichen ab | Hinweis | – |
| Füllstand weicht zwischen den Heizungsgeräten um 15 Prozentpunkte ab | Hinweis | 15 min |
| „Voll“ liegt unter dem Ende der letzten Ladungen | Hinweis | – |
| Warmwasserreserve knapp (`warn_dhw`) | Warnung | – |
| Ladephase nur geschätzt (`limited`) | Hinweis | 30 min |
| Brenner taktet (`short_cycling`) | Hinweis | – |
| Keine oder mehr als 14 Tage alte Sicherung eines Geräts | Hinweis | 60 min |

Welche Schweren gemeldet werden, steht unter Einstellungen der App › Mitteilungen; einzelne
Befunde lassen sich stumm schalten. Die App sichert jedes Gerät einmal in der Woche von selbst.
Unter iOS fragt sie im Hintergrund gelegentlich ab, wann genau, entscheidet das System; auf dem
Mac läuft sie in der Menüleiste weiter, auch ohne Fenster, und startet auf Wunsch beim Anmelden.

## 4 In der Inventur aufgefallen

Die folgenden Punkte betreffen Firmware und Weboberflächen, nicht die App. Sie sind hier festgehalten, weil die App sie zum Teil umgeht und die Firmware sie beheben sollte.

### 4.1 Verteiler

1. Sollwert und Betriebsart, die über die Raumkarte geändert werden, speichert die Firmware in der Konfiguration; die Weboberfläche lädt ihre Kopie aber nicht neu. Das nächste Speichern auf einer beliebigen Karte sendet den alten Sollwert zurück.
2. Nicht gespeicherte Änderungen im Raumeditor und in der Tabelle der Fahrzeiten gehen mit dem Speichern einer anderen Karte mit und gehen bei jedem Neuladen der Konfiguration verloren.
3. Ein Sollwert außerhalb 5–35 °C wird mit 200 quittiert und nicht übernommen; die Oberfläche meldet nichts.
4. Der Hinweis auf veraltete Messwerte nennt fest „Viertelstunde“, obwohl `sensor_timeout_s` einstellbar ist.
5. Weder Raumeditor noch Assistent verhindern, dass ein Thermometer mehreren Räumen zugeordnet wird. Der Assistent kann doppelte Raumkennungen erzeugen.
6. Nach einer Änderung der Fahrzeiten von Hand bleibt `calibrated` gesetzt.
7. Die Zwischenstellung („%“) wird ohne Prüfung gesendet; eine Eingabe ohne Zahl führt zu „Feld position fehlt“.
8. `stop` ist in der Firmware nicht gegen eine laufende Messfahrt gesperrt und bricht sie ab, auch über „Alle aus“.
9. Die Rückfrage zur Werksvorgabe nennt nur Räume und Kreisparameter; zurückgesetzt werden auch WLAN, Gerätename, Kennwort des Zugangspunkts, MQTT, Bezeichnung, Außenfühler und Termine.
10. Eine Sicherung ohne Kopf nimmt die Oberfläche an; die Firmware lehnt sie mit 400 ab.
11. Die Firmwareübertragung hat keine Rückfrage und zeigt statt der Fehlermeldung des Geräts nur den Status.
12. Der Assistent hat keinen Abbruch; das WLAN lässt sich erst nach seinem Abschluss eintragen.
13. Ein gespeichertes Kennwort lässt sich über die Oberfläche nicht löschen (gilt für beide Geräte).

### 4.2 Heizungsgerät

1. Fehlgeschlagene Abfragen zeigt die Oberfläche nicht an; die Werte bleiben stehen.
2. Ein unbekannter Reiter in der Adresse ergibt eine leere Seite.
3. Das Neuzeichnen alle 5 s überschreibt Eingaben in Feldern ohne Fokus, bevor sie gespeichert sind.
4. Die Karte „Pufferspeicher“ und damit ihre Einstellungen sind unsichtbar, solange Füllstand und Spreizung unbekannt sind.
5. Jeder Fehler beim Umschalten eines Heizkreises erscheint als „Heizkreis nicht gefunden“.
6. „Versorgte Verteiler“ bietet nur Verteiler an, die gerade gefunden werden; Speichern entfernt die Zuordnung zu einem Verteiler, der in diesem Moment nicht antwortet.
7. „Alle Heizkreise entfernen“ fragt nicht nach.
8. Die Ersatzwerte der Oberfläche für die Kesselkreispumpe (1,0 und 0,5 K) weichen von der Vorgabe der Firmware (3,0 und 2,0 K) ab.
9. In der Fühlertabelle können zwei Zeilen dieselbe Rolle erhalten (Ablehnung mit 400). Fühler ohne Rolle, Namen und Korrektur werden beim Speichern nicht gesendet und fallen aus der Liste.
10. Die Zeiträume 1 h und 6 h senden dieselbe Anfrage und zeigen beide etwa 9,6 h. Der Text nennt einen Minutentakt, gespeichert wird alle 2 min. Die Reihen hk3 und hk4 haben keine Farbe.
11. „Beim nächsten Brennerstart aufzeichnen“ und „Sofort aufzeichnen“ verwerfen eine vorhandene Aufzeichnung ohne Rückfrage; „Verwerfen“ fragt ebenfalls nicht nach.
12. Der Hinweis in der MQTT-Karte, es werde noch nichts veröffentlicht, ist veraltet; `app_mqtt.c` veröffentlicht Discovery und Zustand alle 10 s.
13. Die Werksvorgabe setzt auch das WLAN zurück, wirkt aber erst nach einem Neustart vollständig: Pumpen, Brenner und MQTT arbeiten bis dahin mit den alten Einstellungen. Der Assistent öffnet danach nicht erneut.
14. Der Assistent nennt „zwei Schritte“ und hat drei. Jede Änderung im Schritt 1-Wire sendet sofort `onewire_pin: [pin, -1]` und schaltet damit Bus 2 ab; das Feld verliert beim Tippen alle 5 s den Fokus. Der Abschluss ersetzt die ganze Fühlerliste: Korrekturen werden 0, eigene Namen werden überschrieben, nicht zugeordnete Fühler entfallen.
15. Jedes `PUT /api/config` löscht den 24-Stunden-Verlauf im Arbeitsspeicher.
16. Für `POST /api/system/clear-logs` gibt es keine Schaltfläche.
17. Werte zwischen −0,9 und −0,1 verlieren in den CSV-Ausgaben das Vorzeichen: `"%d.%d", wert / 10, abs(wert % 10)` ergibt für −5 die Ausgabe `0.5`.
18. Die Kesselkreispumpe hat keinen Schutzlauf, obwohl der Text zum Schutzlauf der Pumpen das nahelegt.

## 5 KI der App

Die Weboberflächen haben keine KI. In der App liest die KI die Anlage über Werkzeuge und greift
nur über Vorschläge ein, die der Nutzer übernimmt oder verwirft. Die Werkzeuge fragen keine Geräte
selbst ab; sie lesen, was die laufende Abfrage, das Befundgedächtnis und der gespeicherte Verlauf
bereits halten. Einzige Ausnahme ist `feinverlauf`, das der Leitstand aus seiner Karte berechnet.

| Werkzeug | Liest oder bewirkt | Quelle |
|---|---|---|
| `anlage_status` | Kurzfassung des Zustands je Gerät, Erreichbarkeit, Alter der Werte, Zuordnung der Heizkreise zu den Verteilern; auf Wunsch die vollständige letzte Antwort eines Geräts | laufende Abfrage |
| `befunde` | offene Befunde mit Text als Beleg, Ort, Quelle und Beginn; auf Wunsch die in den letzten 30 Tagen erledigten | Befundgedächtnis |
| `verlauf` | Zeitreihen bis ein Jahr mit Tiefst-, Höchst-, Mittel- und letztem Wert und einer Tabelle von höchstens 48 Zeilen (Apple-Modelle 16); über 30 Tage in Tagesmitteln oder gröber, Tage ab Mitternacht UTC | gespeicherter Verlauf |
| `ereignisse` | Ereignisse bis 90 Tage, gefiltert nach Gerät und Art; Zusammenfassung je Art und Gerät mit Starts und Laufzeiten von Brenner und Pumpen, Neustartgründen und Ausfalldauer, danach die Ereignisse, jüngste zuerst (höchstens 80, Apple-Modelle 20) | Ereignisablage, aus dem Protokoll des Leitstands |
| `feinverlauf` | Messwerte eines Geräts im Takt der Abfrage (30 s) oder gröber, bis 24 Stunden und acht Reihen, auch `geraet.heap`, `geraet.rssi`, `geraet.laufzeit`; mit Beginn in Ortszeit, etwa für die Minuten vor einem Neustart. Nur im Heimnetz | Leitstand, `GET /api/log/series` |
| `protokolle` | Ladungsprotokoll, Tagesprotokoll, Änderungsprotokoll der App | Heizungsgerät, App |
| `kennzahlen` | Starts und Laufzeit je Tag, Laufzeit je Start, Öl je Tag, Heizgradtage, Verbrauchslinie, Ladungen, Abgas-Vorlauf-Abstand, je Raum Abweichung vom Sollwert und mittlere Ventilstellung | berechnet in der App |
| `einstellungen_lesen` | Werte mit Bezeichnung, Einheit, Bereich, Vorgabe und Freigabe für Vorschläge; ohne Netzwerk, MQTT, Kennwörter, Benutzer, Relaisadressen und Thermometer-Adressen | zuletzt gelesene Konfiguration |
| `wissen_suchen` | die drei passendsten Abschnitte aus Handbuch und Konzepten; nur für die Apple-Modelle, Claude hat beides vollständig in den Anweisungen | Ressourcen der App |
| `aenderung_vorschlagen` | legt einen Vorschlag an; vorher prüft die App Katalog, Freigabe, Bereich, Raster, gegenseitige Bedingungen wie „voll über leer“ und das Zielgerät | – |
| `aktion_vorschlagen` | Messfahrt, Schutzfahrt, Fühlersuche, Ladungsaufzeichnung, Relaisprüfung; Neustart nur, wenn in den letzten sieben Tagen eine Änderung verzeichnet ist, die erst mit dem Neustart wirkt | – |

Für Vorschläge freigegeben sind die Einstellungen, die der Parameterkatalog als freigegeben führt,
dazu die Betriebsart der Heizkreispumpen und der Kesselkreispumpe und die versorgten Verteiler
eines Heizkreises. Speicherwerte trägt die App auf beiden Heizungsgeräten gleich ein.

Vor der ersten Anfrage an Claude fragt die App um Zustimmung zur Übertragung an Anthropic und nennt,
welche Daten dorthin gehen (App-Store-Richtlinie 5.1.2(i)). Ohne Zustimmung stellt sie keine
Anfrage, auch keinen Lagebericht; widerrufen lässt sie sich unter Einstellungen der App › KI. Die
Datenschutzerklärung liegt auf der GitHub-Pages-Seite unter `datenschutz.html` und ist aus den
Einstellungen und dem Zustimmungsblatt verlinkt.

Übernahme eines Vorschlags: Die App liest jedes betroffene Gerät frisch und bricht ab, wenn der
Wert inzwischen nicht mehr der bisherige ist. Dann sichert sie jedes Gerät, schreibt über den
eigenen Endpunkt (Sollwert, Betriebsart eines Raums, Pumpenbetriebsart) oder über
`PUT /api/config` und liest zurück. Scheitert das zweite Gerät, setzt sie das erste auf den
bisherigen Wert zurück. Jede Übernahme steht im Änderungsprotokoll; die Kennzahlen der sieben Tage
davor legt die App zum Vorschlag, nach sieben Tagen dieselben Kennzahlen danach
(Wirkungskontrolle). Die Rücknahme stellt den bisherigen Wert her, sofern am Gerät noch der
übernommene steht; sonst meldet sie die spätere Änderung und schreibt nichts.

| Modell | Einsatz | Stand |
|---|---|---|
| Claude Opus (derzeit Opus 5.5) | Gespräch und Lagebericht; Handbuch und Konzepte im zwischengespeicherten Teil der Anfrage; bei einer Ablehnung übernimmt serverseitig das empfohlene Ersatzmodell | abgedeckt; im Simulator nicht geprüft, weil dort kein Schlüssel hinterlegt ist |
| Claude Sonnet (derzeit Sonnet 5.5) | wie Opus zu etwa der Hälfte des Preises je Token, ebenfalls mit Ersatzmodell | abgedeckt; im Simulator nicht geprüft, weil dort kein Schlüssel hinterlegt ist |
| Claude Haiku (derzeit Haiku 4.5) | Gespräch und Lagebericht zu etwa einem Viertel des Preises von Opus; Haiku 4.5 ohne adaptives Denken und ohne Aufwandsstufe, ohne Ersatzmodell, Kontext 200 000 Token | abgedeckt; im Simulator nicht geprüft, weil dort kein Schlüssel hinterlegt ist |
| Apple Private Cloud Compute | Gespräch und Lagebericht | offen: verlangt die Berechtigung `com.apple.developer.private-cloud-compute`, die Apple auf Antrag vergibt; bis dahin wählbar, aber gesperrt (`HEIZUNG_PCC` in `project.yml`) |
| Apple-Modell auf dem Gerät | kurze Auskünfte ohne Netz, Wissen über `wissen_suchen` | abgedeckt; für den Lagebericht nicht eingesetzt, weil es im Test Befunde falsch wiedergab |

Welches Modell der Reihen Opus, Sonnet und Haiku antwortet, legt die App nicht fest: Höchstens
einmal am Tag und bei „Verbindung prüfen“ fragt sie `GET /v1/models` ab und verwendet je Reihe das
Modell mit dem jüngsten Erscheinungsdatum; Auswahl und Anzeige tragen dessen Namen. Fable wird
nicht angeboten. Aus `capabilities` übernimmt die App, ob das Modell adaptives Denken und eine
Aufwandsstufe kennt, und lässt beides sonst weg. Ohne Antwort gilt der zuletzt gespeicherte Stand,
vor der ersten Abfrage Opus 5.5, Sonnet 5.5 und Haiku 4.5. Ein Gespräch geht mit einem neueren
Modell derselben Reihe weiter; ein Werkzeugzwang (`tool_choice` `any`) wird nicht gesendet, weil
Opus 5.5 und Sonnet 5.5 ihn abweisen.

Weist die API eine Anfrage an das neueste Modell mit 400 oder 404 ab, geht dieselbe Anfrage an das
Modell der Reihe, das zuletzt geantwortet hat (vor der ersten Antwort: das Modell beim Bau der App).
Nimmt dieses sie an, gilt das neueste als abgewiesen: Die App verwendet es nicht mehr, meldet das
einmal und versucht erst ein noch neueres Modell wieder. Weist auch das bewährte Modell ab, lag es
an der Anfrage; dann bleibt es beim Fehler und kein Modell wird gesperrt.

Den Lagebericht erstellt die KI auf Wunsch und von selbst höchstens alle sechs Stunden, nach
einem neuen Befund frühestens nach 30 Minuten, nur bei geöffnetem Fenster und abschaltbar unter
Einstellungen der App › KI. Passen die sichtbaren Befunde nicht mehr zum Bericht der KI oder ist er
älter als einen Tag, zeigt die Übersicht bis zum nächsten Bericht den Bericht aus den Befunden.
Verbrauch (Token, Anteil aus dem Zwischenspeicher, Überlegung) steht je Modell und Monat unter
Einstellungen der App. Gespräche samt Transkript, Vorschläge, Änderungsprotokoll und Lageberichte
liegen als Dateien im Ordner der App; ein Gespräch lässt sich nach einem Neustart fortsetzen.

## 6 Leitstand

Grundlage: `apps/station/main/www/rumpf.html`, Routen in `apps/station/main/st_web.c`. Der
Leitstand regelt nichts; die App führt ihn deshalb nicht als Geräteart, sondern in einer eigenen
Liste neben den Regelgeräten (`BekannterLeitstand`, Datei `leitstaende.json`) und fragt ihn in
einem eigenen Betrieb alle zehn Sekunden ab.

| Funktion der Weboberfläche | Endpunkt | Stelle in der App | Stand |
|---|---|---|---|
| Einrichtung im Zugangspunktbetrieb: Netz wählen, Kennwort | `PUT /api/config` `site`, `wifi` | Einrichtung › Neues Gerät › Leitstand: offenes Netz `leitstand-XXXX`, Ort und WLAN-Zugang; Gerätename bleibt `leitstand` | abgedeckt |
| Aufnahme in die App | `GET /api/state` `device.role` = `station` | Einrichtung › Im Heimnetz (Bonjour) oder Adresse von Hand | abgedeckt |
| Kopfzeile: Adresse, Signal, Laufzeit, freier Speicher und Tiefstwert, Fassung | `GET /api/state` | Geräte › Leitstand, Abschnitt oben; der Tiefstwert unter 30 kB erscheint als Warnung | abgedeckt |
| Geräte der Anlage mit Erreichbarkeit | `GET /api/plant` | Geräte › Leitstand › Geräte der Anlage, als Zahl; die Liste selbst führt die App unter Geräte | anders |
| Außenfühler: Wert, Feuchte, Alter | `GET /api/state` `outdoor` | Geräte › Leitstand › Außenfühler | abgedeckt |
| Funkthermometer in Reichweite | `GET /api/ble` | Geräte › Leitstand › Funkthermometer in Reichweite | abgedeckt |
| Als Außenfühler zuordnen, Zuordnung aufheben | `PUT /api/config` `outdoor.mac` | Thermometer antippen › Als Außenfühler zuordnen; Außenfühler › Zuordnung aufheben | abgedeckt |
| Schlüssel verschlüsselter Thermometer eingeben, ersetzen, entfernen | `POST /api/ble/key` | Thermometer antippen › Schlüssel eingeben; dasselbe Blatt wie am Verteiler | abgedeckt |
| Bild der Anzeige | `GET /api/screen` | Geräte › Leitstand › Anzeige am Gerät | abgedeckt |
| Seite der Anzeige wählen | `POST /api/display` | Geräte › Leitstand › Anzeige am Gerät, Auswahl Anlage oder Leitstand | zusätzlich |
| Protokoll: Tage und Dateien | `GET /api/log/days`, `GET /log/<tag>/<datei>` mit `Range` | kein eigener Bildschirm: Die App übernimmt die Fünfminutenmittel aller Tage in den Verlauf, beim Verbinden und danach alle fünf Minuten, vom laufenden Tag nur die neuen Zeilen. Stand unter Geräte › Leitstand › Verlauf | anders |
| Ereignisse | `GET /log/<tag>/ereignisse.jsonl` mit `Range` | Heizung › Verlauf: Markierungen im Diagramm und Liste darunter; Räume › Raum › Verlauf: Ein- und Ausschalten. `GET /api/log/events` nutzt die App nicht, sie liest aus der eigenen Ablage | anders |
| Analysepaket | – | Einstellungen › Daten › Analysepaket erstellen: ZIP mit je Gerät einer CSV-Datei im gewählten Raster, den Ereignissen, dem Katalog und `anlage.json`, zum Teilen. Die Weboberfläche des Leitstands erzeugt keines | anders |
| HomeKit: Stand, Kopplungen löschen | `GET /api/state` `homekit`, `POST /api/homekit/reset` | Geräte › Leitstand › HomeKit; den Code zeigt nur die Anzeige am Gerät | abgedeckt |
| Karte formatieren, Dauerlastprüfung | `POST /api/log/format`, `POST /api/log/lasttest` | nur Weboberfläche | offen |
| Abfragetakt, Helligkeit, Abdunkeln, Nachtzeit | `PUT /api/config` `poll`, `display` | nur Weboberfläche | offen |
| Ort, Gerätename, WLAN, Kennwort des Einrichtungszugangs | `PUT /api/config` | nur Weboberfläche | offen |
| Neustart, Firmware, Werksvorgabe | `POST /api/system/…`, `POST /api/ota` | nur Weboberfläche | offen |
