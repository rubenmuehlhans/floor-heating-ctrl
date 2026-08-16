#!/usr/bin/env python3
"""Prüft die eingebetteten Weboberflächen auf Übersetzbarkeit.

    python3 tools/check_www.py [anwendung ...]

Ohne Angabe werden alle Anwendungen unter `apps/` geprüft. Die Oberfläche
wird dafür aus ihren Teilen zusammengesetzt (siehe `tools/www.py`) — geprüft
wird also das, was auch im Gerät landet.

Anlass: Beim Bearbeiten mit Textersetzung ist zweimal ein Block an der
falschen Stelle gelandet — einmal JavaScript im Stilblatt, einmal CSS im
Skript. Beides bleibt im Quelltext unauffällig, legt aber im Browser die
gesamte Oberfläche lahm: der Stilblatt-Fehler verschluckt die folgenden
Regeln, der Skript-Fehler beendet die Ausführung vor dem ersten Aufruf.

Geprüft wird deshalb dreierlei:

  1. Der Inhalt von <script> muss sich als JavaScript übersetzen lassen
     (`node --check`). Das ist die eigentliche Absicherung.
  2. Im Stilblatt darf keine JavaScript-Anweisung stehen.
  3. Die Zahl der Blöcke muss stimmen, damit nichts unbemerkt verdoppelt wird.

Rückgabewert 0, wenn alles stimmt, sonst 1.
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from www import quellen, zusammensetzen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# Zeilenanfänge, die im Stilblatt nichts zu suchen haben.
JS_IM_STIL = re.compile(r"^\s*(let|const|var|function|async function|return|if \()\b")

fehler = 0


def melde(app: str, text: str) -> None:
    global fehler
    fehler += 1
    print(f"  FEHLER  {app}: {text}")


def block(inhalt: str, tag: str) -> list[str]:
    return re.findall(rf"<{tag}[^>]*>(.*?)</{tag}>", inhalt, re.S)


def pruefe(app: str) -> None:
    inhalt = zusammensetzen(app)
    print(f"{app}  ({len(inhalt)} Byte aus {len(quellen(app))} Teilen)")

    stile = block(inhalt, "style")
    skripte = block(inhalt, "script")

    if len(stile) != 1:
        melde(app, f"{len(stile)} Stilblöcke, erwartet genau einer")
    if len(skripte) != 1:
        melde(app, f"{len(skripte)} Skriptblöcke, erwartet genau einer")

    for s in stile:
        for nr, zeile in enumerate(s.split("\n"), 1):
            if JS_IM_STIL.match(zeile):
                melde(app, f"JavaScript im Stilblatt, Zeile {nr}: {zeile.strip()[:60]}")
                break

    for s in skripte:
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
            f.write(s)
            weg = f.name
        r = subprocess.run(["node", "--check", weg], capture_output=True, text=True)
        Path(weg).unlink()
        if r.returncode != 0:
            erste = (r.stderr.strip().split("\n") or [""])[0]
            melde(app, f"Skript nicht übersetzbar: {erste}")

    if fehler == 0:
        print(f"  ok      {len(inhalt)} Byte, Stil und Skript in Ordnung")


if __name__ == "__main__":
    apps = sys.argv[1:] or sorted(p.parents[2].name
                                  for p in ROOT.glob("apps/*/main/www/sources.txt"))
    if not apps:
        print("Keine Oberfläche gefunden")
        raise SystemExit(1)
    for a in apps:
        pruefe(a)
    print(f"\n{len(apps)} Oberflächen, {fehler} Fehler")
    raise SystemExit(0 if fehler == 0 else 1)
