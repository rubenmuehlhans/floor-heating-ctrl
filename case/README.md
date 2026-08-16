# Gehäuse

Zweiteiliges Druckgehäuse für die Steuerplatine: Unterschale und Deckel, dazu
Displayaufnahme, Wandlaschen und ein optionaler Hutschienen-Clip. Den Deckel
gibt es in zwei Varianten, mit und ohne Displayausschnitt; beide passen auf
dieselbe Unterschale. Konstruiert
mit CadQuery; Quelle der Wahrheit ist [`fbh_case.py`](fbh_case.py). STEP, STL
und 3MF sind erzeugt und werden nie von Hand geändert.

Der Aufbau folgt der Vorgehensweise aus einem anderen Projekt
(`smartCamper/hardware/case`); die Werkzeuge in [`tools/`](tools/) sind von
dort übernommen. Abweichungen sind in `tools/make_3mf.py` im Kopf vermerkt.

| Maß | Wert |
|---|---|
| Platine | 150,0 × 92,0 mm, Eckradius 3,0 mm |
| Gehäusekörper | 155,0 × 97,0 mm |
| Außenmaß mit Ohren und Laschen | 184,0 × 102,9 × 31,2 mm |
| Wand / Boden / Deckel | 2,0 / 2,0 / 3,0 mm |
| Lichte Höhe über der Platine | 21,0 mm |
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
DISPLAY_MODUL=0 .venv/bin/python tools/make_3mf.py fussbodenheizung-gehaeuse-ohne-display.3mf
```

```bash
PLATTE=zubehoer .venv/bin/python tools/make_3mf.py fussbodenheizung-zubehoer.3mf
```

```bash
PLATTE=proben .venv/bin/python tools/make_3mf.py fussbodenheizung-pruefstuecke.3mf
```

| Datei | Inhalt | Belegt |
|---|---|---|
| `fussbodenheizung-gehaeuse.3mf` | Deckel (3 Teile: Korpus, Logo-Ring, Logo-Akzent) und Unterschale | 184 × 214 mm |
| `fussbodenheizung-gehaeuse-ohne-display.3mf` | dasselbe mit geschlossenem Deckel | 184 × 214 mm |
| `fussbodenheizung-schale-schnapper.3mf` | nur die Unterschale, Schnapp-Dome | 184 × 103 mm |
| `fussbodenheizung-schale-randclips.3mf` | nur die Unterschale, Federzungen | 184 × 103 mm |
| `fussbodenheizung-zubehoer.3mf` | Displayrahmen, 2 × Hutschienen-Clip | 154 × 50 mm |
| `fussbodenheizung-pruefstuecke.3mf` | Prüfstück Südwand, Prüfstück Display | 232 × 58 mm |

Der Deckel ist **ein** Objekt mit drei Teilen; in Bambu Studio lässt sich je
Teil ein Filament wählen. Er steht mit der Oberseite auf der Platte — die
Logotaschen sind damit die ersten Lagen. **Sie müssen gefüllt werden**: bliebe
eine leer, müsste die Lage darüber über Luft brücken.

## ⚠️ Zuerst die Prüfstücke drucken

In den Fertigungsdaten der Platine steht **keine einzige Bauhöhe** — die DXF
zeigt nur den Bestückungsdruck. Am 16.08.2026 sind die vier wichtigsten Höhen
am Board nachgemessen worden und im Skript mit `⭐ GEMESSEN` gekennzeichnet;
`[SCHAETZUNG]` steht noch an der Klemme `220`, den 1×2-Buchsenleisten und
sämtlichen Displaymaßen. Die beiden Prüfstücke kosten zusammen rund eine
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
| `RJ_H` | 14,5 ⭐ | Bauhöhe der RJ11-Buchse |
| `RJ_DEPTH` | 14,0 ⭐ | Bautiefe der RJ11-Buchse |
| `RJ_OPEN_W` | 9,8 | Breite der Stecköffnung |
| `RJ_OPEN_Z0` / `_Z1` | 0,2 / 13,2 ⭐ | Unter- und Oberkante der Öffnung über der Platinenoberkante |
| `DSP_W` / `DSP_H` | 42,0 / 37,5 | Umriss der Displayplatine |
| `DSP_GLASS_W` / `_H` / `_D` | 30,5 / 33,5 / 2,0 | Glasfläche und ihr Überstand |
| `ESP_SOCKET` / `ESP_ABOVE` | 11,16 / 4,7 ⭐ | Buchsenleiste und Aufbau des DevKit (gemessen ist die Summe) |
| `HLK_H` | 18,4 ⭐ | Bauhöhe des Netzteils — bestimmt `TOP_CLEAR` |
| `KF250_H`, `HDR_H` | 10,5 / 8,5 | Klemme, 1×2-Buchsenleisten |
| `TOP_CLEAR` | 21,0 | Lichte Höhe über der Platine |
| `WALL` / `BOTTOM` | 2,0 / 2,0 | Wand- und Bodenstärke |
| `SNAP_D` / `SNAP_SLOT` / `SNAP_BARB_D` | 2,5 / 0,8 / 3,3 | Schnapp-Dom: Schaft, Schlitz, Widerhaken |
| `CLIP_T` / `CLIP_OVER` | 1,0 / 0,6 | Federzunge: Dicke und Übergriff |

⭐ = am Board gemessen. `BOT_CLEAR` liegt fest auf 3,6 mm: die längsten
Lötstellen stehen 3,0 mm unter der Platine vor, darunter bleiben 0,6 mm Luft.

## Aufbau

**Koordinaten.** Ursprung ist die linke untere Platinenecke in der Draufsicht,
`Z = 0` der Gehäuse-Außenboden. Alle XY-Maße kommen aus den Fertigungsdaten in
[`../board/`](../board/) und lassen sich nachvollziehen:

```bash
.venv/bin/python reference/platine_auslesen.py
```

**Südwand.** Elf Stecköffnungen von 9,8 × 13,0 mm auf den Buchsenmitten, dazwischen
3,53 mm Steg. Die Buchsenfront liegt 1,1 mm innerhalb der Platinenkante, der
Stecker überwindet also 3,6 mm Tunnel.

⚠️ Die 13,0 mm lichte Höhe folgen der gemessenen Frontöffnung der Buchse und
sind deutlich mehr, als ein 4P4C-Stecker mit Rastnase braucht (~9,5 mm). Sehr
wahrscheinlich ist die äußere Frontmulde gemessen worden und nicht der
eigentliche Steckerschlitz. Das ist die sichere Richtung — die Buchse begrenzt
selbst, was durchpasst —, kostet aber Material zwischen den elf Löchern. Wird
der Schlitz nachgemessen, gehört `RJ_OPEN_Z1` nach unten.

Die Buchse ist 14,0 mm tief statt der 13,0 aus dem Bestückungsdruck. Wohin der
Millimeter geht, sagt die Tiefe allein nicht — fest liegen nur die Rastzapfen
bei Y = 7,095. Angenommen ist deshalb, dass die Front bei Y = 1,10 bleibt und
der Millimeter nach hinten wächst. Das ist die sichere Richtung: läge die Front
weiter vorn, würde der Steckertunnel nur kürzer. Umgekehrt könnte die Wand
gegen die Buchse drücken.

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

**Platine.** Drei Varianten der Befestigung, über `PCB_HALT` wählbar. Alle
drei haben dieselben Außenmaße und **denselben Deckel** — wer die Wahl später
revidiert, druckt nur die Unterschale neu.

| `PCB_HALT` | Prinzip | Randdehnung | Demontage |
|---|---|---|---|
| `schrauben` (Vorgabe) | 4 × M2,5 selbstschneidend in Dome Ø 6,0 | — | beliebig oft |
| `schnapper` | Schnapp-Dome durch die vorhandenen Ø 2,7-Bohrungen | 1,59 % | Schenkel mit der Pinzette zusammendrücken |
| `randclips` | 4 Federzungen an der Platinenkante | 2,34 % | Zunge mit dem Finger wegdrücken |

```bash
PCB_HALT=schnapper PLATTE=schale .venv/bin/python tools/make_3mf.py fussbodenheizung-schale-schnapper.3mf
```

⚠️⚠️ **Die Federwege sind gerechnet, nicht geraten.** Randdehnung einer
eingespannten Biegefeder: `ε = 3·t·δ / (2·L²)`. PETG verträgt dauerhaft etwa
2 … 3 %; darüber bleibt die Feder verformt oder reißt. Diese Rechnung setzt der
Zungendicke die Grenze — mit 1,4 mm und 0,9 mm Übergriff wären es **4,1 %**,
die Zunge bliebe nach dem ersten Einclipsen offen stehen. Mehr Übergriff
braucht eine **längere** Feder, also `BOT_CLEAR` 6,0 statt 3,6 und damit ein
2,4 mm höheres Gehäuse.

⚠️ Beim Schnapp-Dom entscheidet der **Schlitz** über die Federlänge: er reicht
bis 0,8 mm über den Boden hinunter, tief in den Dom hinein. Endete er an der
Platinenunterkante, wäre die Feder nur 2,1 mm lang und die Randdehnung spränge
auf über 8 %.

⚠️ Die Ausweichtaschen der Randclips liegen **in der Wand**. Ohne die örtliche
Verdickung nach außen hätten sie die Wand durchbrochen — bei einem Netzgerät
ein Loch nach außen. `sonden.py` prüft an allen vier Zungen, dass dahinter
Material steht.

Die Dome selbst sind in allen Varianten gleich: Ø 6,0, 3,6 mm hoch, also
unkritisch schlank. Bei `schrauben` sitzt darin das Ø 2,05-Kernloch (1,98 mm
Domwand).

**Stützung unter der Buchsenreihe.** Die vier Befestigungsbohrungen der Platine
liegen alle in der oberen Hälfte (Y 46,9 … 87,5) — unter der RJ11-Reihe stützte
nichts, und dort wird elfmal ein Stecker hineingedrückt. Dazu kommen in allen
drei Varianten:

- **11 Pads Ø 6,0 auf Y = 3,0**, je eines auf einer Buchsenmitte — genau dort,
  wo der Stecker drückt
- eine **durchgehende Querrippe bei Y 15,0 … 18,0**

Die freie Platinenlänge zwischen Südkante und erster Abstützung sinkt damit von
46,9 auf **28,9 mm**.

⭐ **Wo gestützt werden darf, ist gerechnet, nicht ausgesucht.** Die Unterseite
ist an der Südkante dicht belegt: bei Y ≈ 4 … 7 die Buchsenpins und Rastzapfen,
bei Y ≈ 0,6 … 5,6 die Buchsenleisten. Eine Suche über alle 286 Bohrungen aus
den PTH- und NPTH-Daten liefert zwei freie Bänder — die elf Flecken auf den
Buchsenmitten (5,14 mm bis zur nächsten Lötstelle) und das Band bei Y 15 … 18
(2,22 mm). Dass die freien Flecken mittig unter den Buchsen liegen, ist kein
Zufall: die Pins stehen links und rechts davon.

Die X-Werte der Pads kommen deshalb aus `RJ_X` und nicht aus einer zweiten
Liste. `sonden.py` liest die Bohrdaten bei jedem Lauf neu ein und prüft den
Abstand — ein Pad auf einer Lötstelle hebt die Platine an, und keine der
anderen Prüfungen sieht das: der Platinen-Dummy hat keine Lötstellen, also gibt
es auch keine Kollision.

**Wandstärke.** 2,0 mm für Wand und Boden, 3,0 mm für den Deckel. Die 2,0
sind nicht nur Material: der Steckertunnel vor den RJ11-Buchsen ist
`WALL + CLR + 1,10` und schrumpft damit von 4,0 auf 3,6 mm — je kürzer er ist,
desto sicherer liegt die Rastnase des Steckers außerhalb und lässt sich
drücken.

Dünner geht, aber nicht beliebig. Drei Grenzen:

- Das **Außenohr** überlappt die Wand um `WALL − 0,4`. Unter etwa 0,9 mm wird
  daraus eine Berührung, und tangentiale Berührung heißt kaputtes Netz — genau
  das hatten die ursprünglich runden Ohren. Ein Riegel im Skript bricht ab,
  bevor es so weit kommt.
- Die **Südwand** ist zwischen den elf Öffnungen nur 3,53 mm breit. Sie trägt
  als Leiter mit zwei durchgehenden Holmen, aber dünn wird sie weich.
- Die **Kabelverschraubung M12** will 1 … 4 mm Wand; unter 1,5 sitzt die Mutter
  nicht mehr sauber.

Was die Wand kostet (Unterschale, gemessen):

| `WALL` | Volumen | Tunnel | Ohr-Überlappung |
|---|---|---|---|
| 2,4 | 72,3 cm³ | 4,0 mm | 2,0 mm |
| **2,0** | **61,7 cm³** | **3,6 mm** | **1,6 mm** |
| 1,8 | 56,5 cm³ | 3,4 mm | 1,4 mm |
| 1,6 | 51,3 cm³ | 3,2 mm | 1,2 mm |

```bash
WALL=1.8 BOTTOM=1.8 .venv/bin/python fbh_case.py
```

Wer auf ganze Bahnen kommen will: mit 0,4-mm-Düse und 0,42 Bahnbreite sind
1,68 (4 Bahnen) und 2,10 (5 Bahnen) die sauberen Werte. 2,0 slict Bambu Studio
als fünf leicht schmalere Bahnen — unkritisch, aber nicht exakt.

⚠️ **`LID_T` bleibt bei 3,0 und ist nicht verhandelbar.** Über der 2,0 mm
tiefen Glastasche des Displays blieben sonst weniger als 1,0 mm stehen. Das ist
die engste Stelle des Deckels, nicht die Kantenrundung.

**Wandmontage.** Vier Laschen mit Ø 4,5 an den seitlichen Ohren.
Lochabstand 174,0 × 68,0 mm.

**Hutschiene.** Zwei Clips, je einer unter die Laschen bei Y = 80 geschraubt
(M4-Senkschraube, Kopf bleibt bündig — dort liegt die Schiene auf). Sie
greifen dieselbe waagerechte TS35-Schiene; das Paar verhindert das Verdrehen,
das eine einzelne Schraube zuließe.

**Display.** Das Modul wird von einem Halterahmen gegen die Deckelunterseite
geklemmt, **nicht** über sein Bohrbild gehalten. Damit zählt nur der Umriss —
und der ist mit einem Messschieber in zehn Sekunden nachgemessen, während ein
Bohrbild geraten wäre.

**Deckel ohne Display.** `DISPLAY_MODUL=0` lässt Sichtfenster, Glastasche und
die vier Dome weg; Halterahmen und Displayprüfstück entfallen mit. Umriss,
Lippe, Schrauben, Tastenfelder, Logo und Bauhöhe bleiben gleich — die beiden
Deckel sind gegeneinander austauschbar. Die Bauhöhe sinkt dabei **nicht**:
`TOP_CLEAR` folgt dem Netzteil (18,4 mm), nicht dem Display. Der geschlossene
Deckel wiegt 45,3 statt 42,6 cm³.

⚠️ Die Umgebungsvariable heißt bewusst `DISPLAY_MODUL` und nicht `DISPLAY` —
letztere ist unter X11 belegt und hätte die Variante je nach Umgebung von
selbst umgeschaltet.

**Schraubensitze.** 90°-**Senkungen**, keine zylindrischen Senkbohrungen, also
Senkkopfschrauben M2,5 (DIN 7991 / ISO 10642). Das Kernloch im Ohr ist
**20,0 mm tief** (`EAR_PILOT_H`) und lässt damit Schrauben bis M2,5 × 20 zu;
darunter bleiben noch 8,2 mm massives Ohr stehen, die Wandlasche wird nicht
angeschnitten.

⚠️ Mehr Einschraubtiefe ist nicht automatisch mehr Halt. Eine selbstschneidende
M2,5 × 20 muss über 17 mm Gewinde **formen** — das Drehmoment summiert sich, und
irgendwann reißt eher der Kopf ab oder das Ohr spaltet, als dass die Schraube
hält. Wer die Länge nutzt, sollte langsam und ohne Schlagschrauber eindrehen
oder gleich Kunststoffschrauben mit Flachgewinde nehmen. Für häufiges Öffnen
wäre eine durchgehende Bohrung mit Mutter unter dem Ohr die haltbarere Lösung
als ein langes Kernloch.

Dass es eine Senkung sein muss und keine zylindrische Senkbohrung, ist keine
Optik: der Deckel wird mit der Oberseite aufs Bett gedruckt, eine Ø6,2-Tasche von 1,8 mm läge
also in den ersten Lagen und schlösse sich erst dort über einem Ø3,3-Loch —
sechsmal ein freitragender Ring von 1,45 mm, und genau das war die **einzige**
Stützstelle des Deckels. Der 90°-Kegel läuft mit 45° aus dem Bett heraus und
trägt sich selbst. Beide Deckelvarianten haben damit **null** nach unten
zeigende Flächen.

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

**⚠️⚠️ Zwei Displaydome standen im Displaymodul.** Ihre Unterseite liegt in der
Ebene der Modulrückseite — dort halten sie den Halterahmen —, jede Überlappung
in XY ist also ein Dom im Modul. Zwei ragten 1,0 mm hinein. `check_case.py`
prüft Deckel gegen Platine und Schale, nicht gegen das Modul: das ist kein
Druckteil und kommt in keinem der Körper vor. Gemeldet hat es erst die
Prüfung *Modulplatine gegen Deckel* in `sonden.py`, die es vorher nicht gab.
Beim Verschieben nach außen lief dann der Halterahmen ins Netzteil — die
Displaygruppe sitzt jetzt 2,25 mm weiter südlich.

**⚠️ Eine Prüfung meldete einen Konflikt, den es nicht gab.** Der erste Versuch
verglich Displaydom und Buchsenreihe rein in XY und meldete „−1,10 mm zu eng".
Die Dome reichen aber nur bis zur Modulrückseite herunter und nie auf
Buchsenhöhe. Ersetzt durch den senkrechten Abstand unter dem Halterahmen —
dem einzigen Displayteil, das wirklich tief kommt.

**⚠️ Sechs Schraubensitze und zwei Rippen brauchten Stützmaterial.** Die
zylindrischen Senkbohrungen im Deckel schwebten 1,8 mm über dem Bett, und die
beiden Zugentlastungsrippen der 1-Wire-Durchführung endeten 4,5 mm frei in der
Luft. Beides meldet keine der Prüfungen — ein Überhang ist weder eine
Kollision noch ein verschlossenes Loch. Gefunden über eine Auswertung der nach
unten zeigenden Flächen beider Teile in Druckstellung; die Senkbohrungen sind
jetzt 90°-Senkungen, die Rippen laufen mit 45° aus der Wand.

**⚠️ Der Federarm des Hutschienen-Clips hing in der Luft.** Der Schlitz, der
ihn federn lassen sollte, hat ihn abgetrennt — zwei Körper, beide gültig.
`check_case.py` sieht Zubehörteile nicht; gemeldet hat es erst Bambu Studio mit
`number_of_parts = 2`. Jetzt federt eine Zunge, die an ihrer Wurzel
durchgehend an der Platte hängt, und `sonden.py` prüft alle Zubehörteile auf
einen Körper und ein dichtes Netz.

## Offene Punkte

- Die Höhenlage der RJ11-Stecköffnung ist gemessen, aber vermutlich an der
  Frontmulde statt am Steckerschlitz — siehe **Südwand**.
- Die 18,4 mm des HLK-5M03 liegen 3,4 mm über dem Datenblattmaß (15,0). Das
  Modul sitzt also nicht bis zum Anschlag durchgesteckt. Gebaut ist nach dem
  gemessenen Wert; wird das Modul nachgesetzt, ist das Gehäuse zu hoch, nie zu
  niedrig.
- Klemme `220` und 1×2-Buchsenleisten sind weiterhin geschätzt.
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
| `fbh_case.py` | Konstruktion — Quelle der Wahrheit (`DISPLAY_MODUL=0`, `PCB_HALT=…`) |
| `sonden.py` | Sondenprüfung (Öffnungen, Freiräume, Netzbereich, Zubehör) |
| `laser_gravur.py` | Gravurvorlage aus der Gehäusegeometrie |
| `reference/platine_auslesen.py` | Platinengeometrie aus Gerber und EasyEDA-JSON |
| `reference/platine.txt` | Ergebnis des Auslesens |
| `tools/` | Übernommen aus `smartCamper/hardware/case/tools` |
