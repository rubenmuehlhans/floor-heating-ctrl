#!/usr/bin/env python3
"""Bildschirmfotos der Bedientafel über die Konsole (USB-C).

    python3 tools/aufnahme.py --port /dev/cu.usbmodemXXXX uebersicht raeume heizung

Öffnet je Seite `seite <name>`, nimmt `shot` auf und legt ein PNG in tools/shots/ ab.
Ohne weitere Pakete; das PNG schreibt das Skript selbst (zlib).

⚠️ Der Port wird roh geöffnet, ohne DTR/RTS anzufassen und ohne HUPCL: das USB-Serial/JTAG
des P4 wertet einen Wechsel als Reset, und ein Reset über USB kann ihn im ROM festsetzen
(camperSense, tools/konsole.py und tab5/CLAUDE.md Falle 21). Deshalb kein pyserial.
"""
import argparse
import base64
import os
import select
import termios
import struct
import sys
import time
import zlib
from pathlib import Path


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


class Port:
    def __init__(self, pfad):
        self.fd = os.open(pfad, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0
        a[2] = (a[2] | termios.CS8 | termios.CREAD | termios.CLOCAL) & ~termios.HUPCL
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.rest = b""

    def write(self, d):
        os.write(self.fd, d)

    def leeren(self):
        while select.select([self.fd], [], [], 0.1)[0]:
            if not os.read(self.fd, 65536):
                break
        self.rest = b""

    def readline(self, frist=2.0):
        ende = time.time() + frist
        while b"\n" not in self.rest and time.time() < ende:
            if select.select([self.fd], [], [], 0.2)[0]:
                self.rest += os.read(self.fd, 65536)
        z, _, self.rest = self.rest.partition(b"\n")
        return z

    def close(self):
        os.close(self.fd)


def befehl(s, text, warten=0.3):
    s.write((text + "\n").encode())
    time.sleep(warten)


def aufnehmen(s, zeilen=None):
    """Ein Bild als Zeilen nach Nummer; fehlende kommen aus einer weiteren Aufnahme."""
    zeilen = zeilen if zeilen is not None else {}
    kopf = None
    for _ in range(3):
        s.leeren()
        befehl(s, "shot", 0)
        ende = time.time() + 90
        while time.time() < ende:
            z = s.readline().decode(errors="replace").strip()
            if z.startswith("@@BILD"):
                kopf = [int(x) for x in z.split()[1:4]]
            elif z == "@@ENDE":
                break
            elif z.startswith("@@Z ") and kopf:
                teile = z.split(" ")
                if len(teile) == 3:
                    try:
                        d = base64.b64decode(teile[2], validate=True)
                    except ValueError:
                        continue
                    if len(d) == kopf[0] * 2:
                        zeilen.setdefault(int(teile[1]), d)
        if kopf and len(zeilen) >= kopf[1]:
            break
    if not kopf:
        sys.exit("kein Bild empfangen")
    if len(zeilen) < kopf[1]:
        sys.exit(f"Bild unvollständig: {len(zeilen)} von {kopf[1]} Zeilen")
    b, h = kopf[0], kopf[1]
    return (b, h, b * 2), b"".join(zeilen[y] for y in range(h))


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--port", required=True)
    a.add_argument("seiten", nargs="+")
    o = a.parse_args()
    ziel = HIER / "shots"
    ziel.mkdir(exist_ok=True)
    s = Port(o.port)
    try:
        for seite in o.seiten:
            befehl(s, "seite " + seite, 1.5)
            (b, h, stride), daten = aufnehmen(s)
            p = ziel / (seite.replace(":", "_").replace("/", "_") + ".png")
            png(p, b, h, stride, daten)
            print(" ", p)
    finally:
        s.close()


if __name__ == "__main__":
    main()
