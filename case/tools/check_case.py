# ============================================================================
# Gehäuse prüfen — EIN Aufruf statt handgeschriebener Checks.
#
#     cd ../t-call-a7670 && .venv/bin/python ../tools/check_case.py
#     MODUL=sim7080_case .venv/bin/python ../tools/check_case.py
#
# Prüft, was in dieser Reihenfolge schon schiefgegangen ist:
#   1. Jeder Körper ist EIN gültiger Volumenkörper (`isValid`, 1 Solid).
#      ⚠️ Zwei Solids heißen fast immer: eine Vereinigung hat nicht gegriffen.
#      AUSNAHME Logo-Einleger: der Akzent besteht BEWUSST aus zwei getrennten
#      Körpern (Innenbogen + Knoten, im Asset ebenfalls getrennt). Dort wird
#      nur `isValid` verlangt, nicht Zusammenhang.
#   2. Das TESSELIERTE Netz ist dicht — geprüft über die Kantenparität (jede
#      Kante genau zwei Dreiecke). Ein gültiger B-Rep-Körper kann trotzdem ein
#      kaputtes STL ergeben: beim T-Call berührten die Schraubdome die
#      Kavitätswand exakt tangential (BOSS_D 5,0 gegen CLR 0,5), das gab vier
#      Kanten mit je VIER Dreiecken. Der Slicer meldet so etwas als
#      „non-manifold", CadQuery hätte es nie beanstandet.
#   3. Deckel und Schale kollidieren nicht miteinander und nicht mit der
#      Platine (die Zentrierlippe muss in die Kavität passen, nicht in sie
#      hinein).
#   4. Die Logo-Einleger belegen NICHT dieselbe Zone (beim Mehrfarbdruck ist
#      sonst nicht definiert, welches Filament gewinnt).
#   5. Ecksäulen UND Platinen-Dome sind ANGEBUNDEN, stehen also nicht als
#      Kragarm frei.
#      ⚠️⚠️ Feldbefund 2026-08-06: sie brachen reihenweise ab. Sie standen
#      **0,30 mm** von zwei Wänden entfernt — nah genug, dass es auf jedem
#      Rendering und in jedem Schnitt verbunden AUSSIEHT, aber tragend war es
#      nie. Deshalb prüft der Test einen hauchdünnen Kragen DIREKT an der
#      Säulenhaut (r+0,02 … r+0,25): was dort kein Material hat, berührt
#      nichts — egal, was 0,3 mm weiter kommt.
#
# Rückgabe 0 = alles grün, 1 = mindestens ein Befund. Ausgabe ist deutsch und
# nennt Zahlen, keine Ampeln — der nächste Leser soll selbst urteilen können.
# ============================================================================
import collections
import os
import sys

os.environ["SKIP_EXPORT"] = "1"        # Modul darf beim Import nichts schreiben
sys.path.insert(0, os.getcwd())

import cadquery as cq

MODULE = os.environ.get("MODUL") or {
    "t-call-a7670":  "tcall_case_v2",
    "t-sim7080g-s3": "sim7080_case",
    "truma-esp32-lin": "truma_case",
}.get(os.path.basename(os.getcwd()))
if not MODULE:
    sys.exit("Kein Modul erkannt — MODUL=<name> setzen.")
c = __import__(MODULE)

TOL, ATOL = 0.01, 0.1                  # wie der STL-Export der Skripte
befunde = []


def sagt(zeile, ok=True):
    print(("  " if ok else "  ⚠️ ") + zeile)
    if not ok:
        befunde.append(zeile)


def dicht(shape):
    """(Dreiecke, offene/überzählige Kanten) des tesselierten Netzes."""
    verts, tris = shape.tessellate(TOL, ATOL)
    kante = collections.Counter()
    for t in tris:
        p = [(round(verts[i].x, 4), round(verts[i].y, 4), round(verts[i].z, 4)) for i in t]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            kante[tuple(sorted((p[a], p[b])))] += 1
    return len(tris), sum(1 for v in kante.values() if v != 2)


def teile():
    """Alle benannten Körper des Moduls einsammeln — je nach Gehäuse verschieden."""
    aus = [("Unterschale", c.base, True), ("Deckel", c.lid, True),
           ("Platine (Dummy)", c.pcb, True)]
    for wp, nm in zip(getattr(c, "logo", None) or (),
                      ("Logo Ring", "Logo Akzent", "Logo Wortmarke")):
        if wp is not None:
            aus.append((nm, wp, False))     # darf mehrteilig sein
    for i, cap in enumerate(getattr(c, "caps", None) or [], 1):
        aus.append((f"Taste {i}", cap, True))
    # Prüfstücke: die Prüfscheibe des T-Call nur, wenn es Öffnungen gibt (sonst
    # prüft sie nichts); Rahmen und Wandausschnitte immer.
    if getattr(c, "OPENINGS", None) and hasattr(c, "gauge"):
        aus.append(("Prüfscheibe", c.gauge, True))
    for attr, nm in (("frame", "Prüfrahmen"), ("wallprobe", "Prüfstück Wand")):
        wp = getattr(c, attr, None)
        if wp is not None:
            aus.append((nm, wp, True))
    return aus


print(f"Modul: {MODULE}\n")
print("1) Volumenkörper")
alle = teile()
for name, wp, zusammen in alle:
    sol = wp.solids().vals()
    gut = wp.val().isValid() and (len(sol) == 1 or not zusammen)
    hinweis = ""
    if not gut:
        hinweis = "  <- erwartet: genau 1 gültiger Körper"
    elif len(sol) > 1:
        hinweis = "  (mehrteilig, hier zulässig)"
    sagt(f"{name:18s} {len(sol)} Solid(s), {wp.val().Volume():9.1f} mm³{hinweis}", gut)

print("\n2) Netz dicht (Kantenparität)")
for name, wp, _ in alle:
    n, offen = dicht(wp.val())
    sagt(f"{name:18s} {n:7d} Dreiecke, offene Kanten: {offen}", offen == 0)

print("\n3) Kollisionen")


def schnitt(a, b):
    try:
        v = a.intersect(b).val()
        return v.Volume() if v is not None else 0.0
    except Exception:
        return 0.0                     # leerer Schnitt wirft je nach Version


for n1, n2, a, b in (("Deckel", "Unterschale", c.lid, c.base),
                     ("Deckel", "Platine", c.lid, c.pcb),
                     ("Unterschale", "Platine", c.base, c.pcb)):
    v = schnitt(a, b)
    sagt(f"{n1} ∩ {n2}: {v:8.3f} mm³", v < 0.01)

logo = getattr(c, "logo", None)
if logo and len([w for w in logo if w is not None]) > 1:
    teile_l = [w for w in logo if w is not None]
    v = schnitt(teile_l[0], teile_l[1])
    sagt(f"Logo Ring ∩ Akzent: {v:8.3f} mm³"
         + ("" if v < 0.01 else "  <- doppelt belegt, Farbe undefiniert"), v < 0.01)

print("\n5) Säulen und Dome angebunden?")

def kragen_test(bez, pts, d, z0, z1):
    """Hauchdünner Kragen an der Mantelfläche: was dort nichts berührt, ist
    ein Kragarm — unabhängig davon, was 0,3 mm weiter kommt."""
    r, zmid = d / 2, (z0 + z1) / 2
    schlank = (z1 - z0) / d
    if schlank < 2.0:
        print(f"  {bez}: Schlankheit {schlank:.1f} — unkritisch, nicht geprüft")
        return
    for i, (px, py) in enumerate(pts, 1):
        kragen = (cq.Workplane("XY", origin=(px, py, zmid)).circle(r + 0.25).extrude(2.0)
                  .cut(cq.Workplane("XY", origin=(px, py, zmid - 0.1))
                       .circle(r + 0.02).extrude(2.2)))
        v = kragen.intersect(c.base).val()
        vol = v.Volume() if v is not None else 0.0
        ok = vol > 0.05
        sagt(f"{bez} {i}: {vol:6.2f} mm³ am Mantel, Schlankheit {schlank:.1f}"
             + ("" if ok else "  <- FREISTEHEND, bricht ab"), ok)

geprueft = False
if getattr(c, "PILLARS", None) and hasattr(c, "PILLAR_D"):
    kragen_test("Säule", c.PILLARS, c.PILLAR_D, c.BOTTOM, c.Z_WALL); geprueft = True
# Platinen-Dome: dieselbe Bauart, nur mitten in der Kavität statt in der Ecke —
# und mit dünnerer Wand, weil eine kleinere Schraube hineingeht.
if getattr(c, "HOLES", None) and hasattr(c, "BOSS_D") and hasattr(c, "PCB_BOT"):
    kragen_test("Dom", c.HOLES, c.BOSS_D, c.BOTTOM, c.PCB_BOT); geprueft = True
    wand = (c.BOSS_D - getattr(c, "BOSS_PILOT", 0)) / 2
    sagt(f"Domwand um das Kernloch: {wand:.2f} mm"
         + ("" if wand >= 1.8 else "  <- unter 1,8 mm, selbstschneidende Schraube sprengt sie"),
         wand >= 1.8)
if not geprueft:
    print("  (weder PILLARS noch HOLES/BOSS_D im Modul — Durchschraubung o. Ä.)")

print("\n4) Maße")
bb = c.base.val().BoundingBox()
print(f"  Außen (Schale, inkl. Laschen): {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm")
print(f"  Bauhöhe gesamt Z_TOP: {c.Z_TOP:.1f} mm")
if hasattr(c, "LOGO_DEPTH") and hasattr(c, "LID_T"):
    print(f"  Restwand unter der Logotasche: {c.LID_T - c.LOGO_DEPTH:.1f} mm")
if hasattr(c, "TOP_R"):
    print(f"  Wand über der Kantenrundung:   {c.LID_T - c.TOP_R:.1f} mm"
          + ("" if c.LID_T - c.TOP_R >= 1.0 else "   ⚠️ unter 1 mm"))

print()
if befunde:
    print(f"⚠️ {len(befunde)} Befund(e) — siehe oben.")
    sys.exit(1)
print("Alles grün.")
