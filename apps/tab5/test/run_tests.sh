#!/bin/sh
# Prüfungen der Datenschicht auf dem Mac (kein Gerät nötig).
set -e
cd "$(dirname "$0")/.."
CJSON=managed_components/espressif__cjson/cJSON
[ -d "$CJSON" ] || { echo "cJSON fehlt: einmal idf.py reconfigure laufen lassen"; exit 1; }
mkdir -p host/build
c++ -std=c++17 -O1 -g -Wall -Wno-unused-function -Wno-deprecated-declarations -Imain/kern -I$CJSON \
    -Imain test/pruefung.cpp host/plattform.cpp main/kern/*.cpp $CJSON/cJSON.c -o host/build/pruefung
host/build/pruefung main/beispiel/beispiel.json
