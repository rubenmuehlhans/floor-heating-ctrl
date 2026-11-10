#!/bin/sh
# Vorschau der Bedientafel auf dem Mac: alle Seiten, dunkel und hell, nach host/shots/.
#   host/vorschau.sh [seite …]
set -e
cd "$(dirname "$0")/.."
cmake -S host -B host/build -G Ninja >/dev/null
ninja -C host/build vorschau | grep -v "^\[" || true
mkdir -p host/shots
SEITEN=${*:-"uebersicht befunde raeume raum:fbh_7b44f1/2 heizung verlauf geraete assistent einstellungen wlan"}
host/build/vorschau main/beispiel/beispiel.json host/shots $SEITEN
host/build/vorschau main/beispiel/beispiel.json host/shots hell $SEITEN
