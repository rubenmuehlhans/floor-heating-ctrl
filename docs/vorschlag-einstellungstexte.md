# Vorschlag: Bezeichnungen und Hinweistexte der Einstellungen

Stand 2. November 2026, Grundlage Commit `f578334`. Abgestimmt und umgesetzt am 2. November 2026. Abweichend vom Entwurf bleiben „Leer bei“ und „Voll bei“ ohne Prozentangabe in der Bezeichnung; die offenen Fragen 1 bis 3 sind wie vorgeschlagen entschieden, die Mängel im Code bleiben bis auf die Ersatzwerte der Oberfläche und den festen Text „Viertelstunde“ offen.

## Anlass

`burner.off_hold_s` („Haltezeit aus“ der Brennererkennung) wurde auf 1800 s gestellt, um die
Kesselkreispumpe 30 Minuten nachlaufen zu lassen. Die Einstellung legt aber fest, wie lange das
Abgas fallen muss, bevor der Brenner als aus gilt. Folgen dieses Werts:

- Jeder Brennerlauf wäre gegenüber der Vorgabe von 300 s um rund 25 Minuten zu lang gezählt
  worden, das sind bei 2,2 l/h rund 0,9 l Öl je Lauf zu viel.
- Nachzündungen wären mit dem vorigen Lauf verschmolzen, ein Taktbetrieb wäre nicht erkannt
  worden.
- Ladungen hätten im Protokoll rund 30 Minuten länger gedauert, und Zapfungen in dieser Zeit
  wären nicht gezählt worden.
- Die Pumpe wäre insgesamt rund 65 statt 40 Minuten nachgelaufen. Ihr Nachlauf
  (`boiler_pump.overrun_s`, Vorgabe 1800 s) beginnt erst mit dem erkannten Brennerende.

Kein Hinweistext nennt diese Wirkung. Das Wort „Haltezeit“ steht für vier verschiedene
Einstellungen, „Nachlauf“ für zwei.

## Grundsätze des Vorschlags

1. **Die Bezeichnung nennt den Zweck, nicht das Verfahren.** „Brennerende bestätigt nach“ statt
   „Haltezeit aus“.
2. **Kein Wort steht für zwei Einstellungen.** „Haltezeit“ entfällt. „Nachlauf“ bekommt in beiden
   Fällen einen Zusatz.
3. **Jeder Hinweistext beantwortet drei Fragen:**
   - Was bewirkt die Einstellung an der Anlage?
   - Was folgt aus einem höheren oder niedrigeren Wert?
   - Wann ändert man sie?
4. **Bei Verwechslungsgefahr verweist der Text auf die richtige Einstellung**, mit Pfad, etwa
   „Kesselkreispumpe › Nachlauf nach Brennerende“.
5. **Werte, die man nur bei besonderen Anlagen oder Fühlern ändert, stehen unter „Erweitert“.**
   Das gilt künftig auch in den Weboberflächen; dort gibt es diesen Bereich bisher nicht.
6. **App, Weboberflächen und Handbuch verwenden dieselbe Bezeichnung.** JSON-Schlüssel und API
   bleiben unverändert.
7. **Der Hinweistext nennt die KI-Freigabe nicht mehr.** Das übernimmt bereits das Symbol in der
   App, deshalb entfällt der Satz „Nicht für Vorschläge der KI freigegeben“.

In den Tabellen bedeutet die Spalte **Erw.**, ob die Einstellung unter „Erweitert“ steht.
„neu“ heißt: Sie kommt zusätzlich dorthin.

---

## Heizungsgerät

### Brennererkennung

Einleitung der Seite, neu: „Das Gerät erkennt am Abgasfühler, ob der Brenner läuft. Daraus
ergeben sich Laufzeit, Starts und Ölverbrauch, die Ladungen des Speichers und der Nachlauf der
Kesselkreispumpe. Wirksam ist die Erkennung nur an dem Gerät mit dem Abgasfühler. Die Vorgaben sind
an dieser Anlage erprobt; ändern Sie sie nur, wenn erkannte und tatsächliche Brennerläufe
nachweislich auseinanderliegen.“

| Schlüssel | Bisher (App / Web) | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `burner.delta_on_k` | Einschaltschwelle / Ein ab (K über der Linie) | Start ab Abgas über Ruhewert | Der Brenner gilt als gestartet, wenn das Abgas so weit über seinem Ruhewert liegt, dem kältesten Wert der letzten 24 Stunden. Höher: Starts werden später oder bei kurzen Läufen gar nicht erkannt, Laufzeit und Verbrauch fallen zu niedrig aus. Niedriger: Ein warmes Rohr kann einen Start vortäuschen. Muss über „Ende ab Abgas über Ruhewert“ liegen. | neu |
| `burner.delta_off_k` | Ausschaltschwelle / Aus unter | Ende ab Abgas über Ruhewert | Der Brenner gilt als aus, wenn das Abgas nur noch so weit über dem Ruhewert liegt. Bei warmem Kessel greift meist vorher „Abfall und Anstieg des Abgases“. Nach einem Neustart gilt ein laufender Brenner als aus, wenn das Abgas darunter liegt. Muss unter „Start ab Abgas über Ruhewert“ liegen. | neu |
| `burner.swing_k` | Ausschlag | Abfall und Anstieg des Abgases | Der Brenner gilt als aus, sobald das Abgas um diesen Betrag unter den Höchstwert des Laufs fällt; ein neuer Start verlangt einen Anstieg um diesen Betrag über den Tiefstwert. So wird das Ende auch bei warmem Kessel erkannt. Höher: Das Ende wird später erkannt, Nachzündungen verschmelzen mit dem vorigen Lauf, Laufzeit und Verbrauch steigen. Niedriger: Messschwankungen beenden Läufe, es werden zu viele Starts gezählt. | ja |
| `burner.on_hold_s` | Haltezeit an / Haltezeit ein | Brennerstart bestätigt nach | So lange muss die Startbedingung ununterbrochen anstehen, bevor der Brenner als gestartet gilt; erst ab dann zählt die Laufzeit, und erst dann schaltet die Brennererkennung die Kesselkreispumpe ein. Höher: Laufzeit und Verbrauch je Start um diesen Betrag zu niedrig, sehr kurze Läufe fehlen. Niedriger: Kurze Störungen zählen als Start. | ja |
| `burner.off_hold_s` | Haltezeit aus | Brennerende bestätigt nach | So lange muss das Abgas ununterbrochen fallen, bevor der Brenner als aus gilt. Bis dahin zählt die Laufzeit weiter; erst danach beginnt der Nachlauf der Kesselkreispumpe. Höher: Laufzeit und Ölverbrauch je Lauf zu hoch, Nachzündungen werden nicht als eigene Starts erkannt, die Pumpe läuft entsprechend länger. Niedriger: Ein kurzer Einbruch des Abgases beendet den Lauf vorzeitig. Den Nachlauf der Pumpe stellen Sie unter „Kesselkreispumpe › Nachlauf nach Brennerende“ ein. | ja |
| `burner.duese_l_h` | Düsendurchsatz | Düsendurchsatz | Ölmenge je Stunde Brennerlaufzeit; daraus schätzt das Gerät den Verbrauch. Auf die Regelung wirkt der Wert nicht. Tragen Sie den Wert der eingebauten Düse ein (Tabelle im Handbuch) oder, genauer, den aus zwei Tankablesungen ermittelten. Eine Änderung gilt ab dem laufenden Tag. 0 schaltet die Schätzung ab. | nein |

### Pufferspeicher

Einleitung, neu: „Füllstand, Ladungen und Zapfungen sind Anzeige und Auswertung; keine dieser
Einstellungen schaltet Brenner oder Pumpen. Die App trägt sie auf beiden Heizungsgeräten gleich
ein, weil beide denselben Speicher schätzen.“

| Schlüssel | Bisher | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `buffer.leer_c` | Leer bei | unverändert | Speichertemperatur, die als 0 % angezeigt wird: die Temperatur, bei der die Kesselregelung nachheizt. Schaltet nichts. Wann der Kessel nachheizt, entscheidet seine eigene Regelung; wann die Heizkreise abschalten, entscheidet „Heizkreis › Pumpe aus unter Speicher“. Ist „Leerpunkt selbst nachmessen“ eingeschaltet, führt das Gerät den Wert selbst nach, auch einen von Hand eingetragenen. | nein |
| `buffer.voll_c` | Voll bei | unverändert | Speichertemperatur, die als 100 % angezeigt wird. Tragen Sie die Temperatur ein, die der Speicher am Ende einer Ladung tatsächlich erreicht (Ladungsprotokoll). Zu niedrig: Die Anzeige steht lange auf 100 %. Wann eine Ladung fertig ist, bestimmt dieser Wert nicht. Muss über „Leer bei“ liegen. | nein |
| `buffer.leer_lernen` | Leerpunkt selbst nachmessen | unverändert | Startet der Brenner, nachdem der Speicher durch Verbrauch gefallen ist, nimmt das Gerät die Speichertemperatur in diesem Moment als Messpunkt und führt „Leer bei“ schrittweise dorthin nach. Eingeschaltet lassen. | nein |
| `buffer.lern_drop_k` | Nachmessen ab Abfall | Nachmessen nur nach Abfall um | Ein Brennerstart zählt nur als Messpunkt, wenn der Speicher seit seinem Höchststand mindestens so weit gefallen ist. Niedriger: Starts beim Takten oder nach einer Zapfung verschieben den Leerpunkt nach oben. Höher: Es gibt seltener Messpunkte. | ja |
| `buffer.warn_c` | Warnung Warmwasser | Warnung „Warmwasser knapp“ unter | Fällt der Speicher darunter, meldet das Gerät „Warmwasserreserve knapp“. Nur eine Meldung: Die Heizkreise sperrt „Heizkreis › Pumpe aus unter Speicher“. | nein |
| `buffer.volumen_l` | Speicherinhalt | Speicherinhalt | Rechnet Zapfungen und Ladungen von Kelvin in Kilowattstunden um; steuert nichts. Tragen Sie den Nenninhalt vom Typenschild ein. Bei 0 bleiben die Angaben in Kelvin. | nein |
| `buffer.zapf_drop_k` | Zapfung ab Einbruch | Zapfung erkannt ab Abfall um | Fällt der Speicher bei stehendem Brenner innerhalb des Zeitfensters mindestens so weit, zählt das als Warmwasserzapfung. Im Stillstand kühlt er an dieser Anlage um etwa 0,8 K je Stunde ab. Höher: Kleine Zapfungen fehlen. Niedriger: Stillstandsverlust zählt als Zapfung. | ja |
| `buffer.zapf_win_s` | im Fenster | Zeitfenster der Zapfung | In diesem Zeitraum muss der Abfall liegen. Länger: Auch langsame Abfälle zählen; ab etwa zwei Stunden erreicht der Stillstandsverlust die Schwelle. Kürzer: Langsame Zapfungen fehlen. | ja |
| `buffer.spread_full_k` | Spreizung „voll“ | Ladung fertig unter Spreizung | Läuft der Brenner und liegt der Kesselrücklauf weniger als diesen Betrag unter dem Vorlauf, nimmt der Speicher keine Wärme mehr auf: Die Ladung gilt als fertig. Nur Anzeige und Protokoll; Brenner und Pumpe schaltet der Wert nicht. Höher: „geladen“ erscheint zu früh. Niedriger: spät oder nie. | ja |
| `buffer.spread_hold_s` | Haltezeit | Ladung fertig bestätigt nach | So lange muss die Spreizung ununterbrochen unterschritten sein, bevor die Ladung als fertig gilt. Höher: „geladen“ erscheint später. Niedriger: Schon kurze Engstellen reichen. | ja |
| `buffer.kessel_hot_c` | Vorlauf gilt als heiß ab | Ladung fertig erst ab Vorlauf | Die Ladung gilt nur als fertig, wenn der Kesselvorlauf mindestens so warm ist, denn beim Anfahren aus dem kalten Kessel liegen Vor- und Rücklauf ebenfalls dicht beieinander. Endet der Brenner bei heißerem Vorlauf, gilt die Ladung ebenfalls als fertig. Nur ändern, wenn der Kessel regulär kühler lädt. Nicht zu verwechseln mit „Kesselkreispumpe › Notlauf ab Kesselvorlauf“. | ja |

Abschnitte: „Was leer und voll bedeutet“ (Web) und „Leer und voll“ (App) werden zu **„Leer und
voll“**. `warn_c` steht künftig auch im Web unter „Warmwasser“ und nicht mehr unter „Wann eine
Ladung fertig ist“.

### Kesselkreispumpe

Einleitung, neu: „Die Pumpe zwischen Kessel und Speicher läuft, solange der Brenner läuft, und
danach den Nachlauf lang. Anschließend schaltet sie nach der Spreizung: Sie läuft, solange der
Kessel noch Wärme an den Speicher abgibt, und schaltet ab, bevor sie Speicherwärme in den
auskühlenden Kessel fördert. Über der Notlaufgrenze und ohne Messwerte läuft sie immer.“

| Schlüssel | Bisher | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `boiler_pump.enabled` | Kesselkreispumpe vorhanden | unverändert | Die Pumpe zwischen Kessel und Speicher. Braucht Kesselvor- und -rücklauf an diesem Gerät. Ausgeschaltet sendet das Gerät keine Befehle mehr; das Relais bleibt im letzten Zustand. | nein |
| `boiler_pump.overrun_s` | Nachlauf nach Brennerende (App s, Web min) | Nachlauf nach Brennerende, **überall in min** | So lange läuft die Pumpe nach dem erkannten Brennerende weiter, unabhängig von der Spreizung. Der Kessel bleibt dabei auf Speichertemperatur und zündet gleich nach, solange die Ladung angefordert ist, statt auszukühlen und später einzeln nachzuheizen. Höher: mehr Pumpenstrom und Wärmeverlust über den Kessel. 0: kein Nachlauf. Das Brennerende erkennt das Gerät erst nach „Brennererkennung › Brennerende bestätigt nach“. | nein |
| `boiler_pump.on_k` | Ein ab Spreizung | Ein ab Kessel wärmer als Speicher um | Steht die Pumpe, läuft sie an, sobald der Kesselvorlauf so viel wärmer ist als das Wasser, das aus dem Speicher zurückkäme. Gilt nur bei stehendem Brenner und nach dem Nachlauf. Höher: Restwärme im Kessel wird seltener abgeholt. Niedriger: Die Pumpe läuft öfter kurz an. Muss über „Aus unter Spreizung“ liegen. | neu |
| `boiler_pump.off_k` | Aus unter | Aus unter Spreizung | Läuft die Pumpe, schaltet sie ab, sobald der Kesselvorlauf weniger als diesen Betrag über dem Rücklauf liegt; dann gibt der Kessel keine Wärme mehr ab. Niedriger: Sie läuft lange weiter und fördert Speicherwasser durch den auskühlenden Kessel. Zwischen beiden Schwellen bleibt sie, wie sie ist. | neu |
| `boiler_pump.hold_s` | Haltezeit | Schaltverzögerung | So lange muss eine der beiden Schwellen ununterbrochen über- oder unterschritten sein, bevor die Pumpe schaltet. Nach dem Nachlauf läuft sie deshalb mindestens so lange weiter. Das ist nicht der Nachlauf; den stellen Sie unter „Nachlauf nach Brennerende“ ein. | ja |
| `boiler_pump.min_run_s` | Mindestlaufzeit | unverändert | Kürzer läuft die Pumpe nicht, wenn sie nach der Spreizung schaltet. Brenner, Nachlauf, Notlauf und Handbetrieb gehen vor. | ja |
| `boiler_pump.min_pause_s` | Mindestpause | unverändert | Kürzer steht die Pumpe nicht, wenn sie nach der Spreizung schaltet. Brenner, Nachlauf, Notlauf und Handbetrieb gehen vor. | ja |
| `boiler_pump.emergency_c` | Notgrenze | Notlauf ab Kesselvorlauf | Darüber läuft die Pumpe in jedem Fall, auch gegen die Schwellen, damit ein falsch messender Fühler die Wärmeabfuhr nicht verhindert. Ersetzt nicht den Sicherheitstemperaturbegrenzer des Kessels. Niedriger, nahe der üblichen Vorlauftemperatur: Die Pumpe läuft bei jedem heißen Stillstand und kühlt den Speicher über den Kessel aus. | ja |

Relaisfelder (`topic`, `host`, `relay`, `user`, `pass`) bleiben in Kesselkreispumpe und Heizkreis
gleich benannt wie bisher.

### Heizkreise

| Schlüssel | Bisher | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `circuits[].overrun_s` | Nachlauf | Nachlauf nach Bedarfsende | So lange läuft die Pumpe weiter, nachdem kein versorgter Verteiler mehr Wärme anfordert, damit die schließenden Ventile noch durchströmt werden. Höher: mehr Pumpenstrom. Hat mit dem Nachlauf der Kesselkreispumpe nichts zu tun. | neu |
| `circuits[].min_run_s` | Mindestlaufzeit | unverändert | Kürzer läuft die Pumpe nicht; das verhindert Takten. Frostschutz, Handbetrieb und Schutzlauf gehen vor. Solange eine Pumpe in ihrer Mindestlaufzeit steht, entfällt der tägliche Neustart. | ja |
| `circuits[].min_pause_s` | Mindestpause | unverändert | Kürzer steht die Pumpe nicht; das verhindert Takten. Frostschutz, Handbetrieb und Schutzlauf gehen vor. | ja |
| `circuits[].min_buffer_c` | Speicher mindestens | Pumpe aus unter Speicher | Ist der Speicher kälter, bleibt die Pumpe aus, auch bei Wärmebedarf und im Nachlauf. Liegt bewusst über der nötigen Vorlauftemperatur, weil der Mischer nur herunterregeln kann. Höher: Zwischen diesem Wert und dem Nachheizen des Kessels bekommen die Räume keine Wärme. Niedriger: Die Räume werden mit zu kaltem Wasser versorgt. | nein |
| `circuits[].frost_c` | Frostgrenze | Frostschutz unter Raumtemperatur | Fällt der kälteste Raum eines versorgten Verteilers darunter, auch ein ausgeschalteter, läuft die Pumpe in jedem Fall, auch auf „Hand aus“. Unabhängig davon läuft sie, wenn ihr Vorlauf unter 8 °C fällt. Höher: Die Pumpe läuft dauernd, sobald ein unbeheizter Raum kühl ist. | ja |

Abschnitt „Pumpenlogik“ (App) wird zu **„Pumpe“**, passend zum Web.

### Bedarfsabfrage, Fühler, Zeit

| Schlüssel | Bisher | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `demand_poll_s` | Abfrage alle | Bedarf abfragen alle | Wie oft das Gerät die versorgten Verteiler nach Wärmebedarf fragt. Höher: Pumpen starten und stoppen bis zu diesem Abstand später. Niedriger: mehr Netzlast. | ja |
| `demand_timeout_s` | Zeitgrenze | Verteiler gilt als verstummt nach | Antwortet ein Verteiler, der schon einmal geantwortet hat, so lange nicht, gilt sein Kreis als mit Bedarf: Die Pumpe läuft im Zweifel. Niedriger: Kurze WLAN-Aussetzer lassen die Pumpe anlaufen. Höher: Ein Ausfall wird später bemerkt. | ja |
| `poll_s` | Abtastabstand | Fühler messen alle | Abstand zwischen zwei Messrunden aller Fühler am Bus. Feiner als in diesem Abstand können Brennererkennung, Pumpen und Notlauf nicht reagieren. | ja |
| `probes[].offset_k` | Korrektur | Korrektur (K) | Wird zu jedem Messwert addiert, etwa wenn ein Anlegefühler schlecht am Rohr anliegt. Am Kesselvor- und -rücklauf verschiebt die Korrektur alle Schwellen der Kesselkreispumpe und das Ladungsende, am Speicher die Anzeige und die Sperre der Heizkreise. Am Abgasfühler wirkt sie nicht auf die Brennererkennung. Nur ändern, wenn ein Fühler nachweislich falsch misst. | nein |
| `reboot_hour` | Täglicher Neustart, Stunde / Neustart Stunde | Täglicher Neustart um (Uhr) | Nicht nötig; −1 schaltet ihn ab. Er entfällt für diesen Tag, wenn in dieser Minute eine Heizkreispumpe in ihrer Mindestlaufzeit steht. Ein Neustart bricht den Nachlauf der Kesselkreispumpe und die laufende Ladung im Protokoll ab. | ja |
| `reboot_minute` | Täglicher Neustart, Minute / Minute | Täglicher Neustart, Minute | Minute des täglichen Neustarts. | ja |
| `seize_weekday`, `seize_hour` | Wochentag, Stunde | unverändert | Im Sommer stehen die Heizkreispumpen über Monate still. Zum Termin läuft jede drei Minuten, die in den letzten 24 Stunden nicht gelaufen ist und nicht auf Hand aus steht. Die Kesselkreispumpe hat keinen Schutzlauf. | nein |

---

## Verteiler

### Räume

| Schlüssel | Bisher | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `rooms[].target_c` | Sollwert | Solltemperatur | Die Raumtemperatur, auf die der Verteiler regelt. Am Sollwert stehen die Ventile halb offen; braucht der Raum mehr oder weniger Wärme, pendelt er sich etwas darunter oder darüber ein. | nein |
| `rooms[].mode` | Betriebsart | unverändert | „Ausgeschaltet“ fährt die Ventile des Raums zu; er meldet keinen Wärmebedarf. Einen eigenen Frostschutz hat der Verteiler nicht, den übernimmt das Heizungsgerät mit „Heizkreis › Frostschutz unter Raumtemperatur“. | nein |
| `rooms[].p_band_k` | Proportionalband | Regelbereich | Wie weit die Raumtemperatur vom Sollwert abweichen darf, bis die Ventile ganz öffnen oder schließen: am Sollwert halb offen, etwa einen Regelbereich darunter offen, darüber zu. Kleiner: Die Ventile reagieren schärfer und fahren öfter, die Temperatur schwankt stärker. Größer: ruhiger, aber der Raum weicht weiter vom Sollwert ab. | nein |
| `rooms[].interval_s` | Prüfintervall | Regelung prüft alle | Abstand zwischen zwei Regelschritten des Raums. Kürzer: schnellere Reaktion auf neue Messwerte. Länger: Ein hier geänderter Sollwert wirkt später; über die Übersicht oder die Tasten geändert, wirkt er sofort. | ja |
| `rooms[].step` | Rasterung | Stellschritt der Ventile | Die Ventilstellung wird auf Schritte dieser Größe gerundet; erst wenn sich der gerundete Wert ändert, fährt das Ventil. 0,1 entspricht etwa 4 s Motorlauf. Kleiner: viele kleine Fahrten, mehr Verschleiß, die geschätzte Stellung wird ungenauer. Größer: wenige grobe Sprünge. | ja |
| `rooms[].min_delta` | Mindeständerung | Kleinste Fahrt | Kleinere Abweichungen zwischen Ziel und Stellung lösen keine Fahrt aus. Wirkt erst, wenn der Wert mindestens so groß ist wie der Stellschritt. | ja |

### Kreise (Stellantriebe)

Einleitung, neu: „Die Messfahrt trägt diese Werte je Kreis ein. Ändern Sie sie von Hand nur, wenn
keine Messfahrt möglich ist.“

| Schlüssel | Bisher (App / Web) | Neu | Hinweistext, neu |
|---|---|---|---|
| `channels[].open_ms` | Fahrzeit öffnen / Öffnen | Fahrzeit öffnen | Zeit des Antriebs von ganz zu bis ganz offen; daraus schätzt der Verteiler die Ventilstellung. Zu kurz: Das Ventil öffnet weniger weit als angezeigt, der Raum bleibt kühler. Zu lang: Es öffnet weiter als angezeigt. |
| `channels[].close_ms` | Fahrzeit schließen / Schließen | Fahrzeit schließen | Zeit von ganz offen bis ganz zu. Zu kurz: Das Ventil bleibt vor dem Sitz stehen, es fließt weiter Wasser, obwohl 0 % angezeigt wird. |
| `channels[].max_ms` | Maximallaufzeit / Maximal | Höchstlaufzeit bis Anschlag | Spätestens dann schaltet der Verteiler den Motor ab, wenn er die Endlage nicht erkennt. Gilt nur für Fahrten bis zum Anschlag (Notfahrt, Schutzfahrt, Handsteuerung). Mindestens so lang wie die längere Fahrzeit. |
| `channels[].blank_ms` | Sperrzeit | Anlauf ohne Endlagenerkennung | So lange nach dem Anlaufen wird die Endlage nicht ausgewertet, weil der Anlaufstrom sie sonst vortäuschen würde. 0 wirkt wie 2 s. Zu kurz: Fahrten enden sofort. Zu lang: Eine kurze Fahrt in den Anschlag bleibt unerkannt. |
| `channels[].bemf_mv` | Auslöseschwelle / Schwelle | Endlage erkannt ab | Steigt die Spannung am Messeingang über diesen Wert, gilt die Endlage als erreicht. Zu niedrig: Fahrten enden vor der Endlage, die angezeigte Stellung stimmt nicht. Zu hoch: Der Motor drückt bis zum Ende der Fahrzeit gegen den Anschlag. Wirksam bis etwa 1750 mV. |
| `channels[].bemf_hyst_mv` | Hysterese | Rückfall der Endlagenerkennung | Nach dem Auslösen muss die Spannung um diesen Betrag unter die Schwelle fallen, bevor die Endlage erneut erkannt wird. Höchstens so groß wie „Endlage erkannt ab“. Zu groß: Nach dem Anlaufen wird die Endlage nicht mehr erkannt. |

Das Ergebnis der Messfahrt nennt dieselben Bezeichnungen. Bisher heißen sie dort „Fahrzeit auf“,
„Fahrzeit zu“ und „Auslöseschwelle“.

### Betrieb, Tasten, Schutzfahrt

| Schlüssel | Bisher | Neu | Hinweistext, neu | Erw. |
|---|---|---|---|---|
| `sensor_timeout_s` | Messwert veraltet nach | Thermometer gilt als ausgefallen nach | Ist der letzte Messwert eines Raums älter, setzt dessen Regelung aus: Die Ventile bleiben stehen, der Raum meldet keinen Wärmebedarf. Höher: Bei ausgefallenem Thermometer wird länger nach einem alten Wert geregelt. Niedriger: Bei selten sendenden Thermometern setzt die Regelung öfter aus. | nein |
| `display_brightness` | Helligkeit der Anzeige / Helligkeit Anzeige | Helligkeit der Anzeige | Helligkeit der Anzeige am Gehäuse; 0 ist praktisch dunkel. | ja |
| `touch.thresholds[0..2]` | Schwelle „Sollwert senken“ … / Schwelle Taste 1/2/3 | Schwelle „Sollwert senken“, „Sollwert erhöhen“, „Raum wählen“ | Eine Taste gilt als berührt, sobald ihr Rohwert unter die Schwelle fällt. Lesen Sie unter „Sensoren“ Ruhe- und Berührwert ab und setzen Sie die Schwelle etwa mittig dazwischen. | nein |
| `seize_weekday`, `seize_hour` | Wochentag, Stunde | unverändert | Im Sommer stehen die Ventile über Monate geschlossen. Zum Termin fährt jeder Kreis auf (bis 92 %), auf Anschlag zu und zurück auf seine vorherige Stellung, damit er nicht festsitzt. Übergangen wird ein Kreis, der seit der letzten Schutzfahrt ohnehin gefahren ist, von Hand gehalten oder gerade vermessen wird; nach einem Neustart fahren alle. | nein |
| `reboot_hour`, `reboot_minute` | Täglicher Neustart / Neustart Stunde | wie am Heizungsgerät | Nicht nötig; −1 schaltet ihn ab. Er entfällt für diesen Tag, wenn in dieser Minute ein Ventil fährt oder eine Messfahrt läuft. | ja |

---

## Gemeinsam auf allen Geräten

| Schlüssel | Bisher (Fassungen) | Neu |
|---|---|---|
| `wifi.ap_pass` | Kennwort des Zugangspunkts · Kennwort Zugangspunkt · Kennwort des Einrichtungszugangs · Zugangspunkt-Kennwort | Kennwort des Einrichtungszugangs |
| `mqtt.enabled` | MQTT verwenden · eingeschaltet · MQTT einschalten | MQTT verwenden |
| `site` | Bezeichnung · Bezeichnung der Anlage · Ort (Leitstand) | Bezeichnung |
| `onewire_pin[]` | Bus 1 (GPIO) · Bus 1 · Anschluss (GPIO) | Bus 1 (GPIO), Bus 2 (GPIO); −1 „nicht belegt“ |
| Rollen `hkN_vl/_rl` | Heizkreis 1 Vorlauf · Vorlauf Heizkreis 1 | Heizkreis 1 Vorlauf |
| `mode` von Pumpe und Kesselkreispumpe | Automatik/Ein/Aus · Automatik/Hand ein/Hand aus | Automatik, Hand ein, Hand aus |

Der Leitstand bekommt die Fassung der anderen Geräte: „Heizungsgeräte abfragen alle“, „Verteiler
abfragen alle“ und Einheiten in Klammern statt nach einem Komma.

---

## Sachliche Fehler in den bisherigen Texten

Die folgenden Aussagen widersprechen der Firmware. Sie werden mit den neuen Texten berichtigt.

1. **Verteiler, Kreise ohne Raum:** Laut Text „bleiben [sie] stehen“ (Web an drei Stellen).
   Tatsächlich fahren sie zu.
2. **Schutzfahrt:** Laut Text fährt sie „auf Anschlag auf“ (Web und App). Tatsächlich fährt sie
   bis 92 % auf und auf Anschlag zu.
3. **Täglicher Neustart, beide Geräte:** Laut Web und Handbuch wird er „verschoben“. Tatsächlich
   entfällt er für den Tag.
4. **Schutzlauf am Heizungsgerät:** Der Webtext „jede Pumpe“ schließt die Kesselkreispumpe ein.
   Sie läuft dabei aber nicht mit.
5. **Kesselkreispumpe im Handbuch:** Der Satz „Sie läuft also mindestens die Haltezeit nach“ gilt
   seit dem Nachlauf nicht mehr. In der Konfigurationstabelle fehlt `overrun_s`.
6. **`boiler_pump.on_k`:** App, Web und Handbuch nennen drei verschiedene Bezugsgrößen: Rücklauf,
   Speicher und Bezug. Richtig ist: Bei stehender Pumpe vergleicht `on_k` mit dem Speicher plus
   zuletzt gemessenem Abstand, bei laufender Pumpe vergleicht `off_k` mit dem Rücklauf.

## Mängel im Code, getrennt vom Textvorschlag

Diese Punkte sind keine reinen Textfragen. Ich schlage vor, sie einzeln zu entscheiden:

- **Fester Text im Verteiler-Web:** Die Raumkarte meldet fest „Seit über einer Viertelstunde kein
  Messwert“, unabhängig von `sensor_timeout_s`.
- **Ersatzwerte im Web des Heizungsgeräts:** Für `on_k` und `off_k` gelten dort noch 1 und 0,5.
  Die Firmware verwendet 3 und 2. Das wirkt nur, wenn das Feld fehlt.
- **Darstellung „Erweitert“ in der App:** Die Liste ist flach, die Abschnitte gehen verloren.
  „Wann eine Ladung fertig ist“ verschwindet dadurch als Ganzes.
- **Kennzeichen „kalibriert“:** Es bleibt stehen, wenn eine Fahrzeit von Hand geändert wird.
- **Auslöseschwelle des Verteilers:** Sie ist bis 2200 mV zulässig, wirksam sind nur rund 1750 mV.
- **Kesselkreispumpe ohne Lebenszeichen und MQTT-Abo:** Anders als die Heizkreispumpen bekommt sie
  beides nicht. Die Ausfallregel am Tasmota greift deshalb nicht.
- **Leitstand:** `ap_pass` lässt sich in der Oberfläche nicht löschen. Die Einstellungen des
  Leitstands sind in der App nicht bearbeitbar.

## Offene Fragen

1. Sollen die Weboberflächen einen aufklappbaren Bereich „Erweitert“ bekommen, mit derselben
   Aufteilung wie die App? Ohne ihn lässt sich die Kennzeichnung „selten zu ändern“ dort nur im
   Text ausdrücken.
2. Sollen die neuen Einleitungen der Seiten (Brennererkennung, Pufferspeicher, Kesselkreispumpe,
   Kreise) in App und Web gleich lauten? Der Vorschlag geht davon aus.
3. Soll der Nachlauf der Kesselkreispumpe auch in der App in Minuten angezeigt werden? Der
   Vorschlag geht davon aus; gespeichert bleibt er in Sekunden.
4. Welche der Mängel im Code sollen mit der Textänderung behoben werden, welche später?
