# ============================================================================
# Gravurvorlage fuer den Deckel — LaserPecker 4, 450-nm-Modul.
#
#     .venv/bin/python laser_gravur.py
#
# Erzeugt zwei PNG:
#   gravur_deckel.png          NUR die Gravur. Schwarz auf Weiss, sonst nichts.
#                              Das ist die Datei, die in die Maschine geht.
#   gravur_deckel_lage.png     Dieselbe Gravur mit Deckelumriss, Sichtfenster,
#                              Tastenfeldern, Logotasche und Schraubensenkungen
#                              als graue Hilfslinien — zum Pruefen der Lage,
#                              NICHT zum Gravieren.
#
# ⚠️⚠️ 1064 nm markiert auf Kunststoff nicht: der Faserlaser braucht Russ als
#    Absorber und findet ihn nur in schwarzem Material. Fuer PETG ist der
#    450-nm-Teil des LP4 der richtige.
# ⚠️ Die Geometrie kommt aus `fbh_case` — Kanalabstaende, Tastenmitten und
#    Fensterlage werden NICHT hier nochmal hingeschrieben. Zwei Dinge, die
#    dieselbe Stelle treffen sollen, muessen aus einem Bezugspunkt kommen;
#    sonst wandert die Beschriftung beim naechsten Parameterlauf von den
#    Oeffnungen weg.
# ⚠️ Der Ursprung des Bildes ist die Bounding Box des DECKELS, nicht die
#    Platine. Beim Ausrichten in der LaserPecker-Software auf die Bildbreite
#    skalieren, die unten ausgegeben wird — dann stimmt alles Uebrige.
# ============================================================================
import os
import sys

os.environ["SKIP_EXPORT"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image, ImageDraw, ImageFont

import fbh_case as c

DPI = int(os.environ.get("DPI", "600"))
PXMM = DPI / 25.4
FONT_DIR = "/System/Library/Fonts/Supplemental"
SCHRIFT = os.environ.get("FONT", os.path.join(FONT_DIR, "Arial Bold.ttf"))
SCHRIFT_R = os.environ.get("FONT_R", os.path.join(FONT_DIR, "Arial.ttf"))

bb = c.lid.val().BoundingBox()
X0, Y0 = bb.xmin, bb.ymin
W_MM, H_MM = bb.xlen, bb.ylen
W, H = round(W_MM * PXMM), round(H_MM * PXMM)


def px(x_mm, y_mm):
    """Gehaeuse-mm -> Bildpixel. Y kippt: das Bild misst nach unten."""
    return ((x_mm - X0) * PXMM, (H_MM - (y_mm - Y0)) * PXMM)


def font(mm, fett=True):
    return ImageFont.truetype(SCHRIFT if fett else SCHRIFT_R, round(mm * PXMM))


def text(d, x_mm, y_mm, s, mm, fett=True, fuellung=0, anker="mm"):
    d.text(px(x_mm, y_mm), s, font=font(mm, fett), fill=fuellung, anchor=anker)


def zeichne(d, hilfslinien=False):
    grau = 190
    if hilfslinien:
        x0, y0 = px(bb.xmin, bb.ymax)
        x1, y1 = px(bb.xmax, bb.ymin)
        d.rectangle([x0, y0, x1, y1], outline=grau, width=max(1, round(0.3 * PXMM)))
        w = c.DSP_W / 2, c.DSP_H / 2
        for a, b, lw in ((c.DSP_WIN / 2, c.DSP_WIN / 2, 0.4), (w[0], w[1], 0.2)):
            d.rectangle([*px(c.DSP_CX - a, c.DSP_CY + b), *px(c.DSP_CX + a, c.DSP_CY - b)],
                        outline=grau, width=max(1, round(lw * PXMM)))
        for (tx, ty) in c.TOUCH:
            r = c.TOUCH_D / 2
            d.ellipse([*px(tx - r, ty + r), *px(tx + r, ty - r)], outline=grau,
                      width=max(1, round(0.3 * PXMM)))
        r = c.LOGO_D / 2
        d.ellipse([*px(c.LOGO_CX - r, c.LOGO_CY + r), *px(c.LOGO_CX + r, c.LOGO_CY - r)],
                  outline=grau, width=max(1, round(0.3 * PXMM)))
        for (sx, sy) in c.PILLARS:
            r = c.LID_CBORE_D / 2
            d.ellipse([*px(sx - r, sy + r), *px(sx + r, sy - r)], outline=grau,
                      width=max(1, round(0.3 * PXMM)))

    # --- Kanalnummern ueber den RJ11-Oeffnungen -----------------------------
    # ⚠️ Die Ziffern koennen NICHT auf die Suedwand: der LP4 graviert eine
    #    ebene Flaeche, und die Wand steht senkrecht. Sie stehen deshalb auf
    #    dem Deckel unmittelbar ueber der jeweiligen Buchse — X kommt aus
    #    c.RJ_X, damit Ziffer und Oeffnung nicht auseinanderlaufen koennen.
    for i, x in enumerate(c.RJ_X, 1):
        text(d, x, 6.5, str(i), 5.0)
    text(d, (c.RJ_X[0] + c.RJ_X[-1]) / 2, 13.5, "H E I Z K R E I S E", 2.6, False)

    # --- Tastenfelder --------------------------------------------------------
    # Zuordnung aus der README: GPIO 4 = kaelter, 2 = waermer, 15 = Raumwahl.
    for (tx, ty), zeichen, wort in zip(c.TOUCH, ("−", "+", "○"),
                                       ("KÄLTER", "WÄRMER", "RAUM")):
        text(d, tx, ty, zeichen, 8.0)
        text(d, tx, ty - c.TOUCH_D / 2 - 3.0, wort, 2.4, False)

    # --- Kopfzeile -----------------------------------------------------------
    # ⚠️ Das freie Nordband des Deckels ist schmaler, als es aussieht: westlich
    #    liegt die Logotasche (x 25..55), suedlich das Sichtfenster (y <= 58),
    #    und in derselben Zeile stehen noch Vorlauf- und Netzhinweis. Der erste
    #    Entwurf legte alle drei auf y = 79..84 — sie ueberlappten paarweise.
    text(d, 103.0, 73.0, "FUSSBODENHEIZUNG", 5.0)
    text(d, 103.0, 66.5, "Ventilsteuerung  ·  11 Kreise", 3.0, False)

    # --- 1-Wire-Durchfuehrung beschriften ------------------------------------
    text(d, c.OW_X, 88.5, "VORLAUF", 2.6, False)

    # --- Warnhinweis ueber dem Netzbereich -----------------------------------
    # ⚠️ Steht bewusst dort, wo darunter die Netzklemme sitzt (X >= 113,8,
    #    Y >= 64,2) — nicht irgendwo, wo Platz war.
    # ⚠️ Nicht weiter nach Osten ruecken: die Kopfsenkung der NE-Deckelschraube
    #    sitzt bei (154,65 | 80) und reicht bis x = 151,5.
    nx = 130.0
    text(d, nx - 4.0, 88.5, "230 V", 5.0, anker="lm")
    text(d, nx, 82.0, "Vor dem Öffnen freischalten", 2.4, False)
    # ⚠️ Das Zeichen ⚠ hat in Arial keine Glyphe — im ersten Lauf stand dort ein
    #    leeres Kaestchen. Es wird deshalb gezeichnet, nicht gesetzt.
    ws, wx, wy = 6.0, nx - 8.5, 88.5
    d.polygon([px(wx, wy + ws / 2), px(wx - ws / 2 * 1.15, wy - ws / 2),
               px(wx + ws / 2 * 1.15, wy - ws / 2)], fill=0)
    d.polygon([px(wx, wy + ws / 2 - 1.1), px(wx - 1.05, wy - ws / 2 + 0.9),
               px(wx + 1.05, wy - ws / 2 + 0.9)], fill=255)
    text(d, wx, wy - 0.6, "!", 3.4)


for name, hilf in (("gravur_deckel.png", False), ("gravur_deckel_lage.png", True)):
    img = Image.new("L", (W, H), 255)
    zeichne(ImageDraw.Draw(img), hilf)
    img.save(name, dpi=(DPI, DPI))
    print(f"{name}: {W} x {H} px bei {DPI} dpi")

print(f"\nMassstab 1:1 -> Bildbreite in der LaserPecker-Software auf "
      f"{W_MM:.1f} mm setzen (Hoehe {H_MM:.1f} mm).")
print("Der Bildursprung ist die linke UNTERE Ecke des Deckels in der Draufsicht,")
print("also die Ecke neben RJ1. Material PETG, 450-nm-Modul.")
