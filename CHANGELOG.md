# Änderungen

Die veröffentlichten Fassungen stehen mit Abbildern unter
[Releases](https://github.com/rubenmuehlhans/floor-heating-ctrl/releases).

## Unveröffentlicht

### Verteiler

- **Betriebsart „Nur Außenfühler“.** Eine Platine ohne Stellantriebe, die nur den Außenfühler
  empfängt, braucht keinen Platzhalterraum mehr. Der Einrichtungsassistent fragt zuerst nach der
  Aufgabe; in dieser Betriebsart entfallen Räume, Kreise, Schutzfahrt, das Anfahren unbekannter
  Stellungen, Messfahrt und Fahrbefehle. Oberfläche und Anzeige zeigen den Außenfühler, Home
  Assistant erhält keine Ventile. Konfiguration: `outdoor_only`; `GET /api/state` meldet
  `device.function` mit `outdoor` oder `valves`.

### Leitstand

- **Außentemperatur von einer Verteilerplatine.** Empfängt der Leitstand den Außenfühler nicht
  selbst, übernimmt er den jüngsten Wert einer Verteilerplatine; `outdoor.source` nennt sie.
  Damit erscheint die Außentemperatur auch dann in HomeKit und auf der Anzeige.
- **HomeKit entfernt verwaiste Räume.** Führt ein erreichbarer Verteiler einen Raum nicht mehr,
  verschwindet das Thermostat aus Home.
- **Eigene Zeile im Protokoll auch bei vielen Funkthermometern.** Ab zehn Thermometern in
  Reichweite passte der Kopf der eigenen Zeile nicht mehr in den Puffer von 1280 Byte; die Zeile
  fehlte, das Protokoll meldete „Kopf zu lang“. Der Puffer fasst jetzt den Kopf für alle zwölf
  erfassten Thermometer, die Leser verwenden dieselbe Grenze.

### App

- **Immer die neuesten Claude-Modelle.** Statt fester Kennungen fragt die App bei Anthropic ab,
  welches Opus-, Sonnet- und Haiku-Modell zuletzt erschienen ist, und verwendet diese; Auswahl,
  Assistent und Einstellungen zeigen den Namen mit Version, derzeit Claude Opus 5.5, Claude
  Sonnet 5.5 und Claude Haiku 4.5. Die Abfrage läuft höchstens einmal am Tag und bei „Verbindung
  prüfen“; ein laufendes Gespräch geht mit dem neueren Modell weiter. Die gespeicherte Modellwahl
  bleibt erhalten.
- **Claude Haiku wählbar.** Das schnellste und günstigste Claude-Modell, für kurze Auskünfte. Ob ein
  Modell adaptives Denken und eine Aufwandsstufe kennt, übernimmt die App aus der Modellliste;
  Haiku 4.5 antwortet ohne beides.
- **Rückgriff auf das bewährte Modell.** Weist die API ein neu erschienenes Modell mit 400 oder 404
  ab, beantwortet das zuletzt bewährte Modell derselben Reihe die Anfrage. Das abgewiesene Modell
  verwendet die App danach nicht mehr und meldet das einmal; ein noch neueres versucht sie wieder.
- **Sonnet mit Ersatzmodell.** Wie Opus erhält Sonnet ab 5.5 bei einer Ablehnung das von Anthropic
  empfohlene Ersatzmodell.

### Gerät am Pufferspeicher

- **Befund „Vorlauf und Rücklauf vertauscht“ nur noch bei laufender Pumpe.** Die Haltezeit
  summiert jetzt nur beurteilbare Zeit. Bisher wurde vom ersten Auftreten an gemessen, sodass eine
  Standzeit mitzählte und der erste Wert nach dem Anlaufen, wenn noch das Wasser der Standzeit in
  den Rohren stand, die Meldung sofort auslöste. Geurteilt wird erst nach fünf Minuten
  Pumpenlauf, und nur, solange das Relais die Pumpe als eingeschaltet meldet. Bei stehender Pumpe
  wird der Befund nicht gemeldet.

## v0.5.0 — 3. Oktober 2026

### Leitstand

- **HomeKit-Brücke (Core2).** Je Raum ein Thermostat mit Ist, Soll (5 bis 35 °C in 0,5 K),
  Betriebsart, Heizzustand und Feuchte; Fühler für Außen, Pufferspeicher und Kesselvorlauf.
  Änderungen aus Home gehen nach 900 ms Sammelzeit an den Verteiler; ein nicht erreichbarer
  Verteiler zeigt in Home „Keine Antwort“. Code und QR-Code nur auf der Anzeige (Seite
  Leitstand, mittlere Taste kurz), erzeugt beim ersten Start. `POST /api/homekit/reset` löscht
  die Kopplungen; `GET /api/state` meldet den Stand unter `homekit`. Grundlage ist das
  esp-homekit-sdk von Espressif unter `third_party/`. Ohne PSRAM (Core Basic) bleibt HomeKit aus
  und belegt dort nur die rund 3 KB festen Speicher des SDK.
- **Sechs Seiten und Berührung (Core2).** Zu Anlage und Leitstand kommen Räume (Ist, Soll,
  Ventile, Feuchte), Verlauf (24 Stunden Speicher, Kessel, Außen, Brenner; mittlere Taste:
  Raumwerte), Meldungen (Befunde und Ereignisse des Tages) und Geräte (Erreichbarkeit, WLAN,
  Laufzeit, Speicher, Fassung, Außenfühler; mittlere Taste: Funkthermometer). Wischen blättert,
  Tippen auf den Fuß wirkt wie die Tasten. Der Verlauf liegt im PSRAM und wird nach einem Neustart
  von der Karte gefüllt. Ein neuer Befund schaltet die Anzeige ein und zeigt die Meldungen. Am
  Core Basic bleibt es bei zwei Seiten.
- **Echtzeituhr und Versorgung (Core2).** Die Zeit kommt nach einem Stromausfall aus der
  Echtzeituhr, das Protokoll schreibt ohne Lücke weiter. Ereignisse `versorgung_aus` und
  `versorgung_wieder` mit Ladestand; die Seite Leitstand zeigt Netz oder Akku.

- **Neues Gerät, `apps/station`.** Ein M5Stack Core zeigt den Zustand der Anlage auf zwei Seiten
  (Anlage, Leitstand), fragt Heizungsgeräte und Verteilerplatinen alle 30 s ab, empfängt
  Funkthermometer samt verschlüsselter BTHome-Geräte und liefert die Außentemperatur unter
  `GET /api/demand`. mDNS-Rolle `station`, Kennung `lst_…`. Einrichtung über den Zugangspunkt
  `leitstand-XXXX`, Oberfläche mit Übersicht, Funk, Einstellungen und System, Abbild des
  Bildschirms unter `GET /api/screen`. Aufzeichnung auf SD-Karte und HomeKit folgen; siehe
  `docs/konzept-leitstand.md`.
- **Protokoll auf der SD-Karte.** Je Tag in UTC ein Verzeichnis `protokoll/<jahr>/<tag>/` mit
  Messwerten je Gerät im Takt der Abfrage, Fünfminutenmitteln, Ereignissen (Neustart mit Grund,
  Erreichbarkeit, Firmware, Brenner und Pumpen mit Dauer, Sollwerte, Betriebsarten, Befunde),
  vollständigen Zuständen alle 15 Minuten und `geraete.json`. Spalten nach
  `docs/katalog-messgroessen.md`; eine neue Spalte beginnt eine Datei `<kennung>.2.csv`. Ohne
  gestellte Uhr wird nichts geschrieben. Unter 10 Prozent freiem Platz gehen die ältesten
  Zustände und Rohwerte. Abruf unter `GET /api/log/days` und `GET /log/<tag>/<datei>` mit
  `Range`, Reiter **Protokoll** in der Oberfläche, Kartenstand auf dem Bildschirm.
- **Protokolle der Heizungsgeräte** werden einmal am Tag vollständig nach
  `protokolle/<kennung>.ladungen.csv` und `.tage.csv` übernommen, über eine Zwischendatei.
- **Funkereignisse:** Thermometer verloren, wieder empfangen, Schlüssel falsch.
- **Auswertung am Gerät:** `GET /api/log/series` (Mittel ausgewählter Messgrößen in wählbarem
  Raster) und `GET /api/log/events` (Ereignisse eines Zeitraums).
- **Absturzspeicher über das Netz:** `GET /api/coredump` mit Task, Programmzähler, Ursache und
  Rücksprungadressen.
- **Kartenwechsel im Betrieb.** Alle zehn Sekunden und nach jedem Schreibfehler fragt der
  Leitstand den Status der Karte ab. Antwortet sie nicht, gibt er die Einbindung auf, statt mit der
  Belegungstabelle der alten Karte auf eine neue zu schreiben, und bindet neu ein.
- **Karte formatieren.** `POST /api/log/format` mit Bestätigung und ein Knopf unter **Protokoll**
  formatieren die ganze Karte als eine FAT32-Partition, etwa eine Karte von einem Raspberry Pi, von
  der vorher nur die kleine Startpartition lesbar war.
- **Verklemmung von Anzeige und Protokoll behoben.** Die Anzeige hielt beim Zeichnen die Sperre des
  Busses und fragte den Stand des Protokolls ab; die Kartenprüfung hielt die Sperre des Protokolls
  und wollte den Bus. Unter Last blieben Anzeige, Protokoll und Abfrage stehen, die Oberfläche
  antwortete leer. Unter der Sperre des Protokolls wird der Bus nicht mehr gesperrt.
- **Lautsprecher still.** Der Verstärker des Core Basic hängt an GPIO 25 und ist immer versorgt;
  offen gelassen, pfiff und knisterte er unter Last auf der Karte. Der Pin liegt jetzt fest auf null.
- **Dauerlastprüfung.** `POST /api/log/lasttest {"mb":1024}` schreibt ein Muster auf die Karte,
  während Anzeige und Protokoll weiterlaufen, liest es zurück und vergleicht; `GET` liefert den Stand.
- **Bildschirmabzug unter der Speichersperre**, damit er nicht mit dem Auswerten eines
  Gerätezustands zusammenfällt.
- **Update ohne Abfrage.** Während einer Firmware-Übertragung ruht die Abfrage der Anlage. Beides
  zusammen brauchte mehr Arbeitsspeicher, als der Core Basic hat: Übertragungen brachen ab, der
  freie Speicher fiel auf 376 Byte.
- **Core2 ohne Neustartschleife.** Mit Heimnetz und HomeKit lief der interne Arbeitsspeicher des
  Core2 nach dem Anlegen der Zubehöre auf unter 7 KB; dann fehlten Puffer der Karte und Sperren,
  und `fopen` brach mit `abort()` ab, etwa alle 20 Sekunden. Gewöhnliche Anforderungen, TLS und
  NimBLE gehen jetzt zuerst in den PSRAM (`SPIRAM_MALLOC_ALWAYSINTERNAL=0`); intern bleiben rund
  39 KB frei. Der Core Basic ohne PSRAM ist davon nicht berührt.

### App

Ab Build 7 der App (23. September, TestFlight).

- **Abgleich mit dem Leitstand.** Die App übernimmt die Fünfminutenmittel aus dem Protokoll des
  Leitstands in ihren Verlauf, beim Verbinden und danach alle fünf Minuten; vom laufenden Tag nur
  die neuen Zeilen über `Range`. Die Werte des Leitstands haben Vorrang vor der eigenen
  Aufzeichnung. Der Stand je Leitstand liegt in `Abgleich-<kennung>.json` und wird nach jeder Datei
  gesichert. Anzeige unter Geräte › Leitstand › Verlauf.
- **Ereignisse im Verlauf.** Die Ereignisse des Leitstands liegen je Tag in der Verlaufsablage.
  Heizung › Verlauf markiert Neustarts, Firmwarewechsel, neue Befunde und Ausfälle im Diagramm und
  listet sie darunter mit Grund und Dauer; der Verlauf eines Raums markiert Ein- und Ausschalten.
  Ist eine Datei auf dem Leitstand kürzer als das Übernommene, liest die App den Tag neu. Nach
  **Verlauf löschen** beginnt der Abgleich von vorn und übernimmt die Karte erneut.

Ab Build 8 (23. September, TestFlight):

- **Verlauf in Tafeln.** Heizung › Verlauf zeigt je Gruppe ein eigenes Diagramm mit eigener
  Skala: Kessel und Speicher, Abgas, Brenner und Speicher, jeder Heizkreis, Räume, Vorlauf an den
  Verteilern. Ein Heizkreis zeigt Vor- und Rücklauf, dazwischen die Spreizung als Fläche, grau
  hinterlegt den Pumpenlauf und zuschaltbar die Außentemperatur auf einer festen rechten Achse von
  −10 bis 20 °C; alle Heizkreise teilen eine Skala. Tippen oder Ziehen wählt eine Zeit, die in allen
  Tafeln stehen bleibt; die Legende blendet Reihen aus und ein.
- **Heizkurve je Heizkreis.** Die Karte des Heizkreises führt zu Verlauf und Heizkurve: Vor- und
  Rücklauf über der Außentemperatur, je Platz ein Punkt, nur solange die Pumpe lief, mit
  Ausgleichsgerade, Steigung in K je K, Vorlauf bei 0 °C außen und Bestimmtheit.

Ab Build 9 (23. September, TestFlight):

- **Werkzeuge des Assistenten.** `ereignisse` liest die Ereignisse bis 90 Tage mit
  Zusammenfassung je Art und Gerät (Starts und Laufzeiten, Neustartgründe, Ausfalldauer).
  `feinverlauf` holt Messwerte im Takt der Abfrage vom Leitstand, bis 24 Stunden, auch freien
  Speicher und WLAN-Empfang. `verlauf` reicht bis ein Jahr, über 30 Tage in Tagesmitteln.
- **Analysepaket.** Einstellungen › Daten erzeugt ein ZIP mit je Gerät einer CSV-Datei im
  gewählten Raster, den Ereignissen, dem Katalog der Messgrößen und einer Beschreibung der Anlage,
  zum Teilen; ohne Adressen und Zugangsdaten.

Ab Build 10 (24. September, TestFlight):

- **Wärmepumpen-Check** unter Heizung › Auswertung: Heizlast bei der Normaußentemperatur aus der
  Verbrauchslinie, Wärmeverlust in W/K, Warmwasseranteil, Kalibrierung des Düsendurchsatzes über
  Tankablesungen, nötige Vorlauftemperatur je Heizkreis mit Bewertung nur bei belastbarer
  Heizkurve, Räume, die bei Kälte trotz offener Ventile unter dem Sollwert bleiben.


Ab Build 11 (24. September, TestFlight):

- **Seite der Anzeige** am Core2 aus allen sechs wählbar (Menü), am Core Basic wie bisher Anlage
  oder Leitstand.
- **HomeKit im Leitstand:** Geräte › Leitstand zeigt gekoppelte Geräte und Zubehör und löscht
  die Kopplungen nach Rückfrage. Den Code zeigt die App bewusst nicht.
- Ereignisse `versorgung_aus` und `versorgung_wieder` und die Kartenereignisse des Leitstands in
  Worten.
- **Leitstand über seinen Zugangspunkt einbinden:** Einrichtung › Neues Gerät bietet neben Verteiler
  und Heizungsgerät den Leitstand an. Die App tritt dem offenen Netz `leitstand-XXXX` bei, schreibt
  Ort und WLAN-Zugang und nimmt den Leitstand danach in die eigene Liste auf.
### Alle Geräte

- **Falsche WLAN-Zugangsdaten lassen sich berichtigen.** Scheiterte die Verbindung, versuchte das
  Gerät es alle fünf Sekunden erneut. Jeder Versuch zieht den Einrichtungs-Zugangspunkt auf den
  Kanal des Heimnetzes; verbundene Telefone flogen hinaus, eine Berichtigung war kaum möglich.
  Bei offenem Zugangspunkt jetzt alle 30 Sekunden, solange jemand daran hängt alle fünf Minuten.
  Erst mit dem nächsten Update in Verteiler und Heizungsgerät.
- **Neustart ohne Absturz.** Der Neustart nach einem Update oder über die Oberfläche lief in einer
  Aufgabe mit 2 KB Stapel. `esp_restart()` ruft dort die Abschaltroutinen von WLAN und Bluetooth
  auf; am Leitstand lief der Stapel über, der Absturzspeicher zeigte „Stack overflow“ in
  `restart`, und der Neustart geschah als Absturz. Jetzt 4 KB, in allen drei Firmwares.
- **Neustartgrund.** `GET /api/state` nennt `reset_reason`: `power_on`, `software`, `panic`,
  `task_wdt`, `brownout` und weitere. Am 23. September starteten Kessel und Speicher ohne
  erkennbaren Grund neu; solche Fälle lassen sich damit einordnen.

### Verteilerplatine

- **Abstand zum Anschlag auf.** Gewöhnliche Fahrten öffnen höchstens bis 92 % des Hubs. Die
  Fahrzeit auf aus der Messfahrt reicht bis zum Anschlag; jede Fahrt auf 100 % endete deshalb
  dort, wo ein Steg am Zahnrad den Nippel des Stößels abfängt. Das Blockiermoment ging dabei in
  die Stößelführung des HmIP-VDMOT, das Teil, das an diesen Antrieben bricht. Die Schutzfahrt
  öffnet ebenfalls nur bis 92 % und fährt allein zu auf Anschlag. Notfahrt und Messfahrt fahren
  weiterhin in den Anschlag.
- **Entlastung nach dem Schließen.** Nach einer Fahrt zu, die an der Endlage oder an der
  Maximallaufzeit endet, fährt der Antrieb eine Sekunde wieder auf, etwa 0,1 mm. Die Spindel ist
  selbsthemmend; bisher blieb die volle Blockierkraft danach auf Ventilstift und Dichtung stehen.
  Das Ventil bleibt geschlossen, die Stellung bei 0 %.
- **1-Wire-Treiber 1.1.2.** Die Bauteilverwaltung von Espressif löst `onewire_bus` jetzt zu 1.1.2
  auf. Die ROM-Suche zählt die Bits nach AN187 ab 1, die Ruhezeit nach dem Rücksetzen beträgt 480 µs
  wie in der Spezifikation.

### Heizungsgerät

- **Außentemperatur vom Leitstand.** Das Heizungsgerät fragt zuerst einen Leitstand und erst
  danach die Verteilerplatinen. Solange der Wert des Leitstands frisch ist, übernimmt die
  Pumpensteuerung keinen Außenwert aus den Bedarfsantworten der Verteiler; die Quelle wechselte
  sonst alle paar Sekunden.
- **Kesselkreispumpe läuft, solange der Brenner läuft.** Sie schaltet ein, sobald die
  Brennererkennung den Brenner meldet, ohne Haltezeit und auch in einer Mindestpause, und geht
  erst aus, wenn die Erkennung ihn als aus meldet und danach die Spreizung über die Haltezeit
  darunter liegt. Bisher entschied allein der Abstand zwischen Kesselvorlauf und Speicher. Am
  23. September lag er während eines Brennerlaufs bei 0,9 bis 2,5 K, die Pumpe stand, und der
  Kessel schaltete den Brenner nach gut zwölf Minuten selbst ab; der Speicher stieg um 2,3 K,
  bei den Ladungen zuvor um 12 bis 19 K. Neuer Grund `burner`.
- **Neustart schaltet die Kesselkreispumpe nicht mehr ab.** Beim ersten Rechenschritt nach einem
  Neustart galt die Einschaltschwelle von 3 K; eine Pumpe, die bei 2,5 K Abstand lief, ging mit
  dem Neustart sofort aus. Die Regel beginnt jetzt bei laufender Pumpe und schaltet frühestens
  nach der Haltezeit ab.
- **Brennererkennung über einen Neustart.** Brennerzustand und Bezugslinie werden mit den
  Tageswerten gesichert. Lief der Brenner beim letzten Sichern und ist das Abgasrohr noch warm,
  gilt er nach dem Neustart weiter als laufend, ohne zweiten Start. Bisher begann die
  Bezugslinie am heißen Rohr, und ein laufender Brenner wurde erst nach weiteren 12 K Anstieg
  erkannt, gegen Ende eines Laufs gar nicht; am 23. September vergingen gut vier Minuten. Ein
  Eintrag der Tageswerte aus 0.4.0 wird weiter gelesen.

### Werkzeuge und Dokumentation

- **Attrappe des Leitstands**, `tools/mock_station.py`, mit einem verschlüsselten Climate-Sat und
  zwei offenen Thermometern; `apple/Werkzeuge/attrappen.sh` startet sie auf Port 8325. Sie liefert
  Brennerläufe im Zweistundentakt und Ereignisse mit Neustart, Ausfall, Firmwarewechsel und
  Befund, damit Abgleich und Markierungen im Simulator prüfbar sind.
- `tools/mock_heatsource.py` meldet eine Verbrauchslinie (`trend`), und das Tagesprotokoll folgt ihr;
  damit lässt sich der Wärmepumpen-Check im Simulator prüfen.
- **Handbuch:** Kapitel Leitstand; Kesselkreispumpe mit Brennerregel und Neustart.
- **Prüfungen:** 649 (v0.4.0: 578), darunter der Brennerlauf vom 23. September mit den gemessenen
  Werten; dazu 137 für das Protokoll des Leitstands (`test/host/test_protokoll.c`) gegen
  anonymisierte Zustände der Anlage.
- **Katalog der Messgrößen**, `docs/katalog-messgroessen.md`, für Leitstand und App.
- **`tools/protokoll_pruefen.py`** prüft das Protokoll eines Tages auf der Karte: Spalten,
  Zeitfolge, JSON, Abtastungen je Gerät, unbelegte Lücken.
- **Prüfungen des Protokolls:** 137, dazu das Lesen der eigenen Dateien.

## v0.4.0 — 22. September 2026

### Verteilerplatine

- **Verschlüsselte BTHome-Thermometer.** Der Verteiler empfängt BTHome in Fassung 2, offen und
  mit AES-128-CCM verschlüsselt, etwa vom Climate-Sat von camperSense. Den Schlüssel je Gerät,
  32 Hexadezimalziffern aus der camperSense-App, nimmt die Oberfläche unter **Sensoren**
  entgegen, die Schnittstelle unter `POST /api/ble/key`. Bis zu 16 Schlüssel liegen in einem
  eigenen Eintrag im NVS, nicht in der Konfiguration; `GET /api/ble` nennt den Stand je Gerät
  („fehlt", „falsch", „passt") und die Adressen mit Schlüssel. Die Sicherung trägt die Schlüssel
  im Klartext, das Zurückspielen übernimmt sie, die Werksvorgabe löscht sie. Ein Rahmen mit
  einem nicht höheren Zähler als der zuletzt angenommene bleibt ohne Wirkung, nach zehn Minuten
  ohne gültigen Rahmen gilt er wieder. Werte, die ein Gerät auf mehrere Rahmen verteilt, werden
  feldweise übernommen.
- **Bluetooth gibt dem WLAN Funkzeit zurück.** WLAN und Bluetooth teilen sich ein Funkteil, und
  die Suche nach Thermometern belegte es durchgehend. Die Anmeldung am Einrichtungs-Zugangspunkt
  einer frischen Platine gelang deshalb erst nach mehreren Versuchen. Die Suche belegt das
  Funkteil jetzt zu 30 Prozent und ruht, solange nur der Zugangspunkt läuft; die Anmeldung
  gelingt beim ersten Versuch. Am Erdgeschoss gemessen sind die Messwerte der Thermometer
  dadurch im Mittel 24,7 statt 9,5 s alt, höchstens 112 statt 61 s; die Zeitgrenze liegt bei
  900 s.
- **Bedarf nur aus geregelten Kreisen.** Wärmebedarf melden nur noch Räume, die eingeschaltet
  sind und einen Messwert haben. Kreise ohne Raum zählten bisher mit; weil sie nach dem ersten
  Start auf Anschlag offen stehen, meldeten alle drei Verteiler dauerhaft Bedarf. Von Hand
  gehaltene Ventile zählen weiterhin.
- **Kreise ohne Raum fahren zu.** Einmal in der Minute wird geprüft, ob ein Kreis ohne Raum offen
  steht. Von Hand gehaltene Kreise, Messfahrt und Schutzfahrt bleiben unberührt.
- **Kennwort des Zugangspunkts.** Ein Kennwort unter acht Zeichen wird abgewiesen. Bisher öffnete
  das Gerät den Zugangspunkt in diesem Fall ohne Kennwort, ohne darauf hinzuweisen. Ebenso wird
  MQTT ohne Adresse des Brokers nicht mehr eingeschaltet — beides wie beim Heizungsgerät.

### Heizungsgerät

- **Brennerende am Ausschlag.** Die erste Brennerfahrt der Anlage dauerte laut Gerät vier Stunden
  und sechs Minuten, tatsächlich rund fünfzig Minuten: Ein warmer Kessel hält das Abgasrohr
  dauerhaft über der Ausschaltschwelle. Aus ist der Brenner jetzt auch, wenn das Abgas um 6 K
  unter den Höchstwert der Fahrt fällt; an ist er, wenn es über der Bezugslinie liegt und um
  6 K über den Tiefstwert gestiegen ist. Neue Einstellung `burner.swing_k`.
- **Kesselkreispumpe gegen den Speicher.** Verglichen wird der Kesselvorlauf mit der
  Speichertemperatur, ersatzweise mit dem Rücklauf. Bei stehender Pumpe gleichen sich Vor- und
  Rücklauf an, und ein Kessel mit Restwärme blieb stehen, obwohl der Speicher kälter war.
- **Kesselkreispumpe früher aus.** Die Schwellen gehen von 1,0/0,5 K auf 3,0/2,0 K. Mit 0,5 K lief
  die Pumpe nach dem Brennerende fünf Stunden weiter und hielt nur noch den Kessel auf
  Speichertemperatur; der Speicher hatte seinen Höchststand bei genau 2 K Abstand erreicht.
- **Nullpunkt des Füllstands am Brennerstart.** Läuft der Brenner an, nachdem der Speicher um
  mindestens 3 K gefallen ist, wird der Nullpunkt zu 40 Prozent an den Speicherwert
  herangeführt. Neue Einstellungen `buffer.leer_lernen`, `buffer.lern_drop_k`,
  `buffer.leer_epoch`. Leer- und Vollpunkt sind in der Oberfläche einstellbar.
- **Kalter Anlauf ist keine fertige Ladung.** „Geladen" verlangt zusätzlich einen Kesselvorlauf
  über `buffer.kessel_hot_c` (60 °C). Beim Anfahren aus dem kalten Kessel lagen Vor- und
  Rücklauf 400 s lang dicht beieinander, und die Ladung galt nach wenigen Minuten als fertig.
- **Rückströmung.** Steigt der Kesselrücklauf bei stehender Pumpe und ausgeschaltetem Brenner um
  mindestens 3 K, meldet das Gerät den Befund `backflow`. Beobachtet bei einer
  Warmwasserzapfung: 36,9 auf 46,3 °C bei 32 °C im Vorlauf.
- **Warmwasserzapfungen.** Ein Einbruch des Speichers um mindestens 2 K in 15 Minuten zählt als
  Zapfung, außerhalb von Ladungen. Mit eingetragenem Speicherinhalt auch in Kilowattstunden.
  Neue Einstellungen `buffer.zapf_drop_k`, `buffer.zapf_win_s`, `buffer.volumen_l`.
- **Alle Einstellungen in der Oberfläche.** Sechzehn von vierundvierzig Werten waren nur über die
  Schnittstelle erreichbar, darunter die gesamte Brennererkennung.
- **Tageswerte gesichert.** Während eines Brennerlaufs werden Laufzeit und Starts alle fünf
  Minuten gesichert. Ein Neustart mitten im Lauf verlor bisher die ganze Ladung aus der
  Tagesbilanz.
- **Aufzeichnung übersteht einen Neustart.** Eine scharf geschaltete Aufzeichnung bleibt es.
- **Verlauf bleibt erhalten**, wenn ein Messwert des Nachbargeräts ausfällt. Bisher begann er
  dann von vorn; nach acht Stunden standen vier Messpunkte statt zweihundertfünfzig.
- **Anlagenschema.** Heizkreise des Nachbargeräts erscheinen blass mit ihren Werten, die Pumpe
  ist mit ihrem Zustand beschriftet.
- **`POST /api/system/clear-logs`** verwirft Protokolle und Tageswerte, etwa nach einer Zeit mit
  falscher Erkennung.

### Behoben

- **Vorgaben der Kesselkreispumpe.** Die Einstellungsablage gab einem neu eingerichteten Gerät
  noch 1,0/0,5 K, während Rechenmodul und Prüfungen mit 3,0/2,0 K arbeiteten.
- **`burner.swing_k`** wurde nicht an die Brennererkennung übergeben; eine Änderung blieb
  wirkungslos.
- **Fühler über die Schnittstelle.** Eine Teilangabe, etwa nur Kennung und Rolle, setzte den
  Korrekturwert auf 0. Fehlende Felder behalten jetzt ihren Wert.
- **Fehlende Prüfungen.** Das Heizungsgerät nahm täglichen Neustart, Haltezeiten der
  Brennererkennung, Leer-, Voll- und Warnwert, „Vorlauf heiß", Haltezeit für „geladen" und das
  Zapfungsfenster ohne Prüfung an. Es gelten jetzt die Grenzen, die die Oberfläche schon anzeigte.
  Umgekehrt ließ die Oberfläche als Abfrageabstand der Verteiler bis 600 s zu, das Gerät nimmt
  höchstens 300 s.
- **Meldung zur Busbelegung.** Das Protokoll verlangte nach einer Änderung einen Neustart; die
  Fühlererfassung stellt aber sofort um.
- **Grund der stehenden Kesselkreispumpe.** Er lautete „Rücklauf wärmer als Vorlauf", nach der
  Regel bis zum 18. August. Seitdem wird der Kesselvorlauf mit dem Speicher verglichen; der
  Grund heißt jetzt „Kessel kaum wärmer als der Speicher".

### Werkzeuge und Dokumentation

- **`tools/check_defaults.py`** vergleicht Vorgabewerte, die an zwei Stellen stehen:
  Einstellungsablage und Rechenmodule des Heizungsgeräts, dazu die Werte, mit denen die
  Verteiler-Oberfläche einen neuen Raum anlegt.
- **Handbuch.** Jede Einstellung beider Gerätetypen mit Schlüssel, Vorgabe, zulässigem Bereich
  und Bedeutung; Befunde mit ihren Kennungen und Auslösebedingungen; Proportionalband richtig
  beschrieben (am Sollwert halb offen).
- **Attrappe des Verteilers** mit einem verschlüsselten Climate-Sat, der erst mit dem
  Testschlüssel der Prüfungen Werte liefert, samt `POST /api/ble/key` und Schlüsseln in der
  Sicherung.
- **Prüfungen:** 578 (v0.3.0: 483), darunter AES-128 nach FIPS-197 und BTHome-Rahmen aus dem
  Rahmenbau der Satelliten-Firmware.

## v0.3.0 — 17. August 2026

Zweite Fassung mit Heizungsteil: Kesselkreispumpe, Auswertung der Protokolle (Verbrauchslinie,
Abgas-Vorlauf-Abstand), Plausibilitätsprüfungen der Fühler, Sicherung und Wiederherstellung der
Einstellungen, Außentemperatur für die Heizungsgeräte. Fertige Abbilder und Web-Flasher.
[Versionshinweise](https://github.com/rubenmuehlhans/floor-heating-ctrl/releases/tag/v0.3.0)

## v0.2.0 — 16. August 2026

[Versionshinweise](https://github.com/rubenmuehlhans/floor-heating-ctrl/releases/tag/v0.2.0)
