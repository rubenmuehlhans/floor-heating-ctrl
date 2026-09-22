# Konzept: Wärmeerzeugung, Pufferspeicher und Pumpensteuerung

Erweiterung des Projekts um zwei Geräte im Heizungsbereich. Der Umbau der Projektstruktur, der
dafür vorausgeht, ist in [umbau-projektstruktur.md](umbau-projektstruktur.md) beschrieben.

## Ausgangslage

Die drei Verteilerplatinen regeln die Ventilstellungen der Fußbodenheizung, wissen aber nichts
über die Wärmequelle. Umgekehrt laufen die beiden Heizkreispumpen am Pufferspeicher unabhängig
davon, ob überhaupt ein Ventil offen ist.

Die Anlage: Eine Ölheizung lädt einen Pufferspeicher, aus dem zwei Heizkreise versorgt werden —
Keller mit Erdgeschoss sowie Obergeschoss. Das Trinkwasser wird im Durchlauf aus dem Pufferinhalt
erwärmt, weshalb der Speicher dauerhaft auf Temperatur bleiben muss. Vor jedem Heizkreis sitzt
ein Mischer, der den Vorlauf auf die für die Fußbodenheizung nötige Temperatur herunterregelt.

Kessel und Pufferspeicher stehen in verschiedenen Räumen. Es gibt daher zwei ESP32 mit
DS18B20-Fühlern: einen am Kessel mit drei Fühlern, einen am Speicher mit fünf. Beide laufen mit
veralteter Arduino-Software und werden vollständig ersetzt. Die Pumpen werden über ein
Sonoff-Relais mit Tasmota geschaltet.

## Ziel

1. Alle Messstellen der Anlage laufend erfassen und darstellen, einschließlich Verlauf.
2. Aus Abgas- und Kesseltemperaturen erkennen, ob der Brenner läuft und ob der Speicher fertig
   geladen ist.
3. Die beiden Heizkreispumpen abschalten, solange kein Abnehmer im System ist.

## Abgrenzung

Der Kessel wird nicht gesteuert, die Mischer ebenso wenig. Deren Regelungen bleiben unangetastet.
Die Firmware misst mit und schaltet ausschließlich die beiden Umwälzpumpen. Es wird kein Eingriff
in sicherheitsrelevante Funktionen der Feuerungsanlage vorgenommen.

## Geräte und Messstellen

Eine gemeinsame Firmware `heatsource` für beide Boards. Welche Aufgabe ein Gerät übernimmt,
ergibt sich allein aus den zugeordneten Fühlern und Heizkreisen; einen gesonderten Rollenschalter
gibt es nicht.

### Kesselboard

| Rolle | Zweck |
|---|---|
| `abgas` | Brennerlauf erkennen; DS18B20 außen am Abgasrohr |
| `kessel_vl` | Kesselvorlauf |
| `kessel_rl` | Kesselrücklauf; die Spreizung zeigt den Ladefortschritt |

Abgeleitet und veröffentlicht: Brennerzustand, Laufzeit und Starts des Tages,
Verbrauchsschätzung, Kesselspreizung.

### Speicherboard

| Rolle | Zweck |
|---|---|
| `puffer` | Speichertemperatur, mit Korrekturwert rund 3 K |
| `hk1_vl`, `hk1_rl` | Heizkreis Keller und Erdgeschoss, hinter dem Mischer |
| `hk2_vl`, `hk2_rl` | Heizkreis Obergeschoss, hinter dem Mischer |
| `puffer_unten` | vorgesehen, zunächst unbelegt |

Abgeleitet: Pumpensteuerung beider Kreise, Ladezustand, Warnung bei zu geringer
Warmwasserreserve. Hier hängt auch das Sonoff-Relais.

### Zuordnung der Fühler

Ein DS18B20 meldet sich mit einer 64-Bit-ROM-Adresse, nicht mit seiner Einbaulage. Die Zuordnung
Adresse zu Rolle erfolgt daher in der Weboberfläche, wie schon die Zuordnung Raum zu Thermometer
beim Verteiler. Je Fühler ist ein Korrekturwert in Kelvin hinterlegt.

Zur Identifizierung zeigt die Fühlerliste neben dem Messwert die Änderung der letzten
30 Sekunden. Wer einen Fühler mit der Hand erwärmt, erkennt ihn in der Liste.

### Was der Mischer für die Auswertung bedeutet

`hk1_vl` und `hk2_vl` geben die Stellung des jeweiligen Mischers wieder, nicht die
Speichertemperatur. Ein Rückschluss vom Heizkreisvorlauf auf den Speicherinhalt ist damit
ausgeschlossen. Brauchbar bleiben aus diesen vier Fühlern:

- die Spreizung je Kreis als Beleg, dass tatsächlich Wärme abgegeben wird,
- der Vorlaufwert als Betriebsanzeige.

Aussagen über den Speicher stützen sich ausschließlich auf `puffer` und die Kesselfühler.

## Zusammenspiel der Geräte

Es kommt kein zusätzliches Verfahren hinzu. Alle Geräte melden sich über den bereits vorhandenen
mDNS-Dienst `_fbhctrl._tcp` an und fragen einander über HTTP ab. Ein Broker ist an keiner Stelle
Voraussetzung.

```
Verteiler Keller ┐
Verteiler EG     ├── GET /api/demand ──────┐
Verteiler OG     ┘                         │
                                           ▼
Kesselboard ───── GET /api/measurements ─► Speicherboard ──► Sonoff (MQTT)
  Abgas, Kessel VL/RL                        Puffer, HK1/HK2 VL/RL
  Brennerzustand                             Pumpenlogik, Ladezustand
```

Wesentlich ist die Abstufung der Abhängigkeiten:

| Funktion | benötigt | Verhalten bei Ausfall |
|---|---|---|
| Pumpensteuerung | eigene Fühler, Bedarf der Verteiler | Verteiler nicht erreichbar → Bedarf gilt als vorhanden |
| Ladezustand | zusätzlich Kesselwerte und Brennerzustand | Kesselboard nicht erreichbar → Auswertung nur über `puffer`, wird ausgewiesen |
| Brennererkennung | nur `abgas` am Kesselboard | von den übrigen Geräten unabhängig |

Das Kesselboard kann also ausfallen, ohne dass die Pumpen davon berührt werden.

## Brennererkennung

Der Abgasfühler sitzt außen am Rohr und liefert ein gedämpftes, verzögertes Signal. Ein fester
Schwellwert wäre von der Raumtemperatur des Heizungsraums abhängig; gemessen wird deshalb gegen
eine gleitende Bezugslinie: das Minimum des Abgasfühlers über die letzten 24 Stunden.

| Übergang | Bedingung | Vorgabe |
|---|---|---|
| aus → läuft | `abgas > bezug + delta_on_k` und `abgas > tiefstwert + swing_k`, über `on_hold_s` | 12 K, 6 K, 60 s |
| läuft → aus | `abgas < bezug + delta_off_k` oder `abgas < höchstwert − swing_k`, über `off_hold_s` | 6 K, 6 K, 300 s |

Höchst- und Tiefstwert beziehen sich auf die laufende Phase und beginnen mit jedem Wechsel neu.

Die erste Fassung kannte nur die Bezugslinie. An der Anlage hielt sie den Brenner bei warmem
Kessel vier Stunden lang für an: Nach dem Abschalten kühlt das Abgasrohr nur bis auf die
Kesseltemperatur ab und bleibt damit mehr als 6 K über der Bezugslinie. Der Abfall gegen den
Höchstwert der Fahrt erkennt das Ende unabhängig davon. Der Anstieg gegen den Tiefstwert
verhindert, dass derselbe warme Zustand unmittelbar nach dem Abschalten wieder als Anlauf gilt.
Der Kesselvorlauf als Bezug wurde ebenfalls erprobt und verworfen: Bei einem Warmstart mit
laufender Pumpe lag das Abgas während des Brennerlaufs 17 bis 25 K unter dem Vorlauf.

Die Bezugslinie wird nicht aus einer vollständigen Messreihe gebildet, sondern als kleinstes
Vorkommen in einem wandernden 24-Stunden-Fenster mitgeführt. Das ist träger als ein echtes
gleitendes Minimum und spart den Speicher für einen Tag Messwerte; die Temperatur des kalten
Rohrs ändert sich ohnehin über Wochen, nicht über Stunden.

Erfasst werden Laufzeit und Anzahl der Starts je Tag. Sie liegen im NVS und überdauern einen
Neustart, geschrieben wird bei laufendem Brenner alle fünf Minuten und zum Tageswechsel — häufiger wäre für das Flash
nicht zuträglich, und ein verlorener Wert von wenigen Minuten fällt in einer Tagesbilanz nicht
ins Gewicht. Aus der
Laufzeit und dem eingetragenen Düsendurchsatz in Litern je Stunde ergibt sich eine Schätzung des
Ölverbrauchs. Häufige kurze Starts bei geringer Gesamtlaufzeit weisen auf Taktbetrieb hin und
werden gesondert ausgewiesen.

## Ladezustand des Speichers

Ein einzelner, ungünstig sitzender Pufferfühler lässt keine belastbare Aussage über den
Ladezustand zu, und der Mischer verhindert, dass die Heizkreisfühler ergänzend herangezogen
werden können. Das Vorgehen ist daher zweistufig.

### Zuerst messen, nicht bewerten

- alle Temperaturen im Zwei-Minuten-Raster über 24 Stunden
- zusätzlich eine hochaufgelöste Aufzeichnung einer vollständigen Ladung: 5-Sekunden-Raster,
  1600 Zeilen für gut zwei Stunden, ausgebbar als CSV

Aufgezeichnet werden nur die Messstellen, die beim Start belegt sind — am Kessel drei, am
Speicher sechs. Der Speicherbedarf richtet sich danach (rund 19 kB statt 45 kB bei allen
vierzehn möglichen Rollen), und die CSV-Datei bleibt ohne leere Spalten. Der Puffer wird erst
beim Start belegt und lässt sich über „Verwerfen" wieder freigeben.

Das Speicherboard nimmt die Kesselwerte über `/api/measurements` mit auf, sodass eine
Aufzeichnung alle acht Messstellen enthält. Erst aus diesen Kurven werden die Schwellwerte
abgeleitet.

Das entspricht dem Vorgehen bei der BEMF-Kalibrierung des Verteilers, bei der die gemessenen
Werte deutlich von den vorher angenommenen abwichen — die tatsächlichen Fahrzeiten lagen rund
15 % unter den fest eingestellten.

### Auswertung

Die Kriterien sind nach den ersten Aufzeichnungen angepasst.

- **Geladen**, wenn bei laufendem Brenner `kessel_vl − kessel_rl` unter `spread_full_k`
  (Vorgabe 8 K) fällt, dort `spread_hold_s` (300 s) bleibt und der Kesselvorlauf über
  `kessel_hot_c` (60 °C) liegt: Der Rücklauf ist warm, der Speicher nimmt keine Wärme mehr auf.
  Ebenso, wenn der Brenner von sich aus abschaltet, während der Kesselvorlauf über `kessel_hot_c`
  liegt. Die Bedingung an den Vorlauf kam nach der ersten Aufzeichnung hinzu: Beim Anfahren aus
  dem kalten Kessel liegen Vor- und Rücklauf ebenfalls dicht beieinander, weil beide kalt sind,
  und die Ladung galt nach wenigen Minuten als fertig. Fällt der Füllstand danach unter
  85 Prozent, gilt der Speicher wieder als nicht geladen.
- **Füllstand** als lineare Schätzung zwischen `leer_c` und `voll_c` aus dem Pufferfühler, in
  der Oberfläche als Schätzung gekennzeichnet. Den Nullpunkt misst das Gerät selbst nach: Läuft
  der Brenner an, nachdem der Speicher seit seinem Höchststand um mindestens `lern_drop_k`
  (3 K) gefallen ist, gilt der Speicherwert in diesem Augenblick als leer, und `leer_c` wird um
  40 Prozent des Abstands an ihn herangeführt. Messpunkte unter 15 °C oder weniger als 5 K unter
  `voll_c` werden verworfen.
- **Warnung Warmwasser**, wenn der Pufferwert unter `warn_c` fällt. Da das Trinkwasser im
  Durchlauf erwärmt wird, ist das der praktisch spürbare Grenzfall.

Sind die Kesselwerte nicht erreichbar, entfällt das erste Kriterium; die Oberfläche zeigt dann
„Ladezustand eingeschränkt, Kesselboard nicht erreichbar".

### Korrekturwert des Pufferfühlers

Er wird von Hand eingetragen, wie jeder andere Fühlerkorrekturwert. Eine selbsttätige Ermittlung
ist mit dem vorhandenen Fühlerbestand nicht möglich: die Heizkreisvorläufe liegen hinter dem
Mischer, und der Kesselvorlauf entspricht dem Speicherinhalt nur unmittelbar am Ende einer Ladung
und nur an der oberen Einschichthöhe.

Aus diesem Sonderfall lässt sich in der Aufzeichnung ein Anhaltspunkt ablesen: die Oberfläche
weist die Differenz `kessel_vl − puffer` beim Abschalten des Brenners aus. Übernommen wird sie
nicht selbsttätig.

Anzumerken ist außerdem, dass ein konstanter Korrekturwert eine Abweichung nur an einem
Betriebspunkt ausgleicht. Bei geschichtetem Speicher ändert sich der Fehler mit dem Ladezustand.
Ein zweiter Fühler im unteren Speicherbereich wäre die belastbare Lösung; die Rolle
`puffer_unten` ist dafür vorgesehen.

## Bedarfserkennung

Das Speicherboard findet die Verteiler über mDNS und fragt jeden alle 5 Sekunden ab.

```
GET /api/demand          (neu auf der Verteiler-Firmware)

{ "id": "fbh_a1b2c3",
  "site": "Keller",
  "demand": true,
  "max_target": 1.00,
  "open_channels": 2,
  "rooms_calling": 1,
  "min_room_c": 19.4,
  "sensor_ok": true }
```

`demand` ist wahr, sobald bei einem Kanal die größere von Ist- und Zielstellung über `schwelle`
(Vorgabe 5 %) liegt. Die Zielstellung geht mit ein, damit die Pumpe nicht erst 40 Sekunden später
anläuft, wenn das Ventil offen ist. `min_room_c` ist die kälteste geregelte Raumtemperatur und
dient dem Frostschutz. `sensor_ok` ist falsch, wenn kein Raum einen gültigen Messwert hat.

Alle Werte stehen in `ctl_snapshot_t` bereit; der Endpunkt ist reine Ausgabe ohne eigene Logik.

Zuordnung in der Konfiguration des Speicherboards: Heizkreis 1 erhält Keller und Erdgeschoss,
Heizkreis 2 das Obergeschoss.

### Ausfallverhalten

| Fall | Bewertung |
|---|---|
| Verteiler antwortet, `demand=false` | kein Bedarf |
| Verteiler antwortet länger als `timeout_s` (180 s) nicht, hat aber vorher geantwortet | Bedarf gilt als vorhanden, Störungsmeldung in der Oberfläche |
| Verteiler hat seit dem Start des Speicherboards nie geantwortet | wird nicht gewertet |

Der letzte Fall verhindert, dass ein dauerhaft abgeschaltetes Gerät die Pumpe durchgehend laufen
lässt.

## Pumpensteuerung

Je Heizkreis eine Zustandsmaschine mit den Betriebsarten `auto`, `ein`, `aus`. Im
Automatikbetrieb gilt:

| Bedingung | Wirkung |
|---|---|
| Bedarf und `puffer ≥ min_buffer_c` | Pumpe ein |
| kein Bedarf mehr | Nachlauf `overrun_s` (300 s), dann aus |
| Mindestlaufzeit `min_run_s`, Mindestpause `min_pause_s` (je 180 s) | verhindert kurzes Takten |
| `min_room_c < frost_c` (6 °C) oder Vorlauf unter 8 °C | Pumpe ein, unabhängig von Bedarf und Betriebsart |
| wöchentlicher Termin `seize_weekday`, `seize_hour` | Schutzlauf 3 Minuten, sofern die Pumpe in den letzten 24 Stunden nicht lief |

Der Termin richtet sich nach der Uhr, nicht nach der Standzeit seit dem Einschalten; die erste
Fassung mit `schutzlauf_tage` hätte auf Geräten mit täglichem Neustart nie ausgelöst.

`min_buffer_c` ist wegen des Mischers **oberhalb** der höchsten benötigten
Fußbodenvorlauftemperatur anzusetzen, nicht knapp über Raumtemperatur: Der Mischer kann nur
herunterregeln. Fällt der Speicher unter die benötigte Vorlauftemperatur, öffnet er vollständig
und der Kreis bekommt trotzdem zu wenig. Vorgabe daher 40 °C, im Betrieb aus den aufgezeichneten
Kurven nachzuziehen.

### Kesselkreispumpe

Die Pumpe zwischen Kessel und Speicher war ursprünglich nicht Teil dieses Konzepts; sie wurde
von einem Node-RED-Ablauf geschaltet und ist inzwischen in die Firmware übernommen. Geführt wird
sie nur auf dem Gerät, das Kesselvor- und -rücklauf selbst misst. Mit Werten vom Nachbargerät
zu schalten hieße, bei einem Verbindungsabbruch ohne Messwerte zu entscheiden.

| Bedingung | Wirkung |
|---|---|
| `kessel_vl − puffer ≥ on_k` (3 K) über `hold_s` (120 s) | Pumpe ein |
| `kessel_vl − puffer ≤ off_k` (2 K) über `hold_s` | Pumpe aus |
| Mindestlaufzeit `min_run_s`, Mindestpause `min_pause_s` (je 180 s) | verhindert kurzes Takten |
| keine gültigen Messwerte | Pumpe ein |
| `kessel_vl > emergency_c` (85 °C) | Pumpe ein, unabhängig vom Abstand |

Bezug ist die Speichertemperatur, ersatzweise der Kesselrücklauf. Der Rücklauf allein genügt
nicht: Bei stehender Pumpe nehmen Vor- und Rücklauf die Temperatur des Kesselkörpers an, ihr
Abstand geht gegen null, und ein Kessel mit Restwärme bliebe stehen, obwohl der Speicher kälter
ist. Die Ausschaltschwelle von 2 K stammt aus einer Aufzeichnung: Der Speicher erreichte seinen
Höchststand in dem Augenblick, in dem der Abstand auf 2 K gefallen war. Mit der vorherigen
Schwelle von 0,5 K lief die Pumpe nach dem Brennerende rund fünf Stunden weiter.

## Anbindung des Sonoff-Relais

Geschaltet wird auf einem von zwei Wegen. Ist ein Broker eingerichtet und die Verbindung steht,
geht der Befehl über MQTT; das Relais meldet dann jede Änderung von selbst, auch eine von Hand
am Gerät. Sonst genügt die Adresse des Relais: der Befehl geht unmittelbar an dessen
Schnittstelle `/cm`, und die Antwort enthält den tatsächlichen Zustand. Zwischen zwei Aufrufen
bleibt eine Änderung am Relais dabei unbemerkt, deshalb wird der Sollzustand zyklisch
nachgesendet.

```
http://<adresse>/cm?cmnd=Power1%20ON        ->  {"POWER":"ON"}
http://<adresse>/cm?cmnd=Var1%201           ->  {"Var1":"1"}
```

Verlangt das Relais eine Anmeldung, werden `&user=` und `&password=` angehängt.

### Über MQTT

| Richtung | Topic | Inhalt |
|---|---|---|
| Befehl | `cmnd/<topic>/POWER<n>` | `ON` / `OFF` |
| Rückmeldung | `stat/<topic>/POWER<n>` | `ON` / `OFF` |
| Erreichbarkeit | `tele/<topic>/LWT` | `Online` / `Offline` |
| Lebenszeichen | `cmnd/<topic>/Var1` | `1`, alle 60 s |

Topic und Relaisnummer sind je Heizkreis konfigurierbar. Der gewünschte Zustand wird zusätzlich
alle 60 Sekunden gesendet und außerdem sofort, wenn im LWT `Online` erscheint — damit übernimmt
ein neu gestartetes Relais den richtigen Zustand. Weicht die Rückmeldung länger als 30 Sekunden
vom Sollzustand ab, meldet die Oberfläche eine Störung.

### Rückfallverhalten

Bei Ausfall der Steuerung laufen die Pumpen. Am Relais einzustellen:

```
PowerOnState 1
Rule1 ON Var1#State DO RuleTimer1 900 ENDON ON Rules#Timer=1 DO Power1 1 ENDON
Rule1 1
```

Bleibt das Lebenszeichen aus, schaltet das Relais nach 15 Minuten selbsttätig ein. Die genaue
Regelsyntax ist am Gerät zu prüfen. `PowerRetain` bleibt ausgeschaltet, da der Zustand ohnehin
zyklisch gesendet wird.

Eine laufende Pumpe gegen geschlossene Ventile ist verschwenderisch, aber unschädlich; eine
stehende Pumpe bei Wärmebedarf ist es nicht. Deshalb ist „ein" der Rückfallwert.

## Konfiguration

Wie beim Verteiler ein einziger NVS-Schlüssel mit JSON, geladen als Vorgabewerte mit
anschließender Überlagerung. Ein neu hinzugefügtes Feld ist damit von selbst abwärtskompatibel.

```jsonc
{
  "cfg_version": 1,
  "site": "Pufferspeicher",            // Bezeichnung des Geraets, leer = Einrichtung offen
  "onewire_pin": [13, -1],             // zwei Busse, -1 = unbenutzt
  "poll_s": 10,

  "probes": [
    { "rom": "28FF641E8016034A", "role": "puffer",  "name": "Pufferspeicher", "offset_k": 3.0 },
    { "rom": "28FF641E8016052B", "role": "hk1_vl",  "name": "HK1 Vorlauf",    "offset_k": 0.0 },
    { "rom": "28FF641E8016061C", "role": "hk1_rl",  "name": "HK1 Ruecklauf",  "offset_k": 0.0 }
  ],

  "burner": {                          // nur wirksam, wenn eine Rolle "abgas" zugeordnet ist
    "delta_on_k": 12.0, "delta_off_k": 6.0, "swing_k": 6.0,
    "on_hold_s": 60, "off_hold_s": 300,
    "duese_l_h": 2.2,                  // Duesendurchsatz fuer die Verbrauchsschaetzung
    "wartung_epoch": 0                 // letzte Kesselreinigung, Bezug fuer den Abgasabstand
  },

  "buffer": {
    "voll_c": 62.0, "leer_c": 35.0, "leer_lernen": true, "lern_drop_k": 3.0,
    "spread_full_k": 8.0, "spread_hold_s": 300, "kessel_hot_c": 60.0,
    "warn_c": 40.0, "volumen_l": 0, "zapf_drop_k": 2.0, "zapf_win_s": 900
  },

  "boiler_pump": {                     // nur auf dem Geraet mit Kesselvor- und -ruecklauf
    "enabled": false, "host": "", "relay": 1, "mode": "auto",
    "on_k": 3.0, "off_k": 2.0, "hold_s": 120,
    "min_run_s": 180, "min_pause_s": 180, "emergency_c": 85.0
  },

  "circuits": [
    { "id": 1, "name": "Keller und Erdgeschoss", "enabled": true,
      "vl_role": "hk1_vl", "rl_role": "hk1_rl",
      "peers": ["fbh_a1b2c3", "fbh_d4e5f6"],
      "pump": { "host": "192.168.1.203", "relay": 1 },
      "mode": "auto",
      "overrun_s": 300, "min_run_s": 180, "min_pause_s": 180,
      "min_buffer_c": 40.0, "frost_c": 6.0 }
  ],

  "demand_poll_s": 5, "demand_timeout_s": 180,
  "seize_weekday": 6, "seize_hour": 11,

  "wifi": { "ssid": "", "hostname": "heizung", "ap_pass": "fussboden" },
  "mqtt": { "enabled": false, "uri": "", "user": "", "prefix": "heiz" },
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
  "reboot_hour": -1, "reboot_minute": 0
}
```

Passwörter werden bei `GET /api/config` durch `pass_set: true/false` ersetzt, wie beim Verteiler.
Das Nachbargerät wird über mDNS gefunden; eine Einstellung dafür gibt es nicht. Die Schwelle,
ab der ein Verteiler Bedarf meldet (5 Prozent Ventilstellung), liegt beim Verteiler. Vorgaben,
zulässige Bereiche und die Regeln beim Zusammenführen stehen für jeden Schlüssel im Handbuch
unter „Konfiguration im Einzelnen".

## Schnittstelle

### Heizungsgeräte

```
GET     /api/state              Messwerte samt Fuehlerliste, Brenner, Ladung, Kreise, Pumpen,
                                Auswertung und Befunde
GET     /api/measurements       eigene Fuehler mit Rolle, Wert und Alter
GET     /api/history            Verlauf, Zwei-Minuten-Raster
GET     /api/record             Aufzeichnung einer Ladung als CSV
POST    /api/record/{aktion}    arm | start | stop | discard
GET     /api/log/charges        Ladungsprotokoll als CSV
GET     /api/log/days           Tagesprotokoll als CSV
GET/PUT /api/config
GET     /api/config/backup      Sicherung mit Zugangsdaten
POST    /api/config/restore     Sicherung einspielen
POST    /api/circuit/{n}/mode   auto | ein | aus
POST    /api/boilerpump/{modus} auto | ein | aus
POST    /api/probes/rescan      Bus neu absuchen
GET     /api/peers
GET/POST /api/wifi/scan         Netze suchen
POST    /api/system/{aktion}    restart | factory | clear-logs
POST    /api/ota
```

Die Bedeutung jedes Aufrufs steht im Handbuch unter „Schnittstelle".

`/api/measurements` ist bewusst schlank gehalten, weil es alle 5 Sekunden vom Nachbargerät
abgefragt wird:

```json
{ "id": "heiz_3f21ac", "site": "Kessel", "uptime_s": 84213,
  "probes": [ { "role": "abgas",     "c": 143.2, "age_s": 3 },
              { "role": "kessel_vl", "c":  71.8, "age_s": 3 },
              { "role": "kessel_rl", "c":  54.1, "age_s": 3 } ],
  "burner": { "known": true, "running": true, "since_s": 640,
              "runtime_today_s": 9120, "starts_today": 6 } }
```

### Verteiler-Firmware

Neu: `GET /api/demand` (siehe oben). Alles Übrige bleibt unverändert.

## Aufbau der Firmware

### Neue gemeinsame Komponenten

**`onewire_temp`** — mehrere DS18B20 an bis zu zwei Bussen, Pins als Parameter. Gegenüber dem
vorhandenen `sensors_local` drei Unterschiede:

1. Wandlung per Sammelbefehl: `onewire_bus_reset()`, dann `0xCC` (Skip ROM) und `0x44` (Convert)
   über `onewire_bus_write_bytes()`. Alle Fühler wandeln gleichzeitig, eine Leserunde dauert
   750 ms statt eines Vielfachen davon.
2. Wiederholbarer Suchlauf im Betrieb, damit ein nachgerüsteter Fühler ohne Neustart erscheint.
3. Verwerfen der Ausreißer 85,0 °C (Einschaltwert nach Spannungseinbruch) und −127 °C
   (Leitungsbruch), jeweils mit Zähler je Fühler, damit ein wackelnder Kontakt auffällt.

`sensors_local` bleibt unangetastet und weiterhin dem Verteiler vorbehalten.

**`mqttc`** — esp-mqtt mit Verbindungsaufbau, letztem Willen, `mqttc_publish(topic, payload,
retain)` und `mqttc_subscribe(topic, cb, ctx)` mit Abo-Tabelle. Nötig, weil `app_mqtt.c` der
Verteiler-Firmware alles verwirft, was nicht auf das eigene Präfix passt, und `publish()` nicht
exportiert. Das Speicherboard muss aber fremde Topics beschreiben und mithören. Die
Verteiler-Firmware wird nicht umgestellt.

**`heatlogic`** — Brennererkennung, Ladezustand und Pumpenlogik als reine Rechenmodule ohne
ESP-IDF-Bezug, wie `valve` und `roomctrl`. Zeit wird als Parameter übergeben, damit die Module in
`test/host` gegen aufgezeichnete Messreihen laufen können.

### Änderungen an der Verteiler-Firmware

1. `/api/demand` in `app_web.c` (Stufe 2).
2. `peers` mit Rollenfeld `role` im TXT-Satz und `char role[12]` in `peer_t`, `PEERS_MAX` von 8
   auf 12; der Gerätewechsler in der Kopfzeile führt Verteiler und Heizungsgeräte getrennt auf
   (Stufe 1).

Regelung, Kalibrierung, Anzeige und MQTT bleiben unberührt.

## Oberfläche

Eigene Seite unter `apps/heatsource/main/www/index.html`, gestaltet nach den Vorgaben der
bestehenden Oberfläche. Beide Geräte liefern dieselbe Seite aus; welche Abschnitte erscheinen, richtet
sich nach den zugeordneten Fühlern und Heizkreisen.

| Reiter | Inhalt |
|---|---|
| Übersicht | Anlagenschema als SVG mit Kessel, Speicher, Mischern und Kreisen, Messwerte an der zugehörigen Stelle; Brennerzustand, Füllstandsschätzung, Pumpenzustand, Laufzeit und Starts des Tages, Verbrauchsschätzung. Fremdwerte vom Nachbargerät sind gekennzeichnet. |
| Heizkreise | je Kreis Betriebsart, Bedarf mit Quelle, Vor- und Rücklauf, Spreizung, Zeitparameter, Zustand des Relais, letzter Schaltvorgang |
| Fühler | alle gefundenen ROM-Adressen mit Messwert, Änderung der letzten 30 Sekunden und Zuordnung |
| Verlauf | Diagramm der letzten 24 Stunden, Messreihen einzeln zuschaltbar; Aufzeichnung einer Ladung, Ausgabe als CSV |
| System | WLAN, MQTT, Zeit, Neustart, Firmware-Aktualisierung, Einrichtungsassistent |

Einrichtungsassistent: Bezeichnung des Geräts, Fühler zuordnen, danach — nur wenn Kreise
gewünscht sind — Heizkreise mit Relaisdaten anlegen und die Verteiler den Kreisen zuordnen.

Das gemeinsame Stilblatt wird erst herausgelöst, wenn die zweite Oberfläche steht. `embed_www`
kann mehrere Quellen vor dem Komprimieren aneinanderfügen, sodass zur Laufzeit weiterhin eine
einzige Datei ausgeliefert wird.

## Home Assistant

Beide Heizungsgeräte melden sich über MQTT-Discovery mit eigener Gerätekennung an.

| Gerät | Entitäten |
|---|---|
| Kesselboard | drei Temperaturen, Kesselspreizung, Brennerzustand, Laufzeit und Starts des Tages, Verbrauchsschätzung |
| Speicherboard | fünf Temperaturen, Spreizung je Kreis, Füllstandsschätzung, Warnung Warmwasserreserve, Bedarf und Pumpenzustand je Kreis, Betriebsart je Kreis als Auswahl |

Die Integration unter `custom_components/floor_heating/` wird erweitert: `api.py` bleibt
unverändert nutzbar, der Einrichtungsdialog wertet `device.model` aus und richtet je nach Gerät
die passenden Entitäten ein.

## Umsetzungsreihenfolge

| Stufe | Inhalt | Stand |
|---|---|---|
| 0 | Projektstruktur umbauen, Nachweis, OTA — siehe [umbau-projektstruktur.md](umbau-projektstruktur.md) | umgesetzt |
| 1 | `apps/heatsource`: eigene Konfiguration, `onewire_temp`, Oberfläche mit Schema, Fühlerzuordnung, Verlauf, `/api/measurements`, mDNS mit Rollenfeld, OTA, Prüfstrecke | umgesetzt und an beiden Geräten im Betrieb |
| 2 | `/api/demand` auf den Verteilern, Bedarfsabfrage, `heatlogic` mit Pumpenzustandsmaschine, `mqttc`, Tasmota-Anbindung, Handbetrieb | umgesetzt; drei Relais im Betrieb, angesprochen über HTTP |
| 3 | Brennererkennung, Laufzeit, Starts, Verbrauchsschätzung, Aufzeichnung einer Ladung mit CSV-Ausgabe | umgesetzt; Brennererkennung nach den Aufzeichnungen um den Ausschlag ergänzt |
| 4 | Ladezustand aus den aufgezeichneten Kurven, Kesselkreispumpe, MQTT-Discovery, Erweiterung der Integration | umgesetzt; MQTT-Discovery mangels Broker nicht erprobt |
| 5 (optional) | `netmgr_cfg_t`, gemeinsamer JSON-Unterbau, gemeinsames Stilblatt, `hw_map` und `config_store` des Verteilers nach `apps/manifold/components/` | `netmgr_cfg_t`, `cfgjson` und das Stilblatt umgesetzt; das Verschieben ist offen |

Zur Aufzeichnung: Sie lässt sich scharf schalten und beginnt dann von selbst — der Anfang einer
Ladung ist von Hand kaum zu treffen. Ausgelöst wird über den Brennerzustand, sobald das Gerät ihn
kennt; beendet wird zehn Minuten nach dem Brennerlauf, wobei ein taktender Kessel die
Aufzeichnung nicht zerteilt.

Ein Gerät am Pufferspeicher kennt den Brennerzustand erst, wenn das Gerät am Kessel steht und ihn
über `/api/measurements` meldet. Bis dahin dient der Anstieg der Speichertemperatur als Ersatz:
1,5 K in zwanzig Minuten gelten als beginnende Ladung, eine Viertelstunde ohne neuen Höchstwert
als deren Ende. Der Bezugswert folgt einem Rücklauf sofort und wandert sonst mit dem Fenster
weiter — dieselbe Bauart wie die gleitende Bezugslinie der Brennererkennung und ohne Ringpuffer.
Der Ersatz spricht einige Minuten später an als der Brennerzustand, weil die Wärme erst im
Speicher ankommen muss; für eine Kurve, die zwei Stunden umfasst, fällt das nicht ins Gewicht.

Wann begonnen und wann beendet wird, steht als `rec_trigger` in `components/heatlogic` und ist
dort ohne Hardware geprüft.

Zum Stilblatt: Geteilt wird das Gerüst — Farben, Schriften, Radien, Grundregeln sowie Kopfzeile,
Reiter und Inhaltsfläche. Die Bedienelemente bleiben je Anwendung eigen. Ein Vergleich der
beiden Stände zeigte, dass Schaltflächen, Beschriftungsfelder, Zustandsmarken und Tabellen sich
sachlich auseinanderentwickelt haben; sie zusammenzuzwingen hätte das Erscheinungsbild der
laufenden Verteiler-Firmware verändert, ohne dass jemand danach gefragt hätte.

Zur ersten Inbetriebnahme eines Heizungsgeräts: Nach dem Einspielen öffnet es einen
Zugangspunkt `heizung-XXXX`, danach führt der Assistent durch Bezeichnung und
Fühlerzuordnung. Der 1-Wire-Anschluss steht unter **System** und ist auf GPIO 13
voreingestellt; weicht die vorhandene Verdrahtung ab, wird er dort eingetragen. Die
Fühlererfassung stellt ohne Neustart um.

Die Stufen 0 bis 2 ergeben betriebsfähige Geräte; alles Weitere ist additiv.

## Prüfungen

### Stand

Stand 21. September 2026. Beide Heizungsgeräte laufen an der Anlage, das Gerät am Kessel mit
drei Fühlern, das am Speicher mit fünf, beide an einem Bus an GPIO 13. Geprüft sind die Kette
Verteiler → Bedarf → Pumpenlogik, das Schalten der drei Tasmota-Relais über HTTP, Handbetrieb
und Rückkehr in die Automatik. Seit dem 19. August sind 23 Ladungen aufgezeichnet; an ihnen
sind Brennererkennung, Ladezustand und Kesselkreispumpe nachgestellt, wie in den jeweiligen
Abschnitten beschrieben.

Nicht erprobt ist, was einen Broker voraussetzt: das Schalten über MQTT, die Rückmeldung aus
`stat/…` und die Regel für das Lebenszeichen.

### Ohne Hardware

`heatlogic` in `test/host`, im selben Stil wie die bei Beginn vorhandenen 215 Prüfungen; heute
umfasst der Lauf 524:

- Brennererkennung gegen eine aufgezeichnete Abgaskurve, einschließlich Taktbetrieb
- Pumpenzustandsmaschine: Mindestlaufzeit, Mindestpause, Nachlauf, Frostschutz, Schutzlauf
- Bedarfsauswertung bei ausgefallenem Verteiler und bei nie erreichtem Verteiler
- Ladezustand mit und ohne Kesselwerte

Eingangsdaten sind die Aufzeichnungen aus `/api/record`, damit gegen wirkliche Kurven geprüft
wird und nicht gegen angenommene.

### Auf dem Tisch

1. Fühler anschließen, Suchlauf prüfen, jeden einzeln mit der Hand erwärmen und zuordnen.
2. Sammelwandlung nachmessen: eine Leserunde muss unter einer Sekunde bleiben.
3. Tasmota schalten und die Rückmeldung aus `stat/…` gegenprüfen; Relais stromlos machen und den
   Wiederanlauf beobachten.
4. Netzwerkstecker ziehen: nach 15 Minuten muss die Pumpe über die Tasmota-Regel anlaufen.

### An der Anlage

5. Eine vollständige Ladung aufzeichnen und die Kurven ansehen. Höchstwert des Abgasfühlers
   prüfen — nähert er sich 100 °C, muss der Fühler weiter vom Kessel weg.
6. Alle Räume eines Kreises abschalten und prüfen, dass die Pumpe nach dem Nachlauf abschaltet;
   einen Raum wieder anfordern und die Anlaufzeit messen.
7. Verteiler vom Netz nehmen: die zugehörige Pumpe muss einschalten, die Oberfläche die Störung
   anzeigen.
8. Kesselboard vom Netz nehmen: die Pumpen müssen unverändert schalten, nur der Ladezustand wird
   als eingeschränkt gemeldet.
9. Dauerlauf über eine Woche mit Protokollierung von Speicherverbrauch, Schaltvorgängen je Pumpe
   und Brennerstarts je Tag.

## Offene Punkte

| Punkt | Stand |
|---|---|
| Belastbarkeit des Abgasfühlers | Ein DS18B20 hält 125 °C aus. In 23 Ladungen lag der höchste Abgaswert bei 88,0 °C; der Fühler kann bleiben, wo er ist. |
| Zweiter Pufferfühler | Solange nur ein Fühler vorhanden ist und dieser ungünstig sitzt, bleibt der Füllstand eine Schätzung. Rolle `puffer_unten` ist vorgesehen. |
| Mischerstellung | Der Steuerung nicht bekannt. Ob eine Rückmeldung oder eine eigene Mischerregelung sinnvoll ist, bleibt offen. |
| Buslänge | Entschieden: Beide Geräte kommen mit einem Bus aus, fünf beziehungsweise drei Fühler. Der zweite Bus bleibt als Möglichkeit bestehen. |
| Täglicher Neustart | Ab Werk abgeschaltet. Wird er eingestellt, verschiebt `netmgr_set_reboot_guard()` ihn, solange eine Pumpe in einer Mindestlaufzeit steht; die Tageswerte werden bei laufendem Brenner alle fünf Minuten gesichert. |
| Anzeige am Gerät | Nicht vorgesehen. `ssd1327` stünde zur Verfügung. |
