#!/usr/bin/env python3
"""Prüft den Schnittstellenzugriff der Home-Assistant-Integration.

Nutzt dieselbe Klasse wie die Integration, kommt aber ohne Home Assistant aus.
So lässt sich gegen ein echtes Gerät prüfen, ob Schnittstelle und Zugriff
zueinander passen, bevor die Integration installiert wird.

    python3 tools/check_api.py 192.168.1.250

Lesende Prüfungen laufen immer. Mit --schreiben wird zusätzlich ein Sollwert
verstellt und wieder zurückgesetzt; Ventile fährt das Skript nicht.
"""

from __future__ import annotations

import argparse
import asyncio
import importlib.util
from pathlib import Path

import aiohttp  # noqa: F401  (von api.py benötigt)

# api.py wird unmittelbar aus der Datei geladen, nicht über das Paket: dessen
# __init__.py bindet Home Assistant ein. Gelingt das hier, ist zugleich belegt,
# dass der Zugriff selbst ohne Home Assistant auskommt.
_API_PFAD = (
    Path(__file__).resolve().parent.parent
    / "custom_components"
    / "floor_heating"
    / "api.py"
)
_spec = importlib.util.spec_from_file_location("floor_heating_api", _API_PFAD)
_api = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_api)

FloorHeatingApi = _api.FloorHeatingApi
FloorHeatingError = _api.FloorHeatingError

ok = 0
fehler = 0


def pruefe(bedingung: bool, text: str) -> None:
    global ok, fehler
    if bedingung:
        ok += 1
        print(f"  ok      {text}")
    else:
        fehler += 1
        print(f"  FEHLER  {text}")


async def main(host: str, schreiben: bool) -> int:
    async with aiohttp.ClientSession() as session:
        api = FloorHeatingApi(host, session)

        print("Zustand")
        try:
            state = await api.async_get_state()
        except FloorHeatingError as err:
            print(f"  nicht erreichbar: {err}")
            return 1

        for schluessel in ("rooms", "channels", "device", "net", "calib", "local_sensors"):
            pruefe(schluessel in state, f"/api/state enthält {schluessel}")

        device = state.get("device", {})
        pruefe(bool(device.get("id")), f"Gerätekennung vorhanden: {device.get('id')}")
        pruefe(bool(device.get("mac")), f"MAC vorhanden: {device.get('mac')}")

        for ch in state.get("channels", []):
            fehlend = {"id", "position", "op", "known", "group", "manual", "calibrated"} - set(ch)
            pruefe(not fehlend, f"Heizkreis {ch.get('id')} vollständig")
            break

        for room in state.get("rooms", []):
            fehlend = {"id", "name", "mode", "target_c", "temp_valid", "target_position"} - set(room)
            pruefe(not fehlend, f"Raum {room.get('name')} vollständig")
            break

        print("\nZwischenspeicher über den Änderungszähler")
        vorher = state
        nachher = await api.async_get_state()
        pruefe(isinstance(nachher, dict), "zweite Abfrage liefert einen Zustand")
        pruefe(nachher.get("device") == vorher.get("device"), "Gerätedaten bleiben gleich")

        print("\nKonfiguration")
        try:
            cfg = await api.async_get_config()
            pruefe("rooms" in cfg and "channels" in cfg, "/api/config lesbar")
            pruefe("site" in cfg, f"Anlagenbezeichnung: {cfg.get('site')!r}")
            kalibriert = sum(1 for c in cfg["channels"] if c["calibrated"])
            print(f"  Hinweis  {kalibriert} von {len(cfg['channels'])} Heizkreisen kalibriert")
        except FloorHeatingError as err:
            pruefe(False, f"/api/config: {err}")

        if schreiben and state.get("rooms"):
            print("\nSchreibzugriff (Sollwert)")
            raum = state["rooms"][0]
            alt = raum["target_c"]
            neu = 21.5 if alt != 21.5 else 21.0
            try:
                await api.async_set_room_target(raum["id"], neu)
                await asyncio.sleep(1.0)
                geprueft = await api.async_get_state()
                jetzt = next(r["target_c"] for r in geprueft["rooms"] if r["id"] == raum["id"])
                pruefe(abs(jetzt - neu) < 0.01, f"Sollwert gesetzt: {jetzt}")
                await api.async_set_room_target(raum["id"], alt)
                await asyncio.sleep(1.0)
                zurueck = await api.async_get_state()
                jetzt = next(r["target_c"] for r in zurueck["rooms"] if r["id"] == raum["id"])
                pruefe(abs(jetzt - alt) < 0.01, f"Sollwert zurückgesetzt: {jetzt}")
            except FloorHeatingError as err:
                pruefe(False, f"Schreibzugriff: {err}")

        print(f"\n{ok + fehler} Prüfungen, {fehler} Fehler")
        return 0 if fehler == 0 else 1


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("host", help="Adresse des Geräts, etwa 192.168.1.250")
    p.add_argument("--schreiben", action="store_true", help="auch einen Sollwert setzen")
    args = p.parse_args()
    raise SystemExit(asyncio.run(main(args.host, args.schreiben)))
