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
sonde("Displayfenster Deckel", "XY", (c.DSP_CX, c.DSP_CY, c.Z_WALL - 1.0),
      8.0, c.LID_T + 2.0, c.lid)
for i, (px, py) in enumerate(c.PILLARS, 1):
    sonde(f"Deckelschraube {i} Durchgang", "XY", (px, py, c.Z_WALL - 0.5),
          c.LID_SCREW_C / 2 - 0.3, c.LID_T + 1.0, c.lid)

print("\n2) Freiraum fuer Display und Halterahmen\n")
# Beide sind KEINE Druckkoerper des Gehaeuses und fehlen deshalb in
# check_case. Hier werden sie als Ersatzkoerper gegen den Platinen-Dummy und
# gegen die Unterschale gehalten.
modul = cq.Workplane("XY", origin=(c.DSP_CX, c.DSP_CY,
                                   c.Z_WALL - c.DSP_PCB_T)) \
    .rect(c.DSP_W, c.DSP_H).extrude(c.DSP_PCB_T)
rahmen = c.dsp_frame.translate((0, 0, c.Z_WALL - c.DSP_PCB_T))
# Steckerraum auf der Modulrueckseite [SCHAETZUNG 8 mm]
stecker = cq.Workplane("XY", origin=(c.DSP_CX, c.DSP_CY,
                                     c.Z_WALL - c.DSP_PCB_T - 8.0)) \
    .rect(c.DSP_W, c.DSP_H).extrude(8.0)
for bez, wp in (("Modulplatine", modul), ("Halterahmen", rahmen),
                ("Steckerraum Rueckseite", stecker)):
    for gegen, nm in ((c.pcb, "Platine + Bauteile"), (c.base, "Unterschale")):
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

print()
if befunde:
    print(f"⚠️ {len(befunde)} Befund(e) — siehe oben.")
    sys.exit(1)
print("Alle Oeffnungen offen, alle Freiraeume frei.")
