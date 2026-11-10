#!/usr/bin/env python3
"""Beispielanlage der Bedientafel: Gerätezustände wie /api/state der fünf Geräte.

    python3 tools/beispielanlage.py      -> main/beispiel/beispiel.json

Die Werte folgen der Beispielanlage der App (apple/Packages/Anlage/Sources/Anlage/
Beispielanlage.swift: Mitschnitt vom 28.08.2026 18:04, Räume auf „Heizen“, Bad mit
24 °C Soll). Anders als die App, die ein fertiges Anlagenbild mitbringt, liefert die
Tafel Gerätezustände: Sie laufen durch dieselbe Zusammenführung wie die echten
Antworten und prüfen sie damit mit.

Grundlage sind die Mitschnitte in apple/Packages/Geraeteschnittstelle/Tests/
GeraeteschnittstelleTests/Fixtures/mitschnitt.
"""
import copy
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
FIX = REPO / "apple/Packages/Geraeteschnittstelle/Tests/GeraeteschnittstelleTests/Fixtures/mitschnitt"
OUT = HERE.parent / "main" / "beispiel" / "beispiel.json"


def lade(name):
    return json.loads((FIX / name).read_text())


def kanal(n, raum=None, stellung=0.0, kalibriert=True, hand=False, op="idle", bemf=107):
    return {"id": n, "position": stellung, "known": True, "op": op, "group": (n + 1) // 2,
            "reserved": False, "manual": hand, "pending": False, "bemf_mv": bemf,
            "calibrated": kalibriert, "last_stop": "", "last_move_ms": 0, "seize_step": 0,
            "moved_since_seize": False}


def raum(nr, name, kanaele, soll=20.0, ist=None, feuchte=None, batterie=None, alter=None,
         an=True, sensor=True, ziel=0.0):
    r = {"id": nr, "name": name, "channels": kanaele, "mode": "heat" if an else "off",
         "target_c": soll, "sensor_set": sensor, "temp_valid": ist is not None,
         "target_position": ziel, "next_check_s": 12 + nr * 3}
    if ist is not None:
        r.update({"temp_c": ist, "humidity": feuchte, "battery": batterie, "temp_age_s": alter})
    return r


def verteiler(vorlage, kennung, ort, ip, raeume, kanaele, aussen=False, rssi=-60):
    v = copy.deepcopy(vorlage)
    v["device"].update({"id": kennung, "site": ort, "mac": "A0:B7:65:" + ":".join(
        kennung[4:][i:i + 2].upper() for i in (0, 2, 4))})
    v["net"].update({"ip": ip, "rssi": rssi})
    v["rooms"] = raeume
    belegt = {k: True for r in raeume for k in r["channels"]}
    v["channels"] = [kanaele.get(n, kanal(n, kalibriert=False)) if n in belegt or n in kanaele
                     else kanal(n, kalibriert=False) for n in range(1, 12)]
    if not aussen:
        v["outdoor"] = {"set": False, "valid": False}
    v.pop("bemf_mv", None)
    v.pop("touch_raw", None)
    return v


def main():
    eg = lade("erdgeschoss-state-neu.json")
    kessel = lade("kessel-state-neu.json")
    puffer = lade("puffer-state-neu.json")

    keller = verteiler(eg, "fbh_3a91c4", "Keller", "192.168.1.250", [
        raum(1, "Büro", [1, 2], ist=26.9, feuchte=50.5, batterie=93, alter=520),
        raum(2, "Waschraum", [3], sensor=False),
        raum(3, "Flur/WC", [4], sensor=False),
    ], {1: kanal(1), 2: kanal(2), 3: kanal(3, kalibriert=False), 4: kanal(4)}, rssi=-71)

    eg_k = {n: kanal(n, kalibriert=n in (1, 2, 3, 5, 7, 9)) for n in range(1, 11)}
    for n in (1, 2, 3):
        eg_k[n] = kanal(n, stellung=0.4)
    eg_k[10] = kanal(10, stellung=1.0, hand=True, kalibriert=False)
    erdgeschoss = verteiler(eg, "fbh_5d20e7", "Erdgeschoss", "192.168.1.213", [
        raum(1, "Küche", [1, 2, 3], soll=23.5, ist=23.6, feuchte=61, batterie=84, alter=3, ziel=0.4),
        raum(2, "Wohnzimmer", [8, 9, 10], ist=24.1, feuchte=61, batterie=0, alter=6),
        raum(3, "Kinderzimmer EG", [4, 5], ist=23.1, feuchte=63.4, batterie=74, alter=96),
        raum(4, "Flur", [6], ist=23.4, feuchte=62, batterie=57, alter=16, an=False),
        raum(5, "WC", [7], ist=23.1, feuchte=63.5, batterie=85, alter=24),
    ], eg_k, aussen=True, rssi=-42)

    og_k = {n: kanal(n) for n in (1, 2, 3, 4, 5, 7, 8)}
    og_k[3] = kanal(3, stellung=0.8, bemf=164)
    og_k[4] = kanal(4, stellung=0.62, op="opening", bemf=164)
    obergeschoss = verteiler(eg, "fbh_7b44f1", "Obergeschoss", "192.168.1.46", [
        raum(1, "Kinderzimmer", [1, 2], ist=23.1, feuchte=59.9, batterie=92, alter=94),
        raum(2, "Bad", [3, 4], soll=24.0, ist=23.5, feuchte=62, batterie=60, alter=14, ziel=0.8),
        raum(3, "Schlafzimmer", [5, 7, 8], soll=18.0, ist=23.4, feuchte=62, batterie=84, alter=145),
    ], og_k, rssi=-63)

    for d in (kessel, puffer):
        d["net"]["ip"] = d["net"]["ip"].replace("192.168.0.", "192.168.1.")
    # Bedarf und laufende Pumpen wie in der App
    for q in puffer["demand_sources"]:
        q["host"] = q["host"].replace("192.168.0.", "192.168.1.")
        if q["site"] in ("Erdgeschoss", "Obergeschoss"):
            q.update({"demand": True, "rooms_calling": 1, "open_channels": 2, "max_target": 24})
    for c in puffer["circuits"]:
        c.update({"on": True, "reason": "Abnehmer vorhanden", "demand": True})
        c["relay"].update({"known": True, "online": False, "status": -1})
    for p in kessel["heat_peers"] + puffer["heat_peers"]:
        p["host"] = p["host"].replace("192.168.0.", "192.168.1.")

    # Konfiguration: Speichergrenzen und versorgte Verteiler wie die Beispielanlage der App;
    # Räume mit Regelbereich und Thermometer. Ohne Zugangsdaten.
    att = FIX.parent / "attrappe"
    sp = json.loads((att / "speicher-config.json").read_text())
    sp.setdefault("buffer", {}).update({"voll_c": 62, "leer_c": 52.3, "leer_lernen": True, "leer_epoch": 1787000000, "warn_c": 40})
    sp["circuits"][0].update({"name": "Heizkreis 1", "peers": ["fbh_3a91c4", "fbh_5d20e7"]})
    sp["circuits"][1].update({"name": "Heizkreis 2", "peers": ["fbh_7b44f1"]})
    ke = copy.deepcopy(sp)
    ke["boiler_pump"].update({"enabled": True})
    ke["circuits"] = []
    ke["burner"] = {"duese_l_h": 2.2, "wartung_epoch": 1786966055}
    konfig = [{"id": "heiz_6e03b5", "config": {k: sp[k] for k in ("buffer", "circuits", "burner", "boiler_pump") if k in sp}},
              {"id": "heiz_19c8a2", "config": {k: ke[k] for k in ("buffer", "circuits", "burner", "boiler_pump") if k in ke}}]
    for v in (keller, erdgeschoss, obergeschoss):
        konfig.append({"id": v["device"]["id"], "config": {"site": v["device"]["site"], "sensor_timeout_s": 900,
                       "seize_weekday": 6, "seize_hour": 11,
                       "rooms": [{"id": r["id"], "name": r["name"], "channels": r["channels"], "mode": r["mode"],
                                  "target_c": r["target_c"], "p_band_k": 1.0, "interval_s": 30, "step": 0.1,
                                  "min_delta": 0.01} for r in v["rooms"]]}})
    for c in konfig:
        for g in c["config"].get("circuits", []):
            g.pop("pump", None)
        c["config"].get("boiler_pump", {}).pop("host", None)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    geraete = [keller, erdgeschoss, obergeschoss, kessel, puffer]
    OUT.write_text(json.dumps({"stand": 1787933045, "geraete": geraete, "konfig": konfig}, ensure_ascii=False,
                              separators=(",", ":")) + "\n")
    print(f"{OUT.relative_to(REPO)}: {OUT.stat().st_size} Byte, {len(geraete)} Geräte")


if __name__ == "__main__":
    main()
