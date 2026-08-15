"""Bambu-Studio-Projekt (.3mf) fuer das Gehaeuse — Logo als eigene TEILE.

    cd ../t-call-a7670
    <venv>/python ../tools/make_3mf.py campersense-box-v2.3mf

Das Gehaeuse-Modul wird am Verzeichnisnamen erkannt (`MODUL=<name>` ueber-
schreibt das), der Dateiname faellt daraus, wenn kein Argument kommt.

Ergebnis: eine Datei, zwei Objekte auf der Platte.
  * "Deckel" = EIN Objekt mit VIER Teilen (Korpus, Ring, Akzent, Wortmarke).
    Jedem Teil ist ein Extruder zugewiesen, in Bambu Studio also je Teil eine
    Farbe auswaehlbar — genau das, was mit vier getrennten STL nicht geht.
  * "Unterschale" als zweites Objekt.

Beides steht in DRUCKSTELLUNG (Deckel-Oberseite aufs Bett) und auf der Platte
vorpositioniert.

⚠️ Erzeugt wird mit LOGO_SEPARAT=0, das Logo ist also in den Deckel
   geschnitten und die Einleger fuellen die Taschen passgenau.
⚠️ Bambu Studio SCHREIBT 3MF mit der Production-Extension (eine Datei je
   Objekt unter 3D/Objects/). Zum LESEN genuegt ein einfaches Kern-3MF plus
   Metadata/model_settings.config — genau das steht hier.
"""
import os, sys, zipfile

os.environ["LOGO_SEPARAT"] = "0"        # Logo in den Deckel schneiden
os.environ.setdefault("LOGO_WORD", "0") # Wortmarke aus: mit 0,4-mm-Duese nicht druckbar
os.environ["SKIP_EXPORT"] = "1"         # Modul soll keine Dateien schreiben
sys.path.insert(0, os.getcwd())
import cadquery as cq

HIER = os.path.basename(os.getcwd())
MODULE, DEFAULT_OUT = {
    "t-call-a7670":  ("tcall_case_v2", "campersense-box-v2.3mf"),
    "t-sim7080g-s3": ("sim7080_case", "campersense-sim7080-s3.3mf"),
    "truma-esp32-lin": ("truma_case", "campersense-truma.3mf"),
    "case": ("fbh_case", "fussbodenheizung-gehaeuse.3mf"),
}.get(HIER, ("tcall_case_v2", "campersense-box-v2.3mf"))
MODULE = os.environ.get("MODUL", MODULE)
c = __import__(MODULE)

OUT = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT

# ⚠️⚠️ ABWEICHUNG gegenueber der Fassung in smartCamper/hardware/case/tools:
# dort liegen Gehaeuse UND Pruefstuecke immer gemeinsam auf EINER Platte und
# es kippen nur die `printable`-Flags. Das traegt, solange beides zusammen auf
# die Platte passt. Hier passt es nicht: Deckel und Schale belegen mit
# 185 x 103 mm allein schon zwei volle Reihen der 256er-Platte, und der
# Displayausschnitt ist 54 mm tief. Deshalb waehlt PLATTE aus, WAS ueberhaupt
# platziert wird — drei Dateien statt einer mit umgeschalteten Flags:
#
#   PLATTE=gehaeuse  (Vorgabe)  Deckel mit Logo-Teilen + Unterschale
#   PLATTE=zubehoer              Displayrahmen + Hutschienen-Clips
#   PLATTE=proben                Pruefstueck Suedwand + Pruefstueck Display
#
# PROBEN=1 bleibt als Schreibweise erhalten und bedeutet PLATTE=proben.
PLATTE = os.environ.get("PLATTE", "proben" if os.environ.get("PROBEN", "0") != "0"
                        else "gehaeuse")
if PLATTE not in ("gehaeuse", "zubehoer", "proben"):
    sys.exit(f"PLATTE={PLATTE} unbekannt — gehaeuse | zubehoer | proben")
MIT_GEHAEUSE = PLATTE == "gehaeuse"
MIT_ZUBEHOER = PLATTE == "zubehoer"
MIT_PROBEN = PLATTE == "proben"
TOL, ATOL = 0.008, 0.15
BED = 256.0                              # A1: 256 x 256 mm

# Extruder-Zuordnung: 1 = Gehaeusefarbe, 2 = Ring, 3 = Akzent, 4 = Wortmarke.
# Die Wortmarke faellt weg, wenn LOGO_WORD=0 (Standard) — dann drei Filamente.
LID_PARTS = [("Deckel", c.lid, 1)]
for nm, wp in zip(("Logo Ring", "Logo Akzent", "Logo Wortmarke"), c.logo or ()):
    if wp is not None:
        LID_PARTS.append((nm, wp, len(LID_PARTS) + 1))
# ⚠️ LID_EXTRA: Teile, die IN DERSELBEN POSE wie der Deckel gedruckt werden
# muessen — z. B. eine print-in-place-Klappe (truma-sat-board). `caps` waere
# falsch: die werden separat auf der Platte platziert, und eine Klappe, die
# neben ihrem Scharnier liegt, ist keine Klappe.
for nm, wp in getattr(c, "LID_EXTRA", []) or []:
    LID_PARTS.append((nm, wp, len(LID_PARTS) + 1))

# ⚠️ Ein Deckel mit BUCKEL (truma-sat-board) kann nicht kopfueber gedruckt
# werden — er laege auf dem Buckeldach. Solche Module setzen LID_PRINT_FLIP =
# False und werden Oberseite-OBEN gedruckt; der Versatz nutzt fuer ALLE
# Deckel-Teile dieselbe lid_bb, damit die Logo-Einleger ausgerichtet bleiben.
LID_FLIP = getattr(c, "LID_PRINT_FLIP", True)

def print_pose(wp, bb):
    if not LID_FLIP:
        return wp.translate((0, 0, -bb.zmin))
    """Oberseite aufs Bett — dieselbe Drehung wie beim STL-Export."""
    return wp.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, bb.ymin + bb.ymax, bb.zmax))

def mesh_xml(shape, dx, dy):
    """Tesselieren und als 3MF-<mesh> ausgeben; dx/dy schiebt auf die Platte.

    ⚠️ VERTICES VERSCHWEISSEN. CadQuerys `tessellate()` trianguliert jede Flaeche
    einzeln und teilt Randpunkte NICHT — fuer STL (rein koordinatenbasiert) egal,
    fuer indiziertes 3MF fatal: Bambu Studio meldete sonst 5016 Einzelteile und
    ~40000 offene Kanten. Runden auf 0,1 um fuehrt zusammen, was zusammengehoert
    (kleinstes echtes Merkmal ist ~0,4 mm)."""
    verts, tris = shape.tessellate(TOL, ATOL)
    idx, uniq, remap = {}, [], []
    for p in verts:
        key = (round(p.x, 4), round(p.y, 4), round(p.z, 4))
        j = idx.get(key)
        if j is None:
            j = idx[key] = len(uniq)
            uniq.append(key)
        remap.append(j)
    v = "".join(f'<vertex x="{x + dx:.4f}" y="{y + dy:.4f}" z="{z:.4f}"/>' for x, y, z in uniq)
    keep = [(remap[a], remap[b], remap[k]) for a, b, k in tris]
    keep = [t for t in keep if t[0] != t[1] and t[1] != t[2] and t[0] != t[2]]
    t = "".join(f'<triangle v1="{a}" v2="{b}" v3="{k}"/>' for a, b, k in keep)
    return f"<mesh><vertices>{v}</vertices><triangles>{t}</triangles></mesh>", len(keep)

# ---- Deckelgruppe in Druckstellung, gemeinsame Bezugsbox ----
lid_bb = c.lid.val().BoundingBox()
lid_posed = ([(n, print_pose(wp, lid_bb), e) for n, wp, e in LID_PARTS]
             if MIT_GEHAEUSE else [])
lb = lid_posed[0][1].val().BoundingBox() if lid_posed else lid_bb
bb = c.base.val().BoundingBox()          # Schale steht schon richtig (Boden unten)

# ---- Plattenbelegung: REGALPACKEN ----
# ⚠️⚠️ Vorher lagen Deckel und Schale auf zwei FESTEN Positionen, und alles
# Weitere musste danebenpassen. Das trug genau so lange, wie es zwei Teile
# waren: der Truma-Pruefrahmen ist so gross wie das Gehaeuse selbst (97 x 76),
# und danach gab es keine feste Stelle mehr, die frei ist. Jetzt werden ALLE
# Objekte zeilenweise gepackt, nach Tiefe sortiert (grosse zuerst).
GAP = 8.0

def pack(units, bed=BED, gap=GAP):
    """units: [(key, bbox)] -> ({key: (dx, dy)}, belegte Breite, Tiefe).

    Zeilen laufen in X, die Zeilen selbst in Y. Danach wird der ganze Block
    mittig auf die Platte geschoben."""
    reihen, cur, cx, cd = [], [], gap, 0.0
    for key, b in sorted(units, key=lambda u: -u[1].ylen):
        if cur and cx + b.xlen + gap > bed:
            reihen.append((cur, cd)); cur, cx, cd = [], gap, 0.0
        cur.append((key, b, cx)); cx += b.xlen + gap; cd = max(cd, b.ylen)
    if cur:
        reihen.append((cur, cd))
    pos, y = {}, gap
    for zeile, tiefe in reihen:
        for key, b, x in zeile:
            pos[key] = (x - b.xmin, y + (tiefe - b.ylen) / 2 - b.ymin)
        y += tiefe + gap
    d = dict(units)
    xs = [pos[k][0] + d[k].xmin for k in pos] + [pos[k][0] + d[k].xmax for k in pos]
    ys = [pos[k][1] + d[k].ymin for k in pos] + [pos[k][1] + d[k].ymax for k in pos]
    sx = (bed - (max(xs) - min(xs))) / 2 - min(xs)
    sy = (bed - (max(ys) - min(ys))) / 2 - min(ys)
    return ({k: (dx + sx, dy + sy) for k, (dx, dy) in pos.items()},
            max(xs) - min(xs), max(ys) - min(ys))

units = [("lid", lb), ("base", bb)] if MIT_GEHAEUSE else []

# Tasten: auf den Kopf stellen (Kopf aufs Bett = keine Stuetze fuer den Flansch).
caps_up = []
if MIT_GEHAEUSE:
    for i, cap in enumerate(getattr(c, "caps", []) or []):
        cb = cap.val().BoundingBox()
        up = cap.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, cb.ymin + cb.ymax, cb.zmax))
        caps_up.append((f"Taste {i+1}", up))
        units.append((f"cap{i}", up.val().BoundingBox()))
# Zubehoerteile (EXTRA_PARTS des Moduls) liegen auf der EIGENEN Platte und
# werden NICHT gedreht — sie kommen aus dem Modul schon in Druckstellung.
if MIT_ZUBEHOER:
    for nm, wp in getattr(c, "EXTRA_PARTS", []) or []:
        b = wp.val().BoundingBox()
        caps_up.append((nm, wp.translate((0, 0, -b.zmin))))
        units.append((f"cap{len(caps_up)-1}", caps_up[-1][1].val().BoundingBox()))

# ---- Pruefstuecke: liegen MIT auf der Platte, aber printable=0 ----
# ⚠️ So laesst sich in Bambu Studio einzeln scharfschalten (Rechtsklick →
# druckbar) und zuerst NUR das Pruefstueck drucken. Ohne das Flag wuerde jeder
# Slice-Lauf sie mitdrucken, und wer das Gehaeuse will, muesste sie jedes Mal
# von Hand loeschen.
# ⭐ PROBEN=1 dreht die Zuordnung um: Pruefstuecke druckbar, Deckel und Schale
# nicht. Damit ist der ERSTE Druck (Passprobe, s. README des Gehaeuses) ein
# Datei-Oeffnen und ein Klick auf Slice — ohne Rechtsklick-Fummelei, bei der
# man leicht das falsche Objekt erwischt. Gleiche Plattenpositionen, gleiche
# Objekte, nur die Flags kippen.
NUR_PROBEN = MIT_PROBEN
PR_TEIL   = "1"                             # was auf der Platte liegt, wird gedruckt
PR_PROBE  = "1"

PROBEN = []
if MIT_PROBEN:
    for attr, nm in (("gauge", "Pruefscheibe"), ("frame", "Pruefrahmen"),
                     ("wallprobe", "Pruefstueck Wand")):
        wp = getattr(c, attr, None)
        if wp is not None:
            PROBEN.append((nm, wp))
            units.append((f"probe{len(PROBEN)-1}", wp.val().BoundingBox()))

if not units:
    sys.exit(f"PLATTE={PLATTE}: das Modul {MODULE} liefert dafuer keine Teile.")
POS, span_x, span_y = pack(units)
lid_dx, lid_dy = POS.get("lid", (0.0, 0.0))
base_dx, base_dy = POS.get("base", (0.0, 0.0))
caps_posed = [(nm, up, *POS[f"cap{i}"]) for i, (nm, up) in enumerate(caps_up)]
proben_posed = [(nm, pr, *POS[f"probe{k}"]) for k, (nm, pr) in enumerate(PROBEN)]

objs, faces = [], {}
for i, (name, wp, extr) in enumerate(lid_posed, start=1):
    m, n = mesh_xml(wp.val(), lid_dx, lid_dy)
    objs.append(f'<object id="{i}" type="model">{m}</object>')
    faces[i] = n
BASE_ID = len(lid_posed) + 1
if MIT_GEHAEUSE:
    m, n = mesh_xml(c.base.val(), base_dx, base_dy)
    objs.append(f'<object id="{BASE_ID}" type="model">{m}</object>')
    faces[BASE_ID] = n
    # ⚠️ BEIDE Objekte als Component-Container: ein direkt referenziertes
    # Mesh-Objekt zentriert Bambu um seinen eigenen Schwerpunkt und ignoriert
    # die eingebackene Plattenposition (die Schale kam mit min_z = -9,5 an).
    objs.append('<object id="10" type="model"><components>'
                + "".join(f'<component objectid="{i}"/>'
                          for i in range(1, len(lid_posed) + 1))
                + "</components></object>")
    objs.append(f'<object id="11" type="model"><components>'
                f'<component objectid="{BASE_ID}"/></components></object>')
PROBE_ID0 = 40
for k, (nm, wp, dx, dy) in enumerate(proben_posed):
    m, n = mesh_xml(wp.val(), dx, dy)
    objs.append(f'<object id="{PROBE_ID0 + k}" type="model">{m}</object>')
    faces[PROBE_ID0 + k] = n
    objs.append(f'<object id="{50 + k}" type="model"><components>'
                f'<component objectid="{PROBE_ID0 + k}"/></components></object>')

CAP_ID0 = BASE_ID + 1
for k, (name, wp, dx, dy) in enumerate(caps_posed):
    m, n = mesh_xml(wp.val(), dx, dy)
    objs.append(f'<object id="{CAP_ID0 + k}" type="model">{m}</object>')
    faces[CAP_ID0 + k] = n
    objs.append(f'<object id="{20 + k}" type="model"><components>'
                f'<component objectid="{CAP_ID0 + k}"/></components></object>')

model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
         '<model unit="millimeter" xml:lang="en-US"'
         ' xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02"'
         ' xmlns:BambuStudio="http://schemas.bambulab.com/package/2021">\n'
         ' <metadata name="Application">camperSense CadQuery</metadata>\n'
         ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n'
         ' <resources>' + "".join(objs) + '</resources>\n'
         ' <build>'
         + (f'<item objectid="10" transform="1 0 0 0 1 0 0 0 1 0 0 0" printable="{PR_TEIL}"/>'
            f'<item objectid="11" transform="1 0 0 0 1 0 0 0 1 0 0 0" printable="{PR_TEIL}"/>'
            if MIT_GEHAEUSE else "")
         + "".join(f'<item objectid="{20+k}" transform="1 0 0 0 1 0 0 0 1 0 0 0" printable="{PR_TEIL}"/>'
                   for k in range(len(caps_posed)))
         + "".join(f'<item objectid="{50+k}" transform="1 0 0 0 1 0 0 0 1 0 0 0" printable="{PR_PROBE}"/>'
                   for k in range(len(proben_posed)))
         + '</build>\n</model>\n')

IDENT4 = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"
def part(pid, name, extr):
    return (f'    <part id="{pid}" subtype="normal_part">\n'
            f'      <metadata key="name" value="{name}"/>\n'
            f'      <metadata key="matrix" value="{IDENT4}"/>\n'
            f'      <metadata key="extruder" value="{extr}"/>\n'
            f'      <mesh_stat face_count="{faces[pid]}" edges_fixed="0" degenerate_facets="0"'
            f' facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n'
            f'    </part>\n')

gehaeuse_cfg = ('  <object id="10">\n'
                f'    <metadata key="name" value="Deckel ({len(lid_posed)} Farben)"/>\n'
                '    <metadata key="extruder" value="1"/>\n'
                + "".join(part(i, n, e) for i, (n, _, e) in enumerate(lid_posed, start=1))
                + '  </object>\n'
                '  <object id="11">\n'
                '    <metadata key="name" value="Unterschale"/>\n'
                '    <metadata key="extruder" value="1"/>\n'
                + part(BASE_ID, "Unterschale", 1)
                + '  </object>\n') if MIT_GEHAEUSE else ""

settings = ('<?xml version="1.0" encoding="UTF-8"?>\n<config>\n'
            + gehaeuse_cfg
            + "".join(f'  <object id="{20+k}">\n'
                      f'    <metadata key="name" value="{n}"/>\n'
                      f'    <metadata key="extruder" value="1"/>\n'
                      + part(CAP_ID0 + k, n, 1) + '  </object>\n'
                      for k, (n, _, _, _) in enumerate(caps_posed))
            + "".join(f'  <object id="{50+k}">\n'
                      f'    <metadata key="name" value="{n}"/>\n'
                      f'    <metadata key="extruder" value="1"/>\n'
                      + part(PROBE_ID0 + k, n, 1) + '  </object>\n'
                      for k, (n, _, _, _) in enumerate(proben_posed))
            + '</config>\n')

content_types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                 '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
                 ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
                 ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
                 '</Types>\n')
rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
        ' <Relationship Target="/3D/3dmodel.model" Id="rel-1"'
        ' Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
        '</Relationships>\n')

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", content_types)
    z.writestr("_rels/.rels", rels)
    z.writestr("3D/3dmodel.model", model)
    z.writestr("Metadata/model_settings.config", settings)

print(f"{OUT}  (PLATTE={PLATTE})")
for i, (n, _, e) in enumerate(lid_posed, start=1):
    print(f"   Teil {i}: {n:16s} Extruder {e}  {faces[i]:6d} Dreiecke")
if MIT_GEHAEUSE:
    print(f"   Objekt 11: Unterschale    Extruder 1  {faces[BASE_ID]:6d} Dreiecke")
for k, (n, _, _, _) in enumerate(caps_posed):
    print(f"   Objekt {20+k}: {n:20s} Extruder 1  {faces[CAP_ID0+k]:6d} Dreiecke")
for k, (n, _, _, _) in enumerate(proben_posed):
    print(f"   Objekt {50+k}: {n:20s} druckbar      {faces[PROBE_ID0+k]:6d} Dreiecke")
print(f"   Platte {BED:.0f} mm: Block belegt {span_x:.0f} x {span_y:.0f} mm")
_boxes = []
for nm, b, dx, dy in (([("Deckel", lb, lid_dx, lid_dy),
                        ("Schale", bb, base_dx, base_dy)] if MIT_GEHAEUSE else [])
                      + [(n, w.val().BoundingBox(), x, y) for n, w, x, y in caps_posed]
                      + [(n, w.val().BoundingBox(), x, y) for n, w, x, y in proben_posed]):
    x0, x1, y0, y1 = b.xmin + dx, b.xmax + dx, b.ymin + dy, b.ymax + dy
    warn = "" if (0 <= x0 and x1 <= BED and 0 <= y0 and y1 <= BED) else "   ⚠️ AUSSERHALB DER PLATTE"
    print(f"     {nm:20s} x {x0:6.1f}..{x1:6.1f}  y {y0:6.1f}..{y1:6.1f}{warn}")
    for pn, px0, px1, py0, py1 in _boxes:
        if px0 < x1 and x0 < px1 and py0 < y1 and y0 < py1:
            print(f"     ⚠️⚠️ {nm} UEBERLAPPT {pn}")
    _boxes.append((nm, x0, x1, y0, y1))
