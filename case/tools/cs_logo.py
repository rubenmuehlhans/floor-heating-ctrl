# ============================================================================
# camperSense-Bildmarke als CAD-Geometrie — GETEILT von allen Gehäuse-Skripten.
#
# Geometrie 1:1 aus dem Logo-Asset des Design Systems (viewBox 0 0 48 48):
#   Außenring  r = 16    Strich 5    von  48° bis 312°  (Öffnung rechts)
#   Innenbogen r = 8.5   Strich 4    von 192° bis 300°
#   Knoten     r = 4.4   am Bogenende bei 48°
#
# ⚠️ Die SVG misst Y nach UNTEN, CAD nach OBEN → alle Winkel sind gespiegelt
#    (φ = −θ). Wer die Zahlen unbesehen aus dem Asset übernimmt, bekommt ein
#    an der Waagerechten gespiegeltes Logo — es sieht „fast richtig" aus.
#
# ⚠️ Die WORTMARKE ist hier bewusst NICHT enthalten: mit 0,4-mm-Düse ist sie
#    nicht druckbar (Stege ~0,5 mm = genau EINE Extrusionsbahn, beim Slicen
#    fällt sie auseinander). Der Glyphen-Weg samt Fallen steht weiterhin in
#    `t-call-a7670/tcall_case_v2.py`; für die Gehäuse zählt nur die Marke.
#
# ⚠️ Dies ist die EINZIGE Quelle der Markengeometrie im Repo. Vorher stand sie
#    nur im T-Call-Skript; ein zweites Gehäuse hätte sie kopieren müssen, und
#    genau so entstehen die gedrifteten Zwillinge, die anderswo schon Zeit
#    gekostet haben.
# ============================================================================
import math
import cadquery as cq

SVG_EXTENT = 20.4                      # Knoten reicht am weitesten (16 + 4.4)
RING = (16.0, 5.0, -312.0, -48.0)      # r, Strich, φ von, φ bis
ARC  = (8.5, 4.0, -300.0, -192.0)
NODE = (16.0, 4.4, -48.0)              # auf r = 16 bei φ = −48°


def scale(d):
    """mm je SVG-Einheit für eine Marke mit Außendurchmesser `d`."""
    return d / (2 * SVG_EXTENT)


def arc_band(r, w, a0, a1, z0, h, s):
    """Bogen mit runden Enden: Kreisring, auf den Winkelbereich beschnitten.

    `s` = Maßstab aus `scale()`. Der Keil wird nie größer als 180° gebaut
    (darüber schneidet ein Polygon-Keil die falsche Seite weg) — bei größeren
    Bögen wird stattdessen das Gegenstück abgezogen."""
    r, w = r * s, w * s
    ring = (cq.Workplane("XY", origin=(0, 0, z0)).circle(r + w / 2).extrude(h)
            .cut(cq.Workplane("XY", origin=(0, 0, z0 - 0.1))
                 .circle(r - w / 2).extrude(h + 0.2)))
    span = a1 - a0
    big = (r + w) / math.cos(math.radians(min(span, 360 - span) / 2)) + 2

    def wedge(b0, b1):                 # Keil < 180°, deckt r+w sicher ab
        pts = [(0, 0)] + [(big * math.cos(math.radians(b)), big * math.sin(math.radians(b)))
                          for b in (b0, (b0 + b1) / 2, b1)]
        return (cq.Workplane("XY", origin=(0, 0, z0 - 0.1))
                .polyline(pts).close().extrude(h + 0.2))

    ring = ring.cut(wedge(a1, a0 + 360)) if span > 180 else ring.intersect(wedge(a0, a1))
    for a in (a0, a1):                 # runde Enden
        ring = ring.union(cq.Workplane("XY", origin=(r * math.cos(math.radians(a)),
                                                     r * math.sin(math.radians(a)), z0))
                          .circle(w / 2).extrude(h))
    return ring


def mark(d, cx, cy, z0, h):
    """Bildmarke als (Ring, Akzent), Durchmesser `d`, zentriert auf (cx, cy).

    Höhe `h` ab `z0`. Zwei Körper, damit sie im Mehrfarbdruck getrennte
    Filamente bekommen können."""
    s = scale(d)
    r, w, a0, a1 = RING
    ring = arc_band(r, w, a0, a1, z0, h, s)
    r, w, a0, a1 = ARC
    acc = arc_band(r, w, a0, a1, z0, h, s)
    nr, nw, na = NODE
    acc = acc.union(cq.Workplane("XY", origin=(nr * s * math.cos(math.radians(na)),
                                               nr * s * math.sin(math.radians(na)), z0))
                    .circle(nw * s).extrude(h))
    # ⚠️ Der Knoten sitzt AUF dem Ring — wie in der SVG liegt er obenauf. Ohne
    # diesen Abzug belegen beide Einleger dieselbe Zone (beim T-Call waren es
    # 2,5 mm³ doppelt), und im Mehrfarbdruck ist nicht definiert, wer gewinnt.
    ring = ring.cut(acc)
    return ring.translate((cx, cy, 0)), acc.translate((cx, cy, 0))
