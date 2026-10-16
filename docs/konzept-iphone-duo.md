# Konzept: Die App auf dem iPhone Duo

Das iPhone Duo ist Apples erstes faltbares iPhone, vorgestellt am 9. September 2026. Es hat ein
Außendisplay mit 5,4 Zoll und ein Innendisplay mit 7,6 Zoll und läuft mit iOS 27.1. Die App läuft
darauf ohne Anpassung, nutzt das Innendisplay aber wie ein verbreitertes iPhone: eine Spalte über
die volle Breite, ohne Rücksicht auf die Faltlinie. Das Papier legt fest, wie die App beide Displays
und die Haltungen des Geräts nutzt, welche Bausteine dafür nötig sind und in welchen Etappen sie
entstehen.

Die Regeln knüpfen an Größenklasse, verfügbare Breite und Faltbereich an, nicht an das Gerät. iPad
und Mac erhalten dieselben Anordnungen, sobald ihr Fenster breit genug ist.

Entwürfe mit Werten der Beispielanlage liegen unter [entwuerfe/iphone-duo](entwuerfe/iphone-duo/).
Stand: 16. Oktober 2026, geprüft mit Xcode 27.1 im Simulator iPhone Duo.

## Ausgangslage

### Das Gerät

| Display | Haltung | Punkte | Größenklasse Breite × Höhe | Leisten |
|---|---|---|---|---|
| außen | zugeklappt, hochkant | 466 × 678 | compact × regular | senkrecht am rechten Rand |
| außen | zugeklappt, quer oder als Zelt aufgestellt | 678 × 466 | compact × compact | senkrecht am Rand |
| innen | aufgeklappt, quer | 951 × 669 | regular × regular | senkrecht am rechten Rand |
| innen | aufgeklappt, hochkant | 669 × 951 | regular × regular | waagerecht |

Die Punktmaße ergeben sich aus den Bildspeichern des Simulators (1398 × 2034 und 2007 × 2853 Pixel
bei Faktor 3). Beide Displays haben fast dasselbe Seitenverhältnis, 1,45 außen und 1,42 innen, beides
nahe √2. Eine Hälfte des Innendisplays (475 × 669 pt) hat damit nahezu die Form des Außendisplays.

- **Leisten am Rand.** Tab-Leiste, Zurück und Werkzeugleiste stehen untereinander in einer
  Randspalte; die Tab-Leiste zeigt dort nur Symbole. Ausnahme ist das Innendisplay im Hochformat,
  dort bleiben die Leisten waagerecht. Die Statusanzeige ist ein rundes Element in der Ecke, auf dem
  Außendisplay unter der Kamera.
- **Reservierte Bereiche.** iOS 27.1 meldet die Faltlinie als Bereich der Art `division` und die
  Kameras als `occlusion`. Der Faltbereich ist nur aktiv, solange das Gerät angewinkelt ist; liegt
  es flach, hat er die Breite null.
- **Haltungen.** Zugeklappt; aufgeklappt und flach; angewinkelt wie ein Buch mit senkrechter
  Faltlinie; aufgestellt mit aufliegender unterer Hälfte (Innendisplay hochkant, Faltlinie
  waagerecht); zugeklappt quer, auch als Zelt aufgestellt.
- **Fenster.** Auf dem Innendisplay laufen zwei Apps oder zwei Fenster derselben App nebeneinander
  (Split View). Jede Hälfte hat dann ungefähr die Breite des Außendisplays. Das Außendisplay öffnet
  keine weiteren Fenster.
- **Vorgaben von Apple.** Anordnung nach Größenklasse und verfügbarem Platz, nicht nach Gerätetyp
  oder Ausrichtung; keine eigene Gestaltung je Haltung; der Zustand bleibt beim Wechsel zwischen den
  Displays erhalten. Bedienelemente und zusammengehörige Inhalte sollen im angewinkelten Zustand
  nicht über der Faltlinie liegen; fortlaufend scrollende Inhalte dürfen sie kreuzen.
- **Schnittstellen.** Im SDK von Xcode 27.1 vorhanden: `ArrangementView` mit den Stilen `.split`
  und `.overlay`, `GeometryProxy.reservedRegions(kind:options:)` und `toolbarVerticalBehavior(_:)`,
  alle ab iOS 27.1; `defaultTabBarPlacement(_:)` und `presentationPlacement(_:)` ab iOS 27.0. Die
  App setzt heute iOS 27.0 voraus.

### Wie die App heute darauf aussieht

Stand 0c9b5e0 (Build 14), gebaut mit Xcode 27.1, im Simulator iPhone Duo mit der Beispielanlage.
Aufnahmen: außen [Übersicht](entwuerfe/iphone-duo/ist-aussen-uebersicht.png) und
[Räume](entwuerfe/iphone-duo/ist-aussen-raeume.png), innen
[Übersicht](entwuerfe/iphone-duo/ist-innen-uebersicht.png) und
[Räume](entwuerfe/iphone-duo/ist-innen-raeume.png).

| Stelle | Beobachtung |
|---|---|
| Rahmen | Das System legt die fünf Tabs senkrecht an den rechten Rand und „Fragen“ unter die Statusanzeige. In der App ist dafür nichts zu ändern. |
| außen, Übersicht | Die Geräteleiste ist eine waagerechte Scroll-Ansicht mit `scrollClipDisabled()`. Die Marken „Keller“ und „Erdgeschoss“ laufen unter die Statusanzeige. |
| außen, Übersicht | Die Karte „Beispielanlage“ setzt Text und Schaltfläche nebeneinander. Der Text bricht in Zeilen von 10 bis 16 Zeichen um und trennt Wörter. Nutzbar sind rund 350 pt Breite, auf einem iPhone 18 Pro 370 pt. |
| außen, Räume | Der Verteilerbalken des Kellers ist breiter als seine Karte. Kanal 10 und 11 sind nur durch Wischen erreichbar; nichts weist darauf hin. |
| innen, Übersicht | Jede Seite steht einspaltig über 834 pt Breite. Karten und Text laufen über die Faltlinie; die Hinweise des Lageberichts erreichen 94 Zeichen je Zeile. |
| innen, Räume | Das Raster der Raumkarten (`GridItem(.adaptive(minimum: 320))`) teilt die Fläche links der Randspalte. Die Spaltengrenze liegt bei rund 434 pt, die Faltlinie bei 475 pt; sie verläuft durch die rechte Raumkarte. |
| Größenklasse | Zwei Stellen fragen sie ab: `Hauptansicht` (Assistent als Seitenbereich ab regular) und `Anlagenschema` (Querfassung ab regular). Die Querfassung ist für 780 pt entworfen; in einer Spalte von 443 pt, also 411 pt innerhalb der Karte, erschiene sie auf 53 % verkleinert. |
| Assistent | Ab regular ein Seitenbereich (`inspector`) von 340 bis 560 pt, vorgesehen 400 pt. Seine Kante richtet sich nicht nach der Faltlinie. |
| Navigation | Je Bereich ein `NavigationStack` ohne gebundenen Pfad. Zielseiten ersetzen die Seite über die volle Breite; Liste und Eintrag stehen nie nebeneinander. |

### Was daran fehlt

- Das Innendisplay zeigt nicht mehr Inhalt als das Außendisplay, nur breitere Karten.
- Im angewinkelten Zustand liegt die Faltlinie mitten in Karten, Texten und Bedienelementen.
- Die Haltungen bleiben ungenutzt. Aufgestellt am Kessel oder quer auf dem Tisch zeigt die App
  dieselbe Seite wie in der Hand.
- Auf dem Außendisplay gibt es zwei Darstellungsfehler: die Geräteleiste unter der Statusanzeige und
  die Karte „Beispielanlage“.

## Grundsätze

1. **Eine Spalte hat die Breite des Außendisplays.** Jede Karte wird für 350 bis 443 pt gebaut. Was
   außen passt, passt in jede Hälfte des Innendisplays und in jede Hälfte bei Split View.
2. **Zwei Spalten werden an der Faltlinie getrennt.** Die Grenze liegt auf der Faltlinie, nicht in
   der Mitte der Inhaltsfläche. Die rechte Spalte ist um die Randspalte schmaler: 371 statt 443 pt.
3. **Die Anordnung folgt dem Platz.** Entscheidungen fallen nach Größenklasse, verfügbarer Breite
   und Faltbereich. Die App fragt weder Gerätetyp noch Haltung ab. Ohne Faltlinie, etwa auf iPad und
   Mac, teilt die Mitte der Inhaltsfläche.
4. **Über der Faltlinie liegt nichts.** Keine Karte, kein Diagramm und kein Bedienelement, auch
   nicht im flachen Zustand. Beim Anwinkeln ändert sich die Anordnung deshalb nicht.
5. **Der Zustand bleibt erhalten.** Beim Auf- und Zuklappen bleiben Bereich, geöffnete Seite,
   Auswahl, Scrollstand und der Assistent mit angefangener Eingabe erhalten.
6. **Aufgestellt: Anzeige oben, Bedienung unten.** Bedienelemente liegen auf der aufliegenden
   Hälfte; die obere Hälfte zeigt Werte in einer Schriftgröße für Abstand.
7. **Die Leisten ordnet das System.** Tab-Leiste, Zurück und Werkzeugleiste bleiben Elemente des
   Systems. Einträge der Werkzeugleiste erhalten Symbole, weil Beschriftungen in senkrechten Leisten
   waagerecht bleiben und eine eigene Zeile belegen.

## Anordnungen

### Einspaltig

Gilt für das Außendisplay hochkant, für jede Hälfte bei Split View, für die Seite neben dem
geöffneten Assistenten und für das Innendisplay hochkant, solange es flach liegt. Die Seiten bleiben,
wie sie sind, mit drei Korrekturen:

- Die Geräteleiste bricht um, statt zu scrollen. Gruppenbezeichnung und Marken einer Gruppe bleiben
  zusammen.
- Die Karte „Beispielanlage“ stellt die Schaltfläche unter den Text, wenn beides nebeneinander nicht
  passt.
- Ist der Verteilerbalken breiter als seine Karte, zeigt ein Verlauf am rechten Rand, dass weitere
  Kanäle folgen.

Entwurf: [Zugeklappt](entwuerfe/iphone-duo/1-zugeklappt.svg).

### Doppelseite

Kartenseiten auf dem Innendisplay. Die Abschnitte einer Seite verteilen sich auf zwei Spalten, jede
Spalte scrollt für sich. Welche Abschnitte links und rechts stehen, legt jede Seite fest. Einspaltig
bleibt die heutige Reihenfolge.

| Seite | linke Spalte | rechte Spalte |
|---|---|---|
| Übersicht | Geräteleiste, Lagebericht, Wärmeerzeugung, Befunde | Räume |
| Raum | Messwert, Stellrad, Jetzt prüfen, Ausschalten, Kennzahlen | Ventile, Verlauf, Regelparameter |
| Heizung | Anlagenschema in der Hochfassung, Außen, Speicher, Brenner | Pumpen, Auswertung |

Die Seite Räume braucht keine Verteilung: Ihr Raster hat zwei Spalten fester Breite, links und
rechts der Faltlinie. Grids mit wechselnder Spaltenzahl (`adaptive`) erhalten zweispaltig je Spalte
ein eigenes Raster, damit keine Kachel über der Faltlinie liegt.

Entwürfe: [Übersicht](entwuerfe/iphone-duo/2-aufgeklappt-uebersicht.svg),
[Raum](entwuerfe/iphone-duo/3-aufgeklappt-raum.svg).

### Liste und Eintrag

Listenseiten: Befunde, Geräte, Lageberichte, Änderungsprotokoll. Auf dem Innendisplay steht links die
Liste, rechts der gewählte Eintrag. Ohne Auswahl zeigt die rechte Spalte den ersten Eintrag, bei
Befunden den schwersten.

### Navigation auf dem Innendisplay

1. Eine Kartenseite ersetzt beim Öffnen beide Spalten.
2. Eine Listenseite ersetzt beim Öffnen beide Spalten: Liste links, Eintrag rechts.
3. Formulare und Detailseiten, die eine Spalte füllen (Einstellungen, Regelparameter, Kanal,
   Systemseiten, Befund), öffnen sich in der rechten Spalte. Die linke Spalte bleibt stehen. Zurück
   schließt die rechte Seite und zeigt wieder, was vorher rechts stand.

Die rechte Spalte hat damit einen eigenen Stapel. Wird zum Beispiel aus dem Raum Büro der
Regelparameter geöffnet, bleiben Messwert und Stellrad links sichtbar.

### Assistent

Auf dem Innendisplay öffnet „Fragen“ oder „Nachfragen“ den Assistenten in der rechten Hälfte, von
der Faltlinie bis zur Randspalte. Die Seite rückt einspaltig in die linke Hälfte, der Hinweis, nach
dem gefragt wird, bleibt markiert. Verweise in Antworten, etwa „Im Verlauf zeigen“, öffnen ihr Ziel in
der linken Hälfte; das Gespräch bleibt stehen. Zugeklappt erscheint der Assistent wie heute als
Blatt.

Entwurf: [Assistent](entwuerfe/iphone-duo/4-assistent.svg).

### Verlauf

Diagramme stehen in der linken Spalte, Werte mit Linienart, Brenner und Ereignisse in der rechten.
Das gilt flach wie angewinkelt; ein Diagramm über die volle Breite müsste beim Anwinkeln die
Anordnung wechseln.

Entwurf: [Angewinkelt](entwuerfe/iphone-duo/5-angewinkelt-verlauf.svg).

### Aufgestellt

Innendisplay hochkant, Faltbereich aktiv und waagerecht. Die Seite teilt sich in Anzeige und
Bedienung:

| Seite | obere Hälfte: Anzeige | untere Hälfte: Bedienung |
|---|---|---|
| Heizung | Anlagenschema in einer Fassung für Abstand (Werte 21 pt statt 16 pt, Beschriftung 13 pt statt 10,5 pt) | Betriebsart der Pumpen, Ladung aufzeichnen, Verlauf |
| Raum | Messwert und Verlauf | Stellrad, Jetzt prüfen, Ausschalten |
| Verlauf | Diagramm | Zeitraum, Ereignisse |

Anwendungsfall ist die Arbeit an Kessel und Speicher: Das Gerät steht auf dem Kessel oder einem Regal,
Werte und Pumpen sind sichtbar, ohne es zu halten. Seiten ohne Eintrag in der Tabelle bleiben
einspaltig.

Entwurf: [Aufgestellt](entwuerfe/iphone-duo/6-aufgestellt.svg).

### Anzeigetafel

Außendisplay quer, auch als Zelt aufgestellt (compact × compact). Die Übersicht zeigt sechs Werte in
44 pt: Außen, Speicher, Brenner, Räume, Pumpen und Befunde, darüber den Zustand des Lageberichts.
Antippen öffnet die Seite zum Wert. Die übrigen Seiten bleiben im Querformat, wie sie sind. Die Regel
gilt für jedes iPhone, dessen Größenklassen im Querformat beide compact sind.

Entwurf: [Zelt](entwuerfe/iphone-duo/7-zelt.svg).

## Übergänge

| Anordnung | Zuklappen | Aufklappen |
|---|---|---|
| Doppelseite | Abschnitte in der heutigen Reihenfolge untereinander; oben steht der Abschnitt, in dem zuletzt gescrollt oder getippt wurde | Abschnitte kehren in ihre Spalten zurück; der zuletzt sichtbare Abschnitt steht oben in seiner Spalte |
| Liste und Eintrag | Eintrag wird oberste Seite, Zurück führt zur Liste | Eintrag kehrt in die rechte Spalte zurück |
| Seite in der rechten Spalte | wird oberste Seite über der Seite der linken Spalte | kehrt in die rechte Spalte zurück |
| Assistent | Blatt über der Seite; Gespräch, Bezug und Eingabe bleiben erhalten | Assistent wieder in der rechten Hälfte |
| Aufgestellt, Anzeigetafel | gewöhnliche Seite in der jeweiligen Anordnung | gewöhnliche Seite in der jeweiligen Anordnung |

Entwurf: [Auf- und Zuklappen](entwuerfe/iphone-duo/8-uebergaenge.svg).

## Umsetzung

### Spaltenmaß

Ein Umgebungswert, einmal in `Hauptansicht` aus der Geometrie des Fensters ermittelt:

| Feld | Inhalt |
|---|---|
| `spalten` | 1 oder 2; 2 ab Größenklasse regular, wenn links und rechts der Teilung je mindestens 350 pt Inhaltsbreite bleiben |
| `teilung` | Lage der Spaltengrenze: die Faltlinie aus `reservedRegions(kind: .division, options: .includeInactive)`, sonst die Mitte der Inhaltsfläche |
| `faltbereich` | Rahmen des aktiven Faltbereichs, sonst `nil` |
| `aufgestellt` | Faltbereich aktiv und breiter als hoch |

Die Faltlinie wird auch im flachen Zustand abgefragt (`includeInactive`), damit sich die Teilung beim
Anwinkeln nicht verschiebt. Alle übrigen Ansichten lesen nur das Spaltenmaß.

### Neue Bausteine

| Baustein | Zweck |
|---|---|
| `Doppelseite` | nimmt die Abschnitte einer Seite mit ihrer Spalte entgegen; einspaltig eine Scroll-Ansicht in der heutigen Reihenfolge, zweispaltig je Spalte eine Scroll-Ansicht links und rechts der Teilung |
| `ListeUndEintrag` | Liste links, Eintrag rechts; einspaltig wie heute über `NavigationLink` |
| Pfade je Bereich | ein `NavigationPath` für die linke und einer für die rechte Spalte je Bereich im `AppModell`; Voraussetzung, um beim Zuklappen die rechte Seite auf den Stapel zu legen und beim Aufklappen zurückzunehmen |
| `Aufstellung` | `ArrangementView` mit `.split.axes(.vertical)`, Anzeige als erster, Bedienung als zweiter Teil; nur im Zustand `aufgestellt` |
| `Anzeigetafel` | Übersicht bei compact × compact |

`ArrangementView` eignet sich nicht für die Doppelseite: Kann sie nicht teilen, zeigt sie nur ihren
ersten Teil. Die Doppelseite soll einspaltig alle Abschnitte untereinander zeigen.

### Änderungen an bestehenden Stellen

| Datei | Änderung |
|---|---|
| `Hauptansicht.swift` | Spaltenmaß ermitteln; Breite des Assistenten aus der Teilung, von der Faltlinie bis zur Randspalte |
| `UebersichtAnsicht.swift` | Abschnitte als Doppelseite; Geräteleiste mit dem vorhandenen `Fliesslayout` statt waagerechter Scroll-Ansicht; Karte „Beispielanlage“ mit `ViewThatFits`; Anzeigetafel |
| `RaeumeAnsicht.swift` | Raster mit zwei festen Spalten an der Teilung; Raum als Doppelseite; Hinweis am Verteilerbalken |
| `HeizungAnsicht.swift`, `Anlagenschema.swift` | Fassung nach verfügbarer Breite statt nach Größenklasse, quer ab 600 pt; Fassung für Abstand |
| `VerlaufAnsichten.swift`, `Verlaufstafeln.swift` | Diagramm links, Werte und Ereignisse rechts |
| `AssistentAnsicht.swift` | Verweise öffnen ihr Ziel in der linken Hälfte, solange der Assistent rechts steht |
| `Navigation.swift` | Ziele nach Art einordnen (Kartenseite, Listenseite, Seite für eine Spalte) und entsprechend öffnen |
| Werkzeugleisten | Einträge mit Symbol, etwa `Button("Fertig", systemImage: "checkmark")` |
| `project.yml` | Mindestversion iOS 27.1, siehe offene Entscheidungen |

### Prüfung

- Simulator iPhone Duo in Xcode 27.1, Haltungen über die Bedienelemente von Device Hub. Aufnahmen
  beider Displays mit `xcrun simctl io <Gerät> screenshot --display=<Port>`; die Ports nennt
  `xcrun simctl io <Gerät> enumerate`. Über `simctl` lässt sich die Haltung nicht wechseln. Das
  Abschalten des Innendisplays mit `screenConfig … power off` führte am 16. Oktober zu wiederholten
  Abstürzen von SpringBoard und ersetzt den Wechsel nicht.
- Vorschauen in Xcode für 350 pt, 443 pt, 951 × 669 pt und 669 × 951 pt.
- Am Gerät: Ablesbarkeit von Anzeigetafel und Schema aus zwei Metern, Treffsicherheit an der
  Faltlinie, Wechsel der Haltungen. Ohne Gerät bleiben diese Punkte offen.

## Etappen

### Etappe 1: Außendisplay

Geräteleiste mit Umbruch, Karte „Beispielanlage“ mit `ViewThatFits`, Hinweis am Verteilerbalken.
Die Änderungen gelten für jedes iPhone.

**Prüfschritte:** Aufnahme außen ohne Überdeckung der Statusanzeige. „Beispielanlage“ ohne getrennte
Wörter. Dieselben Seiten auf iPhone 18 Pro und iPhone 17e ohne Überlauf.

### Etappe 2: Spaltenmaß und Doppelseite

Spaltenmaß in `Hauptansicht`, Doppelseite für Übersicht, Raum und Heizung, Raster der Räume an der
Teilung, Anlagenschema nach Breite.

**Prüfschritte:** Aufnahmen innen entsprechen den Entwürfen 2 und 3. Keine Karte reicht über
x = 475 pt. Mac-Fenster und iPad zweispaltig, sobald beide Spalten 350 pt erreichen, darunter
einspaltig.

### Etappe 3: Navigation und Zustand

Pfade je Bereich und Spalte im Modell, `ListeUndEintrag` für Befunde, Geräte, Lageberichte und
Änderungsprotokoll, Seiten für eine Spalte in der rechten Spalte.

**Prüfschritte:** In jedem Bereich eine Seite öffnen, zuklappen, aufklappen: Seite, Auswahl und
Scrollstand stimmen überein. Zugeklappt führt Zurück zur Liste bzw. zur Seite der linken Spalte.

### Etappe 4: Assistent

Assistent in der rechten Hälfte, Verweise öffnen links.

**Prüfschritte:** Linke Kante des Assistenten auf der Faltlinie. „Im Verlauf zeigen“ öffnet den
Verlauf links. Zuklappen mit angefangener Eingabe: Eingabe und Bezug bleiben erhalten.

### Etappe 5: Faltbereich und Verlauf

Abfrage der Faltlinie über `reservedRegions`, Verlauf zweispaltig.

**Prüfschritte:** Angewinkelt in Device Hub liegt kein Bedienelement im Faltbereich. Flach und
angewinkelt ist die Anordnung gleich.

### Etappe 6: Aufgestellt

Heizung, Raum und Verlauf mit Anzeige oben und Bedienung unten; Schema in der Fassung für Abstand.

**Prüfschritte:** Aufnahme entspricht Entwurf 6. Wechsel zwischen flach und aufgestellt ohne
Verlust der Auswahl. Die Betriebsart einer Pumpe lässt sich aufgestellt umschalten.

### Etappe 7: Anzeigetafel

Übersicht im Querformat bei compact × compact.

**Prüfschritte:** Aufnahme entspricht Entwurf 7, im hellen und im dunklen Erscheinungsbild.
Antippen öffnet die Seite zum Wert. Zurück im Hochformat steht die vorherige Seite.

## Offene Entscheidungen

1. Mindestversion iOS 27.1 (Vorschlag) oder iOS 27.0 mit Verfügbarkeitsprüfung an jeder neuen
   Schnittstelle. Das Duo wird mit iOS 27.1 ausgeliefert; die App wird bisher nur über TestFlight
   verteilt.
2. Seitenleiste statt Tab-Leiste auf dem Innendisplay (`defaultTabBarPlacement(.sidebar)`):
   Vorschlag nein. Die Seitenleiste belegt links eine eigene Spalte, auf dem iPad 320 pt; links der
   Faltlinie bliebe für die linke Inhaltsspalte zu wenig Breite.
3. Anzeigetafel im Querformat (Vorschlag) oder die Übersicht im Querformat unverändert.
4. Zweites Fenster für den Verlauf auf dem Innendisplay, wie auf dem Mac: Vorschlag nach Etappe 5.
5. Rechte Spalte ohne Auswahl bei Listen: erster Eintrag (Vorschlag) oder ein leerer Hinweis.

## Risiken

| Risiko | Folge | Gegenmaßnahme |
|---|---|---|
| Kein iPhone Duo zur Prüfung | Ablesbarkeit, Treffsicherheit an der Faltlinie und Wechsel der Haltungen nur im Simulator geprüft | Prüfung am Gerät vor der Verteilung über TestFlight |
| Schnittstellen aus iOS 27.1 ändern sich | Nacharbeit | Abfrage der Faltlinie nur im Spaltenmaß; ohne Faltlinie teilt die Mitte |
| Zwei Scroll-Ansichten je Seite | Der große Titel klappt nur mit einer Scroll-Ansicht ein; Scrollstand je Spalte | Titel über der linken Spalte, Prüfung in Etappe 2 |
| Mehr Anordnungen je Seite | Prüfaufwand steigt | Regeln statt Einzelgestaltung; Vorschauen für vier Größen |
| Mac und iPad ändern sich mit | ungewollte Änderungen an bestehenden Fenstern | Teilung erst ab zwei vollen Spalten; Aufnahmen vorher und nachher |

## Was bewusst nicht vorgesehen ist

- **Keine Gestaltung je Haltung.** Die Anordnung ergibt sich aus Größenklasse, Breite und
  Faltbereich.
- **Kein Inhalt über der Faltlinie,** auch nicht im flachen Zustand.
- **Keine Anzeige auf dem Außendisplay bei aufgeklapptem Gerät.** Es zeigt dann von der Person weg.
- **Keine eigene Fassung der App für das Duo.**
- **Keine Widgets und kein StandBy.** Beides braucht eine Erweiterung mit gemeinsamer Datenablage
  und ist ein eigenes Vorhaben.
- **Keine Seitenleiste auf dem Innendisplay**, siehe offene Entscheidung 2.

## Quellen

- Apple: [Apple unveils iPhone Duo](https://www.apple.com/newsroom/2026/09/apple-unveils-iphone-duo/),
  9. September 2026
- Apple Developer, Tech Talk [Prepare your app for iPhone Duo](https://developer.apple.com/videos/play/tech-talks/111461/)
- Apple Developer, Tech Talk [Strike a pose with adaptive layouts on iPhone Duo](https://developer.apple.com/videos/play/tech-talks/111463/)
- MacRumors: [Apple Details How iOS 27 Adapts to iPhone Duo's Displays](https://www.macrumors.com/2026/09/10/apple-details-how-ios-27-adapts-to-iphone-duo/),
  10. September 2026
- Blake Crosley: [iPhone Duo for Developers](https://blakecrosley.com/blog/iphone-duo-for-developers),
  Displaygrößen und Seitenverhältnisse
- Messungen im Simulator iPhone Duo, Xcode 27.1, 16. Oktober 2026
