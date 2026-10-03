#!/bin/zsh
# Startet die Attrappen der Heizungsgeräte und meldet sie per Bonjour an, wie es die Firmware tut:
# Dienst _fbhctrl._tcp, Instanzname nach dem Ort, TXT-Einträge id, site und role.
#
# Die Anmeldung läuft über die Loopback-Schnittstelle lo0. Sichtbar sind die Attrappen damit nur
# auf diesem Mac, für die App dort und im Simulator. Die echten Geräte im Heimnetz suchen ihre
# Nachbarn ebenfalls per Bonjour und dürfen keine Attrappe unter 127.0.0.1 finden. Die rein
# lokale Anmeldung (dns-sd -lo) wäre ebenso sicher, doch NWBrowser meldet solche Dienste nicht.
#
#   apple/Werkzeuge/attrappen.sh          startet alles, Ende mit Ctrl-C
#   apple/Werkzeuge/attrappen.sh --leer   Speicherboard ohne Fühler, für den Einrichtungsablauf

set -euo pipefail

WURZEL="${0:A:h:h:h}"
cd "$WURZEL"

LEER=()
[[ "${1:-}" == "--leer" ]] && LEER=(--leer)

typeset -a PROZESSE
aufraeumen() {
    for pid in $PROZESSE; do kill "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
    print "Attrappen beendet."
}
trap aufraeumen EXIT INT TERM

belegt() { lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1 }
for port in 8321 8322 8323 8324 8325; do
    if belegt $port; then
        print -u2 "Port $port ist belegt. Laufen die Attrappen schon?"
        exit 1
    fi
done

starte() {
    "$@" >/dev/null 2>&1 &
    PROZESSE+=($!)
}

starte python3 tools/mock_device.py --port 8321
starte python3 tools/mock_heatsource.py --port 8322 $LEER
starte python3 tools/mock_heatsource.py --kessel --port 8323
starte python3 tools/mock_tasmota.py --port 8324 --relais 2
starte python3 tools/mock_station.py --port 8325

for port in 8321 8322 8323 8325; do
    for versuch in {1..50}; do
        curl -fs "http://127.0.0.1:$port/api/state" >/dev/null && break
        sleep 0.1
    done
done

# Name, Port, Host, TXT – Kennung und Ort wie in /api/state der jeweiligen Attrappe
anmelden() {
    starte dns-sd -i lo0 -P "$1 (Attrappe)" _fbhctrl._tcp local "$2" "$3" 127.0.0.1 \
        "id=$4" "site=$1" "role=$5"
}
anmelden Erdgeschoss    8321 attrappe-erdgeschoss.local    fbh_a1b2c3  manifold
anmelden Pufferspeicher 8322 attrappe-pufferspeicher.local heiz_3f21ac heat
anmelden Kessel         8323 attrappe-kessel.local         heiz_9a1b2c heat
anmelden Leitstand      8325 attrappe-leitstand.local      lst_c0ffee  station

print "Attrappen laufen, nur auf diesem Mac per Bonjour sichtbar:"
print "  Verteiler Erdgeschoss   http://127.0.0.1:8321   fbh_a1b2c3"
print "  Pufferspeicher          http://127.0.0.1:8322   heiz_3f21ac${LEER:+  (ohne Fühler)}"
print "  Kessel                  http://127.0.0.1:8323   heiz_9a1b2c"
print "  Tasmota, zwei Relais    http://127.0.0.1:8324/cm?cmnd=Power1"
print "  Leitstand               http://127.0.0.1:8325   lst_c0ffee"
print "Beenden mit Ctrl-C."
wait
