"""Teile in KONSTRUKTIONSLAGE als STL exportieren — Vorstufe fuer blender_scene.py.

    cd ../t-call-a7670
    LOGO_SEPARAT=0 <venv>/python ../tools/render_export.py /tmp/rend

Das Modul wird am Verzeichnisnamen erkannt (`MODUL=<name>` überschreibt das),
damit dasselbe Werkzeug fuer alle Gehaeuse taugt.

LOGO_SEPARAT=0 schneidet das Logo in den Deckel, damit das Bild das fertige
Produkt zeigt. Der Deckel wird hier BEWUSST nicht in Druckstellung gedreht:
in Konstruktionslage stehen alle Teile zusammengebaut.
"""
import sys, os
os.environ["SKIP_EXPORT"] = "1"   # Modul soll beim Import nichts schreiben
from cadquery import exporters
sys.path.insert(0, os.getcwd())
MODULE = os.environ.get("MODUL") or {
    "t-call-a7670":   "tcall_case_v2",
    "t-sim7080g-s3":  "sim7080_case",
    "truma-esp32-lin": "truma_case",
}.get(os.path.basename(os.getcwd()), "tcall_case_v2")
c = __import__(MODULE)
print(f"Modul: {MODULE}")

out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)
parts = [(c.base, "base"), (c.lid, "lid"), (c.pcb, "pcb")]
for nm, wp in getattr(c, "LID_EXTRA", []) or []:
    parts.append((wp, nm.lower().replace(" ", "_")))
# Gehaeuse ohne eigene Platine (Truma) haben mehrere Modul-Dummys — sie zeigen
# im Innenbild, was wo liegt.
for i, m in enumerate(getattr(c, "mods", None) or [], 1):
    parts.append((m, f"mod{i}"))
for wp, nm in zip(c.logo or (), ("logo_ring", "logo_akzent", "logo_word")):
    if wp is not None:                   # Wortmarke fehlt bei LOGO_WORD=0
        parts.append((wp, nm))
# Tasten in RUHELAGE zeigen (Stempel liegt auf dem Taster), nicht in der
# konstruierten Oberstellung — sonst stehen sie im Bild um CAP_GAP zu hoch.
for i, cap in enumerate(getattr(c, "caps", []) or [], 1):
    # ⚠️ CAP_GAP ist optional: die T-Call-Tasten schweben um den Spalt und werden
        # fuers Bild abgesenkt; die Truma-Klappe sitzt schon an ihrem Platz (0).
        parts.append((cap.translate((0, 0, -getattr(c, "CAP_GAP", 0))), f"taste{i}"))
for wp, name in parts:
    exporters.export(wp, os.path.join(out, f"r_{name}.stl"), tolerance=0.004, angularTolerance=0.1)
    print(" ", name)
