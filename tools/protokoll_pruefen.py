#!/usr/bin/env python3
"""Prueft das Protokoll eines Tages auf der Karte des Leitstands.

Holt alle Dateien eines Tages ueber HTTP und prueft, was die Pruefschritte von
Etappe 2 verlangen (docs/konzept-leitstand.md):

  - jede CSV-Zeile hat so viele Zellen wie ihre Kopfzeile; eine
    unvollstaendige letzte Zeile nach einem Stromausfall wird gezaehlt, nicht
    als Fehler gewertet
  - die Zeiten steigen, und sie gehoeren zum Tag des Verzeichnisses
  - jede Zeile von ereignisse.jsonl und zustaende.jsonl ist JSON
  - je Geraet der Anteil der erwarteten Abtastungen (Takt 30 s) und die
    groessten Luecken, dazu ob jede Luecke ueber vier Takte durch ein
    Ereignis belegt ist (nicht_erreichbar oder ein Start des Leitstands)

    python3 tools/protokoll_pruefen.py leitstand.local [2026-09-23] [--takt 30]

Ohne Tag gilt der laufende Tag in UTC. Liest nur; auf der Karte aendert sich
nichts.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import urllib.request


def holen(host: str, pfad: str) -> bytes:
    with urllib.request.urlopen(f"http://{host}{pfad}", timeout=30) as r:
        return r.read()


def zeit(s: str) -> dt.datetime:
    return dt.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=dt.timezone.utc)


def csv_pruefen(name: str, text: str, tag: str, fehler: list[str]) -> list[dt.datetime]:
    zeilen = text.split("\n")
    unvollstaendig = 0
    if zeilen and zeilen[-1] == "":
        zeilen.pop()
    elif zeilen:
        unvollstaendig = 1  # letzte Zeile ohne Zeilenende
        zeilen.pop()
    if not zeilen or not zeilen[0].startswith("zeit"):
        fehler.append(f"{name}: keine Kopfzeile")
        return []
    spalten = zeilen[0].count(",")
    zeiten: list[dt.datetime] = []
    for nr, z in enumerate(zeilen[1:], start=2):
        if z.count(",") != spalten:
            fehler.append(f"{name}:{nr}: {z.count(',')} statt {spalten} Trennzeichen")
            continue
        try:
            t = zeit(z.split(",", 1)[0])
        except ValueError:
            fehler.append(f"{name}:{nr}: Zeit nicht lesbar")
            continue
        if t.strftime("%Y-%m-%d") != tag:
            fehler.append(f"{name}:{nr}: Zeit {t:%Y-%m-%dT%H:%M:%SZ} gehoert nicht zum Tag")
        if zeiten and t < zeiten[-1]:
            fehler.append(f"{name}:{nr}: Zeit springt zurueck")
        zeiten.append(t)
    if unvollstaendig:
        print(f"  {name}: letzte Zeile unvollstaendig (Stromausfall beim Schreiben?)")
    return zeiten


def jsonl_pruefen(name: str, text: str, fehler: list[str]) -> list[dict]:
    saetze = []
    zeilen = text.split("\n")
    rest = zeilen.pop() if zeilen else ""
    if rest:
        print(f"  {name}: letzte Zeile unvollstaendig")
    for nr, z in enumerate(zeilen, start=1):
        try:
            saetze.append(json.loads(z))
        except ValueError:
            fehler.append(f"{name}:{nr}: kein JSON")
    return saetze


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("host")
    ap.add_argument("tag", nargs="?", default=dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d"))
    ap.add_argument("--takt", type=int, default=30, help="Takt der Abfrage in Sekunden")
    a = ap.parse_args()

    liste = json.loads(holen(a.host, f"/api/log/days?tag={a.tag}"))
    namen = [d["name"] for d in liste.get("dateien", [])]
    if "protokolle" in namen:
        namen.remove("protokolle")
    fehler: list[str] = []
    ereignisse: list[dict] = []
    reihen: dict[str, list[dt.datetime]] = {}
    groesse = 0
    for name in sorted(namen):
        daten = holen(a.host, f"/log/{a.tag}/{name}")
        groesse += len(daten)
        text = daten.decode("utf-8", errors="replace")
        if name.endswith(".csv"):
            zeiten = csv_pruefen(name, text, a.tag, fehler)
            if ".5min" not in name:
                geraet = name.split(".")[0]
                reihen.setdefault(geraet, []).extend(zeiten)
        elif name.endswith(".jsonl"):
            saetze = jsonl_pruefen(name, text, fehler)
            if name == "ereignisse.jsonl":
                ereignisse = saetze
        elif name == "geraete.json":
            try:
                json.loads(text)
            except ValueError:
                fehler.append("geraete.json: kein JSON")

    print(f"Tag {a.tag}: {len(namen)} Dateien, {groesse / 1024:.0f} KB")
    # Der Leitstand meldet "nicht_erreichbar" nach drei Fehlabfragen in Folge;
    # belegt sein muss also erst eine Luecke von mehr als vier Takten.
    luecke_grenze = dt.timedelta(seconds=4 * a.takt)
    for geraet, zeiten in sorted(reihen.items()):
        if len(zeiten) < 2:
            continue
        zeiten.sort()
        dauer = (zeiten[-1] - zeiten[0]).total_seconds()
        erwartet = dauer / a.takt + 1
        luecken = [(z0, z1) for z0, z1 in zip(zeiten, zeiten[1:]) if z1 - z0 > luecke_grenze]
        ohne = []
        for z0, z1 in luecken:
            belegt = any(e.get("geraet") in (geraet,) or e.get("art") == "leitstand"
                         for e in ereignisse
                         if z0 <= zeit(e["zeit"]) <= z1 + dt.timedelta(seconds=a.takt))
            if not belegt:
                ohne.append((z0, z1))
        groesste = max(((z1 - z0).total_seconds() for z0, z1 in luecken), default=0)
        print(f"  {geraet:14} {len(zeiten):5} von {erwartet:6.0f} Abtastungen ({100 * len(zeiten) / erwartet:5.1f} %), "
              f"{len(luecken)} Luecken, groesste {groesste:.0f} s, ohne Ereignis {len(ohne)}")
        for z0, z1 in ohne[:5]:
            print(f"      unbelegt: {z0:%H:%M:%S} bis {z1:%H:%M:%S}")
    arten: dict[str, int] = {}
    for e in ereignisse:
        arten[e.get("art", "?")] = arten.get(e.get("art", "?"), 0) + 1
    print("  Ereignisse:", ", ".join(f"{k} {v}" for k, v in sorted(arten.items())) or "keine")
    for f in fehler[:20]:
        print("  FEHLER", f)
    print(f"{len(fehler)} Fehler")
    return 1 if fehler else 0


if __name__ == "__main__":
    sys.exit(main())
