#!/usr/bin/env python3
"""Bildschirmaufnahme des Leitstands ueber USB, ohne WLAN.

Schickt "bild" an die Befehlszeile der Firmware (apps/station/main/st_ui.cpp),
liest die lauflaengencodierten Zeilen und schreibt ein PNG. Braucht nur
pyserial, das mit ESP-IDF ohnehin installiert ist.

    python3 tools/leitstand_bild.py /dev/cu.usbserial-XXXX bild.png [seite]

Mit "seite" wird vorher auf die naechste Seite geblaettert.
"""
import struct
import sys
import time
import zlib

import serial


def png(pfad, breite, hoehe, zeilen):
    roh = b"".join(b"\x00" + bytes(z) for z in zeilen)

    def block(art, daten):
        return (struct.pack(">I", len(daten)) + art + daten
                + struct.pack(">I", zlib.crc32(art + daten) & 0xFFFFFFFF))

    with open(pfad, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(block(b"IHDR", struct.pack(">IIBBBBB", breite, hoehe, 8, 2, 0, 0, 0)))
        f.write(block(b"IDAT", zlib.compress(roh, 9)))
        f.write(block(b"IEND", b""))


def main():
    port, ziel = sys.argv[1], sys.argv[2]
    # DTR und RTS vor dem Oeffnen festlegen: pyserial setzt beide sonst beim
    # Oeffnen, und RTS haengt am Reset des ESP32 -- das Geraet startete neu.
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.5
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    if len(sys.argv) > 3 and sys.argv[3] == "seite":
        s.write(b"seite\n")
        time.sleep(1.5)
        s.reset_input_buffer()
    s.write(b"bild\n")
    breite = hoehe = None
    zeilen = []
    ende = time.time() + 90
    while time.time() < ende:
        zeile = s.readline().decode("ascii", "replace").strip()
        if zeile.startswith("BILD "):
            _, b, h = zeile.split()
            breite, hoehe = int(b), int(h)
            zeilen = []
        elif zeile.startswith("Z") and breite:
            kopf, *teile = zeile.split()
            if not kopf[1:].isdigit() or int(kopf[1:]) != len(zeilen):
                sys.exit(f"Zeile {len(zeilen)} fehlt oder ist gestoert: {zeile[:60]}")
            px = bytearray()
            for teil in teile:
                farbe, _, n = teil.partition("*")
                if len(farbe) != 6 or not all(c in "0123456789abcdef" for c in farbe):
                    sys.exit(f"gestoerte Zeile {len(zeilen)}: {teil[:40]}")
                px += bytes.fromhex(farbe) * (int(n) if n else 1)
            zeilen.append(px[: breite * 3].ljust(breite * 3, b"\x00"))
        elif zeile == "ENDE":
            break
    s.close()
    if not breite or len(zeilen) != hoehe:
        sys.exit(f"unvollstaendig: {len(zeilen)} von {hoehe} Zeilen")
    png(ziel, breite, hoehe, zeilen)
    print(f"{ziel}: {breite}x{hoehe}")


if __name__ == "__main__":
    main()
