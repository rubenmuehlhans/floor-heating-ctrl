# Steuerplatine

Fertigungsdaten der Steuerplatine: Gerber, Bohrdaten, Umriss als DXF, der
EasyEDA-Entwurf und die Stückliste.

| Datei | Inhalt |
|---|---|
| `Gerber_*.zip` | Fertigungsdaten für den Leiterplattenhersteller |
| `PCB_PCB_*.json` | EasyEDA-Entwurf, die bearbeitbare Fassung |
| `PCB_*.dxf` | Umriss, für die Konstruktion des Gehäuses |
| `BOM_*.csv` | Stückliste mit LCSC-Bestellnummern |

Die Platine ist 382 × 234 mm groß und trägt 96 Bauteile in 16 Positionen. Sie
ist in EasyEDA entworfen und von Hand geroutet.

## Lizenz

Copyright 2026 Ruben Mühlhans

Diese Entwurfsdaten beschreiben offene Hardware und stehen unter der
CERN Open Hardware Licence Version 2 – Strongly Reciprocal (CERN-OHL-S v2).
Der Lizenztext steht in [LICENSE](LICENSE).

Sie dürfen diese Daten weitergeben und bearbeiten und Produkte daraus
herstellen, sofern Sie die Bedingungen der CERN-OHL-S v2 einhalten
(<https://ohwr.org/cern_ohl_s_v2.txt>). Wer ein daraus gefertigtes Produkt
weitergibt, muss die zugehörigen Entwurfsdaten zugänglich machen.

Die Weitergabe erfolgt OHNE JEDE AUSDRÜCKLICHE ODER STILLSCHWEIGENDE
GEWÄHRLEISTUNG, insbesondere ohne Gewährleistung der Marktgängigkeit,
zufriedenstellenden Qualität oder Eignung für einen bestimmten Zweck. Es
gelten die Bedingungen der CERN-OHL-S v2.

Bezugsquelle (Source Location): <https://github.com/rubenmuehlhans/floor-heating-ctrl>

Warum nicht die GPL wie die Firmware: Die GPL knüpft ihre Rückgabepflicht an
das Weitergeben von Objektcode. Eine gefertigte Leiterplatte ist aber keine
Kopie der Entwurfszeichnung — die Pflicht liefe ins Leere. Die CERN-OHL knüpft
sie an das Bereitstellen des Produkts und deckt damit auch Rechte ab, die über
das Urheberrecht hinausgehen.

## Herkunft

Das Layout ist eigenständig entstanden. Der Grundgedanke der Schaltung — ein
gesteckter ESP32-Devkit, Kanalauswahl über Schieberegister, Endlagenerkennung
über die Gegenspannung — geht auf das Shield
[esp32_8ch_motor_shield](https://github.com/nliaudat/esp32_8ch_motor_shield)
von nliaudat zurück (CC BY-NC-SA 4.0). Dessen Entwurfsdaten liegen bei OSHWLab
und sind nicht in diesen Bestand eingeflossen.

Unterschiede zum Vorbild: elf statt acht Kanäle, Netzteil auf der Platine statt
Versorgung über USB-C, Anzeige und Schaltschrankfühler mit an Bord.
