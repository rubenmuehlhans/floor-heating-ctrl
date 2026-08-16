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
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parent.parent
WWW = ROOT / "apps" / "heatsource" / "main" / "www" / "index.html"
T0 = time.time()

ROLLEN = [
    "abgas", "kessel_vl", "kessel_rl", "puffer", "puffer_unten",
    "hk1_vl", "hk1_rl", "hk2_vl", "hk2_rl",
]

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
}

SPEICHER = ["puffer", "puffer_unten", "hk1_vl", "hk1_rl", "hk2_vl", "hk2_rl"]
KESSEL = ["abgas", "kessel_vl", "kessel_rl"]

CFG = {
    "cfg_version": 1,
    "site": "Pufferspeicher",
    "onewire_pin": [23, -1],
    "poll_s": 10,
    "probes": [],
    "reboot_hour": -1,
    "reboot_minute": 0,
    "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
    "wifi": {"ssid": "Heimnetz", "hostname": "heizung", "pass_set": True, "ap_pass_set": True},
    "mqtt": {"enabled": False, "uri": "", "user": "", "prefix": "heiz", "pass_set": False},
}

ROMS: dict[str, str] = {}


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
}


def state() -> dict:
    ps = probes()
    belegt = {p["role"]: p["temp_c"] for p in ps if p["role"]}
    abgeleitet = {}
    for name, a, b in [("kessel_spreizung_k", "kessel_vl", "kessel_rl"),
                       ("hk1_spreizung_k", "hk1_vl", "hk1_rl"),
                       ("hk2_spreizung_k", "hk2_vl", "hk2_rl")]:
        if a in belegt and b in belegt:
            abgeleitet[name] = round(belegt[a] - belegt[b], 2)
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
        "history_len": 1440,
    }


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
            body = WWW.read_bytes()
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
                 "host": "192.168.1.250", "hostname": "floor-heating"},
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
        for k, v in patch.items():
            if isinstance(v, dict) and isinstance(CFG.get(k), dict):
                CFG[k].update(v)
            else:
                CFG[k] = v
        self._send({"ok": True})

    def do_POST(self):
        self._send({"ok": True})


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kessel", action="store_true", help="Kesselboard statt Speicherboard")
    p.add_argument("--leer", action="store_true", help="kein Fuehler am Bus")
    p.add_argument("--port", type=int, default=8322)
    args = p.parse_args()

    vorhanden = [] if args.leer else (KESSEL if args.kessel else SPEICHER)
    for i, rolle in enumerate(vorhanden):
        ROMS[rolle] = f"28FF{i:02X}1E8016{ord(rolle[0]):02X}4A"
    if args.kessel:
        CFG["site"] = "Kessel"
    CFG["probes"] = [{"rom": ROMS[r], "role": r, "name": LABEL[r], "offset_k":
                      3.0 if r == "puffer" else 0.0} for r in vorhanden]

    print(f"Attrappe {'Kessel' if args.kessel else 'Speicher'} "
          f"auf http://localhost:{args.port}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
