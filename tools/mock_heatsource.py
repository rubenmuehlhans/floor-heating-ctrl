#!/usr/bin/env python3
"""Attrappe des Wärmeerzeugers.

Liefert die echte Weboberfläche mit erfundenen Messwerten, damit sich das
Bedienbild ohne Hardware ansehen und weiterentwickeln lässt. Bildet die
JSON-Schnittstelle des Geräts nach.

    python3 tools/mock_heatsource.py            ->  http://localhost:8322

Nachgebildet wird ein Speicherboard: Pufferfühler und beide Heizkreise. Mit
--kessel liefert die Attrappe stattdessen ein Kesselboard mit Abgas sowie Vor-
und Rücklauf. Mit --leer meldet sich kein Fühler, so lässt sich der
Einrichtungsassistent im Ausgangszustand ansehen.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, str(Path(__file__).resolve().parent))
from www import zusammensetzen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
T0 = time.time()

ROLLEN = [
    "abgas", "kessel_vl", "kessel_rl", "puffer", "puffer_unten",
    "hk1_vl", "hk1_rl", "hk2_vl", "hk2_rl",
]

# Die Aussentemperatur misst kein Fuehler dieses Geraets; sie kommt ueber die
# Bedarfsantwort vom Verteiler herein und steht deshalb nur unter den
# Fremdwerten.
AUSSEN = ("aussen", (11.4, 3.5, 86400))

# Rolle -> (Grundwert, Schwankung, Periode in Sekunden)
VERLAUF = {
    "abgas": (128.0, 45.0, 900),
    "kessel_vl": (68.0, 9.0, 900),
    "kessel_rl": (52.0, 12.0, 900),
    "puffer": (57.0, 6.0, 2400),
    "puffer_unten": (41.0, 5.0, 2400),
    "hk1_vl": (34.0, 2.5, 600),
    "hk1_rl": (29.0, 2.0, 600),
    "hk2_vl": (32.0, 2.0, 700),
    "hk2_rl": (28.0, 1.6, 700),
    "aussen": AUSSEN[1],
}

SPEICHER = ["puffer", "puffer_unten", "hk1_vl", "hk1_rl", "hk2_vl", "hk2_rl"]
KESSEL = ["abgas", "kessel_vl", "kessel_rl"]

# Heizkreise, wie sie das Gerät am Pufferspeicher führt.
KREISE = [
    {"id": 1, "name": "Keller und Erdgeschoss", "peers": ["fbh_c2e55c", "fbh_a1b2c3"],
     "topic": "pumpe_hk1", "relay": 1},
    {"id": 2, "name": "Obergeschoss", "peers": ["fbh_d4e5f6"],
     "topic": "pumpe_hk2", "relay": 2},
]

MAX_CIRCUITS = 4


def kreis_default(kid: int) -> dict:
    """Vorgabewerte eines Heizkreises, wie sie cfg_circuit_defaults() setzt."""
    return {"id": kid, "name": f"Heizkreis {kid}", "enabled": True, "peers": [],
            "vl_role": f"hk{kid}_vl", "rl_role": f"hk{kid}_rl",
            "pump": {"topic": "", "host": "", "user": "", "relay": 1, "pass_set": False},
            "mode": "auto", "overrun_s": 300, "min_run_s": 180, "min_pause_s": 180,
            "min_buffer_c": 40.0, "frost_c": 6.0}


REC = {"state": "fertig", "samples": 1043, "period_s": 5,
       "started_epoch": int(time.time()) - 5215, "cols": 6, "bytes": 19200,
       "auto": False, "wait_off": False, "tail": False, "tail_left_s": 0,
       "source": "brenner"}

# Nachlauf der selbsttaetigen Aufzeichnung, wie REC_TAIL_S im Geraet.
REC_TAIL_S = 600

CFG = {
    "cfg_version": 1,
    "site": "Pufferspeicher",
    "onewire_pin": [13, -1],
    "poll_s": 10,
    "probes": [],
    "reboot_hour": -1,
    "seize_weekday": 6,
    "seize_hour": 11,
    "reboot_minute": 0,
    "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
    "wifi": {"ssid": "Heimnetz", "hostname": "heizung", "pass_set": True, "ap_pass_set": True},
    "mqtt": {"enabled": False, "uri": "", "user": "", "prefix": "heiz", "pass_set": False},
}

ROMS: dict[str, str] = {}

# Wird in __main__ gesetzt: bildet die Attrappe das Kessel- oder das
# Speicherboard nach?
KESSELBOARD = False


def wert(rolle: str, t: float | None = None) -> float:
    grund, hub, periode = VERLAUF[rolle]
    t = time.time() if t is None else t
    return round(grund + hub * math.sin(2 * math.pi * t / periode), 2)


def probes() -> list[dict]:
    zugeordnet = {p["rom"]: p for p in CFG["probes"]}
    out = []
    for rolle in ROLLEN:
        rom = ROMS.get(rolle)
        if rom is None:
            continue
        eintrag = zugeordnet.get(rom)
        rolle_gesetzt = eintrag["role"] if eintrag else ""
        offset = eintrag["offset_k"] if eintrag else 0.0
        roh = wert(rolle)
        out.append({
            "rom": rom,
            "name": (eintrag["name"] if eintrag and eintrag["name"] else rom),
            "role": rolle_gesetzt,
            "role_label": LABEL.get(rolle_gesetzt, ""),
            "bus": 0,
            "assigned": bool(eintrag),
            "temp_c": round(roh + offset, 2),
            "raw_c": roh,
            "delta30_c": round(wert(rolle) - wert(rolle, time.time() - 30), 2),
            "age_s": 3,
            "offset_k": offset,
            "reads": int((time.time() - T0) / 10),
            "errors": 0,
        })
    return out


LABEL = {
    "abgas": "Abgas", "kessel_vl": "Kessel Vorlauf", "kessel_rl": "Kessel Ruecklauf",
    "puffer": "Pufferspeicher", "puffer_unten": "Pufferspeicher unten",
    "hk1_vl": "Heizkreis 1 Vorlauf", "hk1_rl": "Heizkreis 1 Ruecklauf",
    "hk2_vl": "Heizkreis 2 Vorlauf", "hk2_rl": "Heizkreis 2 Ruecklauf",
    "aussen": "Aussentemperatur",
}


def rec_quelle() -> str:
    """Woran sich der Beginn einer Ladung hier erkennen liesse."""
    rollen = {p["role"] for p in CFG["probes"]}
    if "abgas" in rollen or KESSELBOARD:
        return "brenner"
    if "puffer" in rollen:
        return "speicher"
    return "keiner"


def rec_tick(brenner_laeuft: bool) -> None:
    """Uebergaenge der selbsttaetigen Aufzeichnung, wie rec_tick() im Geraet."""
    if REC["state"] in ("aus", "fertig"):
        REC["source"] = rec_quelle()
    if REC["state"] == "scharf":
        if not brenner_laeuft:
            REC["wait_off"] = False
        elif not REC["wait_off"]:
            REC.update(state="laeuft", samples=0, tail=False, tail_left_s=0,
                       started_epoch=int(time.time()))
    elif REC["state"] == "laeuft":
        REC["samples"] = min(1600, int((time.time() - REC["started_epoch"]) / 5))
        if not REC["auto"]:
            return
        if brenner_laeuft:
            REC.update(tail=False, tail_left_s=0)
        elif not REC["tail"]:
            REC.update(tail=True, tail_left_s=REC_TAIL_S, tail_bis=time.time() + REC_TAIL_S)
        elif time.time() >= REC["tail_bis"]:
            REC.update(state="fertig", tail=False, tail_left_s=0)
        else:
            REC["tail_left_s"] = int(REC["tail_bis"] - time.time())


def state() -> dict:
    ps = probes()
    belegt = {p["role"]: p["temp_c"] for p in ps if p["role"]}
    abgeleitet = {}
    # Die Spreizung der Heizkreise steht je Kreis unter "circuits".
    if "kessel_vl" in belegt and "kessel_rl" in belegt:
        abgeleitet["kessel_spreizung_k"] = round(belegt["kessel_vl"] - belegt["kessel_rl"], 2)
    brenner_laeuft = math.sin(2 * math.pi * time.time() / 900) > 0.2
    rec_tick(brenner_laeuft)
    laufzeit = 4 * 3600 + int(time.time()) % 1800
    starts = 6

    kreise = []
    for c in CFG.get("circuits", []):
        vl = belegt.get(f"hk{c['id']}_vl")
        rl = belegt.get(f"hk{c['id']}_rl")
        bedarf = c["id"] == 1
        kreise.append({
            "id": c["id"], "name": c["name"], "enabled": c.get("enabled", True),
            "mode": c.get("mode", "auto"),
            "on": bedarf,
            "reason": "Abnehmer vorhanden" if bedarf else "kein Abnehmer",
            "since_s": 1820 if bedarf else 640,
            "demand": bedarf, "stale": False, "any_seen": True,
            "vl_c": vl, "rl_c": rl,
            "spread_k": None if vl is None or rl is None else round(vl - rl, 2),
            "path": "mqtt",
            "relay": {"known": True, "on": bedarf, "online": True, "age_s": 4,
                      "mismatch": False},
        })

    quellen = []
    for c in CFG.get("circuits", []):
        for j, pid in enumerate(c.get("peers", [])):
            quellen.append({
                "id": pid,
                "site": {"fbh_c2e55c": "Keller", "fbh_a1b2c3": "Erdgeschoss",
                         "fbh_d4e5f6": "Obergeschoss"}.get(pid, pid),
                "host": f"192.168.1.{250 + j}",
                "seen": True, "demand": c["id"] == 1 and j == 0,
                "max_target": 1.0 if c["id"] == 1 and j == 0 else 0.0,
                "open_channels": 2 if c["id"] == 1 and j == 0 else 0,
                "rooms_calling": 1 if c["id"] == 1 and j == 0 else 0,
                "age_s": 3, "errors": 0})

    # Was das Geraet nicht selbst misst, kommt vom Nachbargeraet.
    fremd = {r: wert(r) for r in ROLLEN if r not in belegt}
    fremd["aussen"] = wert("aussen")
    puffer = belegt.get("puffer", fremd.get("puffer"))
    fuell = None if puffer is None else max(0.0, min(1.0, (puffer - 35.0) / 27.0))
    kvl = belegt.get("kessel_vl", fremd.get("kessel_vl"))
    krl = belegt.get("kessel_rl", fremd.get("kessel_rl"))

    return {
        "device": {"id": "heiz_3f21ac", "mac": "A0:B7:65:3F:21:AC", "site": CFG["site"],
                   "model": "Waermeerzeuger", "role": "heat"},
        "version": "attrappe",
        "uptime_s": int(time.time() - T0),
        "heap": 198000,
        "setup_open": CFG["site"] == "",
        "net": {"sta": True, "ap": False, "ip": "127.0.0.1", "rssi": -58, "time_valid": True},
        "onewire": {"pins": CFG["onewire_pin"], "found": len(ps),
                    "assigned": sum(1 for p in ps if p["assigned"]),
                    "round_ms": 812, "rounds": int((time.time() - T0) / 10),
                    "poll_s": CFG["poll_s"]},
        "probes": ps,
        "derived": abgeleitet,
        "circuits": kreise,
        "demand_sources": quellen,
        "mqtt_connected": True,
        "remote_probes": fremd,
        "burner": {
            "remote": not KESSELBOARD,
            "known": True,
            "running": brenner_laeuft,
            "abgas_c": belegt.get("abgas", fremd.get("abgas")),
            "baseline_c": 21.4,
            "since_s": 1240,
            "runtime_today_s": laufzeit,
            "starts_today": starts,
            "runtime_yesterday_s": 5 * 3600 + 720,
            "starts_yesterday": 7,
            "litres_today": round(laufzeit / 3600 * 2.2, 2),
            "short_cycling": False,
        },
        "charge": {
            "phase": "wird geladen" if brenner_laeuft else "geladen",
            "limited": False,
            "warn_dhw": puffer is not None and puffer < 40.0,
            "kessel_remote": "kessel_vl" not in belegt,
            "since_s": 2400,
            "level": None if fuell is None else round(fuell, 3),
            "spread_k": None if kvl is None or krl is None else round(kvl - krl, 2),
        },
        "record": REC,
        "heat_peers": [{
            "id": "heiz_9a1b2c" if not args_kessel() else "heiz_3f21ac",
            "site": "Kessel" if not args_kessel() else "Pufferspeicher",
            "host": "192.168.1.52", "seen": True, "age_s": 4,
            "roles": 3 if not args_kessel() else 6, "errors": 0}],
        "history_len": 1440,
    }


def args_kessel() -> bool:
    return KESSELBOARD


def history(step: int, hoechstens: int) -> dict:
    punkte = min(hoechstens, 1440 // max(1, step))
    jetzt = time.time()
    reihen = {}
    for rolle in ROLLEN:
        if ROMS.get(rolle) is None or not any(
                p["role"] == rolle for p in CFG["probes"]):
            reihen[rolle] = [None] * punkte
            continue
        reihen[rolle] = [wert(rolle, jetzt - (punkte - 1 - i) * step * 60)
                         for i in range(punkte)]
    return {"step_min": step, "points": punkte, "newest_epoch": int(jetzt),
            "roles": ROLLEN, "series": reihen}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def _send(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)

        if u.path in ("/", "/index.html"):
            body = zusammensetzen("heatsource").encode()
            # Aufnahmehilfe: ?_wizstep=N oeffnet den Assistenten auf dem
            # gewuenschten Schritt. Nur hier, nicht auf dem Geraet.
            if "_wizstep" in q:
                n = int(q["_wizstep"][0])
                body = body.replace(b"wizStep = 0;\n  wizSite = cfg",
                                    b"wizStep = %d;\n  wizSite = cfg" % n)
                body = body.replace(b"if (state?.setup_open) openWizard();",
                                    b"openWizard();")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if u.path == "/api/state":
            return self._send(state())
        if u.path == "/api/config":
            return self._send(CFG)
        if u.path == "/api/measurements":
            ps = [{"role": p["role"], "c": p["temp_c"], "age_s": p["age_s"]}
                  for p in probes() if p["role"]]
            return self._send({"id": "heiz_3f21ac", "site": CFG["site"],
                               "uptime_s": int(time.time() - T0), "probes": ps})
        if u.path == "/api/history":
            return self._send(history(int(q.get("step", ["5"])[0]),
                                      int(q.get("max", ["288"])[0])))
        if u.path == "/api/peers":
            return self._send({"peers": [
                {"id": "fbh_c2e55c", "site": "Keller", "role": "manifold",
                 "host": "192.168.1.250", "hostname": "floor-heating-keller"},
                {"id": "fbh_a1b2c3", "site": "Erdgeschoss", "role": "manifold",
                 "host": "192.168.1.251", "hostname": "floor-heating-eg"},
                {"id": "fbh_d4e5f6", "site": "Obergeschoss", "role": "manifold",
                 "host": "192.168.1.252", "hostname": "floor-heating-og"},
                {"id": "heiz_9a1b2c", "site": "Kessel", "role": "heat",
                 "host": "192.168.1.52", "hostname": "heizung-kessel"},
            ]})
        if u.path == "/api/wifi/scan":
            return self._send({"networks": [{"ssid": "Heimnetz", "rssi": -58, "secure": True},
                                            {"ssid": "Gast", "rssi": -74, "secure": True}]})
        self._send({"ok": False, "error": "unbekannt"}, 404)

    def do_PUT(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            patch = json.loads(self.rfile.read(length))
        except Exception:
            return self._send({"ok": False, "error": "kaputtes JSON"}, 400)
        if len(patch.get("circuits", [])) > MAX_CIRCUITS:
            return self._send({"ok": False,
                               "error": f"Hoechstens {MAX_CIRCUITS} Heizkreise moeglich"}, 400)
        for k, v in patch.items():
            if k == "circuits" and isinstance(v, list):
                # Wie im Geraet: die Liste wird ersetzt, je Kennung dient aber
                # der bisherige Stand als Grundlage. Eine Teilangabe laesst die
                # uebrigen Felder damit stehen.
                alt = {c["id"]: c for c in CFG.get("circuits", [])}
                neu = []
                for e in v:
                    kid = int(e.get("id", len(neu) + 1))
                    z = dict(alt.get(kid) or kreis_default(kid))
                    for feld, wert_ in e.items():
                        if feld == "pump" and isinstance(wert_, dict):
                            z["pump"] = {**z["pump"], **wert_}
                        else:
                            z[feld] = wert_
                    neu.append(z)
                CFG["circuits"] = sorted(neu, key=lambda c: c["id"])
            elif isinstance(v, dict) and isinstance(CFG.get(k), dict):
                CFG[k].update(v)
            else:
                CFG[k] = v
        self._send({"ok": True})

    def do_POST(self):
        pfad = urlparse(self.path).path
        if pfad.startswith("/api/record/"):
            aktion = pfad.rsplit("/", 1)[-1]
            laeuft = math.sin(2 * math.pi * time.time() / 900) > 0.2
            if aktion == "arm":
                if rec_quelle() == "keiner":
                    return self._send({"ok": False, "error": "Dieses Geraet erkennt den "
                                       "Beginn einer Ladung nicht"}, 400)
                REC.update(state="scharf", auto=True, source=rec_quelle(), samples=0, cols=len(CFG["probes"]),
                           bytes=1600 * len(CFG["probes"]) * 2, wait_off=laeuft,
                           tail=False, tail_left_s=0, started_epoch=0)
            elif aktion == "start":
                REC.update(state="laeuft", auto=False, samples=0, cols=len(CFG["probes"]),
                           bytes=1600 * len(CFG["probes"]) * 2, wait_off=False,
                           tail=False, tail_left_s=0, started_epoch=int(time.time()))
            elif aktion == "stop":
                if REC["state"] == "scharf":
                    REC.update(state="aus", auto=False, bytes=0, cols=0)
                else:
                    REC.update(state="fertig", tail=False, tail_left_s=0)
            elif aktion == "discard":
                REC.update(state="aus", auto=False, samples=0, bytes=0, cols=0,
                           wait_off=False, tail=False, tail_left_s=0)
        self._send({"ok": True})


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kessel", action="store_true", help="Kesselboard statt Speicherboard")
    p.add_argument("--leer", action="store_true", help="kein Fuehler am Bus")
    p.add_argument("--port", type=int, default=8322)
    args = p.parse_args()
    globals()["KESSELBOARD"] = args.kessel
    vorhanden = [] if args.leer else (KESSEL if args.kessel else SPEICHER)
    for i, rolle in enumerate(vorhanden):
        ROMS[rolle] = f"28FF{i:02X}1E8016{ord(rolle[0]):02X}4A"
    if args.kessel:
        CFG["site"] = "Kessel"
    CFG["probes"] = [{"rom": ROMS[r], "role": r, "name": LABEL[r], "offset_k":
                      3.0 if r == "puffer" else 0.0} for r in vorhanden]
    if not args.kessel:
        CFG["circuits"] = [
            {"id": k["id"], "name": k["name"], "enabled": True, "peers": k["peers"],
             "vl_role": f"hk{k['id']}_vl", "rl_role": f"hk{k['id']}_rl",
             "pump": {"topic": k["topic"], "host": "", "user": "", "relay": k["relay"],
                      "pass_set": False},
             "mode": "auto", "overrun_s": 300, "min_run_s": 180, "min_pause_s": 180,
             "min_buffer_c": 40.0, "frost_c": 6.0}
            for k in KREISE]

    print(f"Attrappe {'Kessel' if args.kessel else 'Speicher'} "
          f"auf http://localhost:{args.port}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
