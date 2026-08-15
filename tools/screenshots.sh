#!/bin/bash
#
# Erzeugt die Bildschirmaufnahmen der README gegen die Attrappe.
#
#   python3 tools/mock_device.py &
#   tools/screenshots.sh
#
# Voraussetzungen: Google Chrome und ImageMagick.
#
# Hinweis: Headless Chrome erzwingt eine Fensterbreite von mindestens 500 px.
# Die schmalen Ansichten werden deshalb mit 500 px aufgenommen; der mobile
# Umbruch der Oberflaeche greift bei 520 px.
set -e

CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
BASE="${BASE:-http://localhost:8321}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/screenshots"
mkdir -p "$OUT"

shoot() {  # ziel  breite  hoehe  pfad
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars \
    --window-size="$2,$3" --screenshot="$OUT/$1.png" \
    --virtual-time-budget=8000 "$BASE$4" >/dev/null 2>&1
  echo "  $1.png  $(magick "$OUT/$1.png" -format '%wx%h' info:)"
}

# Fuer die Kalibrieraufnahme muss eine Messfahrt abgeschlossen sein.
if ! curl -s "$BASE/api/calib?from=0" | grep -q '"state": "done"'; then
  echo "Messfahrt wird angestossen ..."
  curl -s -X POST -H "Content-Type: application/json" -d '{}' \
    "$BASE/api/calib/4/start" >/dev/null
  until curl -s "$BASE/api/calib?from=0" | grep -q '"state": "done"'; do sleep 3; done
fi

echo "Aufnahmen:"
curl -s "$BASE/mock/ap?on=0" >/dev/null
shoot uebersicht      1280  900 "/?theme=light#ov"
shoot uebersicht-dark 1280  900 "/?theme=dark#ov"
shoot raeume          1280 1000 "/?theme=light#rooms"
shoot kreise          1280  900 "/?theme=light#chans"
shoot sensoren        1280  900 "/?theme=light#sensors"
shoot system          1280 1000 "/?theme=light#sys"
shoot mobil            500  900 "/?theme=light#ov"

# Die Kalibrierkarte liegt unten auf der Kreise-Seite: ganze Seite aufnehmen
# und den Ausschnitt herausschneiden.
"$CHROME" --headless=new --disable-gpu --hide-scrollbars --window-size=1280,2500 \
  --screenshot=/tmp/kreise-voll.png --virtual-time-budget=9000 \
  "$BASE/?theme=light#chans" >/dev/null 2>&1
magick /tmp/kreise-voll.png -crop 1280x700+0+1680 +repage "$OUT/kalibrierung.png"
echo "  kalibrierung.png  $(magick "$OUT/kalibrierung.png" -format '%wx%h' info:)"

# Einrichtungsportal im Zugangspunkt-Betrieb.
curl -s "$BASE/mock/ap?on=1" >/dev/null
shoot einrichtung 500 900 "/?theme=light"
curl -s "$BASE/mock/ap?on=0" >/dev/null

echo "fertig."
