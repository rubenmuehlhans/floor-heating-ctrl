#!/usr/bin/env python3
"""Erzeugt die Fixtures für die Tests der Geräteschnittstelle.

Zwei Quellen:

- die Attrappen (vorher apple/Werkzeuge/attrappen.sh starten) für den aktuellen Aufbau der
  Antworten,
- der Mitschnitt der echten Anlage (messungen/verlauf.jsonl neben dem Repository) für die
  Antworten realer Firmwarestände, der erste und der letzte Datensatz.

Der Mitschnitt wird anonymisiert, denn das Repository ist öffentlich: Gerätekennungen,
MAC-Adressen und Fühler-ROMs werden ersetzt, die Adressen im Heimnetz auf ein anderes Netz
gelegt. Die Ersatzkennungen sind dieselben wie in der Beispielanlage der App.

    python3 apple/Werkzeuge/fixtures.py [pfad/zu/verlauf.jsonl]
"""

import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

WURZEL = Path(__file__).resolve().parents[2]
ZIEL = WURZEL / "apple/Packages/Geraeteschnittstelle/Tests/GeraeteschnittstelleTests/Fixtures"
MITSCHNITT = Path(sys.argv[1]) if len(sys.argv) > 1 else WURZEL.parent / "messungen/verlauf.jsonl"

ATTRAPPEN = {
    "verteiler": (8321, ["state", "config", "demand", "ble", "calib", "peers", "wifi/scan"]),
    "speicher": (8322, ["state", "config", "measurements", "history", "peers"]),
    "kessel": (8323, ["state"]),
    "leitstand": (8325, ["state", "ble", "config", "demand"]),
}

# Ersatzkennungen wie in apple/Packages/Anlage/Sources/Anlage/Beispielanlage.swift
ERSATZ = {
    "kessel": "heiz_19c8a2",
    "puffer": "heiz_6e03b5",
    "keller": "fbh_3a91c4",
    "erdgeschoss": "fbh_5d20e7",
    "obergeschoss": "fbh_7b44f1",
}


def speichern(name, inhalt):
    pfad = ZIEL / name
    pfad.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(inhalt, (dict, list)):
        inhalt = json.dumps(inhalt, ensure_ascii=False, indent=1) + "\n"
    pfad.write_text(inhalt, encoding="utf-8")
    print(f"  {pfad.relative_to(WURZEL)}")


def attrappen():
    print("Attrappen:")
    for geraet, (port, pfade) in ATTRAPPEN.items():
        for pfad in pfade:
            url = f"http://127.0.0.1:{port}/api/{pfad}"
            with urllib.request.urlopen(url, timeout=3) as antwort:
                daten = json.load(antwort)
            speichern(f"attrappe/{geraet}-{pfad.replace('/', '-')}.json", daten)


def ersatz_hex(text, laenge):
    return hashlib.sha256(text.encode()).hexdigest()[:laenge].upper()


def anonymisieren(text, kennungen):
    # Die Firmware bildet die Kennung aus den letzten drei Byte der MAC; die Ersatz-MAC eines
    # Geräts endet deshalb auf seine Ersatzkennung.
    geraete_mac = {}
    for echt, falsch in kennungen.items():
        text = text.replace(echt, falsch)
        geraete_mac[echt.split("_")[1].upper()] = falsch.split("_")[1].upper()

    def mac(treffer):
        roh = treffer.group(1).replace(":", "")
        ende = geraete_mac.get(roh[6:]) or ersatz_hex(roh, 6)
        return '"A0:B7:65:' + ":".join(re.findall("..", ende)) + '"'

    text = re.sub(r'"((?:[0-9A-F]{2}:){5}[0-9A-F]{2})"', mac, text)
    # Fühler-ROMs; die Familie 28 (DS18B20) am Ende bleibt stehen
    text = re.sub(r'"[0-9A-F]{14}28"', lambda m: '"' + ersatz_hex(m.group(0), 14) + '28"', text)
    return re.sub(r"192\.168\.1\.(\d+)", r"192.168.0.\1", text)


def mitschnitt():
    if not MITSCHNITT.exists():
        print(f"Mitschnitt fehlt: {MITSCHNITT}")
        return
    print("Mitschnitt:")
    with MITSCHNITT.open() as f:
        erste = json.loads(f.readline())
        letzte = None
        for zeile in f:
            if zeile.strip():
                letzte = zeile
    letzte = json.loads(letzte)

    kennungen = {}
    for datensatz in (erste, letzte):
        for geraet, falsch in ERSATZ.items():
            echt = (datensatz.get(geraet) or {}).get("device", {}).get("id")
            if echt:
                kennungen[echt] = falsch

    for name, datensatz in (("alt", erste), ("neu", letzte)):
        for geraet in ("kessel", "puffer", "erdgeschoss"):
            text = anonymisieren(json.dumps(datensatz[geraet], ensure_ascii=False), kennungen)
            speichern(f"mitschnitt/{geraet}-state-{name}.json", json.loads(text))
        text = anonymisieren(json.dumps(datensatz["erdgeschoss_demand"], ensure_ascii=False),
                             kennungen)
        speichern(f"mitschnitt/erdgeschoss-demand-{name}.json", json.loads(text))

    rest = [echt for echt in kennungen if echt in json.dumps(
        [json.loads(p.read_text()) for p in (ZIEL / "mitschnitt").glob("*.json")])]
    if rest:
        sys.exit(f"Nicht anonymisiert: {rest}")


if __name__ == "__main__":
    attrappen()
    mitschnitt()
