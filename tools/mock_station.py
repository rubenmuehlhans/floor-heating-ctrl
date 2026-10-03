#!/usr/bin/env python3
"""Attrappe des Leitstands (apps/station).

Bildet die HTTP-Schnittstelle des Leitstands nach und liefert seine echte
Weboberflaeche, damit sich App und Oberflaeche ohne das Geraet pruefen lassen:

    python3 tools/mock_station.py     ->  http://localhost:8325

Funkthermometer: ein Climate-Sat, der verschluesselt sendet und erst mit dem
Testschluessel der Pruefungen in test/host Werte liefert, dazu zwei offene
Xiaomi-Thermometer. Die Anlage (/api/plant) ist erfunden und stimmt mit den
anderen Attrappen nur in den Kennungen ueberein.

Zusaetzlich zur Geraeteschnittstelle:
    /mock/stats        Zaehler der beantworteten Anfragen
"""

import argparse
import calendar
import json
import math
import struct
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))
from www import zusammensetzen  # noqa: E402

T0 = time.time()
KENNUNG = "lst_c0ffee"

KLIMASAT_MAC = "C0:FF:EE:12:34:56"
KLIMASAT_SCHLUESSEL = "00112233445566778899aabbccddeeff"
SCHLUESSEL: dict = {}

CFG = {
    "site": "Leitstand",
    "wifi": {"ssid": "Heimnetz", "pass": "geheim", "hostname": "leitstand", "ap_pass": "",
             "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"},
    "outdoor": {"mac": ""},
    "poll": {"heat_s": 30, "manifold_s": 30},
    "display": {"brightness": 160, "dim_after_min": 2, "night_from_h": 23, "night_to_h": 6},
}
ANZEIGE = {"page": "anlage", "on": True}
STATS = {"anfragen": 0}



HOMEKIT = {"gekoppelt": 2}

def hex_normal(text, ziffern):
    """Hexadezimalziffern ohne Leerzeichen, Doppelpunkte und Bindestriche, oder None."""
    if not isinstance(text, str):
        return None
    t = "".join(c for c in text if c not in " :-\t\n").lower()
    return t if len(t) == ziffern and all(c in "0123456789abcdef" for c in t) else None


def mac_normal(text):
    t = hex_normal(text, 12)
    return ":".join(t[i:i + 2] for i in range(0, 12, 2)).upper() if t else None


def laufzeit():
    return int(time.time() - T0)


def klimasat():
    """Der Climate-Sat, wie ihn /api/ble zeigt: ohne passenden Schluessel ohne Werte."""
    d = {"mac": KLIMASAT_MAC, "name": "campersense-climate-0fe", "rssi": -71, "battery": 0,
         "battery_mv": 0, "packets": 300 + laufzeit() // 2, "format": "bthome", "encrypted": True}
    k = SCHLUESSEL.get(KLIMASAT_MAC)
    if k is None:
        d["key"] = "missing"
    elif k != KLIMASAT_SCHLUESSEL:
        d["key"] = "wrong"
    else:
        d.update(key="ok", temp_c=round(8.4 + 0.6 * math.sin(time.time() / 900), 2),
                 humidity=81.0, battery=100, battery_mv=3010)
    return d


def thermometer():
    return [
        klimasat(),
        {"mac": "A4:C1:38:77:88:99", "name": "ATC_Wohn", "rssi": -84, "temp_c": 21.4,
         "humidity": 50.0, "battery": 77, "battery_mv": 2890, "packets": 900 + laufzeit(),
         "format": "atc1441"},
        {"mac": "A4:C1:38:AA:BB:CC", "name": "ATC_Garage", "rssi": -90, "temp_c": 12.8,
         "humidity": 63.0, "battery": 54, "battery_mv": 2710, "packets": 450 + laufzeit() // 3,
         "format": "atc1441"},
    ]


def aussen():
    """Der zugeordnete Aussenfuehler, wie ihn /api/state zeigt."""
    mac = CFG["outdoor"]["mac"]
    o = {"assigned": bool(mac), "valid": False}
    if not mac:
        return o
    o["mac"] = mac
    t = next((d for d in thermometer() if d["mac"] == mac), None)
    if t and "temp_c" in t:
        o.update(valid=True, temp_c=t["temp_c"], battery=t["battery"], rssi=t["rssi"],
                 age_s=laufzeit() % 17, name=t["name"])
        if "humidity" in t:
            o["humidity"] = t["humidity"]
    return o


def zustand():
    return {
        "device": {"id": KENNUNG, "mac": "C0:FF:EE:00:00:01", "site": CFG["site"],
                   "model": "Leitstand", "role": "station", "board": "M5Stack Core"},
        "version": "attrappe",
        "uptime_s": laufzeit(),
        "reset_reason": "power_on",
        "heap": 52000 + int(3000 * math.sin(time.time() / 60)),
        "heap_min": 35364,
        "heap_block": 38912,
        "psram_free": 0,
        "setup_open": False,
        "net": {"sta": True, "ap": False, "ip": "127.0.0.1", "rssi": -58, "time_valid": True},
        "outdoor": aussen(),
        "ble": {"running": True, "devices": len(thermometer()), "keys": len(SCHLUESSEL)},
        "plant": {"heat": 2, "manifolds": 3, "reachable": 5},
        "display": dict(ANZEIGE),
        "log": {"card": True, "size_mb": 15193, "free_mb": 15120, "today_bytes": len(protokoll_heute()),
                "last_write": int(time.time()) // 30 * 30, "discarded": 0, "errors": 0, "message": "",
                "time_valid": True},
        # HomeKit wie auf dem Core2; den Code zeigt nur die Anzeige am Geraet.
        "homekit": {"active": True, "controllers": HOMEKIT["gekoppelt"], "accessories": 9},
    }


# Protokoll wie auf der Karte des Leitstands: gestern und heute, in UTC. Die
# Werte sind erfunden, das Format folgt docs/katalog-messgroessen.md.
def _iso(t):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t))


def _brennt(t):
    return 1 if t % 7200 < 1500 else 0


def _waerme(t):
    """0..1: steigt, solange der Brenner läuft, und fällt danach langsam ab"""
    x = t % 7200
    return x / 1500 if x < 1500 else math.exp(-(x - 1500) / 2400)


def protokoll_heute(tag_offset=0, mittel=False):
    jetzt = int(time.time())
    beginn = (jetzt // 86400 - tag_offset) * 86400
    ende = min(jetzt, beginn + 86400)
    schritt = 300 if mittel else 30
    zeilen = ["zeit,fuehler.kessel_vl,fuehler.kessel_rl,fuehler.abgas,brenner,kkp"]
    for t in range(beginn, ende, schritt):
        # Der Brenner läuft alle zwei Stunden 25 Minuten; das Mittel ist sein Zeitanteil im Platz.
        proben = range(t, t + schritt, 30)
        brenner = sum(_brennt(p) for p in proben) / len(proben)
        warm = _waerme(t)
        brenner_text = f"{brenner:.2f}".rstrip("0").rstrip(".") if mittel else str(int(brenner))
        zeilen.append(f"{_iso(t)},{50 + 25 * warm:.3f},{45 + 12 * warm:.3f},"
                      f"{25 + 150 * brenner + 20 * warm:.3f},{brenner_text},{brenner_text}")
    return ("\n".join(zeilen) + "\n").encode()


def protokoll_tage():
    heute = int(time.time()) // 86400
    return [time.strftime("%Y-%m-%d", time.gmtime((heute - i) * 86400)) for i in (1, 0)]


def protokoll_ereignisse(tag_offset):
    """Ereignisse eines Tages; vom laufenden Tag nur die vergangenen, die Datei wächst also."""
    jetzt = int(time.time())
    beginn = (jetzt // 86400 - tag_offset) * 86400
    g = "heiz_9a1b2c"
    liste = []
    for t in range(beginn, beginn + 86400, 7200):
        liste.append((t, {"art": "brenner", "ein": True, "dauer_vorher_s": 5700}))
        liste.append((t + 1500, {"art": "brenner", "ein": False, "dauer_vorher_s": 1500}))
    if tag_offset == 1:
        liste += [
            (beginn + 14 * 3600 + 1200, {"art": "nicht_erreichbar"}),
            (beginn + 14 * 3600 + 1890, {"art": "erreichbar", "dauer_s": 690}),
            (beginn + 14 * 3600 + 1895, {"art": "neustart", "laufzeit_vorher_s": 51234, "grund": "brownout"}),
        ]
    else:
        liste += [
            (beginn + 5 * 3600 + 3580, {"art": "neustart", "laufzeit_vorher_s": 86012, "grund": "software"}),
            (beginn + 5 * 3600 + 3600, {"art": "version", "alt": "v0.4.0", "neu": "v0.4.0-1-g47399df"}),
            (beginn + 7 * 3600 + 1800, {"art": "befund", "code": "flow_swapped", "stand": "neu"}),
        ]
    zeilen = [json.dumps({"zeit": _iso(t), "geraet": g, **f}) for t, f in sorted(liste, key=lambda x: x[0]) if t <= jetzt]
    return ("\n".join(zeilen) + "\n").encode() if zeilen else b""


def protokoll_datei(tag, name):
    tage = protokoll_tage()
    if tag not in tage:
        return None
    versatz = 1 - tage.index(tag)
    if name == "heiz_9a1b2c.csv":
        return protokoll_heute(versatz)
    if name == "heiz_9a1b2c.5min.csv":
        return protokoll_heute(versatz, mittel=True)
    if name == "ereignisse.jsonl":
        return protokoll_ereignisse(versatz)
    if name == "geraete.json":
        return json.dumps({"geraete": [{"kennung": "heiz_9a1b2c", "ort": "Kessel", "art": "heat",
                                         "version": "attrappe"}]}).encode()
    return None


def einstellungen():
    w = CFG["wifi"]
    return {
        "site": CFG["site"],
        "wifi": {"ssid": w["ssid"], "pass_set": bool(w["pass"]), "hostname": w["hostname"],
                 "ap_pass_set": bool(w["ap_pass"]), "timezone": w["timezone"]},
        "outdoor": dict(CFG["outdoor"]),
        "poll": dict(CFG["poll"]),
        "display": dict(CFG["display"]),
    }


def anlage():
    def geraet(kennung, ort, host, heap):
        return {"id": kennung, "site": ort, "host": host, "version": "attrappe", "reachable": True,
                "age_s": laufzeit() % 30, "uptime_s": 86000 + laufzeit(), "heap": heap, "rssi": -63}
    kessel = geraet("heiz_9a1b2c", "Kessel", "127.0.0.1", 62000)
    kessel.update(probes={"kessel_vl": 42.1, "kessel_rl": 55.9, "abgas": 34.0},
                  burner={"known": True, "own": True, "running": False, "runtime_today_s": 720, "starts_today": 1},
                  circuits=[], findings=1)
    speicher = geraet("heiz_3f21ac", "Pufferspeicher", "127.0.0.1", 60000)
    speicher.update(probes={"puffer": 55.7, "hk1_vl": 24.8, "hk1_rl": 24.2},
                    burner={"known": True, "own": False, "running": False, "runtime_today_s": 720, "starts_today": 1},
                    charge={"own": True, "level": 0.21, "phase": "keine Ladung"},
                    circuits=[{"id": 1, "name": "Heizkreis 1", "on": True, "vl_c": 24.8, "rl_c": 24.2}], findings=0)
    erdgeschoss = geraet("fbh_a1b2c3", "Erdgeschoss", "127.0.0.1", 52000)
    erdgeschoss["rooms"] = [{"id": 1, "name": "Küche", "temp_c": 21.4, "target_c": 21.0, "heat": True, "position": 0.4}]
    return {"heat": [kessel, speicher], "manifolds": [erdgeschoss], "outdoor": aussen()}


def bedarf():
    d = {"id": KENNUNG, "site": CFG["site"], "role": "station", "demand": False}
    o = aussen()
    if o.get("valid"):
        d.update(outdoor_c=o["temp_c"], outdoor_age_s=o["age_s"])
        if "humidity" in o:
            d["outdoor_humidity"] = o["humidity"]
    return d


def bildschirm():
    """320 x 240 als BMP: Karten und Speicher wie auf der Seite Anlage, ohne Schrift."""
    b, h = 320, 240
    grund, flaeche, linie, waerme, waerme_dunkel = (0x12, 0x15, 0x14), (0x1a, 0x1e, 0x1d), (0x2b, 0x31, 0x2f), (0xe8, 0x79, 0x4b), (0x5c, 0x32, 0x22)
    px = [[grund] * b for _ in range(h)]

    def rechteck(x, y, w, hh, farbe):
        for yy in range(max(0, y), min(h, y + hh)):
            for xx in range(max(0, x), min(b, x + w)):
                px[yy][xx] = farbe

    for x, y, w, hh in ((6, 28, 102, 128), (114, 28, 92, 128), (212, 28, 102, 61), (212, 95, 102, 61),
                        (6, 162, 150, 56), (162, 162, 152, 56)):
        rechteck(x, y, w, hh, flaeche)
    rechteck(0, 22, 320, 1, linie)
    rechteck(0, 221, 320, 1, linie)
    rechteck(135, 52, 50, 96, linie)
    rechteck(137, 54, 46, 92, flaeche)
    rechteck(137, 126, 46, 20, waerme_dunkel)
    rechteck(138, 126, 44, 2, waerme)
    zeile = b * 3
    daten = bytearray()
    for y in range(h):
        for r, g, bl in px[y]:
            daten += bytes((bl, g, r))
    kopf = b"BM" + struct.pack("<IHHI", 54 + len(daten), 0, 0, 54)
    kopf += struct.pack("<IiiHHIIiiII", 40, b, -h, 1, 24, 0, zeile * h, 2835, 2835, 0, 0)
    return kopf + bytes(daten)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, obj, code=200, typ="application/json"):
        STATS["anfragen"] += 1
        daten = obj if isinstance(obj, (bytes, bytearray)) else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", typ)
        self.send_header("Content-Length", str(len(daten)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(daten)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        roh = self.rfile.read(n) if n else b""
        try:
            return json.loads(roh or b"{}")
        except ValueError:
            return None

    def do_GET(self):
        p = urlparse(self.path).path
        if p in ("/", "/index.html"):
            return self._send(zusammensetzen("station").encode(), typ="text/html; charset=utf-8")
        routen = {
            "/api/state": zustand, "/api/config": einstellungen, "/api/plant": anlage,
            "/api/demand": bedarf,
            "/api/ble": lambda: {"running": True, "outdoor": CFG["outdoor"]["mac"], "devices": thermometer(),
                                 "keys": sorted(SCHLUESSEL)},
            "/api/peers": lambda: {"peers": [
                {"id": "heiz_9a1b2c", "site": "Kessel", "role": "heat", "host": "127.0.0.1", "hostname": "attrappe-kessel"},
                {"id": "heiz_3f21ac", "site": "Pufferspeicher", "role": "heat", "host": "127.0.0.1", "hostname": "attrappe-pufferspeicher"},
                {"id": "fbh_a1b2c3", "site": "Erdgeschoss", "role": "manifold", "host": "127.0.0.1", "hostname": "attrappe-erdgeschoss"}]},
            "/api/wifi/scan": lambda: {"running": False, "networks": [
                {"ssid": "Heimnetz", "rssi": -52, "secure": True}, {"ssid": "Nachbar", "rssi": -81, "secure": True}]},
            "/mock/stats": lambda: dict(STATS),
        }
        if p in routen:
            return self._send(routen[p]())
        if p == "/api/log/days":
            tag = parse_qs(urlparse(self.path).query).get("tag", [""])[0]
            if not tag:
                return self._send({"tage": protokoll_tage()})
            if tag not in protokoll_tage():
                return self._send({"ok": False, "error": "Diesen Tag gibt es nicht"}, 404)
            namen = ["geraete.json", "ereignisse.jsonl", "heiz_9a1b2c.csv", "heiz_9a1b2c.5min.csv"]
            return self._send({"tag": tag, "dateien": [{"name": n, "byte": len(protokoll_datei(tag, n))} for n in namen]})
        if p == "/api/log/series":
            q = parse_qs(urlparse(self.path).query)
            try:
                von, bis = int(q["von"][0]), int(q["bis"][0])
                schluessel = q["schluessel"][0].split(",")[:8]
                raster = int(q.get("raster", ["300"])[0])
            except (KeyError, ValueError):
                return self._send({"ok": False, "error": "geraet, schluessel, von und bis angeben"}, 400)
            if q.get("geraet", [""])[0] != "heiz_9a1b2c":
                return self._send({"geraet": q.get("geraet", [""])[0], "raster_s": raster, "spalten": schluessel, "zeilen": []})
            plaetze: dict[int, list[list[float]]] = {}
            for tag in protokoll_tage():
                text = protokoll_datei(tag, "heiz_9a1b2c.5min.csv" if raster >= 300 else "heiz_9a1b2c.csv").decode()
                kopf, *zeilen = text.strip().split("\n")
                spalten = kopf.split(",")
                for z in zeilen:
                    teile = z.split(",")
                    t = calendar.timegm(time.strptime(teile[0], "%Y-%m-%dT%H:%M:%SZ"))
                    if not von <= t < bis:
                        continue
                    werte = [float(teile[spalten.index(k)]) if k in spalten and teile[spalten.index(k)] else None
                             for k in schluessel]
                    plaetze.setdefault(t // raster, []).append(werte)
            zeilen_aus = []
            for platz in sorted(plaetze):
                spalten_werte = list(zip(*plaetze[platz]))
                zeilen_aus.append([_iso(platz * raster)] + [
                    round(sum(v for v in w if v is not None) / len([v for v in w if v is not None]), 3)
                    if any(v is not None for v in w) else None for w in spalten_werte])
            return self._send({"geraet": "heiz_9a1b2c", "raster_s": raster, "spalten": schluessel, "zeilen": zeilen_aus})
        if p == "/api/log/events":
            q = parse_qs(urlparse(self.path).query)
            von, bis = int(q.get("von", ["0"])[0]), int(q.get("bis", ["0"])[0])
            aus = []
            for tag in protokoll_tage():
                for z in protokoll_datei(tag, "ereignisse.jsonl").decode().strip().split("\n"):
                    e = json.loads(z)
                    t = calendar.timegm(time.strptime(e["zeit"], "%Y-%m-%dT%H:%M:%SZ"))
                    if von <= t < bis:
                        aus.append(e)
            return self._send(aus)
        if p.startswith("/log/"):
            teile = p[len("/log/"):].split("/")
            daten = protokoll_datei(teile[0], teile[1]) if len(teile) == 2 else None
            if daten is None:
                return self._send({"ok": False, "error": "Diese Datei gibt es nicht"}, 404)
            ab = 0
            bereich = self.headers.get("Range", "")
            if bereich.startswith("bytes=") and bereich.endswith("-"):
                ab = int(bereich[6:-1] or 0)
            typ = "text/csv; charset=utf-8" if teile[1].endswith(".csv") else "application/json"
            if ab:
                STATS["anfragen"] += 1
                teil = daten[ab:]
                self.send_response(206)
                self.send_header("Content-Type", typ)
                self.send_header("Content-Range", f"bytes {ab}-{len(daten) - 1}/{len(daten)}")
                self.send_header("Content-Length", str(len(teil)))
                self.end_headers()
                self.wfile.write(teil)
                return
            return self._send(daten, typ=typ)
        if p == "/api/screen":
            return self._send(bildschirm(), typ="image/bmp")
        self._send({"ok": False, "error": "Diese Adresse gibt es hier nicht."}, 404)

    def do_POST(self):
        p = urlparse(self.path).path
        if p == "/api/homekit/reset":
            body = self._body() or {}
            if body.get("bestaetigung") != "KOPPLUNGEN LOESCHEN":
                return self._send({"ok": False, "error": "Nur mit {\"bestaetigung\":\"KOPPLUNGEN LOESCHEN\"}"}, 400)
            HOMEKIT["gekoppelt"] = 0
            return self._send({"ok": True}, 202)
        if p == "/api/ota":
            self.rfile.read(int(self.headers.get("Content-Length") or 0))
            return self._send({"ok": True})
        body = self._body()
        if body is None:
            return self._send({"ok": False, "error": "Die Anfrage ist kein JSON"}, 400)
        if p == "/api/ble/key":
            m = mac_normal(body.get("mac"))
            if not m:
                return self._send({"ok": False, "error": "Die MAC-Adresse ist ungültig"}, 400)
            roh = body.get("bindkey")
            if roh in (None, ""):
                SCHLUESSEL.pop(m, None)
            else:
                k = hex_normal(roh, 32)
                if not k:
                    return self._send({"ok": False, "error": "Der Schlüssel muss aus 32 Hexadezimalziffern (0–9, a–f) bestehen"}, 400)
                if m not in SCHLUESSEL and len(SCHLUESSEL) >= 16:
                    return self._send({"ok": False, "error": "Kein Platz für weitere Schlüssel; höchstens 16 lassen sich hinterlegen"}, 400)
                SCHLUESSEL[m] = k
            return self._send({"ok": True})
        if p == "/api/display":
            if "page" in body:
                if body["page"] not in ("anlage", "leitstand"):
                    return self._send({"ok": False, "error": "Unbekannte Seite"}, 400)
                ANZEIGE["page"] = body["page"]
            if isinstance(body.get("on"), bool):
                ANZEIGE["on"] = body["on"]
            return self._send({"ok": True})
        if p == "/api/wifi/scan":
            return self._send({"ok": True})
        if p == "/api/system/restart":
            return self._send({"ok": True})
        if p == "/api/system/factory":
            SCHLUESSEL.clear()
            CFG["outdoor"]["mac"] = ""
            return self._send({"ok": True})
        self._send({"ok": False, "error": "Unbekannte Aktion"}, 404)

    def do_PUT(self):
        p = urlparse(self.path).path
        body = self._body()
        if p != "/api/config" or body is None:
            return self._send({"ok": False, "error": "Die Anfrage ist kein JSON"}, 400)
        if "outdoor" in body:
            mac = (body["outdoor"] or {}).get("mac", "")
            if mac:
                m = mac_normal(mac)
                if not m:
                    return self._send({"ok": False, "error": "Die Adresse des Außenfühlers ist ungültig"}, 400)
                mac = m
            CFG["outdoor"]["mac"] = mac
        if isinstance(body.get("site"), str):
            CFG["site"] = body["site"][:31]
        for gruppe in ("poll", "display"):
            if isinstance(body.get(gruppe), dict):
                CFG[gruppe].update({k: v for k, v in body[gruppe].items() if k in CFG[gruppe]})
        if isinstance(body.get("wifi"), dict):
            w = body["wifi"]
            for k in ("ssid", "hostname", "timezone"):
                if isinstance(w.get(k), str):
                    CFG["wifi"][k] = w[k]
            for k in ("pass", "ap_pass"):
                if isinstance(w.get(k), str) and w[k]:
                    CFG["wifi"][k] = w[k]
        self._send({"ok": True})


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=8325)
    args = p.parse_args()
    print(f"Attrappe des Leitstands auf http://127.0.0.1:{args.port}")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
