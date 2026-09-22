# Änderungen

Die veröffentlichten Fassungen stehen mit Abbildern unter
[Releases](https://github.com/rubenmuehlhans/floor-heating-ctrl/releases).

## Unveröffentlicht, seit v0.3.0

### Verteilerplatine

- **Verschlüsselte BTHome-Thermometer.** Der Verteiler empfängt BTHome in Fassung 2, offen und
  mit AES-128-CCM verschlüsselt, etwa vom Climate-Sat von camperSense. Den Schlüssel je Gerät,
  32 Hexadezimalziffern aus der camperSense-App, nimmt die Oberfläche unter **Sensoren**
  entgegen, die Schnittstelle unter `POST /api/ble/key`. Bis zu 16 Schlüssel liegen in einem
  eigenen Eintrag im NVS, nicht in der Konfiguration; `GET /api/ble` nennt den Stand je Gerät
  („fehlt", „falsch", „passt") und die Adressen mit Schlüssel. Die Sicherung trägt die Schlüssel
  im Klartext, das Zurückspielen übernimmt sie, die Werksvorgabe löscht sie. Ein Rahmen mit
  einem nicht höheren Zähler als der zuletzt angenommene bleibt ohne Wirkung, nach zehn Minuten
  ohne gültigen Rahmen gilt er wieder. Werte, die ein Gerät auf mehrere Rahmen verteilt, werden
  feldweise übernommen.
- **Bluetooth gibt dem WLAN Funkzeit zurück.** WLAN und Bluetooth teilen sich ein Funkteil, und
  die Suche nach Thermometern belegte es durchgehend. Die Anmeldung am Einrichtungs-Zugangspunkt
  einer frischen Platine gelang deshalb erst nach mehreren Versuchen. Die Suche belegt das
  Funkteil jetzt zu 30 Prozent und ruht, solange nur der Zugangspunkt läuft; die Anmeldung
  gelingt beim ersten Versuch. Am Erdgeschoss gemessen sind die Messwerte der Thermometer
  dadurch im Mittel 24,7 statt 9,5 s alt, höchstens 112 statt 61 s; die Zeitgrenze liegt bei
  900 s.
- **Bedarf nur aus geregelten Kreisen.** Wärmebedarf melden nur noch Räume, die eingeschaltet
  sind und einen Messwert haben. Kreise ohne Raum zählten bisher mit; weil sie nach dem ersten
  Start auf Anschlag offen stehen, meldeten alle drei Verteiler dauerhaft Bedarf. Von Hand
  gehaltene Ventile zählen weiterhin.
- **Kreise ohne Raum fahren zu.** Einmal in der Minute wird geprüft, ob ein Kreis ohne Raum offen
  steht. Von Hand gehaltene Kreise, Messfahrt und Schutzfahrt bleiben unberührt.
- **Kennwort des Zugangspunkts.** Ein Kennwort unter acht Zeichen wird abgewiesen. Bisher öffnete
  das Gerät den Zugangspunkt in diesem Fall ohne Kennwort, ohne darauf hinzuweisen. Ebenso wird
  MQTT ohne Adresse des Brokers nicht mehr eingeschaltet — beides wie beim Heizungsgerät.

### Heizungsgerät

- **Brennerende am Ausschlag.** Die erste Brennerfahrt der Anlage dauerte laut Gerät vier Stunden
  und sechs Minuten, tatsächlich rund fünfzig Minuten: Ein warmer Kessel hält das Abgasrohr
  dauerhaft über der Ausschaltschwelle. Aus ist der Brenner jetzt auch, wenn das Abgas um 6 K
  unter den Höchstwert der Fahrt fällt; an ist er, wenn es über der Bezugslinie liegt und um
  6 K über den Tiefstwert gestiegen ist. Neue Einstellung `burner.swing_k`.
- **Kesselkreispumpe gegen den Speicher.** Verglichen wird der Kesselvorlauf mit der
  Speichertemperatur, ersatzweise mit dem Rücklauf. Bei stehender Pumpe gleichen sich Vor- und
  Rücklauf an, und ein Kessel mit Restwärme blieb stehen, obwohl der Speicher kälter war.
- **Kesselkreispumpe früher aus.** Die Schwellen gehen von 1,0/0,5 K auf 3,0/2,0 K. Mit 0,5 K lief
  die Pumpe nach dem Brennerende fünf Stunden weiter und hielt nur noch den Kessel auf
  Speichertemperatur; der Speicher hatte seinen Höchststand bei genau 2 K Abstand erreicht.
- **Nullpunkt des Füllstands am Brennerstart.** Läuft der Brenner an, nachdem der Speicher um
  mindestens 3 K gefallen ist, wird der Nullpunkt zu 40 Prozent an den Speicherwert
  herangeführt. Neue Einstellungen `buffer.leer_lernen`, `buffer.lern_drop_k`,
  `buffer.leer_epoch`. Leer- und Vollpunkt sind in der Oberfläche einstellbar.
- **Kalter Anlauf ist keine fertige Ladung.** „Geladen" verlangt zusätzlich einen Kesselvorlauf
  über `buffer.kessel_hot_c` (60 °C). Beim Anfahren aus dem kalten Kessel lagen Vor- und
  Rücklauf 400 s lang dicht beieinander, und die Ladung galt nach wenigen Minuten als fertig.
- **Rückströmung.** Steigt der Kesselrücklauf bei stehender Pumpe und ausgeschaltetem Brenner um
  mindestens 3 K, meldet das Gerät den Befund `backflow`. Beobachtet bei einer
  Warmwasserzapfung: 36,9 auf 46,3 °C bei 32 °C im Vorlauf.
- **Warmwasserzapfungen.** Ein Einbruch des Speichers um mindestens 2 K in 15 Minuten zählt als
  Zapfung, außerhalb von Ladungen. Mit eingetragenem Speicherinhalt auch in Kilowattstunden.
  Neue Einstellungen `buffer.zapf_drop_k`, `buffer.zapf_win_s`, `buffer.volumen_l`.
- **Alle Einstellungen in der Oberfläche.** Sechzehn von vierundvierzig Werten waren nur über die
  Schnittstelle erreichbar, darunter die gesamte Brennererkennung.
- **Tageswerte gesichert.** Während eines Brennerlaufs werden Laufzeit und Starts alle fünf
  Minuten gesichert. Ein Neustart mitten im Lauf verlor bisher die ganze Ladung aus der
  Tagesbilanz.
- **Aufzeichnung übersteht einen Neustart.** Eine scharf geschaltete Aufzeichnung bleibt es.
- **Verlauf bleibt erhalten**, wenn ein Messwert des Nachbargeräts ausfällt. Bisher begann er
  dann von vorn; nach acht Stunden standen vier Messpunkte statt zweihundertfünfzig.
- **Anlagenschema.** Heizkreise des Nachbargeräts erscheinen blass mit ihren Werten, die Pumpe
  ist mit ihrem Zustand beschriftet.
- **`POST /api/system/clear-logs`** verwirft Protokolle und Tageswerte, etwa nach einer Zeit mit
  falscher Erkennung.

### Behoben

- **Vorgaben der Kesselkreispumpe.** Die Einstellungsablage gab einem neu eingerichteten Gerät
  noch 1,0/0,5 K, während Rechenmodul und Prüfungen mit 3,0/2,0 K arbeiteten.
- **`burner.swing_k`** wurde nicht an die Brennererkennung übergeben; eine Änderung blieb
  wirkungslos.
- **Fühler über die Schnittstelle.** Eine Teilangabe, etwa nur Kennung und Rolle, setzte den
  Korrekturwert auf 0. Fehlende Felder behalten jetzt ihren Wert.
- **Fehlende Prüfungen.** Das Heizungsgerät nahm täglichen Neustart, Haltezeiten der
  Brennererkennung, Leer-, Voll- und Warnwert, „Vorlauf heiß", Haltezeit für „geladen" und das
  Zapfungsfenster ohne Prüfung an. Es gelten jetzt die Grenzen, die die Oberfläche schon anzeigte.
  Umgekehrt ließ die Oberfläche als Abfrageabstand der Verteiler bis 600 s zu, das Gerät nimmt
  höchstens 300 s.
- **Meldung zur Busbelegung.** Das Protokoll verlangte nach einer Änderung einen Neustart; die
  Fühlererfassung stellt aber sofort um.

### Werkzeuge und Dokumentation

- **`tools/check_defaults.py`** vergleicht Vorgabewerte, die an zwei Stellen stehen:
  Einstellungsablage und Rechenmodule des Heizungsgeräts, dazu die Werte, mit denen die
  Verteiler-Oberfläche einen neuen Raum anlegt.
- **Handbuch.** Jede Einstellung beider Gerätetypen mit Schlüssel, Vorgabe, zulässigem Bereich
  und Bedeutung; Befunde mit ihren Kennungen und Auslösebedingungen; Proportionalband richtig
  beschrieben (am Sollwert halb offen).
- **Attrappe des Verteilers** mit einem verschlüsselten Climate-Sat, der erst mit dem
  Testschlüssel der Prüfungen Werte liefert, samt `POST /api/ble/key` und Schlüsseln in der
  Sicherung.
- **Prüfungen:** 578 (v0.3.0: 483), darunter AES-128 nach FIPS-197 und BTHome-Rahmen aus dem
  Rahmenbau der Satelliten-Firmware.

## v0.3.0 — 17. August 2026

Zweite Fassung mit Heizungsteil: Kesselkreispumpe, Auswertung der Protokolle (Verbrauchslinie,
Abgas-Vorlauf-Abstand), Plausibilitätsprüfungen der Fühler, Sicherung und Wiederherstellung der
Einstellungen, Außentemperatur für die Heizungsgeräte. Fertige Abbilder und Web-Flasher.
[Versionshinweise](https://github.com/rubenmuehlhans/floor-heating-ctrl/releases/tag/v0.3.0)

## v0.2.0 — 16. August 2026

[Versionshinweise](https://github.com/rubenmuehlhans/floor-heating-ctrl/releases/tag/v0.2.0)
