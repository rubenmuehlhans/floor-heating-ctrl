"""Setzt eine Weboberfläche aus ihren Teilen zusammen.

Beide Oberflächen bestehen aus mehreren Dateien: einem gemeinsamen Stilblatt
und den anwendungseigenen Teilen. Welche das sind und in welcher Reihenfolge,
steht in `apps/<name>/main/www/sources.txt` — dieselbe Liste liest auch
`cmake/embed_www.cmake`.

Als Baustein für andere Werkzeuge gedacht (Attrappen, Prüfung, Aufnahmen).
Aufruf auf der Kommandozeile schreibt das Ergebnis nach stdout:

    python3 tools/www.py manifold > /tmp/index.html
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def quellen(app: str) -> list[Path]:
    """Die Teile einer Oberfläche, in der Reihenfolge des Zusammensetzens."""
    basis = ROOT / "apps" / app / "main"
    liste = basis / "www" / "sources.txt"
    teile = []
    for zeile in liste.read_text().splitlines():
        zeile = zeile.strip()
        if not zeile or zeile.startswith("#"):
            continue
        teile.append((basis / zeile).resolve())
    return teile


def zusammensetzen(app: str) -> str:
    return "".join(p.read_text() for p in quellen(app))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        raise SystemExit(2)
    sys.stdout.write(zusammensetzen(sys.argv[1]))
