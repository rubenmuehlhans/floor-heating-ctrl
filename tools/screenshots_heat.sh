#!/bin/bash
#
# Erzeugt die Bildschirmaufnahmen des Waermeerzeugers gegen die Attrappe.
#
#   python3 tools/mock_heatsource.py &
#   tools/screenshots_heat.sh
#
# Voraussetzungen: Google Chrome und ImageMagick.
#
# Hinweis: Headless Chrome erzwingt eine Fensterbreite von mindestens 500 px.
# Die schmale Ansicht wird deshalb mit 500 px aufgenommen; der mobile Umbruch
# der Oberflaeche greift bei 520 px.
set -e

CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
BASE="${BASE:-http://localhost:8322}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/screenshots/heizung"
mkdir -p "$OUT"

shoot() {  # ziel  breite  hoehe  pfad
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars \
    --window-size="$2,$3" --screenshot="$OUT/$1.png" \
    --virtual-time-budget=8000 "$BASE$4" >/dev/null 2>&1
  echo "  $1.png  $(magick "$OUT/$1.png" -format '%wx%h' info:)"
}

# Ausschnitt aus einer ganzseitigen Aufnahme: manche Karten liegen unterhalb
# des Sichtbereichs.
crop() {  # ziel  breite  hoehe  pfad  ausschnitt
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars \
    --window-size="$2,$3" --screenshot=/tmp/heat-voll.png \
    --virtual-time-budget=9000 "$BASE$4" >/dev/null 2>&1
  magick /tmp/heat-voll.png -crop "$5" +repage "$OUT/$1.png"
  echo "  $1.png  $(magick "$OUT/$1.png" -format '%wx%h' info:)"
}

echo "Aufnahmen Waermeerzeuger:"
shoot uebersicht      1280 1180 "/?theme=light#ov"
shoot uebersicht-dark 1280 1180 "/?theme=dark#ov"
shoot heizkreise      1280 1250 "/?theme=light#circuits"
shoot fuehler         1280  760 "/?theme=light#probes"
shoot verlauf         1280 1050 "/?theme=light#hist"
shoot system          1280 1000 "/?theme=light#sys"
shoot mobil            500  980 "/?theme=light#ov"

# Anlagenschema allein, fuer die Dokumentation.
crop schema 1280 1180 "/?theme=light#ov" "1216x560+32+150"

echo "fertig."
