#!/usr/bin/env python3
"""Attrappe eines Tasmota-Relais.

Bildet die Schnittstelle `/cm` nach, über die das Heizungsgerät die
Umwälzpumpen schaltet, wenn kein MQTT-Broker eingerichtet ist. Damit lässt
sich der HTTP-Weg prüfen, ohne ein Relais anzuschließen.

    python3 tools/mock_tasmota.py --port 8080

Unterstützt wird, was die Firmware sendet:

    /cm?cmnd=Power1%20ON      schaltet und antwortet {"POWER1":"ON"}
    /cm?cmnd=Power1           fragt nur ab
    /cm?cmnd=Var1%201         Lebenszeichen, antwortet {"Var1":"1"}

Mit --relais 1 verhält sich die Attrappe wie ein einkanaliges Gerät: Tasmota
lässt die Nummer im Antwortschlüssel dann weg, und genau das muss die Firmware
auch verstehen. Mit --kennwort wird eine Anmeldung verlangt.
"""

from __future__ import annotations

import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, unquote

ZUSTAND: dict[int, bool] = {}
VARS: dict[str, str] = {}
PROTOKOLL: list[str] = []
ARGS = None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
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
        q = parse_qs(u.query)

        if u.path != "/cm":
            return self._send({"WARNING": "Unknown command"}, 404)

        if ARGS.kennwort:
            if q.get("user", [""])[0] != ARGS.benutzer or \
               q.get("password", [""])[0] != ARGS.kennwort:
                return self._send({"WARNING": "Need user=&password="}, 401)

        cmnd = unquote(q.get("cmnd", [""])[0]).strip()
        teile = cmnd.split(None, 1)
        befehl = teile[0] if teile else ""
        wert = teile[1] if len(teile) > 1 else None
        zeit = time.strftime("%H:%M:%S")

        if befehl.lower().startswith("power"):
            nummer = int(befehl[5:] or "1")
            if nummer < 1 or nummer > ARGS.relais:
                return self._send({"WARNING": "Invalid Index"})
            if wert is not None:
                oben = wert.upper()
                if oben == "TOGGLE":
                    ZUSTAND[nummer] = not ZUSTAND[nummer]
                elif oben in ("ON", "1"):
                    ZUSTAND[nummer] = True
                elif oben in ("OFF", "0"):
                    ZUSTAND[nummer] = False
                PROTOKOLL.append(f"{zeit}  Relais {nummer} -> {'EIN' if ZUSTAND[nummer] else 'AUS'}")
                print(PROTOKOLL[-1], flush=True)
            # Einkanalige Geraete lassen die Nummer im Schluessel weg.
            schluessel = "POWER" if ARGS.relais == 1 else f"POWER{nummer}"
            return self._send({schluessel: "ON" if ZUSTAND[nummer] else "OFF"})

        if befehl.lower().startswith("var"):
            name = "Var" + befehl[3:]
            if wert is not None:
                VARS[name] = wert
                PROTOKOLL.append(f"{zeit}  Lebenszeichen {name} = {wert}")
                print(PROTOKOLL[-1], flush=True)
            return self._send({name: VARS.get(name, "")})

        return self._send({"WARNING": "Unknown command"})


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--relais", type=int, default=1, help="Zahl der Kanaele")
    p.add_argument("--benutzer", default="admin")
    p.add_argument("--kennwort", default="", help="verlangt eine Anmeldung")
    ARGS = p.parse_args()

    for i in range(1, ARGS.relais + 1):
        ZUSTAND[i] = False

    print(f"Tasmota-Attrappe mit {ARGS.relais} Relais auf Port {ARGS.port}"
          + (", mit Anmeldung" if ARGS.kennwort else ""))
    ThreadingHTTPServer(("0.0.0.0", ARGS.port), Handler).serve_forever()
