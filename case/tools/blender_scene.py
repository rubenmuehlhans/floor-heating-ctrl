# Blender-Szene fuer das camperSense-Gehaeuse — headless via bpy, Blender 5.x.
#
# Zweistufig, weil Blender ein MESH-Modeller ist: die exakte Geometrie kommt aus
# CadQuery, Blender macht nur das Bild.
#   1) cd ../t-call-a7670
#      LOGO_SEPARAT=0 <venv>/python ../tools/render_export.py /tmp/rend
#   2) /Applications/Blender.app/Contents/MacOS/Blender -b \
#        -P ../tools/blender_scene.py -- /tmp/hero.png 256 hero
#      (Skript liest die r_*.stl aus SEINEM Verzeichnis -> vorher dorthin kopieren
#       oder das Skript neben die STLs legen)
#
# Argumente: <ausgabe.png> <samples> <hero|explo|lid|shell|east|innen>
# Umgebung:  EXP (Belichtung, Default 0.30), VT (Ansichtstransform, Default Standard)
import bpy, sys, math, os
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[0] if argv else "/tmp/case.png"
SAMPLES = int(argv[1]) if len(argv) > 1 else 48
MODE = argv[2] if len(argv) > 2 else "hero"
EXP = float(os.environ.get("EXP", "0.30"))
VT  = os.environ.get("VT", "Standard")   # AgX entsaettigt Teal/Amber sichtbar
HERE = os.path.dirname(os.path.abspath(__file__))
S = 0.02                                  # mm -> Szeneneinheiten (Box ~1,9 lang)

# ---- leere Szene ----
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

def import_stl(path):
    if not os.path.exists(path):        # Wortmarke fehlt bei LOGO_WORD=0
        return None
    before = set(bpy.data.objects)
    if hasattr(bpy.ops.wm, "stl_import"):          # Blender 4.2+ / 5.x
        bpy.ops.wm.stl_import(filepath=path, global_scale=1.0)
    else:
        bpy.ops.import_mesh.stl(filepath=path)     # Fallback aeltere Versionen
    new = list(set(bpy.data.objects) - before)
    return new[0] if new else None

def mat(name, rgb, rough=0.45, metal=0.0):
    m = bpy.data.materials.new(name)
    if not m.use_nodes:
        m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*rgb, 1.0)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = metal
    return m

def srgb(h):                                       # Hex -> linear (Blender rechnet linear)
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return tuple(lin(int(h[i:i+2], 16)) for i in (0, 2, 4))

PARTS = [
    ("r_base.stl",        mat("PETG_Body",  srgb("2B3138"), 0.52)),
    ("r_lid.stl",         mat("PETG_Lid",   srgb("2B3138"), 0.52)),
    ("r_pcb.stl",         mat("PCB",        srgb("14532D"), 0.60)),
    ("r_logo_ring.stl",   mat("Logo_Ring",  srgb("34B3C2"), 0.38)),
    ("r_logo_akzent.stl", mat("Logo_Akz",   srgb("E9A63B"), 0.38)),
    ("r_logo_word.stl",   mat("Logo_Word",  srgb("EAEFF1"), 0.42)),
    ("r_mod1.stl",        mat("Modul1",     srgb("1B3A63"), 0.55)),
    ("r_mod2.stl",        mat("Modul2",     srgb("1B3A63"), 0.55)),
    ("r_taste1.stl",      mat("Taste1",     srgb("C9CFD4"), 0.45)),
    ("r_taste2.stl",      mat("Taste2",     srgb("C9CFD4"), 0.45)),
    ("r_taste3.stl",      mat("Taste3",     srgb("C9CFD4"), 0.45)),
]

objs = []
for fn, m in PARTS:
    o = import_stl(os.path.join(HERE, fn))
    if o is None:
        print("   uebersprungen (nicht vorhanden):", fn); continue
    o.name = fn[2:-4]
    o.data.materials.clear(); o.data.materials.append(m)
    o.data.polygons.foreach_set("use_smooth", [False] * len(o.data.polygons))
    objs.append(o)
print(f"importiert: {len(objs)} Teile")

# ---- alles skalieren ----
for o in objs:
    o.scale = (S, S, S)

# Explosionsansicht: Deckel + Logo nach oben, Platine leicht darunter.
# ⚠️ NACH dem Skalieren und mit S multiplizieren: `location` ist in WELT-
# einheiten und von der Objektskalierung unberuehrt — 26.0 waeren hier 26
# Einheiten statt 26 mm (Faktor 50 zu weit, alles aus dem Bild).
if MODE == "explo":
    for o in objs:
        if o.name.startswith(("lid", "logo", "taste")):
            o.location.z += 26.0 * S
        elif o.name.startswith(("pcb", "mod")):
            o.location.z += 9.0 * S

# Einzelteil-Ansichten: nicht gebrauchte Teile aus der Szene werfen
KEEP = {"lid": ("lid", "logo", "taste"),
        "shell": ("base", "pcb", "mod"),
        "innen": ("base", "pcb", "mod")}.get(MODE)
if KEEP:
    # ⚠️ ERST die Listen bilden, DANN loeschen: nach bpy.data.objects.remove()
    # ist die Python-Referenz toter StructRNA und jeder Zugriff wirft.
    keep = [o for o in objs if o.name.startswith(KEEP)]
    for o in [o for o in objs if not o.name.startswith(KEEP)]:
        bpy.data.objects.remove(o, do_unlink=True)
    objs = keep
if MODE == "lid":                     # in DRUCKposition zeigen (Oberseite aufs Bett)
    for o in objs:
        o.rotation_euler[0] = math.pi
bpy.context.view_layer.update()
mn = Vector(( 1e9,  1e9,  1e9)); mx = Vector((-1e9, -1e9, -1e9))
for o in objs:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        mn = Vector(map(min, mn, w)); mx = Vector(map(max, mx, w))
ctr = (mn + mx) / 2
for o in objs:
    o.location -= Vector((ctr.x, ctr.y, mn.z))     # auf z=0 stellen, xy zentriert
bpy.context.view_layer.update()
span = (mx - mn)
print(f"Szenengroesse: {span.x:.2f} x {span.y:.2f} x {span.z:.2f}")

# ---- Boden ----
bpy.ops.mesh.primitive_plane_add(size=40)
floor = bpy.context.object
floor.name = "Floor"
floor.data.materials.append(mat("Floor", srgb("6E757C"), 0.65))

# ---- Licht: Key / Fill / Rim ----
def area(name, loc, energy, size, target=Vector((0, 0, span.z * 0.35))):
    bpy.ops.object.light_add(type="AREA", location=loc)
    L = bpy.context.object
    L.name = name
    L.data.energy = energy
    L.data.size = size
    d = (target - Vector(loc)).normalized()
    L.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    return L

area("Key",  ( 2.6,  -2.2, 3.4), 620*EXP, 4.0)
area("Fill", (-3.0,  -1.4, 1.8), 200*EXP, 5.0)
area("Rim",  (-1.2,   3.2, 2.6), 300*EXP, 3.0)

sc.world = bpy.data.worlds.new("World")
if not sc.world.use_nodes:
    sc.world.use_nodes = True
sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.075*EXP, 0.08*EXP, 0.085*EXP, 1)
sc.world.node_tree.nodes["Background"].inputs[1].default_value = 1.0

# ---- Kamera ----
CD = 1.0 + max(0.0, span.z - 0.45) * 1.15          # Explosion braucht Abstand
# Stirnseite: Kamera OESTLICH der Ostwand (span.x/2), sonst schaut sie an ihr vorbei
# ⚠️ Die Innenansicht braucht ihren EIGENEN Abstand aus der Grundflaeche —
# CD haengt an span.z, und die ist bei einer offenen Schale klein. Mit CD kam
# die Kamera auf 3,6 Einheiten und sah eine Ecke in Grossaufnahme.
DI = max(span.x, span.y) * 1.95
POS = ((span.x/2 + 2.7, -2.3, 1.5) if MODE == "east"
       else (0.0, -0.55*DI, 0.95*DI) if MODE == "innen"
       else (3.1*CD, -3.4*CD, 2.5*CD))
bpy.ops.object.camera_add(location=POS)
cam = bpy.context.object
cam.data.lens = 110 if MODE == "east" else 50 if MODE == "innen" else 85
AIM = (Vector((span.x/2 - 0.25, 0, span.z * 0.45)) if MODE == "east"
       else Vector((0, 0, span.z * 0.30)) if MODE == "innen"
       else Vector((0, 0, span.z * 0.45)))
look = AIM - cam.location
cam.rotation_euler = look.to_track_quat("-Z", "Y").to_euler()
sc.camera = cam

# ---- Render ----
sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = SAMPLES
sc.cycles.use_denoising = True
sc.render.resolution_x, sc.render.resolution_y = 1500, 950
sc.render.film_transparent = False
sc.view_settings.view_transform = VT
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("gerendert ->", OUT)
