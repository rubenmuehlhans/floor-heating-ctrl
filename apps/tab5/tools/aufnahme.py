#!/usr/bin/env python3
"""Bildschirmfotos der Bedientafel über die Konsole (USB-C).

    ~/panelvenv/bin/python3 tools/aufnahme.py --port /dev/cu.usbmodemXXXX uebersicht raeume heizung

Öffnet je Seite `seite <name>`, nimmt `shot` auf und legt ein PNG in tools/shots/ ab.
Braucht pyserial; das PNG schreibt das Skript selbst (zlib), ohne weitere Pakete.
"""
import argparse
import base64
import struct
import sys
import time
import zlib
from pathlib import Path

import serial

HIER = Path(__file__).resolve().parent


def png(pfad, b, h, stride, daten):
    roh = bytearray()
    for y in range(h):
        roh.append(0)
        zeile = daten[y * stride:y * stride + b * 2]
        for x in range(b):
            v = zeile[2 * x] | zeile[2 * x + 1] << 8
            roh += bytes(((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    pfad.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", b, h, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(bytes(roh), 6)) + chunk(b"IEND", b""))


def befehl(s, text, warten=0.3):
    s.write((text + "\n").encode())
    s.flush()
    time.sleep(warten)


def aufnehmen(s):
    s.reset_input_buffer()
    befehl(s, "shot", 0)
    kopf, teile = None, []
    ende = time.time() + 60
    while time.time() < ende:
        z = s.readline().decode(errors="replace").strip()
        if z.startswith("@@BILD"):
            kopf = [int(x) for x in z.split()[1:4]]
        elif z == "@@ENDE":
            break
        elif kopf and z and not z.startswith(("I (", "W (", "E (")):
            teile.append(z)
    if not kopf:
        sys.exit("kein Bild empfangen")
    return kopf, base64.b64decode("".join(teile))


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--port", required=True)
    a.add_argument("seiten", nargs="+")
    o = a.parse_args()
    ziel = HIER / "shots"
    ziel.mkdir(exist_ok=True)
    with serial.Serial(o.port, 115200, timeout=2) as s:
        for seite in o.seiten:
            befehl(s, "seite " + seite, 1.2)
            (b, h, stride), daten = aufnehmen(s)
            p = ziel / (seite.replace(":", "_").replace("/", "_") + ".png")
            png(p, b, h, stride, daten)
            print(" ", p)


if __name__ == "__main__":
    main()
