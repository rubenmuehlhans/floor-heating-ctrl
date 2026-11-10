#!/usr/bin/env python3
"""API-Schlüssel von Anthropic in die Bedientafel eintragen, über die Konsole (USB-C).

    python3 tools/schluessel.py --port /dev/cu.usbmodemXXXX ~/Downloads/anthropic_tab5_key.txt

Liest die erste Zeile der Datei und schickt sie mit `ki_schluessel` an die Tafel. Weder das
Skript noch die Tafel geben den Schlüssel aus; die Tafel bestätigt mit den letzten vier
Zeichen. Danach unter Geräte › Einstellungen der Tafel der Übertragung zustimmen.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from aufnahme import Port  # noqa: E402  (öffnet den Port ohne Reset)


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--port", required=True)
    a.add_argument("datei")
    o = a.parse_args()
    schluessel = Path(o.datei).expanduser().read_text().strip().splitlines()[0].strip()
    if not schluessel.startswith("sk-ant-"):
        sys.exit("Die Datei enthält keinen Schlüssel der Form sk-ant-…")
    s = Port(o.port)
    try:
        s.leeren()
        s.write(f"ki_schluessel {schluessel}\n".encode())
        ende = time.time() + 5
        while time.time() < ende:
            z = s.readline(0.5).decode(errors="replace").strip()
            if z.startswith("@@SCHLUESSEL"):
                print(z.removeprefix("@@SCHLUESSEL "))
                return
        sys.exit("keine Antwort der Tafel")
    finally:
        s.close()


if __name__ == "__main__":
    main()
