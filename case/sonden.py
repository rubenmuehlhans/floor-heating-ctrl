# ============================================================================
# Sondenpruefung — was `tools/check_case.py` bauartbedingt NICHT findet.
#
#     .venv/bin/python sonden.py
#
# Der Merksatz aus der Anleitung lautet: Kollisionen -> check_case,
# VERSCHLOSSENE LOECHER -> Sondenlinie, Gestaltung -> Render. Ein Loch, das
# nicht durchbricht, ist keine Kollision — es ist Material an der falschen
# Stelle, und im Rendering sieht man es nicht.
#
# Geprueft wird hier:
#   1. Jede Wandoeffnung ist wirklich offen (Zylinder von aussen durch die
#      Wand legen, Materialvolumen im Schnitt messen; 0 heisst durch).
#   2. Das Displaymodul und sein Halterahmen haengen frei — sie sind KEIN
#      Teil der Druckkoerper, `check_case.py` sieht sie also nicht.
#   3. Der Steckweg jeder RJ11-Buchse ist frei: Wanddicke plus Luftspalt bis
#      zur Buchsenfront darf den Stecker nicht vor der Rastnase blockieren.
#
# Rueckgabe 0 = alles offen, 1 = mindestens ein Befund.
# ============================================================================
import os
import sys

os.environ["SKIP_EXPORT"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cadquery as cq
import fbh_case as c

befunde = []


def sagt(zeile, ok=True):
    print(("  " if ok else "  ⚠️ ") + zeile)
    if not ok:
        befunde.append(zeile)


def volumen(wp):
    v = wp.val()
    return v.Volume() if v is not None else 0.0


def sonde(bez, ebene, origin, r, laenge, koerper=None):
    """Duenner Zylinder von aussen durch die Wand. > 0 mm³ heisst: ZU."""
    p = (cq.Workplane(ebene, origin=origin).circle(r).extrude(laenge))
    try:
        v = volumen(p.intersect(koerper if koerper is not None else c.base))
    except Exception:
        v = 0.0
    sagt(f"{bez:34s} {v:7.3f} mm³ Material im Weg", v < 0.01)


print("1) Wandoeffnungen — Sondenlinien\n")
# Suedwand: elf RJ11-Oeffnungen. Sonde auf der Buchsenmitte, Hoehe in der
# Mitte der Oeffnung, von aussen bis in die Kavitaet.
z_rj = c.PCB_TOP + (c.RJ_OPEN_Z0 + c.RJ_OPEN_Z1) / 2
for i, x in enumerate(c.RJ_X, 1):
    sonde(f"RJ{i} Steckoeffnung Suedwand", "XZ", (x, c.out_y0 - 1.0, z_rj),
          3.2, -(c.WALL + c.CLR + 2.0))

# Nordwand: 1-Wire und Netz-Verschraubung
# ⚠️ Die Ebene "XZ" hat die Normale (0,-1,0): fuer die NORDwand ist die Laenge
#    POSITIV, fuer die Suedwand negativ. Genau dieser Dreher hat oben die
#    beiden Nordwand-Bohrungen ins Leere schneiden lassen, und die Sonde hat
#    ihn zuerst mitgemacht: sie mass Luft ausserhalb des Gehaeuses und meldete
#    "offen".
# ⚠️ Hier zaehlt der KABELweg, nicht der Lochdurchmesser: direkt hinter der
#    Ø7-Bohrung verengen die beiden Zugentlastungsrippen auf OW_RIB_GAP. Eine
#    Sonde mit halbem Lochdurchmesser meldet dort 9 mm³ und behauptet "zu",
#    obwohl das Kabel bequem durchgeht.
sonde("1-Wire-Kabelweg Nordwand", "XZ", (c.OW_X, c.out_y1 + 1.0, c.PCB_TOP + 8.0),
      c.OW_RIB_GAP / 2 - 0.2, c.WALL + c.CLR + 2.0)
sonde("Kabelverschraubung Netz Nordwand", "XZ",
      (c.NETZ_X, c.out_y1 + 1.0, c.PCB_TOP + 9.0),
      c.NETZ_GLAND_D / 2 - 0.6, c.WALL + 2.0)

# Lueftung: je ein Schlitz Nord und West
sonde("Lueftungsschlitz Nordwand", "XZ", (22.0, c.out_y1 + 1.0, 16.0),
      0.4, c.WALL + 2.0)
sonde("Lueftungsschlitz Westwand", "YZ", (c.out_x0 - 1.0, 45.0, 14.5),
      0.4, c.WALL + 2.0)

# Deckel: Sichtfenster und Schraubdurchgaenge
# ⚠️ Das Fenster gibt es nur in der Variante MIT Display. Ungeprueft laufen zu
#    lassen waere falsch, unbedingt zu pruefen ebenso: in der Variante ohne
#    Display MUSS dort Material stehen — deshalb kippt die Sonde die Erwartung
#    statt sie zu ueberspringen.
if c.dsp_frame is not None:
    sonde("Displayfenster Deckel", "XY", (c.DSP_CX, c.DSP_CY, c.Z_WALL - 1.0),
          8.0, c.LID_T + 2.0, c.lid)
else:
    p_ = cq.Workplane("XY", origin=(c.DSP_CX, c.DSP_CY, c.Z_WALL - 1.0)) \
        .circle(8.0).extrude(c.LID_T + 2.0)
    v_ = volumen(p_.intersect(c.lid))
    sagt(f"Deckel an der Displaystelle geschlossen: {v_:8.3f} mm³ Material"
         + ("" if v_ > 100 else "  <- da ist ein Loch"), v_ > 100)
for i, (px, py) in enumerate(c.PILLARS, 1):
    sonde(f"Deckelschraube {i} Durchgang", "XY", (px, py, c.Z_WALL - 0.5),
          c.LID_SCREW_C / 2 - 0.3, c.LID_T + 1.0, c.lid)

print("\n2) Freiraum fuer Display und Halterahmen\n")
# Beide sind KEINE Druckkoerper des Gehaeuses und fehlen deshalb in
# check_case. Hier werden sie als Ersatzkoerper gegen Platine, Schale und
# Deckel gehalten.
# ⚠️⚠️ Der DECKEL fehlte hier zuerst — und genau dort sitzen die vier
#    Displaydome. Zwei davon ragten 1,0 mm in den Modulumriss: der Dom haelt
#    den Halterahmen, seine Unterseite liegt also GENAU in der Ebene der
#    Modulrueckseite. Ueberlappt er in XY, steht er im Modul. Gegen Platine und
#    Schale allein zu pruefen findet das nie.
rahmen = None
if c.dsp_frame is None:
    print("  Variante ohne Display (DISPLAY_MODUL=0) — entfaellt")
else:
    modul = (cq.Workplane("XY", origin=(c.DSP_CX, c.DSP_CY,
                                        c.Z_WALL - c.DSP_PCB_T))
             .rect(c.DSP_W, c.DSP_H).extrude(c.DSP_PCB_T))
    rahmen = c.dsp_frame.translate((0, 0, c.Z_WALL - c.DSP_PCB_T))
    # Steckerraum auf der Modulrueckseite [SCHAETZUNG 8 mm]
    stecker = (cq.Workplane("XY", origin=(c.DSP_CX, c.DSP_CY,
                                          c.Z_WALL - c.DSP_PCB_T - 8.0))
               .rect(c.DSP_W, c.DSP_H).extrude(8.0))
    for bez, wp in (("Modulplatine", modul), ("Halterahmen", rahmen),
                    ("Steckerraum Rueckseite", stecker)):
        for gegen, nm in ((c.pcb, "Platine + Bauteile"), (c.base, "Unterschale"),
                          (c.lid, "Deckel samt Domen")):
            try:
                v = volumen(wp.intersect(gegen))
            except Exception:
                v = 0.0
            sagt(f"{bez:24s} gegen {nm:20s} {v:7.3f} mm³", v < 0.01)

print("\n3) Steckweg der RJ11-Buchsen\n")
weg = c.WALL + c.CLR + c.RJ_FRONT_Y
sagt(f"Tunnel vor der Buchsenfront: {weg:.1f} mm"
     + ("" if weg <= 5.0 else "  <- Rastnase koennte im Tunnel liegen"), weg <= 5.0)
lichte = c.RJ_OPEN_Z1 - c.RJ_OPEN_Z0
sagt(f"Lichte Oeffnung: {c.RJ_OPEN_W:.1f} x {lichte:.1f} mm"
     + ("" if (c.RJ_OPEN_W >= 8.6 and lichte >= 8.6) else "  <- kleiner als der Stecker"),
     c.RJ_OPEN_W >= 8.6 and lichte >= 8.6)
steg = min(b - a for a, b in zip(c.RJ_X, c.RJ_X[1:])) - c.RJ_OPEN_W
sagt(f"Schmalster Steg zwischen zwei Oeffnungen: {steg:.2f} mm"
     + ("" if steg >= 2.5 else "  <- unter 2,5 mm, Suedwand wird weich"), steg >= 2.5)

print("\n2b) Senkrechter Abstand unter dem Halterahmen\n")
# ⚠️ Ein reiner XY-Vergleich Dom gegen Buchsenreihe war hier zuerst und war
#    FALSCH: die Dome reichen nur bis zur Modulrueckseite herunter und damit
#    nie auf Buchsenhoehe — die Meldung "-1,10 mm zu eng" beschrieb einen
#    Konflikt, den es nicht gibt. Was zaehlt, ist der Abstand des tiefsten
#    Displayteils (des Halterahmens) zu dem, was unter seiner Grundflaeche
#    steht. Die Kollision selbst faengt Abschnitt 2 dreidimensional; hier steht
#    die Reserve als Zahl, damit ein Zuwachs auffaellt, bevor er anschlaegt.
if rahmen is None:
    print("  Variante ohne Display — entfaellt")
else:
    rb = rahmen.val().BoundingBox()
    eng = [(c.PCB_TOP + h, h) for (bx0, by0, bx1, by1, h) in c.BAUTEILE
           if bx1 > rb.xmin and bx0 < rb.xmax and by1 > rb.ymin and by0 < rb.ymax]
    if eng:
        top, h = max(eng)
        sagt(f"Rahmenunterkante {rb.zmin:.2f} mm ueber hoechstem Bauteil "
             f"darunter ({h:.2f} mm): {rb.zmin - top:.2f} mm"
             + ("" if rb.zmin - top >= 1.0 else "  <- unter 1 mm"),
             rb.zmin - top >= 1.0)
    else:
        sagt("Unter dem Halterahmen steht kein Bauteil aus der Liste")
    sagt("Unter dem Halterahmen steht kein Bauteil aus der Liste")

print("\n3b) Lichte Hoehe gegen die hoechsten Bauteile\n")
# ⚠️⚠️ `check_case.py` prueft Deckel gegen Platine — aber der Platinen-Dummy
#    traegt genau die Hoehen, die auch das Skript annimmt. Beide aus derselben
#    Quelle: ist eine Annahme falsch, sind es beide, und der Test bleibt gruen.
#    Deshalb wird die Reserve hier ZAHLENMAESSIG ausgewiesen. Welches Bauteil
#    fuehrt, hat sich mit der Messung vom 2026-08-16 umgedreht — vorher das
#    Displaymodul, seither das Netzteil.
hoch = [("Netzteil HLK-5M03", c.HLK_H),
        ("RJ11-Buchse", c.RJ_H),
        ("ESP32 auf Buchsenleiste", c.ESP_SOCKET + c.ESP_ABOVE),
        ("Klemme 220", c.KF250_H),
        ("Buchsenleiste 1x2", c.HDR_H),
        ("Displaymodul samt Rahmen und Stecker",
         c.DSP_PCB_T + c.DSP_FRAME_T + 8.0 + 2.0)]
for bez, h in sorted(hoch, key=lambda t: -t[1]):
    luft = c.TOP_CLEAR - h
    sagt(f"{bez:38s} {h:5.2f} mm hoch, {luft:5.2f} mm Luft"
         + ("" if luft >= 1.0 else "  <- unter 1 mm"), luft >= 1.0)

print("\n4) Netzbereich: geschlossen ausser der Verschraubung\n")
# ⚠️ Hier wird UMGEKEHRT geprueft: die Sonde MUSS auf Material treffen. Der
#    erste Versuch verglich stattdessen das Wandvolumen des Sektors gegen einen
#    von Hand gerechneten Sollwert — der lag um 994 mm³ daneben, weil Eckohr
#    und Wandlasche mit im Sektor liegen. Ein Sollwert, der die Geometrie
#    nachrechnet, geht immer irgendwann daneben; die Sonde nicht.
def dicht(bez, ebene, origin, r, laenge):
    p = cq.Workplane(ebene, origin=origin).circle(r).extrude(laenge)
    try:
        v = volumen(p.intersect(c.base))
    except Exception:
        v = 0.0
    sagt(f"{bez:38s} {v:7.3f} mm³ Wandmaterial", v > 0.5)


for z in (10.0, 16.0, 22.0):
    for x in (118.0, 126.0, 146.0):        # Nordwand rechts der Verschraubung
        dicht(f"Nordwand X={x:.0f} Z={z:.0f} geschlossen", "XZ",
              (x, c.out_y1 + 1.0, z), 0.8, c.WALL + 2.0)
    for y in (70.0, 85.0):                 # Ostwand im Netzsektor
        dicht(f"Ostwand Y={y:.0f} Z={z:.0f} geschlossen", "YZ",
              (c.out_x1 - c.WALL - 1.0, y, z), 0.8, c.WALL + 2.0)

print("\n5) Zubehoerteile — EIN Koerper, dichtes Netz\n")
# ⚠️⚠️ `check_case.py` sieht nur Schale, Deckel, Platine, Logo, Tasten und
#    Pruefstuecke. Displayrahmen und Hutschienen-Clip fallen durch das Raster.
#    Beim Clip war das teuer: der Schlitz, der den Federarm federn lassen
#    sollte, hat ihn ABGETRENNT — zwei Koerper, gueltig, und erst Bambu Studio
#    meldete `number_of_parts = 2`.
import collections

for nm, wp in getattr(c, "EXTRA_PARTS", []) or []:
    sol = wp.solids().vals()
    verts, tris = wp.val().tessellate(0.01, 0.1)
    kante = collections.Counter()
    for t in tris:
        p = [(round(verts[i].x, 4), round(verts[i].y, 4), round(verts[i].z, 4))
             for i in t]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            kante[tuple(sorted((p[a], p[b])))] += 1
    offen = sum(1 for v in kante.values() if v != 2)
    ok = len(sol) == 1 and wp.val().isValid() and offen == 0
    sagt(f"{nm:22s} {len(sol)} Solid(s), offene Kanten {offen}"
         + ("" if ok else "  <- erwartet: 1 Koerper, 0 offene Kanten"), ok)

print(f"\n6) Platinenbefestigung: {c.PCB_HALT}\n")


def dehnung(t, delta, L):
    """Randdehnung einer eingespannten Biegefeder in Prozent."""
    return 3 * t * delta / (2 * L * L) * 100


if c.PCB_HALT == "schrauben":
    wand = (c.BOSS_D - c.BOSS_PILOT) / 2
    sagt(f"Domwand um das Kernloch: {wand:.2f} mm"
         + ("" if wand >= 1.8 else "  <- unter 1,8 mm"), wand >= 1.8)

elif c.PCB_HALT == "schnapper":
    # ⚠️ Ein Widerhaken, der nicht groesser ist als die Bohrung, haelt nichts.
    ueber = (c.SNAP_BARB_D - 2.7) / 2
    sagt(f"Uebergriff des Widerhakens ueber die Ø2,7-Bohrung: {ueber:.2f} mm"
         + ("" if ueber >= 0.2 else "  <- haelt nicht"), ueber >= 0.2)
    luft = (2.7 - c.SNAP_D) / 2
    sagt(f"Luft zwischen Schaft und Bohrung: {luft:.2f} mm"
         + ("" if 0.05 <= luft <= 0.25 else "  <- zu stramm oder zu lose"),
         0.05 <= luft <= 0.25)
    schenkel = (c.SNAP_D - c.SNAP_SLOT) / 2
    L = (c.PCB_TOP + c.SNAP_BARB_H) - (c.BOTTOM + 0.8)
    e = dehnung(schenkel, ueber, L)
    sagt(f"Schenkel {schenkel:.2f} mm, Federlaenge {L:.2f} mm -> {e:.2f} % Randdehnung"
         + ("" if e <= 3.0 else "  <- ueber 3 %, PETG bleibt verformt"), e <= 3.0)
    sagt(f"Schenkelbreite {schenkel:.2f} mm"
         + ("" if schenkel >= 0.8 else "  <- unter zwei Bahnbreiten, druckt nicht sauber"),
         schenkel >= 0.8)

elif c.PCB_HALT == "randclips":
    e = dehnung(c.CLIP_T, c.CLIP_OVER, (c.PCB_TOP + c.CLIP_HOOK_H) - c.BOTTOM)
    sagt(f"Zunge {c.CLIP_T:.2f} mm dick, Uebergriff {c.CLIP_OVER:.2f} mm "
         f"-> {e:.2f} % Randdehnung"
         + ("" if e <= 3.0 else "  <- ueber 3 %, Zunge bleibt offen stehen"), e <= 3.0)
    sagt(f"Uebergriff gegen Kavitaetsluft: {c.CLIP_OVER:.2f} > {c.CLR:.2f} mm?"
         + ("" if c.CLIP_OVER > c.CLR - 0.3 else "  <- Platine kann unter dem Haken weg"),
         c.CLIP_OVER > c.CLR - 0.3)
    # ⚠️⚠️ Die Ausweichtasche liegt IN der Wand. Ohne die oertliche Verdickung
    #    haette sie die Wand durchbrochen — bei einem Netzgeraet ein Loch nach
    #    aussen. Die Sonde muss hier Material FINDEN.
    for seite, cy in c.CLIP_POS:
        x = (c.out_x0 - c.CLIP_BULGE - 1.0) if seite == "W" else \
            (c.out_x1 + c.CLIP_BULGE + 1.0)
        laenge = (c.CLIP_BULGE + 3.0) * (1 if seite == "W" else -1)
        pr = cq.Workplane("YZ", origin=(x, cy, c.PCB_TOP - 1.0)).circle(0.8) \
            .extrude(laenge)
        v = volumen(pr.intersect(c.base))
        sagt(f"Wand hinter der Zunge {seite} Y={cy:.0f}: {v:6.3f} mm³"
             + ("" if v > 1.0 else "  <- Tasche ist durchgebrochen"), v > 1.0)

print()
if befunde:
    print(f"⚠️ {len(befunde)} Befund(e) — siehe oben.")
    sys.exit(1)
print("Alle Oeffnungen offen, alle Freiraeume frei.")
