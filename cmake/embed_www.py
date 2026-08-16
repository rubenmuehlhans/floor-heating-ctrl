#!/usr/bin/env python3
"""Fügt die Quelldateien einer Weboberfläche zusammen und komprimiert sie.

    embed_www.py <ziel.gz> <quelle> [<quelle> ...]

Die Quellen werden in der angegebenen Reihenfolge byteweise aneinandergehängt.
Damit lässt sich ein gemeinsames Stilblatt zwischen einen anwendungseigenen
Kopf und Rumpf schieben, ohne die Oberfläche zur Laufzeit aus mehreren Dateien
zusammensetzen zu müssen.

Zeitstempel und Dateiname bleiben aus dem gzip-Kopf heraus: sonst unterscheidet
sich das Programmabbild bei jedem Bau, und der Vergleich zweier Fassungen wird
wertlos.
"""

import gzip
import sys


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2

    ziel, quellen = argv[1], argv[2:]
    with open(ziel, "wb") as roh:
        with gzip.GzipFile(filename="", mode="wb", fileobj=roh,
                           compresslevel=9, mtime=0) as aus:
            for name in quellen:
                with open(name, "rb") as teil:
                    aus.write(teil.read())
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
