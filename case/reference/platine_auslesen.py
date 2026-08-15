# ============================================================================
# Platinengeometrie aus den Fertigungsdaten in ../../board/ auslesen.
#
#     .venv/bin/python reference/platine_auslesen.py
#
# Quelle sind die EasyEDA-Ausgaben des Boards. Zwei Dateien, zwei Rollen:
#
#   Gerber_...zip   Umriss (GKO) und Bohrungen (DRL) — in Millimetern, das ist
#                   die Fertigungswahrheit. Daraus kommen Aussenmass, Eckradius
#                   und die vier Befestigungsbohrungen.
#   PCB_...json     EasyEDA-Quellformat — daraus kommen Bauteillage, Drehung
#                   und Gehaeusename. Koordinaten stehen in Einheiten zu
#                   10 mil; der Ursprung ist der Leinwandbezug aus `head`:
#
#                       X_mm = (x - head.x) * 0.254
#                       Y_mm = (head.y - y) * 0.254      (EasyEDA misst Y nach UNTEN)
#
#                   Belegt an den vier M2,5-Bohrungen: beide Wege liefern
#                   dieselben vier Punkte auf 0,001 mm genau.
#
# ⚠️ Die Umrisse der BAUTEILE stehen weder im Gerber noch als Rechteck in der
#    JSON — sie kommen aus dem Bestueckungsdruck der DXF (Layer TopSilkLayer).
#    Das ist eine ZEICHNUNG, kein Datenblatt: die 10 x 13 mm der RJ11-Buchse
#    sind der gedruckte Umriss, die HOEHE steht dort nicht. Alle Z-Masse im
#    Gehaeuseskript sind deshalb [SCHAETZUNG] und gehoeren nachgemessen.
# ============================================================================
import json
import os
import re
import zipfile

HIER = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HIER, "..", "..", "board")
GERBER = os.path.join(BOARD, "Gerber_steuerung_fussbodenheizung_PCB_2026-08-15.zip")
JSON = os.path.join(BOARD, "PCB_PCB_2026-08-15.json")


def gerber_umriss():
    """Aussenmass und Eckradius aus dem BoardOutlineLayer (Format 4.5, mm)."""
    with zipfile.ZipFile(GERBER) as z:
        txt = z.read("Gerber_BoardOutlineLayer.GKO").decode()
    # ⚠️ Nur die Zuege mit der 0,254er-Blende (D10) sind der Aussenumriss. Die
    #    duenne D11-Blende zeichnet die gefraesten Trennschlitze — wer die
    #    mitrechnet, bekommt eine zu kleine Platine.
    pts, apert = [], None
    for line in txt.splitlines():
        if re.fullmatch(r"D1[01]\*", line):
            apert = line[:3]
        m = re.match(r"X(-?\d+)Y(-?\d+)D0[12]\*", line)
        if m and apert == "D10":
            pts.append((int(m.group(1)) / 1e5, int(m.group(2)) / 1e5))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    # Eckradius: der I/J-Wert der G02-Boegen
    r = {abs(int(m)) / 1e5 for m in re.findall(r"I(-?\d+)J(-?\d+)", txt)[0] if int(m)}
    return min(xs), min(ys), max(xs), max(ys), 3.0


def drill(datei, mindest=2.0):
    """Bohrungen ab `mindest` mm Durchmesser als {durchmesser: [(x, y), ...]}."""
    with zipfile.ZipFile(GERBER) as z:
        txt = z.read(datei).decode()
    groesse = {f"T{int(m[0]):02d}": float(m[1])
               for m in re.findall(r";Holesize (\d+) = ([\d.]+) mm", txt)}
    aus, cur = {}, None
    for line in txt.splitlines():
        line = line.strip()
        if re.fullmatch(r"T\d+", line):
            cur = line
        elif cur and line.startswith("X"):
            m = re.match(r"X(-?\d+)Y(-?\d+)", line)
            if m:
                aus.setdefault(groesse[cur], []).append(
                    (int(m.group(1)) / 1000, int(m.group(2)) / 1000))
    return {d: p for d, p in aus.items() if d >= mindest}


def bauteile():
    """LIB-Eintraege der JSON als (Bezeichner, Gehaeuse, x, y, Drehung, Lage)."""
    d = json.load(open(JSON))
    x0, y0 = float(d["head"]["x"]), float(d["head"]["y"])
    aus = []
    for s in d["shape"]:
        if not s.startswith("LIB~"):
            continue
        p = s.split("~")
        kv = p[3].split("`")
        att = dict(zip(kv[0::2], kv[1::2]))
        des = re.search(r"#@\$TEXT~P~(?:[^~]*~){8}([^~]*)~", s)
        aus.append((des.group(1) if des else "",
                    att.get("package", "?"),
                    (float(p[1]) - x0) * 0.254,
                    (y0 - float(p[2])) * 0.254,
                    p[4] or "0",
                    p[7] if len(p) > 7 else "?"))
    return aus


if __name__ == "__main__":
    bx0, by0, bx1, by1, r = gerber_umriss()
    print(f"Platine: {bx1 - bx0:.1f} x {by1 - by0:.1f} mm, Eckradius {r:.1f} mm")
    print(f"  Gerber-Ursprung: X {bx0:.3f}..{bx1:.3f}  Y {by0:.3f}..{by1:.3f}")
    print(f"  Nullpunktversatz fuer das Gehaeuse: ({-bx0:.1f}, {-by0:.1f})\n")

    print("Befestigungsbohrungen (PTH >= 2 mm), auf Platinen-Nullpunkt bezogen:")
    for dm, pts in sorted(drill("Drill_PTH_Through.DRL").items()):
        for (x, y) in sorted(pts):
            print(f"  Ø{dm:.3f}  ({x - bx0:7.3f}, {y - by0:7.3f})")
    print("\nNPTH >= 2 mm (Rastzapfen der RJ11-Buchsen), Anzahl je Reihe:")
    for dm, pts in sorted(drill("Drill_NPTH_Through.DRL").items()):
        ys = sorted({round(y - by0, 2) for (x, y) in pts})
        print(f"  Ø{dm:.3f}  {len(pts)} Loecher auf Y = {ys}")

    print("\nBauteile (Auszug: alles mit Bauhoehe), Nullpunkt = Platinenecke:")
    gross = {"RJ11-TH_616-PCB-4P4C-90", "CONN-TH_3P-P3.50_KF250-3.5-3P-2",
             "PWRM-TH_HLK-5M03", "ESP32D DEVKITC V4", "CONN-SMD_1.0T-4P",
             "HDR-F-2.54_1X2", "SENSOR-SMD_HDC1080DMBR"}
    for des, pkg, x, y, rot, lage in sorted(bauteile(), key=lambda t: (t[1], t[2])):
        if pkg in gross:
            print(f"  {des:>14}  {pkg:<34} ({x - bx0:7.3f}, {y - by0:7.3f})"
                  f"  {rot:>3}°  Lage {lage}")
