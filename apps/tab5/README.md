# Bedientafel auf dem M5Stack Tab5

Die App der Fußbodenheizung als Firmware für den M5Stack Tab5 (ESP32-P4, 5″, 1280 × 720,
Funk über den ESP32-C6). Die Tafel zeigt dieselben fünf Bereiche wie die App auf iPhone, iPad
und Mac — Übersicht, Räume, Assistent, Heizung, Geräte — im Aufbau der iPad-Fassung
(Seitenleiste, Doppelseite mit zwei Spalten) und mit den Farben, Formaten und Texten der App.
Der Assistent spricht wie in der App mit Claude von Anthropic, über den eigenen API-Schlüssel.

Vorbild für Aufbau, Board-Unterstützung und Funk ist der Tab5 von camperSense
(`smartCamper/satellite-display/tab5`); Fallen, die dort Zeit gekostet haben, sind an den
betreffenden Stellen im Quelltext vermerkt.

## Stand

| Bereich | Umfang | Vorlage in der App |
|---|---|---|
| Übersicht | Geräteleiste, Beispielanlage, Lagebericht mit Hinweisen, Wärmeerzeugung, drei schwerste Befunde, Räume als Kacheln je Etage; Befunde und Befund als Unterseite | `UebersichtAnsicht.swift` |
| Räume | je Etage Verteilerbalken und Raumkarten mit Stellrad und Ein/Aus; Raumdetail mit Messung, Stellrad, „Jetzt prüfen“, Kennzahlen, Ventilen und Verlauf (24 Std., 7 Tage) | `RaeumeAnsicht.swift`, `Stellrad.swift` |
| Heizung | Anlagenschema (Querfassung) über die volle Breite, Pufferspeicher, Brenner, Heizkreise und Kesselkreispumpe mit Betriebsart, Verlauf (6 Std. bis 7 Tage) | `HeizungAnsicht.swift`, `Anlagenschema.swift`, `VerlaufAnsichten.swift` |
| Geräte | Liste und gewähltes Gerät: Zustand, Kanäle bzw. Fühler am Bus, „Alle auf/zu/an die Regelung“, Neustart; WLAN; Einstellungen der Tafel | `GeraeteAnsicht.swift` |
| Assistent | Gespräch mit Werkzeugen, Überlegung und Werkzeugzeilen zum Aufklappen, Vorschlagskarten mit Übernehmen, Verwerfen und Rückgängig, Bezug aus „Fragen“, Lagebericht von selbst (höchstens alle sechs Stunden), frühere Lageberichte | `AssistentAnsicht.swift`, Packages `Assistent`, `Sprachmodelle` |

Die Befundregeln, die Zusammenführung der Gerätestände und der Verlauf (5-Minuten-Raster, sieben
Tage) laufen auf der Tafel selbst, wie in der App.

**Noch nicht übernommen:** Einstellungsformulare der Geräte, Messfahrt, Räume und Kanäle,
Sensoren und Funkschlüssel, Sicherung und Firmware-Übertragung (dafür bleibt die Weboberfläche
der Geräte), Wärmepumpen-Check, Verbrauchslinie, Ladungsaufzeichnung, Heizkurve, Wirkungskontrolle
der Vorschläge nach sieben Tagen, frühere Gespräche, Fotos an den Assistenten, Verlauf vom
Leitstand übernehmen. Aus den Befundregeln fehlen Sicherungen, „Pumpe läuft, die Wärme kommt
nicht an“ und „Speicher gilt früher als voll“.

**Am Gerät noch nicht geprüft.** Die Firmware baut ohne Warnung; Oberfläche, Datenschicht und
Werkzeuge sind auf dem Mac geprüft (Vorschau und Prüfungen unten). Der erste Lauf am Tab5 mit
echten Geräten und echtem API-Schlüssel steht aus.

## Bauen und Flashen

```bash
. ~/esp/esp-idf-6.0.2/export.sh
idf.py -C apps/tab5 build
idf.py -C apps/tab5 -p /dev/cu.usbmodemXXXX flash monitor
```

- Bootloader `0x2000`, Partitionstabelle `0x10000`, App `0x20000`.
- ⚠️ `sdkconfig.defaults` wirkt nur auf eine frische `sdkconfig`: nach einer Änderung `rm -f sdkconfig`.
- ⚠️ Eine neue Datei in `main/ui`, `main/kern` oder `main/geraet` sieht CMake erst nach
  `idf.py reconfigure`.
- ⚠️ Nach einem Reset über USB kann der P4 im ROM hängen bleiben (camperSense, Falle 21).
  Ausweg ohne Kabelziehen: `esptool.py --chip esp32p4 -p … --after watchdog_reset read-mac`.

## Vorschau und Prüfungen auf dem Mac

```bash
apps/tab5/host/vorschau.sh            # alle Seiten, dunkel und hell → host/shots/
apps/tab5/test/run_tests.sh           # Datenschicht, Befunde, Claude-Strom, Werkzeuge
```

Beide laufen mit der Beispielanlage (`main/beispiel/beispiel.json`, erzeugt von
`tools/beispielanlage.py` aus den Mitschnitten der App) und brauchen nur einmal
`idf.py reconfigure`, damit LVGL und cJSON in `managed_components` liegen.

Bildschirmfotos vom Gerät: `~/panelvenv/bin/python3 tools/aufnahme.py --port … uebersicht raeume`.

## Aufbau

| Datei | Aufgabe |
|---|---|
| `main/main.cpp` | Startreihenfolge (der Kommentar dort ist Vertrag) |
| `main/daten.h`, `eins.h` | Schnittstelle der Oberfläche zur Anlage und zu den Einstellungen |
| `main/kern/anlage.*`, `zusammen.cpp` | Anlagenbild aus den Geräteständen (`Zusammenfuehrung.swift`) |
| `main/kern/befunde.*` | Befundregeln und Befundgedächtnis |
| `main/kern/verlauf.*` | Verlauf im 5-Minuten-Raster, sieben Tage |
| `main/kern/fmt.*` | Zahlen und Zeiten deutsch, geschütztes Leerzeichen vor der Einheit, MEZ/MESZ |
| `main/kern/claude.*` | Messages-API: Anfrage, Strom (SSE), Transkript |
| `main/kern/werkzeuge.*`, `katalog.*` | die zehn Werkzeuge der App, Parameter für Vorschläge |
| `main/kern/gespraech.*` | Werkzeugschleife, Vorschläge übernehmen und zurücknehmen, Lagebericht |
| `main/kern/anweisungen.*` | Systemanweisungen; das Wissen (Handbuch, Konzepte) wird aus `docs/` eingebettet |
| `main/geraet/daten.cpp` | Abfrage-Task: mDNS, Abfragetakte, Befehle mit Rücklesen, Ablage in LittleFS |
| `main/geraet/netz.cpp` | Funkmodul (ESP-Hosted), WLAN, Uhrzeit |
| `main/geraet/assist.cpp` | Task des Assistenten mit eigener HTTPS-Verbindung |
| `main/geraet/konsole.cpp` | Befehle über USB: `info`, `seite`, `wifi`, `shot`, `neustart` |
| `main/ui/*` | Oberfläche; `stil.*` hält Farben, Schriften und Bausteine der App |
| `tools/gen_lv_fonts.py` | Schriften (Inter, JetBrains Mono) und Symbole (Lucide) nach `main/gen/` |
| `host/` | Vorschau auf dem Mac |

`main/kern` und `main/ui` kommen ohne ESP-IDF aus; das hält Vorschau und Prüfungen auf dem Mac
möglich. Netz läuft nur in eigenen Tasks, die Oberfläche im LVGL-Task; dazwischen stehen Zustände
mit `generation()`.

## Assistent

Wie in der App:

- **Schlüssel und Zustimmung.** Ohne API-Schlüssel und ohne Zustimmung „Daten an Anthropic“
  stellt die Tafel keine Anfrage. Beides steht unter Geräte › Einstellungen der Tafel; der
  Schlüssel liegt im NVS und erscheint nirgends im Klartext.
- **Was übertragen wird.** Messwerte, Einstellungen ohne Zugangsdaten, Befunde, Protokolle, Namen
  der Räume und Geräte, Fragen und Gesprächsverlauf, Handbuch und Konzepte (rund 165 000
  Zeichen, durch den Prompt-Cache meist zu einem Zehntel berechnet). Kennwörter, WLAN-Daten und
  Relaisadressen entfernt `json::ohne_zugangsdaten` vor jedem Werkzeugergebnis.
- **Eingriffe nur über Vorschläge.** `aenderung_vorschlagen` prüft gegen den Parameterkatalog
  (Bereich, Schritt, „steht bereits auf …“); übernommen wird erst auf der Karte. Die Tafel liest
  dann frisch, bricht ab, wenn der Wert inzwischen geändert wurde, schreibt nach den Regeln der
  Firmware (Räume vollständig, Heizkreise mit allen Kennungen), liest zurück und vergleicht.
  Scheitert eine von mehreren Änderungen, nimmt sie die vorigen zurück.
- **Modell.** Opus 5.5 (Vorgabe), Sonnet 5.5 oder Haiku 4.5; adaptives Denken außer bei Haiku.

Unterschiede zur App: Die Tafel sichert vor dem Schreiben nicht (das bleibt der App), und ihr
Verlauf reicht sieben statt bis zu 365 Tage zurück.
