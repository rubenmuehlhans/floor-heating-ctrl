# Dokumentation

| Dokument | Inhalt |
|---|---|
| [Benutzerhandbuch](handbuch.md) | Bedienung, Einrichtung, Wartung und Fehlersuche beider Gerätearten |
| [Konzept: Wärmeerzeugung, Pufferspeicher und Pumpensteuerung](konzept-waermeerzeuger.md) | Messstellen, Brennererkennung, Ladezustand, Bedarfserkennung, Pumpenlogik, Schnittstellen |
| [Umbau auf zwei Anwendungen](umbau-projektstruktur.md) | Aufteilung des Projekts, gemeinsame Komponenten, Nachweis der Unverändertheit |

Die Übersicht über die Hardware, den Quelltextbestand und die Prüfungen steht in der
[README](../README.md) im Wurzelverzeichnis.

## Bildschirmaufnahmen

Sie entstehen gegen die Geräteattrappen, damit sie ohne angeschlossene Hardware
reproduzierbar sind:

```bash
python3 tools/mock_device.py &      && tools/screenshots.sh
python3 tools/mock_heatsource.py &  && tools/screenshots_heat.sh
```

Die Konzepte beschreiben, warum etwas so gebaut ist: [Wärmeerzeugung](konzept-waermeerzeuger.md), [Auswertung](konzept-auswertung.md) und der [Umbau auf zwei Anwendungen](umbau-projektstruktur.md).

`screenshots.sh` legt die Aufnahmen der Verteilerplatine in `screenshots/` ab,
`screenshots_heat.sh` die des Heizungsgeräts in `screenshots/heizung/`.
Vorausgesetzt werden Google Chrome und ImageMagick.

Beide Oberflächen bestehen aus mehreren Dateien — einem gemeinsamen Stilblatt unter
`www/stil.css` und den anwendungseigenen Teilen. Was in welcher Reihenfolge
zusammengesetzt wird, steht in `apps/<name>/main/www/sources.txt`; dieselbe Liste lesen
die Bauregel und die Werkzeuge. Zum Ansehen des Ergebnisses:

```bash
python3 tools/www.py manifold > /tmp/index.html
```
