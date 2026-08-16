"""Konstanten der Integration."""

from __future__ import annotations

from datetime import timedelta

DOMAIN = "floor_heating"

CONF_HOST = "host"

# Der Gerätezustand wird über einen Änderungszähler als ETag ausgeliefert.
# Fährt kein Ventil, antwortet das Gerät mit 304 und erspart sich den Aufbau
# der Antwort; ein kurzes Abfrageintervall ist damit vertretbar.
DEFAULT_SCAN_INTERVAL = timedelta(seconds=5)

# Grenzen der Solltemperatur. Sie entsprechen der Prüfung in der Firmware
# (cfg_validate in components/config_store/config_store.c).
MIN_TEMP = 5.0
MAX_TEMP = 35.0
TEMP_STEP = 0.5

MANUFACTURER = "Eigenbau"

# Zwei Gerätearten teilen sich die Integration: die Verteilerplatine mit den
# Ventilen und das Heizungsgerät an Kessel beziehungsweise Pufferspeicher. Sie
# melden ihre Art im Feld device.role.
ROLE_MANIFOLD = "manifold"
ROLE_HEAT = "heat"

# Klartext der Messstellen des Heizungsgeräts. Die Firmware liefert nur den
# Kurznamen, damit die Antwort schlank bleibt.
ROLE_LABELS = {
    "abgas": "Abgas",
    "kessel_vl": "Kessel Vorlauf",
    "kessel_rl": "Kessel Rücklauf",
    "puffer": "Pufferspeicher",
    "puffer_unten": "Pufferspeicher unten",
    "hk1_vl": "Heizkreis 1 Vorlauf",
    "hk1_rl": "Heizkreis 1 Rücklauf",
    "hk2_vl": "Heizkreis 2 Vorlauf",
    "hk2_rl": "Heizkreis 2 Rücklauf",
    "hk3_vl": "Heizkreis 3 Vorlauf",
    "hk3_rl": "Heizkreis 3 Rücklauf",
    "hk4_vl": "Heizkreis 4 Vorlauf",
    "hk4_rl": "Heizkreis 4 Rücklauf",
    "aussen": "Außentemperatur",
}

PUMP_MODES = ["auto", "ein", "aus"]

SERVICE_CALIBRATE = "calibrate"
SERVICE_CALIBRATE_ACCEPT = "calibrate_accept"
SERVICE_CALIBRATE_ABORT = "calibrate_abort"
SERVICE_FORCE = "force"
SERVICE_RELEASE = "release"
SERVICE_CHECK_NOW = "check_now"

ATTR_CHANNEL = "channel"
ATTR_DIRECTION = "direction"
ATTR_ROOM = "room"
