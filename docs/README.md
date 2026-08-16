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

`screenshots.sh` legt die Aufnahmen der Verteilerplatine in `screenshots/` ab,
`screenshots_heat.sh` die des Heizungsgeräts in `screenshots/heizung/`.
Vorausgesetzt werden Google Chrome und ImageMagick.
