#!/usr/bin/env python3
"""Attrappe der Ventilsteuerung.

Liefert die echte Weboberflaeche mit erfundenen Messwerten, damit sich das
Bedienbild ohne Hardware ansehen und weiterentwickeln laesst. Bildet die
JSON-Schnittstelle des Geraets nach, einschliesslich ETag und 304.

    python3 tools/mock_device.py     ->  http://localhost:8321

Mit --app laesst sich die Oberflaeche einer anderen Anwendung ausliefern; der
Port ergibt sich dann aus der Anwendung, sofern nicht --port gesetzt ist.

Zusaetzlich zur Geraeteschnittstelle:
    /mock/ap?on=1      Zugangspunkt-Betrieb (Einrichtungsportal)
    /mock/stats        Zaehler der beantworteten Anfragen
"""

import argparse
import json
import math
import sys
import random
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

sys.path.insert(0, str(Path(__file__).resolve().parent))
from www import zusammensetzen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PORTS = {"manifold": 8321, "heatsource": 8322}
APP = "manifold"
T0 = time.time()

CFG = {
    "cfg_version": 1,
    "site": "Erdgeschoss",
    "rooms": [
        {"id": 1, "name": "Küche", "channels": [1, 2, 3], "sensor_mac": "A4:C1:38:11:22:33",
         "mode": "heat", "target_c": 20.0, "p_band_k": 1.0, "interval_s": 30,
         "min_delta": 0.01, "step": 0.1},
        {"id": 2, "name": "Kinderzimmer unten", "channels": [4, 5], "sensor_mac": "A4:C1:38:44:55:66",
         "mode": "heat", "target_c": 21.0, "p_band_k": 1.0, "interval_s": 30,
         "min_delta": 0.01, "step": 0.1},
        {"id": 3, "name": "Flur/WC", "channels": [6, 7], "sensor_mac": None,
         "mode": "heat", "target_c": 19.0, "p_band_k": 1.0, "interval_s": 30,
         "min_delta": 0.01, "step": 0.1},
        {"id": 4, "name": "Wohnzimmer", "channels": [8, 9, 10], "sensor_mac": "A4:C1:38:77:88:99",
         "mode": "off", "target_c": 20.5, "p_band_k": 1.0, "interval_s": 30,
         "min_delta": 0.01, "step": 0.1},
    ],
    "channels": [
        {"id": n, "open_ms": 39000, "close_ms": 40000, "max_ms": 45000, "blank_ms": 2000,
         "bemf_mv": 190, "bemf_hyst_mv": 30, "calibrated": n in (1, 2),
         "bemf_group": (n + 1) // 2}
        for n in range(1, 12)
    ],
    "touch": {"enabled": True, "thresholds": [1000, 870, 1000]},
    "display_brightness": 2,
    "sensor_timeout_s": 900,
    "reboot_hour": 10,
    "reboot_minute": 0,
    "seize_weekday": 6,
    "seize_hour": 11,
    "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
    "wifi": {"ssid": "Heimnetz", "hostname": "floor-heating", "pass_set": True},
    "mqtt": {"enabled": True, "uri": "mqtt://192.168.1.10:1883", "user": "esp",
             "prefix": "fbh", "pass_set": True},
}

POSITIONS = [0.3, 0.3, 0.3, 0.7, 0.7, 0.5, 0.5, 0.0, 0.0, 0.0, 0.0]
TEMPS = {1: 20.6, 2: 20.2, 4: 21.4}

AP_MODE = [False]   # ueber /mock/ap?on=1 umschaltbar
MANUAL = set()      # Kreise im Handbetrieb (Notstellung)

# Aenderungszaehler wie im Geraet: er steigt nur bei Ereignissen, nicht
# fortlaufend. Waehrend einer Fahrt wird trotzdem immer voll geantwortet.
REV = [1]
STATS = {"full": 0, "not_modified": 0}


def moving_now():
    """CH4 faehrt periodisch, damit sich der schnelle Takt beobachten laesst."""
    return (int(time.time() - T0) % 40) < 8


def bump():
    REV[0] += 1

CALIB = {"state": "idle", "channel": 0, "group": 1, "phase": 0, "sample_count": 0,
         "sample_period_ms": 50, "message": "",
         "marks": {"close_from": 0, "close_to": 0, "open_from": 0, "open_to": 0,
                   "baseline_close_mv": 0, "stall_close_mv": 0,
                   "baseline_open_mv": 0, "stall_open_mv": 0}}
SAMPLES = []
CALIB_T0 = 0.0


def synth_samples(elapsed_ms):
    """Erzeugt einen glaubwuerdigen BEMF-Verlauf: Anlaufstrom, Fahrniveau,
    Anstieg beim Blockieren, Ruhepause, dann dasselbe in Gegenrichtung."""
    out = []
    n = int(elapsed_ms / 50)
    for i in range(n):
        t = i * 50
        if t < 400:
            v = 260 - t * 0.3            # Anlaufstrom
        elif t < 21000:
            v = 78 + random.uniform(-9, 9)
        elif t < 22500:
            v = 78 + (t - 21000) * 0.22  # blockiert
        elif t < 25500:
            v = 5 + random.uniform(0, 4)  # Ruhepause
        elif t < 25900:
            v = 250 - (t - 25500) * 0.3
        elif t < 46000:
            v = 82 + random.uniform(-9, 9)
        elif t < 47500:
            v = 82 + (t - 46000) * 0.21
        else:
            v = 5 + random.uniform(0, 4)
        out.append(max(0, int(v)))
    return out


def state():
    t = time.time() - T0
    tick = int(t / 12)
    if getattr(state, "tick", None) != tick:
        state.tick = tick
        bump()

    rooms = []
    for r in CFG["rooms"]:
        rid = r["id"]
        has = rid in TEMPS
        temp = TEMPS.get(rid, 0) + math.sin(t / 30 + rid) * 0.15
        diff = r["target_c"] - temp
        pos = min(1.0, max(0.0, round(((diff + 1) / 2) / 0.1) * 0.1)) if r["mode"] == "heat" else 0.0
        rooms.append({
            "id": rid, "name": r["name"], "channels": r["channels"], "mode": r["mode"],
            "target_c": r["target_c"], "sensor_set": r["sensor_mac"] is not None,
            "temp_valid": has, "temp_c": round(temp, 2),
            "humidity": 43 + rid, "battery": 90 - rid * 3,
            "temp_age_s": int(t) % 90, "target_position": round(pos, 2),
            "next_check_s": 30 - int(t) % 30,
        })

    channels = []
    for n in range(1, 12):
        moving = moving_now() and n == 4
        channels.append({
            "id": n, "position": POSITIONS[n - 1], # CH5 steht mit unbekannter Stellung (schraffiert), CH11 ohne Raum
            # (gestrichelt) -- so sind beide Zustaende auseinanderzuhalten.
            "known": n != 5,
            "op": "opening" if moving else "idle", "group": (n + 1) // 2,
            "reserved": CALIB["state"] == "running" and CALIB["channel"] == n,
            "manual": n in MANUAL,
            "pending": False,
            "bemf_mv": 78 + int(random.uniform(0, 12)) if moving else int(random.uniform(2, 9)),
            "seize_step": 0, "moved_since_seize": n <= 4,
            "calibrated": n in (1, 2),
            "last_stop": "Endlage erkannt" if n <= 3 else "Zielstellung erreicht",
            "last_move_ms": 12400,
        })

    return {
        "revision": REV[0], "uptime_s": int(t) + 7321, "heap": 148000 + int(random.uniform(0, 4000)),
        "version": "1.0.0",
        "net": ({"connected": False, "ip": "", "ap_active": True, "ap_ip": "192.168.4.1",
                 "rssi": 0, "time_valid": False} if AP_MODE[0] else
                {"connected": True, "ip": "192.168.1.241", "ap_active": False, "ap_ip": "",
                 "rssi": -58 - int(random.uniform(0, 6)), "time_valid": True}),
        "rooms": rooms, "channels": channels,
        "seize": {"running": False, "pending": 0, "done": 0, "days_left": 3,
                  "weekday": CFG["seize_weekday"], "hour": CFG["seize_hour"]},
        "bemf_mv": [int(random.uniform(2, 9)) for _ in range(6)],
        "touch_raw": [1420 + int(random.uniform(-20, 20)), 1180 + int(random.uniform(-20, 20)),
                      1390 + int(random.uniform(-20, 20))],
        "local_sensors": {
            "ds18b20": [
                {"address": "0xB50119270DBF0B28", "valid": True, "temp_c": 34.6},
                {"address": "0x89011926E8465428", "valid": True, "temp_c": 33.1},
                {"address": "0xA00129292188F428", "valid": True, "temp_c": 29.8},
                {"address": "0x3C0114536608AA28", "valid": False, "temp_c": 0},
            ],
            "hdc1080": {"valid": True, "temp_c": 24.3, "humidity": 38.5},
        },
        "calib": calib_status(),
    }


def calib_status():
    global SAMPLES
    c = dict(CALIB)
    if c["state"] == "running":
        elapsed = (time.time() - CALIB_T0) * 1000
        SAMPLES = synth_samples(min(elapsed, 49000))
        c["sample_count"] = len(SAMPLES)
        c["phase"] = 1 if elapsed < 22500 else (2 if elapsed < 25500 else 3)
        c["marks"] = dict(c["marks"], close_from=0, close_to=min(len(SAMPLES), 450),
                          open_from=510 if elapsed > 25500 else 0,
                          open_to=min(len(SAMPLES), 950) if elapsed > 47500 else 0)
        if elapsed > 49000:
            finish_calib()
            c = dict(CALIB)
    return c


def finish_calib():
    CALIB.update({
        "state": "done", "phase": 0, "sample_count": len(SAMPLES),
        "message": "Fertig: zu 22,5 s, auf 21,5 s, Schwelle 205 mV",
        "marks": {"close_from": 0, "close_to": 450, "open_from": 510, "open_to": 950,
                  "baseline_close_mv": 78, "stall_close_mv": 330,
                  "baseline_open_mv": 82, "stall_open_mv": 337},
        "suggestion": {"close_ms": 22500, "open_ms": 21500, "max_ms": 26200,
                       "bemf_mv": 205, "hyst_mv": 63},
    })


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path in ("/", "/index.html"):
            body = zusammensetzen(APP).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif u.path == "/mock/fresh":
            CFG["site"] = ""
            CFG["rooms"] = []
            self._send({"site": CFG["site"], "rooms": 0})
        elif u.path == "/mock/ap":
            AP_MODE[0] = parse_qs(u.query).get("on", ["1"])[0] == "1"
            if AP_MODE[0]:
                CFG["wifi"]["ssid"] = ""
                CFG["wifi"]["pass_set"] = False
            self._send({"ap_mode": AP_MODE[0]})
        elif u.path == "/api/state":
            body = state()
            tag = '"%d"' % REV[0]
            busy = moving_now() or CALIB["state"] == "running"
            if not busy and self.headers.get("If-None-Match") == tag:
                STATS["not_modified"] += 1
                self.send_response(304)
                self.send_header("ETag", tag)
                self.end_headers()
                return
            STATS["full"] += 1
            payload = json.dumps(body).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("ETag", tag)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif u.path == "/mock/stats":
            self._send(dict(STATS, revision=REV[0], busy=moving_now()))
        elif u.path == "/api/config":
            self._send(CFG)
        elif u.path == "/api/calib":
            frm = int(parse_qs(u.query).get("from", ["0"])[0])
            self._send({"calib": calib_status(), "from": frm,
                        "samples": SAMPLES[frm:frm + 512]})
        elif u.path == "/api/ble":
            self._send({"devices": [
                {"mac": "A4:C1:38:11:22:33", "name": "ATC_Kueche", "rssi": -62,
                 "temp_c": 20.61, "humidity": 44.2, "battery": 87, "battery_mv": 2914,
                 "packets": 4210, "format": "pvvx"},
                {"mac": "A4:C1:38:44:55:66", "name": "ATC_Wohn", "rssi": -71,
                 "temp_c": 21.38, "humidity": 41.0, "battery": 78, "battery_mv": 2861,
                 "packets": 3980, "format": "pvvx"},
                {"mac": "A4:C1:38:77:88:99", "name": "ATC_Kind", "rssi": -80,
                 "temp_c": 20.14, "humidity": 46.7, "battery": 64, "battery_mv": 2790,
                 "packets": 2210, "format": "atc1441"},
                {"mac": "A4:C1:38:AA:BB:CC", "name": "ATC_Garage", "rssi": -88,
                 "temp_c": 6.90, "humidity": 71.3, "battery": 55, "battery_mv": 2733,
                 "packets": 640, "format": "pvvx"},
            ]})
        elif u.path == "/api/peers":
            # Die uebrigen Platinen im Haus, wie sie ueber mDNS gefunden werden.
            self._send({"peers": [
                {"id": "fbh_c2e55c", "site": "Keller", "role": "manifold",
                 "host": "192.168.1.240", "hostname": "floor-heating-keller"},
                {"id": "fbh_d4e5f6", "site": "Obergeschoss", "role": "manifold",
                 "host": "192.168.1.242", "hostname": "floor-heating-og"},
                {"id": "heiz_3f21ac", "site": "Pufferspeicher", "role": "heat",
                 "host": "192.168.1.51", "hostname": "heizung-speicher"},
            ]})
        elif u.path == "/api/demand":
            # Wird vom Heizungsgeraet abgefragt: liegt hier Waermebedarf an?
            kanaele = state()["channels"]
            offen = [c for c in kanaele if c["position"] > 0.05]
            self._send({
                "id": "fbh_a1b2c3", "site": CFG["site"],
                "demand": bool(offen),
                "max_target": max((c["position"] for c in kanaele), default=0.0),
                "open_channels": len(offen),
                "rooms_calling": sum(1 for r in CFG["rooms"] if r["mode"] == "heat"),
                "min_room_c": 20.1, "sensor_ok": True})
        elif u.path == "/api/wifi/scan":
            self._send({"networks": [
                {"ssid": "Heimnetz", "rssi": -54, "secure": True},
                {"ssid": "Heimnetz-Gast", "rssi": -61, "secure": True},
                {"ssid": "Nachbar-WLAN", "rssi": -79, "secure": True},
                {"ssid": "FreeWifi", "rssi": -88, "secure": False},
            ]})
        else:
            self._send({"ok": False, "error": "unbekannt"}, 404)

    def do_POST(self):
        global CALIB_T0
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw or b"{}")
        except Exception:
            body = {}
        p = self.path

        if p.startswith("/api/room/"):
            parts = p.split("/")
            rid, action = int(parts[3]), parts[4]
            for r in CFG["rooms"]:
                if r["id"] == rid:
                    if action == "target":
                        r["target_c"] = round(float(body["target_c"]), 1)
                    elif action == "mode":
                        r["mode"] = body["mode"]
            bump()
            self._send({"ok": True})
        elif p.startswith("/api/channel/"):
            seg = p.split("/")[3]
            targets = list(range(1, 12)) if seg == "all" else [int(seg)]
            cmd = body.get("cmd")
            for n in targets:
                if cmd == "open":
                    POSITIONS[n - 1] = 1.0; MANUAL.add(n)
                elif cmd == "close":
                    POSITIONS[n - 1] = 0.0; MANUAL.add(n)
                elif cmd == "stop":
                    MANUAL.add(n)
                elif cmd == "auto":
                    MANUAL.discard(n)
                elif cmd == "position":
                    POSITIONS[n - 1] = round(float(body.get("position", 0)), 2)
            bump()
            self._send({"ok": True})
        elif p.endswith("/start") and p.startswith("/api/calib/"):
            n = int(p.split("/")[3])
            CALIB.update({"state": "running", "channel": n, "group": (n + 1) // 2,
                          "phase": 1, "message": "Kalibrierung laeuft"})
            CALIB.pop("suggestion", None)
            CALIB_T0 = time.time()
            SAMPLES.clear()
            bump()
            self._send({"ok": True})
        elif p == "/api/calib/abort":
            CALIB.update({"state": "failed", "phase": 0, "message": "Vom Anwender abgebrochen"})
            bump()
            self._send({"ok": True})
        elif p == "/api/calib/accept":
            s = CALIB.get("suggestion")
            if s:
                ch = CFG["channels"][CALIB["channel"] - 1]
                ch.update({k: s[k] for k in ("close_ms", "open_ms", "max_ms")})
                ch["bemf_mv"] = s["bemf_mv"]
                ch["bemf_hyst_mv"] = s["hyst_mv"]
                ch["calibrated"] = True
            bump()
            self._send({"ok": True})
        elif p == "/api/calib/discard":
            CALIB.update({"state": "idle", "phase": 0, "message": "", "sample_count": 0})
            CALIB.pop("suggestion", None)
            SAMPLES.clear()
            bump()
            self._send({"ok": True})
        else:
            bump()
            self._send({"ok": True})

    def do_PUT(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            CFG.update(json.loads(self.rfile.read(length)))
        except Exception:
            pass
        self._send({"ok": True})


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--app", default="manifold", choices=sorted(PORTS),
                   help="welche Oberflaeche ausgeliefert wird")
    p.add_argument("--port", type=int, default=0,
                   help="abweichender Port; Vorgabe richtet sich nach --app")
    args = p.parse_args()

    APP = args.app
    port = args.port or PORTS[args.app]
    print(f"Attrappe {args.app} auf http://localhost:{port}")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
