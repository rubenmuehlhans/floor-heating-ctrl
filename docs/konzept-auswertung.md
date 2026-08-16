# Konzept: Auswertung der Anlage mit statistischen Verfahren

Die Anlage misst inzwischen an vier Geräten: Raumtemperaturen, Außentemperatur, Abgas, Kessel
vor und zurück, Speicher, Vor- und Rücklauf je Heizkreis, dazu Brennerzustand, Ventilstellungen
und Pumpenzustände. Dieses Papier beschreibt, was sich daraus ableiten lässt — Anomalien,
Wirkungsgrad, Zustand der Bauteile — und in welcher Reihenfolge das umzusetzen wäre.

## Ausgangslage

### Was heute vorliegt

| Größe | Auflösung | Vorrat |
|---|---|---|
| Alle Rollen der Heizungsgeräte | 1 min | 24 h, im Arbeitsspeicher |
| Alle Rollen bei scharfer Aufzeichnung | 5 s | 2 h 13 min, im Arbeitsspeicher |
| Laufzeit und Starts des Brenners | Tageswerte | heute und gestern, im NVS |
| Alles Übrige | Momentwert | keiner |

### Was daran im Weg steht

**Nach einem Stromausfall bleiben je Heizungsgerät achtzehn Byte.** Laufzeit und Starts von
heute und gestern — sonst nichts. Der Vierundzwanzigstundenverlauf und die Ladungsaufzeichnung
liegen im Arbeitsspeicher und sind weg. Es gibt kein Dateisystem, keinen PSRAM, und die
NVS-Partition fasst 61 440 Byte, von denen die Konfiguration schon einen Teil belegt.

**Eine Ladung hinterlässt keine Spur**, solange niemand vorher die Aufzeichnung scharf schaltet.
Die eine Aufzeichnung, die das Gerät hält, wird von der nächsten überschrieben.

**Der Ölverbrauch ist keine Messung.** `litres_today` ist Düsendurchsatz mal Laufzeit — ein
Nennwert aus der Konfiguration, kein Zählerstand. Eine Wirkungsgradaussage, die darauf aufbaut,
misst die eigene Annahme.

**Es fehlt der Volumenstrom.** Die Spreizung je Heizkreis wird gemessen, aber ohne Massenstrom
lässt sich daraus keine Leistung bilden. Dasselbe am Kessel.

**Der Rechner ist klein.** ESP32-WROOM-32, zwei Kerne, Fließkomma nur einfacher Genauigkeit in
Hardware, keine Vektoreinheit. Auf der Verteilerplatine sind vom schnellen Befehlsspeicher noch
8 549 Byte frei, das Heizungsgerät hat mehr Luft. Ein neuronales Netz hat hier nichts zu suchen —
und bräuchte auch nichts zu suchen.

### Welche Verfahren zur Datenmenge passen

Ein Winter bringt rund zweihundert Ladungen und etwa hundertachtzig Heiztage. Das ist zu wenig
für Verfahren, die aus Beispielen lernen, und genau richtig für Verfahren, die eine bekannte
physikalische Beziehung anpassen und Abweichungen davon melden. Konkret: gleitende Regression,
robuste Streuungsmaße, Zerfallskonstanten. Das rechnet ein ESP32 nebenbei, es ist erklärbar, und
es liefert im ersten Winter Ergebnisse statt im dritten.

Wo mehr nötig ist — mehrdimensionale Ausreißersuche über viele Merkmale — gehört es nicht auf das
Gerät, sondern auf den Rechner, der die ausgelesenen Daten auswertet.

## Stufe 0: Datengrundlage

Ohne diesen Schritt ist alles Weitere gegenstandslos. Er lohnt sich auch für sich allein.

### Ladungsprotokoll

Je abgeschlossener Ladung ein Datensatz von rund 40 Byte im NVS, die letzten 64 im Ring — das
sind 2,5 kB und deckt einen Winter ab.

| Feld | Herkunft |
|---|---|
| Beginn (Unixzeit), Dauer | vorhandene Ladeerkennung `charge_tick` |
| Brennerlaufzeit, Startzahl in dieser Ladung | Brennererkennung |
| Speicher vorher und nachher | `puffer` |
| Kesselvorlauf höchstens, Abgas höchstens | `kessel_vl`, `abgas` |
| Außentemperatur im Mittel | `aussen` |
| Ölschätzung | Laufzeit × Düsendurchsatz |

Geschrieben wird einmal je Ladung, also wenige Male am Tag — das verträgt der NVS. Der Nutzen ist
unmittelbar: Zum ersten Mal existiert eine Reihe statt eines Momentwerts.

### Tagesprotokoll

Je Tag rund 20 Byte, 365 Tage im Ring, also gut 7 kB: Laufzeit, Starts, Ölschätzung,
Heizgradtage (Summe der Stundenwerte von 20 °C minus Außentemperatur, nur positive Anteile),
kälteste und wärmste Außentemperatur, Pumpenlaufzeit je Kreis.

Heizgradtage sind die Bezugsgröße für alles Folgende: Ohne sie ist Verbrauch nicht vergleichbar,
weil ein kalter Januar mehr braucht als ein milder.

### Ausgabe

Beide Protokolle über je einen Endpunkt als CSV, wie die Ladungsaufzeichnung heute schon. Dazu
die fehlenden Größen in den MQTT-Strom aufnehmen — Abgaswert, Bezugslinie, Spreizung je Kreis,
Außenwerte — und die Zustandsklassen richtigstellen: Laufzeit, Starts und Ölmenge gehören als
`total_increasing` gemeldet, sonst führt Home Assistant keine Langzeitstatistik. Die eigene
Integration macht das bereits richtig, die MQTT-Anmeldung nicht.

**Aufwand:** überschaubar, alles vorhandene Bausteine. **Nutzen:** Grundlage für Stufe 1 und 2,
und für sich schon eine Verbrauchshistorie, die es heute nicht gibt.

## Stufe 1: Auswertung auf dem Gerät

Alles hier ist gleitende Statistik in wenigen Fließkommazahlen, gerechnet im vorhandenen
Sekundentakt. Es gehört als reines Rechenmodul nach `components/` — prüfbar auf dem Rechner wie
`heatlogic` und `schedule`.

### Verbrauchslinie und Abweichung

Aus dem Tagesprotokoll wird laufend eine Gerade angepasst: Brennerlaufzeit über Heizgradtagen.
Rekursive Regression, zwei Koeffizienten, dazu die Streuung der Abweichungen — rund vierzig Byte
Zustand.

Die Steigung ist der Wärmebedarf des Hauses je Gradtag, der Achsenabschnitt der Grundverbrauch
für Warmwasser. Beides sind Kennzahlen, die man sonst schätzt.

Ein Tag, dessen Verbrauch mehr als drei Streuungen über der Linie liegt, wird gemeldet. Das
findet: ein Fenster, das offen steht; ein Ventil, das klemmt; eine Pumpe, die durchläuft; einen
Kessel, der schlechter wird. Es findet es **im Vergleich mit dem eigenen Haus**, nicht gegen
einen Katalogwert.

### Abgastemperatur als Wirkungsgradmaß

Der Abstand zwischen höchster Abgastemperatur und höchstem Kesselvorlauf einer Ladung ist ein
Maß dafür, wie viel Wärme durch den Schornstein geht statt ins Wasser. Bei sauberem Kessel ist er
klein und stabil; Rußablagerungen heben ihn über Wochen an.

Aus dem Ladungsprotokoll ergibt sich dieser Abstand je Ladung. Ein gleitender Median über die
letzten fünfzig Ladungen, verglichen mit dem Median der ersten fünfzig nach der letzten Wartung,
zeigt die Verschlechterung als Zahl. **Ein Prozentpunkt Wirkungsgrad entspricht grob zwanzig
Kelvin Abgastemperatur** — die Größenordnung ist also relevant, und der Zeitpunkt der nächsten
Reinigung wird eine Messung statt eines Kalendereintrags.

### Stillstandsverlust des Speichers

In Zeiten ohne Brennerlauf und ohne laufende Pumpe kühlt der Speicher ab. Aus dem Verlauf lässt
sich die Zeitkonstante bestimmen: Der Temperaturunterschied zur Umgebung fällt exponentiell, die
Konstante ist die Dämmgüte.

Sie ändert sich normalerweise nicht. Tut sie es doch — der Speicher kühlt schneller aus als im
Vorjahr —, deutet das auf durchströmte Leitungen bei stehender Pumpe: ein Rückschlagventil, das
nicht schließt, oder Schwerkraftzirkulation. Das ist ein Fehler, der Öl kostet und den sonst
niemand bemerkt.

### Plausibilität der Fühler

Mehrere Prüfungen, die sich aus der Anlage selbst ergeben:

- Bei laufender Pumpe und warmem Speicher **muss** der Vorlauf eines Kreises über dem Rücklauf
  liegen. Dauerhaft umgekehrt heißt: Fühler vertauscht.
- Der Speicher kann nicht wärmer sein als der Kesselvorlauf während einer Ladung.
- Zwei Fühler, deren Werte über Tage identisch sind, sitzen vermutlich am selben Rohr.
- Ein Fühler, dessen Fehlerzähler steigt, meldet einen Wackelkontakt.

Die erste Prüfung hätte den vertauschten Vorlauf an Heizkreis 1 gefunden, ohne dass jemand
Zahlen vergleichen musste.

### Zustand der Stellantriebe

Auf der Verteilerplatine: Die Messfahrt liefert Fahrzeit und Auslöseschwelle je Kreis. Die
wöchentliche Schutzfahrt fährt ohnehin jeden Kreis auf Anschlag — sie kann dabei die Fahrzeit
messen, ohne dass eine eigene Messfahrt nötig wäre.

Wächst die Fahrzeit eines Antriebs über Monate, sitzt das Ventil schwerer. Das kündigt einen
Ausfall an, bevor der Raum kalt bleibt.

## Stufe 2: Auswertung außerhalb des Geräts

Was mehrere Merkmale gleichzeitig betrachtet, gehört auf einen Rechner. Grundlage sind die
ausgelesenen Protokolle aus Stufe 0.

Ein Werkzeug unter `tools/` liest die CSV-Dateien aller Geräte ein und erzeugt einen Bericht:

- **Ausreißersuche über die Ladungen.** Merkmale je Ladung: Dauer, Startzahl, Temperaturhub des
  Speichers, Abgas-Vorlauf-Abstand, Außentemperatur. Ein robustes Verfahren — etwa
  Isolationswald — findet Ladungen, die aus der Reihe fallen, ohne dass man vorher weiß, wonach
  man sucht.
- **Gebäudekennwerte.** Aus Außentemperatur, Raumtemperaturen und Ventilstellungen lassen sich
  Zeitkonstante und Wärmeverlustbeiwert des Hauses schätzen. Das ist ein Energieausweis aus dem
  laufenden Betrieb.
- **Prüfung der Heizkurve.** Aus Außentemperatur und der Vorlauftemperatur, mit der die Räume
  ihren Sollwert halten, ergibt sich die tatsächlich nötige Kurve. Steht der Mischer höher, wird
  Öl verbrannt, das durch die Rücklaufanhebung wieder verschwindet. Der Mischer ist der Steuerung
  nicht bekannt, aber die nötige Vorlauftemperatur ist ableitbar — und damit die Aussage, ob er
  zu hoch eingestellt ist.

## Stufe 3: Was zusätzliche Messstellen brächte

Nicht Software, sondern Hardware — aber die Reihenfolge des Nutzens ist klar:

| Ergänzung | Was sie ermöglicht |
|---|---|
| **Tankfüllstand zweimal jährlich von Hand** | Aus zwei Ablesungen und der summierten Brennerlaufzeit ergibt sich der **tatsächliche** Düsendurchsatz. Die Ölschätzung wird damit zur Messung, rückwirkend für alle Ladungen. Kosten: null. |
| **Volumenstromgeber je Heizkreis** | Erst damit wird aus Spreizung eine Leistung in Kilowatt, und aus Leistung über Zeit eine Wärmemenge. Das ist die Voraussetzung für jede echte Wirkungsgradaussage. |
| **Zweiter Pufferfühler unten** | Die Rolle gibt es schon. Mit zwei Höhen wird aus der Füllstandsschätzung eine Schichtungsmessung. |
| **Stromzähler am Brenner** | Trennt Brennerlauf von Vorspülung und macht die Erkennung unabhängig vom Abgasfühler. |

Die erste Zeile ist die wichtigste und die billigste: Zwei Zahlen im Jahr verwandeln die einzige
erfundene Größe der Anlage in eine gemessene.

## Reihenfolge

| Stufe | Inhalt | Voraussetzung |
|---|---|---|
| 0 | Ladungs- und Tagesprotokoll im NVS, vollständiger MQTT-Strom, richtige Zustandsklassen | keine |
| 1a | Plausibilitätsprüfungen der Fühler | Stufe 0 nicht nötig |
| 1b | Verbrauchslinie und Tagesabweichung | Tagesprotokoll |
| 1c | Abgas-Vorlauf-Abstand je Ladung | Ladungsprotokoll |
| 1d | Stillstandsverlust, Fahrzeit der Antriebe | Stufe 0 |
| 2 | Auswertung auf dem Rechner | ein Winter Daten |
| 3 | zusätzliche Messstellen | Entscheidung über Hardware |

Stufe 1a lohnt sich sofort und unabhängig: Sie braucht keine Historie, nur die laufenden Werte.

## Was bewusst nicht vorgesehen ist

**Kein neuronales Netz auf dem Gerät.** Zweihundert Ladungen im Winter sind zu wenig zum Lernen
und zu viel für die Behauptung, es sei nötig. Die physikalischen Beziehungen sind bekannt; sie
anzupassen ist genauer als sie zu erraten.

**Keine selbsttätige Regelung aufgrund der Auswertung.** Die Verfahren melden, sie greifen nicht
ein. Eine Regelung, die aus einer Statistik heraus die Pumpe abschaltet, ist im Fehlerfall nicht
mehr nachvollziehbar — und der Fehlerfall ist eine kalte Wohnung.

**Keine Verbrauchsprognose.** Sie klingt nützlich und ist es selten: Was sie vorhersagt, hängt am
Wetterbericht, nicht an der Anlage.
