#!/usr/bin/env python3
"""Prueft, ob die Vorgaben der Konfigurationsablage zu den Rechenmodulen passen.

Das Heizungsgeraet haelt seine Vorgaben zweimal: in den Rechenmodulen unter
components/heatlogic (bp_defaults, burner_defaults, ...) und in der
Konfigurationsablage, aus der ein neues Geraet seine Einstellungen bezieht. Die
beiden sind schon einmal auseinandergelaufen -- die Kesselkreispumpe hatte im
Modul 3,0/2,0 und in der Ablage noch 1,0/0,5, und die Ablage gewinnt.

Dasselbe gilt fuer die Verteiler-Oberflaeche: Sie legt neue Raeume mit eigenen
Werten an, und die gehen beim Speichern so ins Geraet. Verglichen werden beide
Stellen mit room_defaults().

    python3 tools/check_defaults.py
"""
import pathlib, re, sys

WURZEL = pathlib.Path(__file__).resolve().parent.parent
ABLAGE = WURZEL / "apps/heatsource/components/config_store/config_store.c"

PAARE = [
    ("boiler_pump", "boilerpump.c", "bp_defaults",
     ["on_k", "off_k", "hold_s", "min_run_s", "min_pause_s", "emergency_c"]),
    ("burner", "heatlogic.c", "burner_defaults",
     ["delta_on_k", "delta_off_k", "swing_k", "on_hold_s", "off_hold_s", "duese_l_h"]),
    ("buffer", "heatlogic.c", "charge_defaults",
     ["spread_full_k", "spread_hold_s", "voll_c", "leer_c", "warn_c", "kessel_hot_c",
      "lern_drop_k"]),
]
# Felder, die in Ablage und Modul verschieden heissen.
UMBENANNT = {("buffer", "zapf_drop_k"): ("zapfung.c", "zapf_defaults", "drop_k"),
             ("buffer", "zapf_win_s"): ("zapfung.c", "zapf_defaults", "win_s")}
# Heizkreise haben in der Ablage eine eigene Vorgabefunktion.
KREIS = ["overrun_s", "min_run_s", "min_pause_s", "min_buffer_c", "frost_c"]


def zahl(text, muster):
    m = re.search(muster + r"\s*=\s*([-0-9.]+)f?;", text)
    return float(m.group(1)) if m else None


def rumpf(text, name):
    a = text.index(f"void {name}(")
    return text[a:text.index("\n}\n", a)]


def modul(datei, name):
    return rumpf((WURZEL / "components/heatlogic" / datei).read_text(), name)


ablage = ABLAGE.read_text()
vorgaben = rumpf(ablage, "cfg_defaults")
vergleiche = []  # (Feld, erste Quelle, Wert, zweite Quelle, Wert)
for gruppe, datei, fn, felder in PAARE:
    m = modul(datei, fn)
    for feld in felder:
        vergleiche.append((f"{gruppe}.{feld}", "Ablage", zahl(vorgaben, rf"out->{gruppe}\.{feld}"),
                           fn, zahl(m, rf"cfg->{feld}")))
for (gruppe, feld), (datei, fn, mf) in UMBENANNT.items():
    vergleiche.append((f"{gruppe}.{feld}", "Ablage", zahl(vorgaben, rf"out->{gruppe}\.{feld}"),
                       fn, zahl(modul(datei, fn), rf"cfg->{mf}")))
kreis = rumpf(ablage, "cfg_circuit_defaults")
m = modul("heatlogic.c", "pump_defaults")
for feld in KREIS:
    vergleiche.append((f"circuits[].{feld}", "Ablage", zahl(kreis, rf"c->{feld}"),
                       "pump_defaults", zahl(m, rf"cfg->{feld}")))

# Neue Raeume der Verteiler-Oberflaeche gegen room_defaults() des Verteilers.
raum = rumpf((WURZEL / "components/config_store/config_store.c").read_text()
             .replace("static void room_defaults", "void room_defaults"), "room_defaults")
oberflaeche = (WURZEL / "apps/manifold/main/www/rumpf.html").read_text()
stellen = [m.start() for m in re.finditer(r'mode: "heat",\s*target_c:', oberflaeche)]
if not stellen:
    vergleiche.append(("neuer Raum", "Oberflaeche", None, "room_defaults", None))
for n, stelle in enumerate(stellen, 1):
    text = oberflaeche[stelle:oberflaeche.index("}", stelle)]
    for feld in ["target_c", "p_band_k", "interval_s", "min_delta", "step"]:
        w = re.search(rf"{feld}:\s*([-0-9.]+)", text)
        vergleiche.append((f"neuer Raum ({n}. Stelle) {feld}", "Oberflaeche",
                           float(w.group(1)) if w else None,
                           "room_defaults", zahl(raum, rf"r->{feld}")))

fehler = 0
for feld, qa, a, qb, b in vergleiche:
    if a is None or b is None or a != b:
        fehler += 1
        print(f"  {feld}: {qa} {a}, {qb} {b}")
print(f"{len(vergleiche)} Vorgaben verglichen, {fehler} Abweichungen")
sys.exit(1 if fehler else 0)
