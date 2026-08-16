# Umbau auf zwei Anwendungen mit gemeinsamen Komponenten

Dieses Dokument beschreibt Stufe 0 der Erweiterung um die Heizungsgeräte
([Konzept](konzept-waermeerzeuger.md)): das Projekt wird von einer Anwendung auf zwei umgestellt,
die sich `components/` teilen. Es ist ein rein mechanischer Umbau — an der Verteiler-Firmware
ändert sich fachlich nichts, und genau das muss danach belegt sein.

## Ausgangslage

| Merkmal | Stand vor dem Umbau |
|---|---|
| Wurzel-`CMakeLists.txt` | vier Zeilen, `project(floor-heating-ctrl)`, kein `EXTRA_COMPONENT_DIRS` |
| Anwendung | genau ein `main/` mit 6 Quelldateien, 3364 Zeilen |
| Komponenten | 13 unter `components/`, 3085 Zeilen |
| Bau | `idf.py build` aus der Projektwurzel |
| Abbild | 1 234 240 Byte von 1 966 080 Byte Partitionsgröße |
| Varianten | keine — kein Kconfig, keine `#ifdef`-Zweige, keine Prüfstrecke |

## Zielaufbau

```
floor-heating-ctrl/
  cmake/embed_www.cmake        gemeinsame Regel fuer die eingebettete Oberflaeche
  components/                  gemeinsam genutzt
  apps/manifold/               Verteiler-Firmware
    CMakeLists.txt
    partitions.csv
    sdkconfig.defaults
    dependencies.lock
    main/
  apps/heatsource/             Firmware der Heizungsgeraete (Stufe 1)
    components/config_store/   eigene Fassung, verdraengt die gemeinsame
  test/  tools/  docs/  custom_components/   unveraendert in der Wurzel
```

In der Wurzel bleibt keine `CMakeLists.txt`. Gebaut wird mit `-C`:

```bash
idf.py -C apps/manifold build
```

## Warum die Komponenten nicht angefasst werden

ESP-IDF sucht Komponenten in einer festen Reihenfolge (`tools/cmake/project.cmake`): zuerst
`<app>/main`, dann `EXTRA_COMPONENT_DIRS`, zuletzt `<app>/components`. Später gefundene
Komponenten verdrängen früher gefundene gleichen Namens.

Damit lässt sich die einzige harte Kopplung ohne Eingriff auflösen:
`components/netmgr/include/netmgr.h` bindet `config_store.h` ein und führt `app_config_t` in
seiner Schnittstelle; `config_store.h` bindet seinerseits `hw_map.h` ein und trägt damit die
Annahme von elf Ventilkanälen in jede Anwendung, die WLAN benutzt.

Die Heizungs-Anwendung legt statt dessen unter `apps/heatsource/components/config_store/` eine
eigene Fassung mit eigenem Schema ab. Sie muss lediglich den von `netmgr` benutzten Ausschnitt
bereitstellen — nachweislich elf Felder und drei Funktionen:

| Verwendung in `components/netmgr/netmgr.c` | Zeilen |
|---|---|
| `cfg->wifi.ssid`, `cfg->wifi.pass` | 125, 126, 188, 189, 312, 318, 356, 407 |
| `cfg->wifi.hostname` | 154, 337, 338, 394, 395 |
| `cfg->wifi.ap_pass` | 161, 162 |
| `cfg->timezone` | 330, 393 |
| `cfg_peek()->reboot_hour`, `->reboot_minute` | 263, 264 |
| `cfg_lock()`, `cfg_unlock()`, `cfg_peek()` | 262–265 |

Weder `rooms` noch `channels` noch `touch_thresh` werden von `netmgr` gelesen. Der Vertrag ist
damit stillschweigend und wird als Kommentarblock in `netmgr.h` festgehalten — die einzige
Änderung am gemeinsamen Bestand in dieser Stufe, und eine, die den Objektcode nicht anfasst.

Ein eigenes `hw_map` braucht die Heizungs-Anwendung nicht: die neue Komponente `onewire_temp`
nimmt den Pin als Parameter, ein Display ist nicht vorgesehen, und die Platinenbausteine des
Verteilers werden ausgeschlossen.

### Warum kein sauberer Schnitt an `netmgr` und `config_store`

Der bessere Endzustand wäre eine eigene `netmgr_cfg_t` und ein gemeinsamer JSON-Unterbau. Beides
ist aufgeschoben, nicht verworfen (siehe Stufe 5 im Konzept). Drei Gründe:

1. `config_store.c` ist die Datei, deren Fehlverhalten am teuersten ist. Schlägt `cfg_from_json`
   fehl, fällt `cfg_init` auf `cfg_defaults` zurück, und dort steht `room_count = 0` — die
   gesamte Raumaufteilung des laufenden Geräts wäre weg.
2. Sie ist von den 215 Prüfungen in `test/host` nicht berührt, weil sie an NVS und cJSON hängt.
   Es gibt für einen Umbau also keine Absicherung.
3. Der Ertrag wäre gering. Real gemeinsam nutzbar sind etwa 60 Zeilen, und ob die beiden Schemata
   sich decken, weiß man erst, wenn das zweite existiert.

Herausgezogen wird später aus zwei laufenden Fassungen, nicht auf Verdacht aus einer.

## Zwei Fallstricke

### Absolute Pfade im Abbild

ESP-IDF setzt `-fmacro-prefix-map=<Projektwurzel>=.`, damit `__FILE__` relative Pfade ergibt.
Nach dem Umzug ist die Projektwurzel `apps/manifold`, und `components/` liegt außerhalb. Die drei
heute im Abbild stehenden Zeichenketten

```
./components/config_store/config_store.c
./components/netmgr/netmgr.c
./main/main.c
```

würden dann zu zwei absoluten Pfaden mit dem Heimatverzeichnis. Beide Anwendungen tragen deshalb
eine zusätzliche Abbildung auf das Wurzelverzeichnis des Repositorys ein. Geprüft wird mit
`strings` gegen die gesicherte Liste.

### Zeitstempel im gzip-Kopf

Die bisherige Zeile `gzip.open(sys.argv[2], 'wb', 9)` schreibt die aktuelle Uhrzeit und den
Dateinamen `index.html` in den gzip-Kopf. Dadurch unterscheidet sich das Abbild schon heute von
Bau zu Bau. Die neue Regel setzt `mtime=0` und leeren Dateinamen; das kostet rund 15 Byte
Unterschied gegenüber dem laufenden Abbild und macht den Bau reproduzierbar.

Diese Änderung kommt deshalb in einen **eigenen Commit nach** dem Verschieben — sonst ließe sich
beim Größenvergleich nicht mehr trennen, ob eine Abweichung vom gzip-Kopf oder von einem falschen
Pfad stammt.

## Die Dateien im Einzelnen

### `apps/manifold/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)

# Gemeinsame Komponenten liegen eine Ebene ueber den Anwendungen.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${REPO_ROOT}/components")

# Wiederverwendbare Bauregeln.
list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
include(embed_www)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# __FILE__ soll weiterhin "./main/..." und "./components/..." ergeben statt
# absoluter Pfade. Die engere Regel steht zuerst.
idf_build_set_property(COMPILE_OPTIONS
    "-fmacro-prefix-map=${CMAKE_CURRENT_LIST_DIR}=."
    "-fmacro-prefix-map=${REPO_ROOT}=."
    APPEND)

project(floor-heating-ctrl)
```

Die Reihenfolge ist bindend:

- `EXTRA_COMPONENT_DIRS` und `CMAKE_MODULE_PATH` **vor** `include(project.cmake)`
- `idf_build_set_property` **nach** `include(project.cmake)` — vorher gibt es die Funktion nicht —
  und **vor** `project()`, weil danach `idf_build_process` bereits gelaufen ist

Der Projektname bleibt `floor-heating-ctrl`. Das hält `esp_app_desc_t.project_name`, den
Artefaktnamen und die OTA-Zeile der README unverändert.

### `apps/heatsource/CMakeLists.txt` (Stufe 1)

Gleicher Aufbau, zusätzlich:

```cmake
# Bauteile des Verteilers werden hier nicht gebraucht. Ausschliessen spart
# nicht nur Uebersetzungszeit: der Komponentenverwalter loest alle gefundenen
# Manifeste auf, bevor der Abhaengigkeitsbaum beschnitten wird, und schriebe
# sonst fremde Abhaengigkeiten in dependencies.lock.
set(EXCLUDE_COMPONENTS hw_map sr74hc595 bemf valve roomctrl atc_ble ssd1327
                       sensors_local i2cbus)

project(heat-source-ctrl)
```

### `cmake/embed_www.cmake`

```cmake
# Legt eine Weboberflaeche komprimiert ins Programmabbild.
#
#   embed_www(TARGET  <komponentenbibliothek>
#             OUTPUT  <dateiname>
#             SOURCES <datei> [<datei> ...])
#
# Die Quelldateien werden in der angegebenen Reihenfolge byteweise
# aneinandergehaengt und danach mit gzip -9 gepackt. Damit laesst sich ein
# gemeinsames Stilblatt zwischen einen app-eigenen Kopf und Rumpf schieben,
# ohne die Oberflaeche zur Laufzeit aus mehreren Dateien zusammenzusetzen.
#
# Der Symbolname folgt dem Dateinamen aus OUTPUT: aus "index.html.gz" werden
# _binary_index_html_gz_start und _binary_index_html_gz_end.
#
# Zeitstempel und Dateiname bleiben aus dem gzip-Kopf heraus, damit zwei
# Uebersetzungen derselben Quellen dasselbe Ergebnis liefern.

function(embed_www)
    cmake_parse_arguments(EW "" "TARGET;OUTPUT" "SOURCES" ${ARGN})

    if(NOT EW_TARGET OR NOT EW_OUTPUT OR NOT EW_SOURCES)
        message(FATAL_ERROR "embed_www: TARGET, OUTPUT und SOURCES sind Pflicht")
    endif()
    if(EW_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "embed_www: unbekannte Angabe ${EW_UNPARSED_ARGUMENTS}")
    endif()

    set(sources "")
    foreach(src IN LISTS EW_SOURCES)
        get_filename_component(abs "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${abs}")
            message(FATAL_ERROR "embed_www: Quelldatei fehlt: ${abs}")
        endif()
        list(APPEND sources "${abs}")
    endforeach()

    idf_build_get_property(python PYTHON)
    set(gz "${CMAKE_CURRENT_BINARY_DIR}/${EW_OUTPUT}")

    add_custom_command(
        OUTPUT "${gz}"
        COMMAND "${python}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_www.py" "${gz}" ${sources}
        DEPENDS ${sources} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_www.py"
        COMMENT "Oberflaeche zusammenfuegen und komprimieren: ${EW_OUTPUT}"
        VERBATIM)

    string(MAKE_C_IDENTIFIER "www_gzip_${EW_OUTPUT}" stem)
    add_custom_target(${stem} DEPENDS "${gz}")
    add_dependencies(${EW_TARGET} ${stem})

    target_add_binary_data(${EW_TARGET} "${gz}" BINARY)
endfunction()
```

Der Symbolname bleibt derselbe wie heute: `target_add_binary_data` leitet ihn über
`MAKE_C_IDENTIFIER` aus dem Dateinamen ab, `index.html.gz` ergibt also weiterhin
`_binary_index_html_gz_start` und `_binary_index_html_gz_end`. Die `extern`-Deklarationen in
`app_web.c` bleiben unangetastet. Beide Anwendungen dürfen denselben Ausgabenamen verwenden, die
Bauverzeichnisse sind getrennt.

### `apps/manifold/main/CMakeLists.txt`

Die `REQUIRES`-Liste bleibt zeichengleich. Ersetzt wird nur der Python-Block:

```cmake
embed_www(TARGET  ${COMPONENT_LIB}
          OUTPUT  index.html.gz
          SOURCES www/index.html)
```

## Ablagen: pro Anwendung oder gemeinsam

| Datei | Ort | In Git | Begründung |
|---|---|---|---|
| `sdkconfig.defaults` | je Anwendung | ja | ESP-IDF sucht ausschließlich `${CMAKE_SOURCE_DIR}/sdkconfig.defaults` |
| `partitions.csv` | je Anwendung | ja | wird relativ zu `PROJECT_DIR` aufgelöst |
| `dependencies.lock` | je Anwendung | ja | Vorgabeort ist `PROJECT_DIR`; pinnt Version und Prüfsumme |
| `managed_components/` | je Anwendung | nein | erzeugt |
| `sdkconfig`, `sdkconfig.old` | je Anwendung | nein | erzeugt |
| `build/` | je Anwendung | nein | erzeugt |

`.gitignore` braucht keine Änderung: die vorhandenen Einträge haben keinen führenden
Schrägstrich und greifen deshalb auf jeder Ebene.

`partitions.csv` wird bewusst kopiert statt geteilt. Ein gemeinsamer Pfad änderte die Zeile
`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` und damit die erzeugte `sdkconfig` — genau das, was in
dieser Stufe unverändert bleiben muss. Eine Partitionstabelle beschreibt ohnehin ein einzelnes
Flash-Abbild, nicht eine Bauart.

Eine gemeinsame `sdkconfig.common` ist technisch möglich, wird aber erst erwogen, wenn beide
Anwendungen stehen. In dieser Stufe ist `apps/manifold/sdkconfig.defaults` ein reines `git mv`
mit unverändertem Inhalt — nur so ist die erzeugte `sdkconfig` beweisbar identisch.

## Unterschiede der `sdkconfig.defaults`

Die Heizungs-Anwendung braucht kein Bluetooth. Gegenüber der Verteiler-Fassung entfallen die
15 NimBLE- und Koexistenz-Zeilen, ersetzt durch:

```
# Kein Bluetooth: die Fuehler haengen am 1-Wire-Bus.
CONFIG_BT_ENABLED=n
```

`CONFIG_ESP_COEX_SW_COEXIST_ENABLE` braucht keine eigene Zeile — die Option hängt in Kconfig an
`ESP_WIFI_ENABLED && BT_ENABLED` und verschwindet mit `BT_ENABLED=n` von selbst.

Ersparnis, aus `build/floor-heating-ctrl.map` des laufenden Abbilds gemessen (belegte Abschnitte
`.flash.text`, `.flash.rodata`, `.iram0.*`, `.dram0.*`):

| Archiv | Byte |
|---|---:|
| `libbtdm_app.a` (Steuerwerk) | 79 464 |
| `libbt.a` (NimBLE-Wirt) | 31 238 |
| `libcoexist.a` | 10 376 |
| `libesp_coex.a` | 679 |
| `libatc_ble.a` | 2 780 |
| **Summe** | **124 537** |

`libphy.a` (46 109 Byte) bleibt, das WLAN braucht sie. Zusammen mit den entfallenden
Verteilerbausteinen liegt die Heizungs-Firmware voraussichtlich bei 0,95 bis 1,05 MB. Die
Partitionstabelle bleibt deshalb unverändert; die 1,9-MB-Slots reichen mit großem Abstand.

Beim Arbeitsspeicher sind die 8 640 Byte statisches `.dram0` der kleinere Teil — das
BLE-Steuerwerk belegt beim Start zusätzlich Halde. Erwartet werden 40 bis 60 kB mehr freie Halde;
nachmessbar über das Feld `heap` in `/api/state`.

## Reihenfolge

Jeder Schritt ein eigener Commit auf dem Zweig `umbau/zwei-apps`. `main` bleibt bis zum Nachweis
die belegbar laufende Fassung. Abbrechen heißt in jedem Zustand:

```bash
git checkout main && rm -rf build sdkconfig managed_components && idf.py build
```

| # | Schritt | Ändert Objektcode | Risiko |
|---|---|---|---|
| 0 | Referenzdaten sichern (siehe unten) | – | – |
| 1 | Zweig anlegen, `git mv`, beide `CMakeLists.txt`-Zeilen eintragen | nur `__FILE__`, und das soll gleich bleiben | mittel — einziges reales Risiko, vollständig durch die Prüfungen 1–5 abgedeckt |
| 2 | `cmake/embed_www.cmake`, Umstellung der Einbettung | ja, rund 15 Byte gzip-Kopf | niedrig |
| 3 | README, `tools/mock_device.py`, `tools/screenshots.sh` | nein | keins |
| 4 | OTA auf 192.168.1.250, Belege einholen | – | niedrig, der Bootlader springt bei Fehlstart zurück |
| 5 | Zweig nach `main` überführen | – | – |
| 6 | Prüfstrecke `.github/workflows/build.yml` | nein | keins |

Was ausdrücklich **nicht** in denselben Schritt gehört:

- Verschieben und `embed_www` gemeinsam — sonst ist die Byteabweichung nicht mehr zuzuordnen.
- Verschieben und ein späterer Umzug von `hw_map`/`config_store` nach `apps/manifold/components/`
  — der ändert die `__FILE__`-Zeichenkette von `config_store.c` und damit garantiert das Abbild,
  also genau die Prüfung, die Schritt 1 tragen soll.
- Die zweite Anwendung bauen, bevor die erste per OTA bestätigt ist.

## Nachweis, dass die Verteiler-Firmware unverändert ist

### Vor dem ersten `git mv`

```bash
cd /Users/rubenmuehlhans/Developer/esphome/floor-heating-ctrl
mkdir -p /tmp/fbh-ref
cp build/floor-heating-ctrl.bin  /tmp/fbh-ref/ref.bin
cp build/floor-heating-ctrl.map  /tmp/fbh-ref/ref.map
cp sdkconfig                     /tmp/fbh-ref/sdkconfig
cp dependencies.lock             /tmp/fbh-ref/dependencies.lock
strings -a build/floor-heating-ctrl.bin | grep -E '^\./' | sort -u > /tmp/fbh-ref/pfade.txt
. ~/esp/esp-idf-6.0.2/export.sh && idf.py size-components > /tmp/fbh-ref/groessen.txt
curl -s http://192.168.1.250/api/config > /tmp/fbh-ref/geraetekonfig.json
```

Die letzte Zeile ist die wichtigste: sie sichert die Raumaufteilung des laufenden Geräts, bevor
irgendetwas passiert.

### Bau nach dem Umzug

```bash
rm -rf build sdkconfig sdkconfig.old managed_components   # Wurzelreste
. ~/esp/esp-idf-6.0.2/export.sh
idf.py -C apps/manifold set-target esp32
idf.py -C apps/manifold build
```

### Sechs Prüfungen, in dieser Reihenfolge

**1 — Erzeugte Konfiguration identisch.** Die stärkste Einzelprüfung: gleiche `sdkconfig` heißt
gleiche Kconfig-Ableitung für jede Übersetzungseinheit.

```bash
diff /tmp/fbh-ref/sdkconfig apps/manifold/sdkconfig && echo "sdkconfig identisch"
```

**2 — Abhängigkeiten identisch.**

```bash
diff /tmp/fbh-ref/dependencies.lock apps/manifold/dependencies.lock && echo "Lock identisch"
```

**3 — Keine absoluten Pfade im Abbild.**

```bash
strings -a apps/manifold/build/floor-heating-ctrl.bin | grep -E '^\./' | sort -u \
  | diff /tmp/fbh-ref/pfade.txt - && echo "Pfade identisch"
strings -a apps/manifold/build/floor-heating-ctrl.bin | grep -c '/Users/'   # muss 0 sein
```

Schlägt die erste Zeile fehl, hat der Übersetzer die beiden `-fmacro-prefix-map`-Regeln anders
gewichtet als angenommen. Dann die Reihenfolge in der `CMakeLists.txt` tauschen und erneut bauen.

**4 — Größe.**

```bash
ls -l apps/manifold/build/floor-heating-ctrl.bin      # erwartet 1234240
idf.py -C apps/manifold size-components | diff /tmp/fbh-ref/groessen.txt -
```

Nach Schritt 1 identisch bis auf null Byte; nach Schritt 2 ein Unterschied von rund 15 Byte im
eingebetteten Block, sonst nichts.

**5 — Bytevergleich, auf die Beschreibungsstruktur eingegrenzt.** Bitgleich kann das Abbild nicht
sein, weil `esp_app_desc_t` Bau-Datum und -Uhrzeit trägt. Erwartet wird, dass *alle* abweichenden
Bytes dort liegen:

```bash
cmp -l /tmp/fbh-ref/ref.bin apps/manifold/build/floor-heating-ctrl.bin \
  | awk '{printf "0x%x\n", $1-1}' | sort -u | head -40
```

Alle Adressen müssen im Bereich 0x20 bis 0x100 liegen. Eine einzige Adresse außerhalb ist ein
echter Befund und über die `.map`-Dateien zu klären, bevor etwas auf das Gerät geht.

**6 — Prüfstand ohne Hardware.**

```bash
make -C test/host                       # 215 Pruefungen, 0 Fehler
python3 tools/mock_device.py &
python3 tools/check_api.py 127.0.0.1:8321
```

### OTA auf das laufende Gerät

Erst wenn 1 bis 6 sauber sind:

```bash
curl -X POST --data-binary @apps/manifold/build/floor-heating-ctrl.bin \
     http://192.168.1.250/api/ota
```

Danach vier Belege:

1. Protokollzeile `Neue Firmware bestaetigt, kein Ruecksprung` aus `main.c`. Bleibt sie aus, rollt
   der Bootlader beim nächsten Neustart zurück — das ist die eingebaute Absicherung
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) und der Grund, warum dieser Umbau die Anlage nicht
   dauerhaft stilllegen kann.
2. `curl -s http://192.168.1.250/api/state` — Felder `version`, `heap`, `device.channels` (muss 11
   sein), `revision`.
3. `python3 tools/check_api.py 192.168.1.250 --schreiben`.
4. `curl -s http://192.168.1.250/api/config | diff /tmp/fbh-ref/geraetekonfig.json -`.

## Anzupassende Werkzeuge und Texte

| Datei | Änderung |
|---|---|
| `README.md` | Baubefehle mit `-C apps/manifold`, Pfad zum Abbild, Verzeichnisbaum, Abschnitt „Aufbau des Quelltextbestands" |
| `tools/mock_device.py` | fester Pfad `main/www/index.html` und fester Port 8321 werden zu Parametern `--app` und `--port` |
| `tools/screenshots.sh` | Ausgabeverzeichnis je Anwendung, sobald die zweite Oberfläche existiert |
| `test/host/Makefile` | unverändert — `hw_map`, `valve`, `roomctrl` bleiben in `components/` |
| `tools/check_api.py` | unverändert — zeigt auf `custom_components/`, das nicht verschoben wird |
| `hacs.json`, `custom_components/` | unverändert — HACS liest ausschließlich `custom_components/` |

## Prüfstrecke

Mit zwei Anwendungen auf einem gemeinsamen `components/` kann eine Änderung an `netmgr`, `peers`
oder `sensors_local` die jeweils andere Anwendung stillschweigend brechen, weil sie beim
örtlichen Bauen nicht mit übersetzt wird. `.github/workflows/build.yml`:

```yaml
name: Bau
on: [push, pull_request]
jobs:
  firmware:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        app: [manifold, heatsource]
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v6.0.2
          target: esp32
          path: apps/${{ matrix.app }}
  logik:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: make -C test/host
```

`fail-fast: false`, damit ein Fehler in der neuen Anwendung das Ergebnis der laufenden Firmware
nicht verdeckt. Die Matrix wird erweitert, sobald `apps/heatsource` existiert — nicht später.

Zusätzlich sinnvoll, sobald beide `dependencies.lock` versioniert sind: ein Abgleich der
Versionen, damit die gemeinsamen Komponenten in beiden Anwendungen gegen denselben Stand von
cJSON und mDNS übersetzt werden.
