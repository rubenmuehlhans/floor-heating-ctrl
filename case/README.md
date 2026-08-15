# Gehäuse

Zweiteiliges Druckgehäuse für die Steuerplatine: Unterschale und Deckel, dazu
Displayaufnahme, Wandlaschen und ein optionaler Hutschienen-Clip. Konstruiert
mit CadQuery; Quelle der Wahrheit ist [`fbh_case.py`](fbh_case.py). STEP, STL
und 3MF sind erzeugt und werden nie von Hand geändert.

Der Aufbau folgt der Vorgehensweise aus einem anderen Projekt
(`smartCamper/hardware/case`); die Werkzeuge in [`tools/`](tools/) sind von
dort übernommen. Abweichungen sind in `tools/make_3mf.py` im Kopf vermerkt.

| Maß | Wert |
|---|---|
| Platine | 150,0 × 92,0 mm, Eckradius 3,0 mm |
| Gehäusekörper | 155,8 × 97,8 mm |
| Außenmaß mit Ohren und Laschen | 184,8 × 103,3 × 28,0 mm |
| Wand / Boden / Deckel | 2,4 / 2,4 / 3,0 mm |
| Lichte Höhe über der Platine | 18,0 mm |
| Werkstoff | PETG (siehe **Sicherheit**) |

## ⚠️ Sicherheit

Auf der Platine liegt **Netzspannung**: ein HLK-5M03 und die Klemme `220`. Die
gefrästen Trennschlitze der Platine grenzen den Netzbereich auf X ≥ 113,8 und
Y ≥ 64,2 ein. Das Gehäuse hält diesen Bereich frei — dort gibt es keine
Lüftungsschlitze, keine Displayaufnahme und keine Bohrung außer der
Kabelverschraubung. Der Skriptlauf [`sonden.py`](sonden.py) prüft das.

Zwei Punkte bleiben in Ihrer Verantwortung:

- **PETG ist nicht flammwidrig** (UL94 HB). Für ein dauerhaft netzgespeistes
  Gerät ist ein Werkstoff der Klasse V-1 oder V-0 vorzuziehen — PETG-FR,
  PC-FR oder ABS-FR. Auf einem offenen Drucker fällt ABS aus; wer beim
  offenen Drucker bleibt, sollte das Gerät nicht unbeaufsichtigt betreiben
  oder die Baugruppe zusätzlich in einen zugelassenen Installationskasten
  setzen.
- **Die Zugentlastung der Netzzuleitung ist keine Option.** Vorgesehen ist
  eine Kabelverschraubung M12 × 1,5 in der Nordwand (Bohrung Ø 12,2 bei
  X = 137). Eine blanke Bohrung genügt nicht.

Die Klemme `220` hat 3,5 mm Rastermaß. Ob das für die vorgesehene
Anschlussart ausreicht, ist eine Frage an die Platine, nicht an das Gehäuse.

## Erzeugen

```bash
cd case && .venv/bin/python fbh_case.py
```

Die Umgebung ist eine geteilte venv (`cadquery`, `ezdxf`, `Pillow`); der
Symlink `.venv` zeigt auf die des Nachbarprojekts. Neu anlegen:

```bash
python3.12 -m venv .venv && .venv/bin/pip install cadquery ezdxf shapely Pillow
```

Prüfen — beides gehört dazu:

```bash
MODUL=fbh_case .venv/bin/python tools/check_case.py
```

```bash
.venv/bin/python sonden.py
```

`check_case.py` findet ungültige Körper, undichte Netze und Kollisionen.
`sonden.py` findet, was dabei durchfällt: **verschlossene Löcher** (eine
Kollision ist es nicht — es ist Material an der falschen Stelle), die
Freiräume von Displaymodul und Halterahmen und die Vollständigkeit der
Netzbereichs-Abschottung. Beim ersten Bau hat genau das zwei Fehler gefunden,
die alle anderen Prüfungen passiert hatten (siehe **Gefundene Fehler**).

## Druckdateien

Drei Platten, drei Dateien — sie passen nicht gemeinsam auf 256 × 256 mm:

```bash
.venv/bin/python tools/make_3mf.py fussbodenheizung-gehaeuse.3mf
```

```bash
PLATTE=zubehoer .venv/bin/python tools/make_3mf.py fussbodenheizung-zubehoer.3mf
```

```bash
PLATTE=proben .venv/bin/python tools/make_3mf.py fussbodenheizung-pruefstuecke.3mf
```

| Datei | Inhalt | Belegt |
|---|---|---|
| `fussbodenheizung-gehaeuse.3mf` | Deckel (3 Teile: Korpus, Logo-Ring, Logo-Akzent) und Unterschale | 185 × 215 mm |
| `fussbodenheizung-zubehoer.3mf` | Displayrahmen, 2 × Hutschienen-Clip | 154 × 50 mm |
| `fussbodenheizung-pruefstuecke.3mf` | Prüfstück Südwand, Prüfstück Display | 232 × 58 mm |

Der Deckel ist **ein** Objekt mit drei Teilen; in Bambu Studio lässt sich je
Teil ein Filament wählen. Er steht mit der Oberseite auf der Platte — die
Logotaschen sind damit die ersten Lagen. **Sie müssen gefüllt werden**: bliebe
eine leer, müsste die Lage darüber über Luft brücken.

## ⚠️ Zuerst die Prüfstücke drucken

In den Fertigungsdaten der Platine steht **keine einzige Bauhöhe** — die DXF
zeigt nur den Bestückungsdruck. Jede Z-Höhe im Skript ist geschätzt und dort
mit `[SCHAETZUNG]` markiert. Die beiden Prüfstücke kosten zusammen rund eine
Stunde Druckzeit und stehen gegen zwei verworfene Großteile:

- **Prüfstück Südwand** — ein 10 mm tiefer Schnitt durch die fertige Schale mit
  allen elf RJ11-Öffnungen. Prüft, ob die Stecker einrasten und ob die Rastnase
  erreichbar bleibt.
- **Prüfstück Display** — der Displayausschnitt aus dem fertigen Deckel mit
  Sichtfenster, Glastasche und den vier Domen. Prüft Fenstergröße und
  Einbautiefe des Moduls.

Beide sind **aus den fertigen Körpern geschnitten**, nicht nachmodelliert — sie
können deshalb nicht vom Gehäuse abweichen.

Passt etwas nicht, ändert sich kein Quelltext. Die kritischen Maße hängen an
Umgebungsvariablen:

```bash
RJ_OPEN_Z0=2.4 RJ_OPEN_Z1=11.8 DSP_GLASS_D=2.4 .venv/bin/python fbh_case.py
```

| Variable | Vorgabe | Was sie steuert |
|---|---|---|
| `RJ_H` | 13,0 | Bauhöhe der RJ11-Buchse |
| `RJ_OPEN_W` | 9,8 | Breite der Stecköffnung |
| `RJ_OPEN_Z0` / `_Z1` | 1,6 / 11,0 | Unter- und Oberkante der Öffnung über der Platinenoberkante |
| `DSP_W` / `DSP_H` | 42,0 / 37,5 | Umriss der Displayplatine |
| `DSP_GLASS_W` / `_H` / `_D` | 30,5 / 33,5 / 2,0 | Glasfläche und ihr Überstand |
| `ESP_SOCKET` / `ESP_ABOVE` | 8,5 / 4,7 | Buchsenleiste und Aufbau des DevKit |
| `HLK_H`, `KF250_H`, `HDR_H` | 15,1 / 10,5 / 8,5 | Bauhöhen Netzteil, Klemme, Buchsenleisten |
| `TOP_CLEAR` | 18,0 | Lichte Höhe über der Platine |

## Aufbau

**Koordinaten.** Ursprung ist die linke untere Platinenecke in der Draufsicht,
`Z = 0` der Gehäuse-Außenboden. Alle XY-Maße kommen aus den Fertigungsdaten in
[`../board/`](../board/) und lassen sich nachvollziehen:

```bash
.venv/bin/python reference/platine_auslesen.py
```

**Südwand.** Elf Stecköffnungen auf den Buchsenmitten. Die Buchsenfront liegt
1,1 mm innerhalb der Platinenkante, der Stecker überwindet also 4,0 mm Tunnel.

**Nordwand.** Kabelverschraubung M12 × 1,5 für die Netzzuleitung (X = 137, im
Netzbereich) und Ø 7 für den Vorlauffühler (X = 76) mit zwei Rippen als
Klemmschlitz. Dazu Lüftungsschlitze über dem DevKit.

**Westwand.** Lüftungsschlitze auf Höhe des HDC1080 — er misst das
Schaltschrankklima, ohne Luftaustausch misst er das Gehäuseinnere.

**Deckelbefestigung.** Sechs M3 in **angeformten Außenohren**, nicht in
Innensäulen. Die Platine füllt die Kavität vollständig aus; jede Innensäule
läge in einer RJ11-Buchse, im DevKit oder im Netzteil. Nebeneffekt: die
Zentrierlippe läuft ringsum ununterbrochen. Die Südkante bekommt keine
Schraube — dort steht über die volle Breite die Steckerreihe; sie wird von der
Lippe und den beiden Ohren bei Y = 12 gehalten.

**Platine.** Vier M2,5 selbstschneidend von oben in Dome (Ø 6,0, Kernloch
Ø 2,05 — 1,98 mm Domwand). Die Dome sind 3 mm hoch, also unkritisch schlank.

**Wandmontage.** Vier Laschen mit Ø 4,5 an den seitlichen Ohren.
Lochabstand 174,8 × 68,0 mm.

**Hutschiene.** Zwei Clips, je einer unter die Laschen bei Y = 80 geschraubt
(M4-Senkschraube, Kopf bleibt bündig — dort liegt die Schiene auf). Sie
greifen dieselbe waagerechte TS35-Schiene; das Paar verhindert das Verdrehen,
das eine einzelne Schraube zuließe.

**Display.** Das Modul wird von einem Halterahmen gegen die Deckelunterseite
geklemmt, **nicht** über sein Bohrbild gehalten. Damit zählt nur der Umriss —
und der ist mit einem Messschieber in zehn Sekunden nachgemessen, während ein
Bohrbild geraten wäre.

**Tasten.** Drei Membranfelder Ø 18 mit 1,0 mm Restwand, von innen ausgespart.
Die Sensorfläche (Kupferscheibe Ø 17,5 oder Alu-Klebeband) wird in die Mulde
gelegt und verklebt; die Leitungen gehen an GPIO 4, 2 und 15 der
Buchsenleiste. Die Deckeloberseite bleibt glatt — beschriftet wird graviert.

## Gravur

```bash
.venv/bin/python laser_gravur.py
```

`gravur_deckel.png` ist die Datei für den LaserPecker 4 (600 dpi, schwarz auf
weiß, Bildbreite **166,8 mm** — das ist die Deckelbreite über die Ohren).
`gravur_deckel_lage.png` zeigt zusätzlich Umriss, Sichtfenster, Tastenfelder,
Logotasche und Schraubensenkungen als graue Hilfslinien; sie dient nur der
Lagekontrolle und gehört nicht in die Maschine.

⚠️ **Mit 1064 nm markiert PETG nicht** — der Faserlaser braucht Ruß als
Absorber und findet ihn nur in schwarzem Material. Für Kunststoff ist der
450-nm-Teil des LP4 der richtige.

Kanalnummern, Tastenmitten und Fensterlage werden nicht doppelt gepflegt: das
Skript importiert `fbh_case` und leitet alles aus denselben Werten ab. Ändert
sich ein Buchsenabstand, wandert die Ziffer mit.

## Gefundene Fehler

Zwei Befunde aus dem Bau, damit sie nicht wiederkehren:

**⚠️⚠️ Zwei Bohrungen wurden ins Freie geschnitten.** Die Ebene `"XZ"` hat in
CadQuery die Normale (0, −1, 0): `extrude(+L)` läuft nach **Süden**.
1-Wire-Durchführung und Netzverschraubung standen mit negativer Länge da und
lagen damit außerhalb des Gehäuses. Der Körper blieb gültig, das Netz dicht,
die Kavität kollisionsfrei — `check_case.py` meldete nichts. Gefunden hat es
erst die Sondenlinie. Und die Sonde hat den Dreher zunächst mitgemacht: sie
maß Luft außerhalb des Gehäuses und meldete „offen".

**⚠️⚠️ Runde Außenohren ergaben ein kaputtes STL.** Ein runder Zapfen, dessen
Mitte außerhalb der Wandflucht liegt, schneidet die gerade Wand in sehr
flachem Winkel. Der 1,5-mm-Kantenradius des Deckels lief dort in eine fast
entartete Ecke: 24 offene Kanten im tesselierten Netz an genau diesen zwölf
Stellen. Rechteckige Ohren treffen die Wand mit zwei sauberen 90°-Ecken; auch
deren Außenecken bleiben scharf, weil eine XY-Verrundung die Seitenflächen als
Bogen bis in die Wandflucht zöge und das Problem abgeschwächt zurückbrächte.

**⚠️ Der Federarm des Hutschienen-Clips hing in der Luft.** Der Schlitz, der
ihn federn lassen sollte, hat ihn abgetrennt — zwei Körper, beide gültig.
`check_case.py` sieht Zubehörteile nicht; gemeldet hat es erst Bambu Studio mit
`number_of_parts = 2`. Jetzt federt eine Zunge, die an ihrer Wurzel
durchgehend an der Platte hängt, und `sonden.py` prüft alle Zubehörteile auf
einen Körper und ein dichtes Netz.

## Offene Punkte

- Alle Z-Höhen sind geschätzt. Prüfstücke drucken, nachmessen, neu erzeugen.
- Die Maße des Displaymoduls sind die des Waveshare 1,5″ OLED aus der
  Katalogangabe, nicht aus einem Datenblatt. Glasfläche und Glasüberstand
  (`DSP_GLASS_*`) sind reine Annahmen.
- Der Hutschienen-Clip ist das einzige Teil, dessen Funktion sich nicht
  rechnerisch belegen lässt — ob die Zunge mit der gewählten Stärke sinnvoll
  federt, zeigt erst der Druck.
- Die Micro-USB-Buchse des DevKit liegt bei X ≈ 59, also 91 mm von jeder
  Außenkante entfernt. Sie ist bewusst nicht herausgeführt; Aktualisierungen
  laufen über OTA. Zum Flashen muss der Deckel ab.

## Dateien

| Datei | Inhalt |
|---|---|
| `fbh_case.py` | Konstruktion — Quelle der Wahrheit |
| `sonden.py` | Sondenprüfung (Öffnungen, Freiräume, Netzbereich, Zubehör) |
| `laser_gravur.py` | Gravurvorlage aus der Gehäusegeometrie |
| `reference/platine_auslesen.py` | Platinengeometrie aus Gerber und EasyEDA-JSON |
| `reference/platine.txt` | Ergebnis des Auslesens |
| `tools/` | Übernommen aus `smartCamper/hardware/case/tools` |
