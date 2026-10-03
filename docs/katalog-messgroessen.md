# Katalog der Messgrößen

Stand: 23. September 2026. Grundlage für das Protokoll des Leitstands und den Verlauf der App.
Umgesetzt in C in `components/protokoll/katalog.c`, in Swift in `apple/Packages/Verlauf`
(`Abtastung`). Die Schlüssel sind in beiden Umsetzungen dieselben; die App kennt davon heute die
mit „App“ gekennzeichneten.

## Grundsätze

- Ein Schlüssel gilt innerhalb eines Geräts. Das Gerät steht im Dateinamen
  (`<kennung>.csv`), nicht im Schlüssel.
- Welche Spalten ein Gerät hat, folgt aus seinem Aufbau: Fühler mit Rolle, Räume, Kanäle,
  Heizkreise. Fehlt ein Messwert, bleibt die Zelle leer; die Spalte bleibt bestehen. Eine neue
  Spalte kommt nur hinzu, wenn sich der Aufbau ändert, etwa durch einen neuen Raum. Dann beginnt
  eine neue Datei `<kennung>.2.csv` mit erweiterter Kopfzeile.
- Jedes Gerät zeichnet nur auf, was es selbst misst. Den Brenner zeichnet das Gerät mit dem
  Abgasfühler auf, den Füllstand das Gerät mit dem Pufferfühler.
- Zeiten stehen in UTC im Format ISO 8601 (`2026-09-23T05:37:57Z`).
- Zahlen mit Punkt, ganze Zahlen ohne Nachkommastellen, sonst höchstens drei.

## Heizungsgerät

| Schlüssel | Bedeutung | Einheit | Mittel | Quelle in `/api/state` | App |
|---|---|---|---|---|---|
| `fuehler.<rolle>` | eigener, zugeordneter Fühler | °C | Mittelwert | `probes[].temp_c` | ja |
| `brenner` | Brenner läuft, nur mit eigenem Abgasfühler | 0/1 | Anteil | `burner.running` | ja |
| `fuellstand` | geschätzte Ladung, nur mit eigenem Pufferfühler | Anteil | Mittelwert | `charge.level` | ja |
| `pumpe.<id>` | Pumpe des Heizkreises läuft | 0/1 | Anteil | `circuits[].on` | ja |
| `kkp` | Kesselkreispumpe läuft, wenn eingerichtet | 0/1 | Anteil | `boiler_pump.on` | ja |
| `brenner.starts` | Starts des laufenden Tages | Anzahl | letzter Wert | `burner.starts_today` | nein |
| `brenner.laufzeit` | Laufzeit des laufenden Tages | s | letzter Wert | `burner.runtime_today_s` | nein |
| `brenner.oel` | geschätzter Verbrauch des Tages | l | letzter Wert | `burner.litres_today` | nein |
| `abgas.bezug` | Bezugslinie der Brennererkennung | °C | Mittelwert | `burner.baseline_c` | nein |
| `ladung.phase` | Ladephase, Kennzahl siehe unten | Kennzahl | letzter Wert | `charge.phase` | nein |
| `ladung.spreizung` | Spreizung am Kessel | K | Mittelwert | `charge.spread_k` | nein |
| `kreis.<id>.bedarf` | ein Abnehmer meldet Bedarf | 0/1 | Anteil | `circuits[].demand` | nein |
| `kreis.<id>.veraltet` | eine Bedarfsquelle antwortet nicht mehr | 0/1 | Anteil | `circuits[].stale` | nein |
| `befunde` | offene Befunde | Anzahl | letzter Wert | `findings` | nein |

## Verteiler

| Schlüssel | Bedeutung | Einheit | Mittel | Quelle | App |
|---|---|---|---|---|---|
| `raum.<n>.ist` | Raumtemperatur, nur mit gültigem Messwert | °C | Mittelwert | `rooms[].temp_c` | ja |
| `raum.<n>.soll` | Sollwert, leer bei ausgeschaltetem Raum | °C | Mittelwert | `rooms[].target_c` | ja |
| `raum.<n>.stellung` | Zielstellung der Ventile | Anteil | Mittelwert | `rooms[].target_position` | ja |
| `raum.<n>.feuchte` | Luftfeuchte des Raumthermometers | % | Mittelwert | `rooms[].humidity` | ja |
| `raum.<n>.betrieb` | Betriebsart, Kennzahl siehe unten | Kennzahl | letzter Wert | `rooms[].mode` | nein |
| `raum.<n>.batterie` | Batterie des Raumthermometers | % | letzter Wert | `rooms[].battery` | nein |
| `kanal.<n>.stellung` | Stellung des Kanals, wenn bekannt | Anteil | Mittelwert | `channels[].position` | ja |
| `vorlauf.<i>` | 1-Wire-Fühler i der Platine | °C | Mittelwert | `local_sensors.ds18b20[i]` | ja |
| `aussen` | Außenfühler, wenn eingetragen | °C | Mittelwert | `outdoor.temp_c` | ja |
| `bedarf` | Bedarfsmeldung an die Heizungsgeräte | 0/1 | Anteil | `/api/demand`: `demand` | nein |
| `raeume.rufend` | Räume mit Bedarf | Anzahl | Mittelwert | `/api/demand`: `rooms_calling` | nein |
| `kanaele.offen` | offene Kanäle | Anzahl | Mittelwert | `/api/demand`: `open_channels` | nein |

## Leitstand

| Schlüssel | Bedeutung | Einheit | Mittel |
|---|---|---|---|
| `funk.<adresse>.temp` | empfangenes Funkthermometer | °C | Mittelwert |
| `funk.<adresse>.feuchte` | dessen Luftfeuchte | % | Mittelwert |
| `funk.<adresse>.batterie` | dessen Batterie | % | letzter Wert |
| `funk.<adresse>.rssi` | dessen Empfangsstärke | dBm | Mittelwert |

Die Adresse ist die MAC-Adresse in Großbuchstaben mit Doppelpunkten. Die Spalten sind nach
Adresse geordnet, damit sie nicht von der Reihenfolge abhängen, in der die Thermometer zuerst
empfangen wurden.

## Alle Geräte

| Schlüssel | Bedeutung | Einheit | Mittel |
|---|---|---|---|
| `geraet.heap` | freier Arbeitsspeicher | Byte | Mittelwert |
| `geraet.rssi` | WLAN-Empfang | dBm | Mittelwert |
| `geraet.laufzeit` | Zeit seit dem letzten Neustart | s | letzter Wert |

## Kennzahlen

| Schlüssel | Wert | Bedeutung |
|---|---|---|
| `raum.<n>.betrieb` | 0 | aus (`mode` = `off`) |
| | 1 | heizen (jede andere Betriebsart) |
| `ladung.phase` | 0 | unbekannt |
| | 1 | keine Ladung |
| | 2 | wird geladen |
| | 3 | geladen |

## Fünfminutenmittel

Plätze zu 300 s in UTC, Zeitstempel am Beginn des Platzes, wie im Raster der App. Ein
Schaltzustand wird zum Anteil: Lief der Brenner drei von fünf Minuten, steht dort 0,6. Zähler
und Kennzahlen gehen mit ihrem letzten Wert ein. Ein Platz ohne jeden Wert bekommt keine Zeile.

## Ereignisse

Je Zeile in `ereignisse.jsonl` ein Objekt mit `zeit`, `geraet`, `art` und diesen Feldern:

| Art | Felder | Anlass |
|---|---|---|
| `neustart` | `laufzeit_vorher_s`, `grund` | Laufzeit kleiner als bei der vorigen Abfrage; `grund` aus `reset_reason`, sofern das Gerät ihn meldet |
| `nicht_erreichbar` | — | drei Abfragen in Folge ohne Antwort |
| `erreichbar` | `dauer_s` | wieder erreichbar, mit der Dauer der Lücke |
| `version` | `alt`, `neu` | andere Firmware |
| `brenner` | `ein`, `dauer_vorher_s` | Ein- oder Ausschalten; die Dauer fehlt beim ersten Wechsel nach einem Neustart des Leitstands |
| `pumpe` | `pumpe` (Kreis oder `kkp`), `ein`, `dauer_vorher_s` | ebenso |
| `sollwert` | `raum`, `alt`, `neu` | Sollwert geändert |
| `betriebsart` | `raum`, `alt`, `neu` (`heiz`, `aus`) | Raum ein- oder ausgeschaltet |
| `befund` | `code`, `stand` (`neu`, `erledigt`) | Befund eines Heizungsgeräts |
| `funk` | `adresse`, `name`, `stand` (`verloren`, `wieder`, `schluessel_falsch`) | Funkthermometer eine Viertelstunde stumm, wieder empfangen, oder sein Schlüssel passt nicht |
| `leitstand` | `was` (`start`, `karte`, `karte_verloren`, `karte_formatiert`, `versorgung_aus`, `versorgung_wieder`), `grund`, `verworfen`, `groesse_mb`, `akku_prozent` | Start des Leitstands, Wechsel der Karte, Ausfall und Rückkehr der Versorgung (Core2); `verworfen` zählt Abfragen vor der ersten gültigen Uhrzeit |

## Neustartgrund

`reset_reason` in `/api/state` aller drei Firmwares: `power_on`, `software`, `panic`,
`int_wdt`, `task_wdt`, `wdt`, `brownout`, `deepsleep`, `ext`, `sdio`, `usb`, `jtag`, `efuse`,
`pwr_glitch`, `cpu_lockup`, `unknown`. `software` steht auch nach einem Firmware-Update und einem
Neustart über die Oberfläche.
