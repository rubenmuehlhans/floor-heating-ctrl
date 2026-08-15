# ============================================================================
# Gehaeuse fuer die Ventilsteuerung Fussbodenheizung  —  parametrisch, CadQuery
#
# Koordinaten: Ursprung = linke untere Platinenecke in der Draufsicht,
# Z = 0 = Gehaeuse-Aussenboden. XY-Masse stammen aus den Fertigungsdaten in
# ../board/ und sind mit `reference/platine_auslesen.py` nachvollziehbar.
#
# ⚠️⚠️ ALLE Z-HOEHEN SIND [SCHAETZUNG]. In den Fertigungsdaten steht keine
#      einzige Bauhoehe — die DXF zeigt nur den Bestueckungsdruck. Vor dem
#      Komplettdruck die beiden Pruefstuecke drucken (PLATTE=proben) und die
#      markierten Masse nachmessen.
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

# RJ11-Buchsen 4P4C, 90°, Gehaeuse 10,0 (X) x 13,0 (Y) laut Bestueckungsdruck.
# Die Steckoeffnung zeigt nach SUEDEN, die Front liegt bei Y = 1,10 — also
# 1,1 mm INNERHALB der Platinenkante. Der Stecker muss Wand + 1,6 mm Luft
# ueberwinden.
RJ_X = [8.367, 21.702, 35.291, 48.880, 62.342, 75.804,
        89.266, 102.601, 115.936, 129.398, 142.987]
RJ_FRONT_Y, RJ_DEPTH, RJ_BODY_W = 1.10, 13.0, 10.0
RJ_H = float(os.environ.get("RJ_H", "13.0"))          # [SCHAETZUNG] Bauhoehe
# Steckoeffnung: 4P4C-Stecker 7,65 breit, mit Rastnase ~8,6 hoch. Grosszuegig,
# weil die Hoehenlage selbst geschaetzt ist — das Pruefstueck entscheidet.
RJ_OPEN_W = float(os.environ.get("RJ_OPEN_W", "9.8"))
RJ_OPEN_Z0 = float(os.environ.get("RJ_OPEN_Z0", "1.6"))   # ueber Platinenoberkante
RJ_OPEN_Z1 = float(os.environ.get("RJ_OPEN_Z1", "11.0"))

ONEWIRE_RJ = (75.677, 67.423)          # 1-Wire-Buchse, MITTEN auf der Platine
ONEWIRE_FRONT_Y = 57.85                # Front zeigt nach Sueden, nicht erreichbar

# ESP32-DevKitC V4 auf Buchsenleisten. Pinreihen Y = 61,455 / 86,855 (1 Zoll),
# Pin 1 = 3V3 bei X = 8,24 → das WROOM-Modul liegt WESTLICH, die Micro-USB-
# Buchse OESTLICH bei X ~ 59. Sie liegt damit 91 mm von jeder Aussenkante
# entfernt und ist nur bei offenem Deckel erreichbar (Firmware laeuft per OTA).
# ⚠️ Der Footprint zeichnet 50,8 x 30,48 — das reale Board misst 54,4 x 27,9.
#    Fuer das Gehaeuse zaehlt das reale Board.
ESP = (31.100, 74.155, 54.4, 27.9)     # cx, cy, L, B
ESP_SOCKET = float(os.environ.get("ESP_SOCKET", "8.5"))   # [SCHAETZUNG] Buchsenleiste
ESP_ABOVE = float(os.environ.get("ESP_ABOVE", "4.7"))     # [SCHAETZUNG] Platine + WROOM

HLK = (105.268, 75.045, 34.2, 20.2)    # HLK-5M03, Datenblattmass
HLK_H = float(os.environ.get("HLK_H", "15.1"))            # hoechster Aufbau
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
WALL, BOTTOM, LID_T = 2.4, 2.4, 3.0
TOP_R, CORNER_R = 1.5, 4.0
CLR = 0.5                              # Luft rings um die Platine
BOT_CLEAR = 3.0                        # unter der Platine: nur Loetstellen
# ⚠️ TOP_CLEAR wird NICHT vom hoechsten Bauteil bestimmt (HLK 15,1), sondern
#    vom Displaymodul, das vom Deckel nach unten haengt: Glas 2,0 + Platine 1,6
#    + Halterahmen 2,5 + Steckerhoehe. 18,0 laesst ueber dem Netzteil 2,9 mm
#    Luft und unter dem Display 12 mm.
TOP_CLEAR = float(os.environ.get("TOP_CLEAR", "18.0"))
LIP_H, LIP_W, LIP_CLR = 2.0, 1.5, 0.15

# Deckelschrauben M3 in AUSSENOHREN — siehe Kopfkommentar.
EAR_D, EAR_PILOT = 7.0, 2.5
LID_SCREW_C, LID_CBORE_D, LID_CBORE_H = 3.3, 6.2, 1.8
# Wandlaschen an den vier seitlichen Ohren
TAB, TAB_T, TAB_HOLE = 13.0, 3.2, 4.5

BOSS_D, BOSS_PILOT = 6.0, 2.05         # Platinendome, M2,5 selbstschneidend

# Displaymodul (Waveshare 1,5" OLED, SSD1327, 128 x 128).
# ⚠️ Modulplatine und Bohrbild sind NICHT aus einem Datenblatt geprueft.
#    Das Modul wird deshalb NICHT ueber sein Bohrbild gehalten, sondern von
#    einem Halterahmen gegen die Deckelunterseite geklemmt — dann zaehlt nur
#    der Umriss, und der ist leicht nachzumessen.
DSP_CX, DSP_CY = 110.0, 39.25
DSP_W = float(os.environ.get("DSP_W", "42.0"))
DSP_H = float(os.environ.get("DSP_H", "37.5"))
DSP_PCB_T = float(os.environ.get("DSP_PCB_T", "1.6"))
DSP_GLASS_W = float(os.environ.get("DSP_GLASS_W", "30.5"))   # [SCHAETZUNG]
DSP_GLASS_H = float(os.environ.get("DSP_GLASS_H", "33.5"))   # [SCHAETZUNG]
DSP_GLASS_D = float(os.environ.get("DSP_GLASS_D", "2.0"))    # [SCHAETZUNG] Glas ueber Modulplatine
DSP_WIN = 28.6                         # Sichtfenster (Aktivflaeche 26,86 + Rand)
DSP_BOSS = [(85.0, 39.25), (135.0, 39.25), (110.0, 18.5), (110.0, 60.0)]
DSP_BOSS_D, DSP_BOSS_PILOT = 6.0, 2.05
DSP_FRAME_T = 2.5

# Kapazitive Tasten: ESP32-eigene Touch-Pins (GPIO 4 / 2 / 15). Auf der Platine
# unbeschaltet — die Leitungen gehen direkt an die Buchsenleiste des DevKit.
# Im Deckel drei Membranfelder; die Sensorflaeche (Kupferscheibe oder
# Alu-Klebeband) wird von innen dagegen gesetzt.
TOUCH = [(25.0, 39.25), (47.0, 39.25), (69.0, 39.25)]
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

# Platinendome (M2,5 selbstschneidend von oben, Kernloch blind)
for (hx, hy) in HOLES:
    base = base.union(zyl(hx, hy, BOSS_D, BOTTOM, PCB_BOT))
    base = base.cut(zyl(hx, hy, BOSS_PILOT, PCB_BOT - 2.8, PCB_BOT + 0.1))

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
for s in (-1, 1):
    x0 = OW_X + s * OW_RIB_GAP / 2
    x1 = x0 + s * OW_RIB_W
    base = base.union(rrect(min(x0, x1), cav_y1 - 4.5, max(x0, x1), cav_y1,
                            PCB_TOP + 1.0, Z_WALL - LIP_H - 0.5, 0))

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
    for z in (10.0, 13.0, 16.0, 19.0, 22.0):
        base = base.cut(rrect(10.0, cav_y1 - 1.0, 34.0, out_y1 + 1.0,
                              z - 1.0, z + 1.0, 0.9))
    for z in (8.5, 11.5, 14.5, 17.5, 20.5):
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
    lid = lid.cut(zyl(px, py, LID_CBORE_D, Z_TOP - LID_CBORE_H, Z_TOP + 0.1))

# --- Displayaufnahme ---------------------------------------------------------
# Aufbau von oben: 1,0 mm Deckel ueber dem Glas, Glastasche DSP_GLASS_D tief,
# Modulplatine liegt an der Deckelunterseite an, Halterahmen klemmt sie gegen
# die vier Dome.
lid = lid.cut(rrect(DSP_CX - DSP_WIN / 2, DSP_CY - DSP_WIN / 2,
                    DSP_CX + DSP_WIN / 2, DSP_CY + DSP_WIN / 2,
                    Z_WALL - 0.1, Z_TOP + 0.1, 1.5))
lid = lid.cut(rrect(DSP_CX - DSP_GLASS_W / 2, DSP_CY - DSP_GLASS_H / 2,
                    DSP_CX + DSP_GLASS_W / 2, DSP_CY + DSP_GLASS_H / 2,
                    Z_WALL - 0.1, Z_WALL + DSP_GLASS_D, 1.0))
DSP_BOSS_Z0 = Z_WALL - DSP_PCB_T       # Unterkante Dom = Rueckseite der Modulplatine
for (bx, by) in DSP_BOSS:
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
dsp_frame = rrect(min(_fx) - 4.0, min(_fy) - 4.0, max(_fx) + 4.0, max(_fy) + 4.0,
                  0, DSP_FRAME_T, 4.0)
dsp_frame = dsp_frame.cut(rrect(DSP_CX - DSP_W / 2 + 2.5, DSP_CY - DSP_H / 2 + 2.5,
                                DSP_CX + DSP_W / 2 - 2.5, DSP_CY + DSP_H / 2 - 2.5,
                                -0.1, DSP_FRAME_T + 0.1, 2.0))
for (bx, by) in DSP_BOSS:
    dsp_frame = dsp_frame.cut(zyl(bx, by, DSP_BOSS_PILOT + 0.7, -0.1, DSP_FRAME_T + 0.1))
dsp_frame = dsp_frame.translate((0, 0, -DSP_FRAME_T))   # in Druckstellung legen

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
_fr = lid.intersect(rrect(min(_fx) - 8.0, min(_fy) - 8.0, max(_fx) + 8.0,
                          max(_fy) + 8.0, Z_WALL - 4.0, Z_TOP + 0.1, 0))
_fb = _fr.val().BoundingBox()
frame = _fr.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, _fb.ymin + _fb.ymax, _fb.zmax))

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
EXTRA_PARTS = [("Displayrahmen", dsp_frame)]
if din_clip is not None:
    EXTRA_PARTS += [("Hutschienen-Clip 1", din_clip.translate((60, 0, 0))),
                    ("Hutschienen-Clip 2", din_clip.translate((-60, 0, 0)))]

# =============================================================== Pruefen ====
if os.environ.get("SKIP_EXPORT") == "1":
    print("SKIP_EXPORT=1 — Geometrie gebaut, nichts geschrieben")
else:
    for name, wp in [("unterschale", base), ("deckel", lid), ("platine", pcb),
                     ("displayrahmen", dsp_frame), ("wandprobe", wallprobe),
                     ("displayprobe", frame)] + ([("hutschienenclip", din_clip)]
                                                 if din_clip is not None else []):
        sol = wp.solids().vals()
        ok = "ok" if (len(sol) == 1 and wp.val().isValid()) else "⚠️ PRUEFEN"
        print(f"{name:16s} {len(sol)} solid(s), {sum(s.Volume() for s in sol):9.0f} mm³  [{ok}]")

    asm = cq.Assembly(name="fbh_gehaeuse")
    asm.add(base, name="unterschale", color=cq.Color(0.55, 0.57, 0.60))
    asm.add(lid, name="deckel", color=cq.Color(0.35, 0.38, 0.42))
    asm.add(pcb, name="platine_dummy", color=cq.Color(0.10, 0.50, 0.20))
    asm.add(dsp_frame.translate((0, 0, Z_WALL - DSP_PCB_T)), name="displayrahmen",
            color=cq.Color(0.30, 0.32, 0.35))
    if logo:
        asm.add(logo[0], name="logo_ring", color=cq.Color(0.204, 0.702, 0.761))
        asm.add(logo[1], name="logo_akzent", color=cq.Color(0.914, 0.651, 0.231))
    asm.save("fbh_gehaeuse.step")

    exporters.export(base, "fbh_unterschale.stl", tolerance=0.01)
    exporters.export(dsp_frame, "fbh_displayrahmen.stl", tolerance=0.01)
    exporters.export(wallprobe, "fbh_pruefstueck_suedwand.stl", tolerance=0.01)
    exporters.export(frame, "fbh_pruefstueck_display.stl", tolerance=0.01)
    if din_clip is not None:
        exporters.export(din_clip, "fbh_hutschienen_clip.stl", tolerance=0.01)
    bb = lid.val().BoundingBox()
    exporters.export(lid.rotate((0, 0, 0), (1, 0, 0), 180)
                     .translate((0, bb.ymin + bb.ymax, bb.zmax)),
                     "fbh_deckel.stl", tolerance=0.01)
    if logo:
        for wp, name in zip(logo, ("logo_ring", "logo_akzent")):
            exporters.export(wp, f"fbh_{name}.stl", tolerance=0.005)

    bb = base.val().BoundingBox()
    print(f"\nAussenmasse mit Ohren und Laschen: {bb.xlen:.1f} x {bb.ylen:.1f} "
          f"x {Z_TOP:.1f} mm")
    print(f"Gehaeusekoerper ohne Ohren:        {out_x1 - out_x0:.1f} x "
          f"{out_y1 - out_y0:.1f} mm")
    print("Export fertig.")
