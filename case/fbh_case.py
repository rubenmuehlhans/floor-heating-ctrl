# ============================================================================
# Gehaeuse fuer die Ventilsteuerung Fussbodenheizung  —  parametrisch, CadQuery
#
# Koordinaten: Ursprung = linke untere Platinenecke in der Draufsicht,
# Z = 0 = Gehaeuse-Aussenboden. XY-Masse stammen aus den Fertigungsdaten in
# ../board/ und sind mit `reference/platine_auslesen.py` nachvollziehbar.
#
# ⭐ Z-HOEHEN: am 2026-08-16 am Board GEMESSEN sind RJ11 (Bauhoehe und Lage
#      der Steckoeffnung), HLK-5M03, ESP32 samt Buchsenleiste und der
#      Loetstellenueberstand unter der Platine. Sie sind mit ⭐ GEMESSEN
#      gekennzeichnet. [SCHAETZUNG] steht noch an der Klemme 220, den
#      1x2-Buchsenleisten und allen Displaymassen — dafuer gibt es die
#      Pruefstuecke (PLATTE=proben).
#
# ⚠️⚠️⚠️ NETZSPANNUNG. Auf der Platine sitzen ein HLK-5M03 (230 V AC/DC) und
#      eine Netzklemme. Die gefraesten Trennschlitze der Platine trennen den
#      Netzbereich ab: X >= 113,8 und Y >= 64,2. In diesem Bereich hat das
#      Gehaeuse KEINE Lueftungsschlitze, keine Displayaufnahme und keine
#      Bohrung ausser der Kabelverschraubung fuer die Netzzuleitung.
#      PETG ist nicht flammwidrig (UL94 HB). Siehe README, Abschnitt Sicherheit.
#
# Besonderheiten gegenueber den Gehaeusen in smartCamper/hardware/case:
#  - Die Platine fuellt die Kavitaet vollstaendig aus. Ecksaeulen INNEN gibt es
#    deshalb nicht (jede Lage kollidiert mit RJ11-Buchse, DevKit oder Netzteil);
#    die Deckelschrauben sitzen in sechs ANGEFORMTEN AUSSENOHREN. Das laesst die
#    Zentrierlippe ausserdem ringsum ungeteilt.
#  - Vier Wandlaschen sind an vier dieser Ohren angeformt; der Hutschienen-Clip
#    schraubt sich auf zwei davon (Zubehoerplatte).
#  - Display und Tasten sitzen NICHT auf der Platine. Das Modul haengt im
#    Deckel, die drei kapazitiven Tasten sind Membranfelder im Deckel.
# ============================================================================
import os
import sys

import cadquery as cq
from cadquery import exporters

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import cs_logo

# ---------------------------------------------------------------- Platine ---
# Aus Gerber (Umriss, Bohrungen) und EasyEDA-JSON (Bauteillage). Verlaesslich.
PCB_W, PCB_L, PCB_T = 150.0, 92.0, 1.6
PCB_R = 3.0
HOLES = [(3.414, 53.200), (62.342, 87.236), (143.876, 87.490), (145.654, 46.850)]

# RJ11-Buchsen 4P4C, 90°, 10,0 mm breit laut Bestueckungsdruck. Die
# Steckoeffnung zeigt nach SUEDEN.
RJ_X = [8.367, 21.702, 35.291, 48.880, 62.342, 75.804,
        89.266, 102.601, 115.936, 129.398, 142.987]
# ⭐ GEMESSEN: Bautiefe 14,0 mm. Der Bestueckungsdruck zeichnet 13,0 — er ist
#    eine Zeichnung, kein Datenblatt.
# ⚠️⚠️ Die gemessene TIEFE sagt nicht, wohin der eine Millimeter geht. Fest
#    liegen nur die Rastzapfen: ihre NPTH-Bohrungen sitzen bei Y = 7,095, und
#    der Footprint setzt sie 6,0 mm hinter die Front. Bleibt es dabei, steht
#    die Front weiterhin bei Y = 1,10 und der Millimeter waechst nach HINTEN,
#    in freies Platinengebiet.
#    Diese Annahme ist die sichere: waere die Front in Wirklichkeit weiter
#    vorn, wird der Steckertunnel nur KUERZER als gerechnet. Umgekehrt — Front
#    nach vorn angenommen und die Wand nachgezogen — koennte die Wand gegen die
#    Buchse druecken. Nachzumessen ist der Abstand Platinenkante -> Buchsenfront;
#    solange er nicht vorliegt, bleibt RJ_FRONT_Y auf dem Wert aus dem Druck.
RJ_FRONT_Y, RJ_BODY_W = 1.10, 10.0
RJ_DEPTH = float(os.environ.get("RJ_DEPTH", "14.0"))
# ⭐ GEMESSEN am Board (2026-08-16), nicht mehr geschaetzt: 16,10 mm ab
#    Platinenunterseite, also 14,50 ueber der Platinenoberkante.
RJ_H = float(os.environ.get("RJ_H", "14.5"))
# ⭐ Steckoeffnung ebenfalls gemessen: Unterkante 2,0 / Oberkante 14,6 ab
#    Platinenunterseite, also 0,4 … 13,0 ueber der Platine.
# ⚠️ Das sind 12,6 mm lichte Hoehe — deutlich mehr, als ein 4P4C-Stecker mit
#    Rastnase braucht (~9,5). Gemessen wurde damit sehr wahrscheinlich die
#    aeussere FRONTMULDE der Buchse, nicht der eigentliche Steckerschlitz. Das
#    ist die sichere Richtung: die Buchse begrenzt selbst, was durchpasst, eine
#    Wandoeffnung in Muldengroesse kann also nicht zu klein sein. Sie kostet
#    aber Material zwischen den elf Loechern — wird der Schlitz nachgemessen,
#    gehoert RJ_OPEN_Z1 nach unten.
RJ_OPEN_W = float(os.environ.get("RJ_OPEN_W", "9.8"))
RJ_OPEN_Z0 = float(os.environ.get("RJ_OPEN_Z0", "0.2"))   # ueber Platinenoberkante
RJ_OPEN_Z1 = float(os.environ.get("RJ_OPEN_Z1", "13.2"))

ONEWIRE_RJ = (75.677, 67.423)          # 1-Wire-Buchse, MITTEN auf der Platine
ONEWIRE_FRONT_Y = 57.85                # Front zeigt nach Sueden, nicht erreichbar

# ESP32-DevKitC V4 auf Buchsenleisten. Pinreihen Y = 61,455 / 86,855 (1 Zoll),
# Pin 1 = 3V3 bei X = 8,24 → das WROOM-Modul liegt WESTLICH, die Micro-USB-
# Buchse OESTLICH bei X ~ 59. Sie liegt damit 91 mm von jeder Aussenkante
# entfernt und ist nur bei offenem Deckel erreichbar (Firmware laeuft per OTA).
# ⚠️ Der Footprint zeichnet 50,8 x 30,48 — das reale Board misst 54,4 x 27,9.
#    Fuer das Gehaeuse zaehlt das reale Board.
ESP = (31.100, 74.155, 54.4, 27.9)     # cx, cy, L, B
# ⭐ GEMESSEN: 17,46 ab Platinenunterseite, also 15,86 ueber der Platine.
# ⚠️ Gemessen ist nur die SUMME. Die Aufteilung ist gerechnet: der Aufbau des
#    DevKit ueber seiner eigenen Platine ist bekannt (1,6 Platine + 3,1 WROOM),
#    der Rest faellt auf die Buchsenleiste — 11,16 mm, also eine hohe Bauform,
#    keine 8,5er. Fuer die Bauhoehe zaehlt ohnehin nur die Summe; die
#    Aufteilung braucht allein der Platinen-Dummy, um den Raum ZWISCHEN den
#    Leisten wieder freizuschneiden.
ESP_ABOVE = float(os.environ.get("ESP_ABOVE", "4.7"))
ESP_SOCKET = float(os.environ.get("ESP_SOCKET", "11.16"))

HLK = (105.268, 75.045, 34.2, 20.2)    # HLK-5M03, Datenblattmass
# ⭐⭐ GEMESSEN: 20,0 ab Platinenunterseite, also 18,40 ueber der Platine.
# ⚠️ Das Datenblatt nennt 15,0 Bauhoehe. Die 3,4 mm Differenz heissen, dass das
#    Modul nicht bis zum Anschlag durchgesteckt sitzt. Gebaut wird nach dem
#    GEMESSENEN Wert — wird das Modul spaeter nachgesetzt, ist das Gehaeuse zu
#    hoch, nie zu niedrig. Dieses Mass bestimmt TOP_CLEAR und damit die
#    gesamte Bauhoehe.
HLK_H = float(os.environ.get("HLK_H", "18.4"))            # hoechster Aufbau
KF250 = (137.526, 74.916, 12.0, 12.1)  # Netzklemme, Bestueckungsdruck
KF250_H = float(os.environ.get("KF250_H", "10.5"))        # [SCHAETZUNG]
KF250_WIRE_X = 142.23                  # Leitungseinfuehrung zeigt nach OSTEN
HDR_XY = [(15.000, 3.064), (28.433, 3.191), (42.149, 3.191), (55.484, 3.064),
          (68.946, 3.318), (82.535, 3.318), (96.251, 3.445), (109.205, 3.318),
          (122.667, 3.445), (136.256, 3.318), (136.256, 11.065)]
HDR_H = float(os.environ.get("HDR_H", "8.5"))             # [SCHAETZUNG] Buchsenleiste 1x2
SH10 = [(20.305, 53.835), (30.084, 54.000), (45.959, 54.000)]   # I2C-2, I2C-1, 1-Wire
SH10_H = 3.0
HDC1080 = (3.096, 45.199)              # Klimafuehler auf thermisch getrennter Insel

# Netzbereich der Platine (aus den gefraesten Trennschlitzen). Hier NICHTS
# hineinbauen ausser der Kabelverschraubung.
NETZ_X0, NETZ_Y0 = 113.8, 64.2

# -------------------------------------------------------------- Parameter ---
# ⭐ Wand und Boden von 2,4 auf 2,0 (Vorgabe, ueber die Umgebung aenderbar).
#    Zwei Gruende, und Material ist der schwaechere davon: der STECKERTUNNEL
#    vor den RJ11-Buchsen ist WALL + CLR + 1,10 und schrumpft damit von 4,0 auf
#    3,6 mm. Je kuerzer er ist, desto sicherer liegt die Rastnase des Steckers
#    ausserhalb und laesst sich druecken.
# ⚠️ Weiter herunter geht es, aber nicht beliebig — drei Grenzen:
#    - Das AUSSENOHR ueberlappt die Wand um WALL - 0,4. Unter etwa 0,9 wird
#      daraus eine Beruehrung, und tangentiale Beruehrung heisst kaputtes Netz
#      (die 24 offenen Kanten der runden Ohren waren genau das). Ab WALL = 0,4
#      ist es exakt tangential. Der Riegel unten meldet das.
#    - Die Suedwand ist zwischen den elf Oeffnungen nur 3,53 mm breit; sie
#      traegt als Leiter mit zwei durchgehenden Holmen, aber duenn wird sie
#      weich.
#    - Die Kabelverschraubung M12 will 1 … 4 mm Wand. Unter 1,5 sitzt die
#      Mutter nicht mehr sauber.
# ⚠️⚠️ LID_T bleibt bei 3,0 und ist NICHT verhandelbar: ueber der 2,0 mm tiefen
#    Glastasche des Displays blieben sonst weniger als 1,0 mm stehen. Das ist
#    die engste Stelle des Deckels, nicht TOP_R.
WALL = float(os.environ.get("WALL", "2.0"))
BOTTOM = float(os.environ.get("BOTTOM", "2.0"))
LID_T = 3.0
TOP_R, CORNER_R = 1.5, 4.0
CLR = 0.5                              # Luft rings um die Platine
# ⭐ GEMESSEN: die laengsten Loetstellen stehen 3,0 mm unter der Platine vor.
#    3,6 laesst darunter 0,6 mm Luft zum Boden.
BOT_CLEAR = 3.6
# ⚠️⚠️ TOP_CLEAR folgt dem NETZTEIL, nicht dem Display. Solange der HLK mit
#    15,1 geschaetzt war, war das Displaymodul das hoechste Teil im Gehaeuse
#    (Glas 2,0 + Platine 1,6 + Halterahmen 2,5 + Stecker ~8) und 18,0 reichte.
#    Gemessen sind es 18,4 — damit ueberholt das Netzteil das Display, und
#    18,0 waere eine Kollision gewesen. 21,0 laesst ueber dem HLK 2,6 mm.
#    ⚠️ Wer hier wieder herunterdreht, muss BEIDE Ketten nachrechnen; welche
#    fuehrt, hat sich mit einer einzigen Messung umgedreht.
TOP_CLEAR = float(os.environ.get("TOP_CLEAR", "21.0"))
LIP_H, LIP_W, LIP_CLR = 2.0, 1.5, 0.15

# Deckelschrauben M3 in AUSSENOHREN — siehe Kopfkommentar.
EAR_D, EAR_PILOT = 7.0, 2.5
# ⚠️⚠️ SENKUNG, nicht Senkbohrung. Eine zylindrische Ø6,2-Tasche von 1,8 mm
#    war die EINZIGE Ueberhangstelle des Deckels: er wird mit der Oberseite
#    aufs Bett gedruckt, die Tasche liegt also in den ersten Lagen und
#    schliesst sich erst bei 1,8 mm ueber einem Ø3,3-Loch — sechsmal ein
#    freitragender Ring von 1,45 mm. Ein 90°-Kegel laeuft stattdessen mit 45°
#    aus dem Bett heraus und traegt sich selbst.
# ⚠️ Damit gehoert eine SENKKOPFschraube M3 hinein (DIN 7991 / ISO 10642),
#    keine Zylinderkopf. Der Kopf sitzt dann buendig mit der Deckeloberseite.
LID_SCREW_C = 3.3
LID_CSK_D = 6.4                        # Kopf Ø6,0 + 0,4 Luft
LID_CSK_H = (LID_CSK_D - LID_SCREW_C) / 2      # 90° -> Tiefe = halbe Differenz
# Wandlaschen an den vier seitlichen Ohren
TAB, TAB_T, TAB_HOLE = 13.0, 3.2, 4.5

# ---------------------------------------------- Platinenbefestigung --------
# Drei Varianten, ueber die Umgebung waehlbar:
#     PCB_HALT=schrauben   (Vorgabe)  4 x M2,5 selbstschneidend in Dome
#     PCB_HALT=schnapper              Schnapp-Dome DURCH die vorhandenen Ø2,7
#     PCB_HALT=randclips              Federzungen an der Platinenkante
# ⭐ Alle drei haben dieselben Aussenmasse und denselben Deckel. Wer die Wahl
#    spaeter revidiert, druckt nur die Unterschale neu.
#
# ⚠️⚠️ Die Federwege sind GERECHNET, nicht geraten. Randdehnung einer
#    eingespannten Biegefeder: eps = 3*t*delta / (2*L^2). PETG vertraegt
#    dauerhaft etwa 2 … 3 %; darueber bleibt die Feder verformt oder reisst.
#      Schnapp-Dom   t 0,85  delta 0,30  L 4,9  ->  1,6 %
#      Federzunge    t 1,10  delta 0,60  L 6,8  ->  2,1 %
#    ⚠️ Genau diese Rechnung setzt der Zungendicke die Grenze: mit 1,4 mm und
#    0,9 mm Uebergriff waeren es 4,1 % — die Zunge bliebe nach dem ersten
#    Einclipsen offen stehen. Wer mehr Uebergriff will, braucht eine LAENGERE
#    Feder, also BOT_CLEAR 6,0 statt 3,6 und damit ein 2,4 mm hoeheres Gehaeuse.
PCB_HALT = os.environ.get("PCB_HALT", "schrauben")
if PCB_HALT not in ("schrauben", "schnapper", "randclips"):
    raise SystemExit(f"PCB_HALT={PCB_HALT} unbekannt — "
                     "schrauben | schnapper | randclips")

BOSS_D, BOSS_PILOT = 6.0, 2.05         # Platinendome, M2,5 selbstschneidend

# Schnapp-Dom: Schaft durch die Bohrung, Widerhaken darueber, Laengsschlitz
# bis tief in den Dom hinein — der Schlitz IST die Federlaenge.
SNAP_D = float(os.environ.get("SNAP_D", "2.5"))        # Bohrung 2,7
SNAP_SLOT = float(os.environ.get("SNAP_SLOT", "0.8"))
SNAP_BARB_D = float(os.environ.get("SNAP_BARB_D", "3.3"))
SNAP_BARB_H = 0.5                      # gerade Unterseite, haelt die Platine
SNAP_LEAD = 1.6                        # Einfuehrkegel darueber

# Federzungen an der Kante. Sie stehen auf dem Boden und weichen in eine Tasche
# aus, die von aussen durch eine oertliche Wandverdickung gedeckt bleibt.
CLIP_POS = [("W", 25.0), ("W", 70.0), ("E", 25.0), ("E", 58.0)]
# ⚠️ 1,0 statt 1,1 mm Zungendicke: `sonden.py` rechnet die Federlaenge bis zur
#    HAKENoberkante (6,2 mm) und nicht bis zur Platinenoberkante, wie meine
#    Ueberschlagsrechnung — damit standen 2,58 % statt 2,14 % im Protokoll. Die
#    duennere Zunge bringt es auf 2,34 %. Die Sonde hat recht: belastet wird die
#    Zunge dort, wo die Platine am Haken vorbeilaeuft.
CLIP_W, CLIP_T = 8.0, 1.0              # Breite laengs der Wand, Dicke
# ⚠️ CLIP_GAP begrenzt das Spiel der Platine ZWISCHEN den Zungen. Bei 0,15 je
#    Seite bleiben vom 0,6er Uebergriff im unguenstigsten Fall nur 0,3 mm
#    Eingriff — die Platine liegt ja an einer Seite an. 0,10 macht daraus 0,4.
CLIP_GAP = 0.10                        # Luft zur Platinenkante
CLIP_OVER, CLIP_HOOK_H, CLIP_LEAD = 0.6, 1.0, 1.2
CLIP_FLEX, CLIP_BULGE = 1.2, 3.0

# Displaymodul (Waveshare 1,5" OLED, SSD1327, 128 x 128).
# ⚠️ Modulplatine und Bohrbild sind NICHT aus einem Datenblatt geprueft.
#    Das Modul wird deshalb NICHT ueber sein Bohrbild gehalten, sondern von
#    einem Halterahmen gegen die Deckelunterseite geklemmt — dann zaehlt nur
#    der Umriss, und der ist leicht nachzumessen.
# ⚠️ DSP_CY ist nach Sueden gerueckt (39,25 -> 37,0). Der noerdliche Dom
#    schiebt den Halterahmen 4 mm ueber sich hinaus, und der lief bei
#    DSP_CY = 39,25 in das Netzteil (Y ab 64,9, Oberkante 26,0 — der Rahmen
#    liegt bei 24,5 … 27,0). Nach Sueden ist Platz: dort stehen nur die
#    RJ11-Buchsen, und die enden 2,4 mm unter dem Rahmen.
# ⚠️ Die Variante ohne Display schaltet ueber die Umgebung, nicht ueber den
#    Quelltext:  DISPLAY_MODUL=0 .venv/bin/python fbh_case.py
#    Der Name ist bewusst NICHT `DISPLAY` — die Variable ist unter X11 belegt
#    und haette die Variante je nach Umgebung von selbst umgeschaltet.
# ⭐ Beide Deckel passen auf DIESELBE Unterschale: Umriss, Lippe, Schrauben und
#    Bauhoehe sind unveraendert. TOP_CLEAR folgt dem Netzteil (18,4), nicht dem
#    Display — der Deckel ohne Display macht das Gehaeuse also nicht flacher.
MIT_DISPLAY = os.environ.get("DISPLAY_MODUL", "1") != "0"
_SFX_HALT = "" if os.environ.get("PCB_HALT", "schrauben") == "schrauben" \
            else "_" + os.environ.get("PCB_HALT")
FILE_SFX = ("" if MIT_DISPLAY else "_ohne_display") + _SFX_HALT
DSP_CX, DSP_CY = 110.0, 37.0
DSP_W = float(os.environ.get("DSP_W", "42.0"))
DSP_H = float(os.environ.get("DSP_H", "37.5"))
DSP_PCB_T = float(os.environ.get("DSP_PCB_T", "1.6"))
DSP_GLASS_W = float(os.environ.get("DSP_GLASS_W", "30.5"))   # [SCHAETZUNG]
DSP_GLASS_H = float(os.environ.get("DSP_GLASS_H", "33.5"))   # [SCHAETZUNG]
DSP_GLASS_D = float(os.environ.get("DSP_GLASS_D", "2.0"))    # [SCHAETZUNG] Glas ueber Modulplatine
DSP_WIN = 28.6                         # Sichtfenster (Aktivflaeche 26,86 + Rand)
# ⚠️⚠️ Die vier Dome muessen in XY VOLLSTAENDIG neben dem Modulumriss liegen.
#    Ihre Unterseite liegt in der Ebene der Modulrueckseite — dort haelt sie
#    den Halterahmen —, jede Ueberlappung ist also ein Dom IM Modul. Die
#    beiden mittleren standen zuerst bei Y = 18,5 und 60,0 und ragten damit je
#    1,0 mm in den Umriss (Modul Y 20,5 … 58,0). Gemeldet hat das erst die
#    Pruefung "Modulplatine gegen Deckel" in `sonden.py`; gegen Platine und
#    Schale allein war alles gruen.
# ⚠️ Nach Sueden ist der Weg begrenzt: bei Y = 17,0 bleiben zum Ruecken der
#    RJ11-Buchsenreihe (Y = 15,10 bei 14,0 mm Bautiefe) 1,4 mm. Nach Norden
#    begrenzt das Netzteil (Y ab 64,9).
DSP_BOSS = [(85.0, 37.0), (135.0, 37.0), (110.0, 14.5), (110.0, 59.5)]
DSP_BOSS_D, DSP_BOSS_PILOT = 6.0, 2.05
DSP_FRAME_T = 2.5

# Kapazitive Tasten: ESP32-eigene Touch-Pins (GPIO 4 / 2 / 15). Auf der Platine
# unbeschaltet — die Leitungen gehen direkt an die Buchsenleiste des DevKit.
# Im Deckel drei Membranfelder; die Sensorflaeche (Kupferscheibe oder
# Alu-Klebeband) wird von innen dagegen gesetzt.
TOUCH = [(25.0, 37.0), (47.0, 37.0), (69.0, 37.0)]
TOUCH_D, TOUCH_MEMBRANE = 18.0, 1.0

# Kabeldurchfuehrungen, beide in der NORDwand
OW_X, OW_D = 76.0, 7.0                 # 1-Wire, Vorlauffuehler
OW_RIB_GAP, OW_RIB_W = 3.8, 3.0        # Zugentlastung: Klemmschlitz aus zwei Rippen
NETZ_X, NETZ_GLAND_D = 137.0, 12.2     # Netzzuleitung, M12x1,5-Verschraubung

VENTS = True
LOGO = True
LOGO_D = float(os.environ.get("LOGO_D", "30.0"))
LOGO_CX, LOGO_CY = 40.0, 76.0
LOGO_DEPTH = 0.6                       # Taschentiefe = 3 Lagen a 0,2 mm
LOGO_SEPARAT = os.environ.get("LOGO_SEPARAT", "1") != "0"
LOGO_GAP = 8.0

DIN_CLIP = True                        # Hutschienen-Clip TS35 (Zubehoerplatte)

# ------------------------------------------------------------- abgeleitet ---
PCB_BOT = BOTTOM + BOT_CLEAR
PCB_TOP = PCB_BOT + PCB_T
Z_WALL = PCB_TOP + TOP_CLEAR
Z_TOP = Z_WALL + LID_T

cav_x0, cav_y0 = -CLR, -CLR
cav_x1, cav_y1 = PCB_W + CLR, PCB_L + CLR
out_x0, out_y0 = cav_x0 - WALL, cav_y0 - WALL
out_x1, out_y1 = cav_x1 + WALL, cav_y1 + WALL

# ⚠️⚠️ Die Ohren sind RECHTECKIG, nicht rund — und das ist kein Geschmack.
#    Ein runder Zapfen, dessen Mitte ausserhalb der Wandflucht liegt, schneidet
#    die gerade Wand in einem sehr flachen Winkel. Der 1,5-mm-Kantenradius des
#    Deckels laeuft dort in eine fast entartete Ecke: der erste Bau lieferte an
#    genau diesen zwoelf Stellen 24 offene Kanten im tesselierten Netz
#    (`check_case.py`, Punkt 2) — gueltiger Volumenkoerper, kaputtes STL.
#    Das Rechteck trifft die Wand mit zwei sauberen 90°-Ecken.
# ⚠️ Das innere Ende liegt 0,4 mm VOR der Kavitaetswand, also noch im
#    Wandmaterial: das Ohr ueberlappt die Wand, statt sie zu kuessen, und ragt
#    trotzdem nicht in die Kavitaet (dort steht die Platine).
EAR_W, EAR_L = 8.0, 7.5                # Breite laengs der Wand, Auskragung
SIDE_EAR_Y = (12.0, 80.0)              # Ohren mit Wandlasche
NORTH_EAR_X = (42.0, 108.0)
EAR_W_X, EAR_E_X = cav_x0 - 0.4 - EAR_L / 2, cav_x1 + 0.4 + EAR_L / 2
EAR_N_Y = cav_y1 + 0.4 + EAR_L / 2
# ⚠️ Sued fehlt bewusst: die Suedwand traegt ueber die volle Breite die elf
#    RJ11-Oeffnungen, ein Aussenohr wuerde davor stehen. Die Suedkante haelt
#    die umlaufende Lippe; die beiden Ohren bei Y = 12 stuetzen die Ecken.
PILLARS = ([(EAR_W_X, y) for y in SIDE_EAR_Y]
           + [(EAR_E_X, y) for y in SIDE_EAR_Y]
           + [(x, EAR_N_Y) for x in NORTH_EAR_X])
PILLAR_D, PILLAR_PILOT = EAR_D, EAR_PILOT      # Namen fuer tools/check_case.py

TAB_END = 14.5                         # Lasche ragt so weit ueber die Wand hinaus
TAB_HOLE_OFF = 9.5                     # Lochmitte ab Wandaussenkante


def ohr(px, py, z0, z1):
    """Aussenohr — RECHTECKIG, s. Kommentar oben.

    ⚠️ Auch die Aussenecken bleiben scharf. Eine Verrundung in XY zoege die
    Seitenflaechen als Bogen bis in die Wandflucht hinein, und dann trifft das
    Ohr die Wand wieder nicht im rechten Winkel — dasselbe Netzproblem, nur
    schwaecher. Die obere Kante verrundet ohnehin TOP_R."""
    if abs(py - EAR_N_Y) < 1e-6:       # Nordwand: kragt in +Y aus
        return rrect(px - EAR_W / 2, py - EAR_L / 2, px + EAR_W / 2, py + EAR_L / 2,
                     z0, z1, 0)
    return rrect(px - EAR_L / 2, py - EAR_W / 2, px + EAR_L / 2, py + EAR_W / 2,
                 z0, z1, 0)


# ⚠️ Riegel gegen die tangentiale Beruehrung: das Ohr beginnt 0,4 mm vor der
#    Kavitaetswand, ueberlappt die Aussenwand also um WALL - 0,4. Wird die Wand
#    zu duenn, kuesst es sie nur noch — und ein tangentialer Uebergang gibt ein
#    nicht-mannigfaltiges Netz, das CadQuery nie beanstandet.
if WALL - 0.4 < 0.9:
    raise SystemExit(f"WALL={WALL}: Ohr ueberlappt die Wand nur um "
                     f"{WALL - 0.4:.2f} mm — mindestens 0,9 noetig.")


def rrect(x0, y0, x1, y1, z0, z1, r):
    wp = (cq.Workplane("XY", origin=((x0 + x1) / 2, (y0 + y1) / 2, z0))
          .rect(x1 - x0, y1 - y0).extrude(z1 - z0))
    return wp.edges("|Z").fillet(r) if r else wp


def zyl(x, y, d, z0, z1):
    return cq.Workplane("XY", origin=(x, y, z0)).circle(d / 2).extrude(z1 - z0)


# =========================================================== Unterschale ====
base = rrect(out_x0, out_y0, out_x1, out_y1, 0, Z_WALL, CORNER_R)

# Aussenohren + Wandlaschen ANLEGEN, bevor die Kavitaet geschnitten wird.
for (px, py) in PILLARS:
    base = base.union(ohr(px, py, 0, Z_WALL))
for seite, ex in (("W", EAR_W_X), ("E", EAR_E_X)):
    for ty in SIDE_EAR_Y:
        if seite == "W":
            tx0, tx1, hx = out_x0 - TAB_END, out_x0 + 1.0, out_x0 - TAB_HOLE_OFF
        else:
            tx0, tx1, hx = out_x1 - 1.0, out_x1 + TAB_END, out_x1 + TAB_HOLE_OFF
        tab = rrect(tx0, ty - TAB / 2, tx1, ty + TAB / 2, 0, TAB_T, 3.0)
        base = base.union(tab)
        base = base.cut(zyl(hx, ty, TAB_HOLE, -0.1, TAB_T + 0.1))

base = base.cut(rrect(cav_x0, cav_y0, cav_x1, cav_y1, BOTTOM, Z_WALL + 1, 2.0))

# Platinendome — in allen Varianten die Auflage, darueber je nach PCB_HALT
for (hx, hy) in HOLES:
    base = base.union(zyl(hx, hy, BOSS_D, BOTTOM, PCB_BOT))

if PCB_HALT == "schrauben":
    for (hx, hy) in HOLES:              # Kernloch blind, Boden bleibt zu
        base = base.cut(zyl(hx, hy, BOSS_PILOT, PCB_BOT - 2.8, PCB_BOT + 0.1))

elif PCB_HALT == "schnapper":
    for (hx, hy) in HOLES:
        z_barb = PCB_TOP                # Unterseite des Widerhakens
        base = base.union(zyl(hx, hy, SNAP_D, PCB_BOT, z_barb + SNAP_BARB_H))
        base = base.union(zyl(hx, hy, SNAP_BARB_D, z_barb, z_barb + SNAP_BARB_H))
        # Einfuehrkegel: laeuft flach genug aus, um sich selbst zu tragen
        base = base.union(cq.Workplane("XY", origin=(hx, hy, z_barb + SNAP_BARB_H))
                          .circle(SNAP_BARB_D / 2)
                          .workplane(offset=SNAP_LEAD).circle(0.4).loft())
        # ⚠️ Der Schlitz ZULETZT und bis Z = BOTTOM + 0,8 hinunter — er ist die
        #    Federlaenge. Endet er an der Platinenunterkante, ist die Feder nur
        #    2,1 mm lang und die Randdehnung springt auf ueber 8 %.
        base = base.cut(rrect(hx - 6, hy - SNAP_SLOT / 2, hx + 6, hy + SNAP_SLOT / 2,
                              BOTTOM + 0.8, z_barb + SNAP_BARB_H + SNAP_LEAD + 0.1, 0))

elif PCB_HALT == "randclips":
    for seite, cy in CLIP_POS:
        vz = 1.0 if seite == "W" else -1.0          # +1: Zunge zeigt nach Osten
        kante = 0.0 if seite == "W" else PCB_W      # Platinenkante
        innen = kante - vz * CLIP_GAP               # Innenflaeche der Zunge
        aussen = innen - vz * CLIP_T
        # oertliche Wandverdickung nach AUSSEN, damit hinter der Tasche Wand bleibt
        wa = (out_x0 if seite == "W" else out_x1) - vz * CLIP_BULGE
        wb = out_x0 if seite == "W" else out_x1
        base = base.union(rrect(min(wa, wb), cy - CLIP_W / 2 - 2.5,
                                max(wa, wb), cy + CLIP_W / 2 + 2.5, 0, Z_WALL, 0))
        # Tasche: Ausweichraum hinter der Zunge, offen zur Kavitaet
        ta = aussen - vz * CLIP_FLEX
        base = base.cut(rrect(min(ta, innen + vz * 0.05), cy - CLIP_W / 2 - 0.6,
                              max(ta, innen + vz * 0.05), cy + CLIP_W / 2 + 0.6,
                              BOTTOM, Z_WALL + 1, 0))
        # Zunge
        base = base.union(rrect(min(aussen, innen), cy - CLIP_W / 2,
                                max(aussen, innen), cy + CLIP_W / 2,
                                BOTTOM, PCB_TOP + CLIP_HOOK_H + CLIP_LEAD, 0))
        # Haken: gerade Unterseite auf PCB_TOP, darueber 63°-Einfuehrschraege
        prof = [(innen, PCB_TOP), (innen + vz * CLIP_OVER, PCB_TOP),
                (innen + vz * CLIP_OVER, PCB_TOP + CLIP_HOOK_H),
                (innen, PCB_TOP + CLIP_HOOK_H + CLIP_LEAD)]
        base = base.union(cq.Workplane("XZ", origin=(0, cy + CLIP_W / 2, 0))
                          .polyline(prof).close().extrude(CLIP_W))

# Kernloecher der Deckelschrauben ZULETZT, sonst fuellt die Wand sie wieder zu.
for (px, py) in PILLARS:
    base = base.cut(zyl(px, py, EAR_PILOT, Z_WALL - 9.0, Z_WALL + 0.1))

# --- Suedwand: elf RJ11-Oeffnungen -------------------------------------------
# Die Oeffnungen sitzen auf der Buchsenmitte, nicht auf einem eigenen Raster —
# eine Groesse, ein Bezugspunkt (RJ_X), damit sich Wand und Buchse nicht
# auseinanderentwickeln koennen.
for x in RJ_X:
    base = base.cut(rrect(x - RJ_OPEN_W / 2, out_y0 - 1.0, x + RJ_OPEN_W / 2,
                          cav_y0 + 1.0, PCB_TOP + RJ_OPEN_Z0,
                          PCB_TOP + RJ_OPEN_Z1, 1.2))

# --- Nordwand: 1-Wire-Durchfuehrung mit Zugentlastung ------------------------
# ⚠️⚠️ VORZEICHEN. Die Ebene "XZ" hat die Normale (0,-1,0): `extrude(+L)` laeuft
#    nach SUEDEN. Beide Nordwand-Bohrungen standen zuerst mit negativem Wert da
#    und wurden damit ins Freie ausserhalb des Gehaeuses geschnitten — der
#    Koerper blieb gueltig, die Kavitaet dicht, `check_case.py` gruen. Erst die
#    Sondenlinie in `sonden.py` hat es gezeigt. Suedwand-Schnitte brauchen
#    umgekehrt den negativen Wert.
base = base.cut(cq.Workplane("XZ", origin=(OW_X, out_y1 + 0.5, PCB_TOP + 8.0))
                .circle(OW_D / 2).extrude(WALL + CLR + 1.5))
# Klemmschlitz: zwei Rippen, ueber die volle Tiefe an der Nordwand angebunden —
# kein Kragarm. Sie enden unter der Lippe.
# ⚠️ Die Unterseite laeuft mit 45° aus der Wand heraus statt waagerecht in der
#    Luft zu enden. Als Rechteck waren die beiden Rippen die einzige Stelle der
#    Schale, die wirklich Stuetzmaterial gebraucht haette: 4,5 mm Auskragung,
#    freitragend ab Z = PCB_TOP + 1,0. Tiefer ansetzen geht nicht — dort liegt
#    die Platine.
_rib_z0, _rib_z1 = PCB_TOP + 1.0, Z_WALL - LIP_H - 0.5
_rib_d = 4.5
for _s in (-1, 1):
    _x0 = OW_X + _s * OW_RIB_GAP / 2
    _x1 = _x0 + _s * OW_RIB_W
    _rippe = (cq.Workplane("YZ", origin=(min(_x0, _x1), 0, 0))
              .polyline([(cav_y1, _rib_z0), (cav_y1, _rib_z1),
                         (cav_y1 - _rib_d, _rib_z1),
                         (cav_y1 - _rib_d, _rib_z0 + _rib_d)]).close()
              .extrude(OW_RIB_W))
    base = base.union(_rippe)

# --- Nordwand: Kabelverschraubung M12x1,5 fuer die Netzzuleitung -------------
# ⚠️ Sitzt im NETZBEREICH der Platine (X >= 113,8) und fuehrt die Leitung auf
#    kurzem Weg zur Klemme. Eine Verschraubung, keine blosse Bohrung: die
#    Zugentlastung ist bei Netzspannung nicht verhandelbar.
base = base.cut(cq.Workplane("XZ", origin=(NETZ_X, out_y1 + 0.5, PCB_TOP + 9.0))
                .circle(NETZ_GLAND_D / 2).extrude(WALL + 2.0))

# --- Lueftung ----------------------------------------------------------------
# ⚠️ Nur Nord (ueber dem DevKit) und West (am Klimafuehler). Der Boden bleibt
#    geschlossen: wandmontiert liegt er an der Wand und wuerde nichts belueften.
#    Der Netzbereich bleibt vollstaendig geschlossen.
if VENTS:
    for z in (10.0, 13.0, 16.0, 19.0, 22.0, 25.0):
        base = base.cut(rrect(10.0, cav_y1 - 1.0, 34.0, out_y1 + 1.0,
                              z - 1.0, z + 1.0, 0.9))
    for z in (8.5, 11.5, 14.5, 17.5, 20.5, 23.5):
        base = base.cut(rrect(out_x0 - 1.0, 35.0, cav_x0 + 1.0, 55.0,
                              z - 1.0, z + 1.0, 0.9))

# ================================================================ Deckel ====
lid = rrect(out_x0, out_y0, out_x1, out_y1, Z_WALL, Z_TOP, CORNER_R)
for (px, py) in PILLARS:
    lid = lid.union(ohr(px, py, Z_WALL, Z_TOP))
# ⚠️ Verrunden VOR den Durchbruechen — sonst erwischt faces(">Z").edges() auch
#    Senkungen, Sichtfenster und Tastenfelder.
lid = lid.faces(">Z").edges().fillet(TOP_R)

# Zentrierlippe, ringsum ungeteilt (moeglich, weil keine Saeule innen steht)
lip = (rrect(cav_x0 + LIP_CLR, cav_y0 + LIP_CLR, cav_x1 - LIP_CLR, cav_y1 - LIP_CLR,
             Z_WALL - LIP_H, Z_WALL + 0.1, 1.8)
       .cut(rrect(cav_x0 + LIP_CLR + LIP_W, cav_y0 + LIP_CLR + LIP_W,
                  cav_x1 - LIP_CLR - LIP_W, cav_y1 - LIP_CLR - LIP_W,
                  Z_WALL - LIP_H - 1, Z_WALL + 0.2, 1.2)))
lid = lid.union(lip)

# Deckelschrauben M3: Durchgang + Kopfsenkung
for (px, py) in PILLARS:
    lid = lid.cut(zyl(px, py, LID_SCREW_C, Z_WALL - LIP_H - 0.1, Z_TOP + 0.1))
    lid = lid.cut(cq.Workplane("XY", origin=(px, py, Z_TOP - LID_CSK_H))
                  .circle(LID_SCREW_C / 2).workplane(offset=LID_CSK_H)
                  .circle(LID_CSK_D / 2).loft())

# --- Displayaufnahme ---------------------------------------------------------
# Aufbau von oben: 1,0 mm Deckel ueber dem Glas, Glastasche DSP_GLASS_D tief,
# Modulplatine liegt an der Deckelunterseite an, Halterahmen klemmt sie gegen
# die vier Dome.
DSP_BOSS_Z0 = Z_WALL - DSP_PCB_T       # Unterkante Dom = Rueckseite der Modulplatine
if MIT_DISPLAY:
    lid = lid.cut(rrect(DSP_CX - DSP_WIN / 2, DSP_CY - DSP_WIN / 2,
                        DSP_CX + DSP_WIN / 2, DSP_CY + DSP_WIN / 2,
                        Z_WALL - 0.1, Z_TOP + 0.1, 1.5))
    lid = lid.cut(rrect(DSP_CX - DSP_GLASS_W / 2, DSP_CY - DSP_GLASS_H / 2,
                        DSP_CX + DSP_GLASS_W / 2, DSP_CY + DSP_GLASS_H / 2,
                        Z_WALL - 0.1, Z_WALL + DSP_GLASS_D, 1.0))
for (bx, by) in (DSP_BOSS if MIT_DISPLAY else []):
    # ⚠️ Der Dom muss den Deckelkoerper UEBERLAPPEN, nicht an ihm enden. Mit
    #    Z_WALL + 0,01 entstanden 24 offene Kanten im tesselierten Netz —
    #    genau der Fall, den `check_case.py` Punkt 2 sucht.
    lid = lid.union(zyl(bx, by, DSP_BOSS_D, DSP_BOSS_Z0, Z_WALL + 1.0))
    lid = lid.cut(zyl(bx, by, DSP_BOSS_PILOT, DSP_BOSS_Z0 - 0.1, Z_TOP - 0.8))

# --- Tastenfelder ------------------------------------------------------------
# Mulde von INNEN, TOUCH_MEMBRANE bleibt stehen. Die Deckeloberseite bleibt
# glatt — beschriftet wird sie graviert (tools/laser_gravur.py).
for (tx, ty) in TOUCH:
    lid = lid.cut(zyl(tx, ty, TOUCH_D, Z_WALL - 0.1,
                      Z_TOP - TOUCH_MEMBRANE))

# ====================================================== Displayrahmen =======
# Klemmt die Modulplatine gegen die Deckelunterseite. Haelt ueber den UMRISS,
# nicht ueber das Bohrbild des Moduls — siehe Kommentar bei DSP_*.
_fx = [b[0] for b in DSP_BOSS]
_fy = [b[1] for b in DSP_BOSS]
dsp_frame = None if not MIT_DISPLAY else rrect(min(_fx) - 4.0, min(_fy) - 4.0, max(_fx) + 4.0, max(_fy) + 4.0,
                  0, DSP_FRAME_T, 4.0)
if dsp_frame is not None:
    dsp_frame = dsp_frame.cut(rrect(DSP_CX - DSP_W / 2 + 2.5, DSP_CY - DSP_H / 2 + 2.5,
                                    DSP_CX + DSP_W / 2 - 2.5, DSP_CY + DSP_H / 2 - 2.5,
                                    -0.1, DSP_FRAME_T + 0.1, 2.0))
    for (bx, by) in DSP_BOSS:
        dsp_frame = dsp_frame.cut(zyl(bx, by, DSP_BOSS_PILOT + 0.7, -0.1,
                                      DSP_FRAME_T + 0.1))
    dsp_frame = dsp_frame.translate((0, 0, -DSP_FRAME_T))   # in Druckstellung

# ==================================================== Hutschienen-Clip ======
# Zwei gleiche Teile, je eines UNTER die Wandlasche bei Y = 80 geschraubt
# (W und E, 174,8 mm auseinander). Sie greifen dieselbe waagerechte TS35-Schiene;
# das Paar verhindert das Verdrehen, das eine einzelne Schraube zuliesse.
#
# Masse nach EN 60715: Schiene 35 mm breit, Randhoehe 7,5 mm, Blech 1,0 mm.
# Der Clip liegt mit der Plattenoberseite auf den Schienenraendern auf; die
# beiden Nasen greifen darueber.
#
# ⚠️⚠️ Der erste Entwurf hatte einen frei stehenden Federarm — und der Schlitz,
#    der ihn federn lassen sollte, hat ihn vom Rest GETRENNT. Bambu Studio
#    meldete `number_of_parts = 2`; `check_case.py` sieht den Clip nicht, weil
#    er weder Deckel noch Schale ist. Jetzt federt eine ZUNGE, die an ihrer
#    Wurzel durchgehend an der Platte haengt: Material bleibt zusammen, nur
#    duenner.
# ⚠️ Die Zunge federt in Z, nicht in Y — beim Aufrasten haengt man die feste
#    Nase ein und drueckt; die gefederte Nase laeuft ueber ihre Schraege nach
#    OBEN aus dem Weg. Deshalb sitzt die Schraege unter der Nase.
DIN_L, DIN_B, DIN_T = 26.0, 46.0, 5.0
DIN_SLOT = 35.4                        # lichte Weite zwischen den Hakenwaenden
DIN_RAIL_T = 1.4                       # Freiraum fuer das 1,0-mm-Schienenblech
DIN_LIP = 1.6                          # Uebergriff der Nase
DIN_WALL = 3.0                         # Wandstaerke der Haken
DIN_TONGUE_W, DIN_TONGUE_T, DIN_TONGUE_Y = 16.0, 2.2, 4.0


def hutschienen_clip():
    yh = DIN_SLOT / 2
    z1 = DIN_T + DIN_RAIL_T            # Unterkante der Nasen
    z2 = z1 + DIN_LIP                  # Oberkante der Haken
    c = rrect(-DIN_L / 2, -DIN_B / 2, DIN_L / 2, DIN_B / 2, 0, DIN_T, 4.0)

    # --- feste Seite: Wand ueber die volle Breite, Nase rechteckig ---
    c = c.union(rrect(-DIN_L / 2, -yh - DIN_WALL, DIN_L / 2, -yh, DIN_T, z2, 0))
    c = c.union(rrect(-DIN_L / 2, -yh, DIN_L / 2, -yh + DIN_LIP, z1, z2, 0))

    # --- Federzunge: seitlich freigeschnitten, von unten duenner ---
    for s in (-1, 1):
        c = c.cut(rrect(s * DIN_TONGUE_W / 2, DIN_TONGUE_Y,
                        s * (DIN_TONGUE_W / 2 + 2.0), DIN_B / 2 + 0.1,
                        -0.1, DIN_T + 0.1, 0))
    c = c.cut(rrect(-DIN_TONGUE_W / 2, DIN_TONGUE_Y, DIN_TONGUE_W / 2, DIN_B / 2 + 0.1,
                    -0.1, DIN_T - DIN_TONGUE_T, 0))

    # --- gefederte Seite: Wand und Nase stehen AUF der Zunge ---
    c = c.union(rrect(-DIN_TONGUE_W / 2, yh, DIN_TONGUE_W / 2, yh + DIN_WALL, DIN_T, z2, 0))
    # Nase als Keil: senkrechte Aussenflanke, Schraege nach innen unten
    keil = (cq.Workplane("YZ", origin=(-DIN_TONGUE_W / 2, 0, 0))
            .polyline([(yh, z1), (yh, z2), (yh - DIN_LIP, z2)]).close()
            .extrude(DIN_TONGUE_W))
    c = c.union(keil)

    # Senkschraube M4 (DIN 7991, Kopf Ø7,5): der Kopf MUSS buendig mit der
    # Plattenoberseite bleiben — dort liegt die Schiene auf.
    c = c.cut(zyl(0, 0, TAB_HOLE, -0.1, DIN_T + 0.1))
    c = c.cut(cq.Workplane("XY", origin=(0, 0, DIN_T - 2.3))
              .circle(TAB_HOLE / 2).workplane(offset=2.3).circle(7.7 / 2)
              .loft().translate((0, 0, 0)))
    return c


din_clip = hutschienen_clip() if DIN_CLIP else None

# ============================================================ Pruefstuecke ==
# ⭐ Beide werden AUS dem fertigen Koerper geschnitten, nicht nachmodelliert.
#    Damit koennen sie nicht vom Gehaeuse abweichen.
wallprobe = base.intersect(rrect(-4.0, out_y0 - 1.0, PCB_W + 4.0, 7.0, 0, Z_WALL, 0))
# Displayausschnitt aus dem Deckel, in DRUCKSTELLUNG gedreht (wie der Deckel
# selbst): sonst zeigt die Glastasche nach unten und braucht Stuetzen.
frame = None
if MIT_DISPLAY:
    _fr = lid.intersect(rrect(min(_fx) - 8.0, min(_fy) - 8.0, max(_fx) + 8.0,
                              max(_fy) + 8.0, Z_WALL - 4.0, Z_TOP + 0.1, 0))
    _fb = _fr.val().BoundingBox()
    frame = _fr.rotate((0, 0, 0), (1, 0, 0), 180).translate(
        (0, _fb.ymin + _fb.ymax, _fb.zmax))

# ================================================ camperSense-Bildmarke =====
logo = None
if LOGO:
    try:
        cx = LOGO_CX if not LOGO_SEPARAT else out_x1 + LOGO_GAP + LOGO_D / 2
        ring, acc = cs_logo.mark(LOGO_D, 0, 0, Z_TOP - LOGO_DEPTH, LOGO_DEPTH)
        # ⚠️ Auf die BILD-Mitte ausrichten, nicht auf den Ursprung — der Knoten
        #    steht ueber, die Bounding-Box-Mitte liegt gut 1 mm daneben.
        b0 = cq.Compound.makeCompound([ring.val(), acc.val()]).BoundingBox()
        dx, dy = cx - (b0.xmin + b0.xmax) / 2, LOGO_CY - (b0.ymin + b0.ymax) / 2
        ring, acc = ring.translate((dx, dy, 0)), acc.translate((dx, dy, 0))
        if not LOGO_SEPARAT:
            for teil in (ring, acc):
                lid = lid.cut(teil)
            if not lid.val().isValid():
                raise RuntimeError("Deckel nach dem Logo-Schnitt ungueltig")
        logo = (ring, acc)
        bb = cq.Compound.makeCompound([ring.val(), acc.val()]).BoundingBox()
        print(f"Logo: x {bb.xmin:.1f}..{bb.xmax:.1f}, y {bb.ymin:.1f}..{bb.ymax:.1f} mm")
    except Exception as e:
        print(f"Logo uebersprungen ({type(e).__name__}: {e})")
        logo = None

# ======================================================= Platinen-Dummy =====
# Traegt die BAUTEILE mit, nicht nur den Umriss: nur so meldet
# tools/check_case.py eine Kollision zwischen Deckel und Netzteil, DevKit oder
# RJ11-Buchse — und genau die sind hier eng.
pcb = rrect(0, 0, PCB_W, PCB_L, PCB_BOT, PCB_TOP, PCB_R)
for (hx, hy) in HOLES:
    pcb = pcb.cut(zyl(hx, hy, 2.7, PCB_BOT - 0.1, PCB_TOP + 0.1))

BAUTEILE = []
for x in RJ_X:
    BAUTEILE.append((x - RJ_BODY_W / 2, RJ_FRONT_Y, x + RJ_BODY_W / 2,
                     RJ_FRONT_Y + RJ_DEPTH, RJ_H))
BAUTEILE.append((ONEWIRE_RJ[0] - RJ_BODY_W / 2, ONEWIRE_FRONT_Y,
                 ONEWIRE_RJ[0] + RJ_BODY_W / 2, ONEWIRE_FRONT_Y + RJ_DEPTH, RJ_H))
for (hx, hy) in HDR_XY:
    BAUTEILE.append((hx - 1.3, hy - 2.6, hx + 1.3, hy + 2.6, HDR_H))
for (cx_, cy_) in SH10:
    BAUTEILE.append((cx_ - 3.2, cy_ - 2.3, cx_ + 3.2, cy_ + 2.3, SH10_H))
BAUTEILE.append((ESP[0] - ESP[2] / 2, ESP[1] - ESP[3] / 2,
                 ESP[0] + ESP[2] / 2, ESP[1] + ESP[3] / 2, ESP_SOCKET + ESP_ABOVE))
BAUTEILE.append((HLK[0] - HLK[2] / 2, HLK[1] - HLK[3] / 2,
                 HLK[0] + HLK[2] / 2, HLK[1] + HLK[3] / 2, HLK_H))
BAUTEILE.append((KF250[0] - KF250[2] / 2, KF250[1] - KF250[3] / 2,
                 KF250[0] + KF250[2] / 2, KF250[1] + KF250[3] / 2, KF250_H))
for (bx0, by0, bx1, by1, h) in BAUTEILE:
    pcb = pcb.union(rrect(bx0, by0, bx1, by1, PCB_TOP, PCB_TOP + h, 0))

# ⚠️ Der DevKit steht auf Buchsenleisten: unter dem Board liegen nur die beiden
#    Leisten, nicht der volle Umriss. Der Block oben ist damit zu voll — er
#    wuerde eine Kollision melden, wo Luft ist. Deshalb wird der Bereich
#    zwischen den Leisten unterhalb der DevKit-Platine wieder freigeschnitten.
pcb = pcb.cut(rrect(ESP[0] - ESP[2] / 2 - 0.1, 62.8, ESP[0] + ESP[2] / 2 + 0.1, 85.5,
                    PCB_TOP + 0.01, PCB_TOP + ESP_SOCKET, 0))

# =========================================================== Zubehoerteile ==
EXTRA_PARTS = [("Displayrahmen", dsp_frame)] if dsp_frame is not None else []
if din_clip is not None:
    EXTRA_PARTS += [("Hutschienen-Clip 1", din_clip.translate((60, 0, 0))),
                    ("Hutschienen-Clip 2", din_clip.translate((-60, 0, 0)))]

# =============================================================== Pruefen ====
if os.environ.get("SKIP_EXPORT") == "1":
    print("SKIP_EXPORT=1 — Geometrie gebaut, nichts geschrieben")
else:
    _teile = [("unterschale", base), ("deckel", lid), ("platine", pcb),
              ("wandprobe", wallprobe)]
    _teile += [(n, w) for n, w in (("displayrahmen", dsp_frame),
                                   ("displayprobe", frame),
                                   ("hutschienenclip", din_clip)) if w is not None]
    for name, wp in _teile:
        sol = wp.solids().vals()
        ok = "ok" if (len(sol) == 1 and wp.val().isValid()) else "⚠️ PRUEFEN"
        print(f"{name:16s} {len(sol)} solid(s), {sum(s.Volume() for s in sol):9.0f} mm³  [{ok}]")

    asm = cq.Assembly(name="fbh_gehaeuse")
    asm.add(base, name="unterschale", color=cq.Color(0.55, 0.57, 0.60))
    asm.add(lid, name="deckel", color=cq.Color(0.35, 0.38, 0.42))
    asm.add(pcb, name="platine_dummy", color=cq.Color(0.10, 0.50, 0.20))
    if dsp_frame is not None:
        asm.add(dsp_frame.translate((0, 0, Z_WALL - DSP_PCB_T)),
                name="displayrahmen", color=cq.Color(0.30, 0.32, 0.35))
    if logo:
        asm.add(logo[0], name="logo_ring", color=cq.Color(0.204, 0.702, 0.761))
        asm.add(logo[1], name="logo_akzent", color=cq.Color(0.914, 0.651, 0.231))
    asm.save(f"fbh_gehaeuse{FILE_SFX}.step")

    exporters.export(base, f"fbh_unterschale{_SFX_HALT}.stl", tolerance=0.01)
    exporters.export(wallprobe, f"fbh_pruefstueck_suedwand{_SFX_HALT}.stl", tolerance=0.01)
    if dsp_frame is not None:
        exporters.export(dsp_frame, "fbh_displayrahmen.stl", tolerance=0.01)
    if frame is not None:
        exporters.export(frame, "fbh_pruefstueck_display.stl", tolerance=0.01)
    if din_clip is not None:
        exporters.export(din_clip, "fbh_hutschienen_clip.stl", tolerance=0.01)
    bb = lid.val().BoundingBox()
    exporters.export(lid.rotate((0, 0, 0), (1, 0, 0), 180)
                     .translate((0, bb.ymin + bb.ymax, bb.zmax)),
                     f"fbh_deckel{FILE_SFX}.stl", tolerance=0.01)
    if logo:
        for wp, name in zip(logo, ("logo_ring", "logo_akzent")):
            exporters.export(wp, f"fbh_{name}{FILE_SFX}.stl", tolerance=0.005)

    bb = base.val().BoundingBox()
    print(f"\nAussenmasse mit Ohren und Laschen: {bb.xlen:.1f} x {bb.ylen:.1f} "
          f"x {Z_TOP:.1f} mm")
    print(f"Gehaeusekoerper ohne Ohren:        {out_x1 - out_x0:.1f} x "
          f"{out_y1 - out_y0:.1f} mm")
    print("Export fertig.")
