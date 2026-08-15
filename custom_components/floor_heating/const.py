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

SERVICE_CALIBRATE = "calibrate"
SERVICE_CALIBRATE_ACCEPT = "calibrate_accept"
SERVICE_CALIBRATE_ABORT = "calibrate_abort"
SERVICE_FORCE = "force"
SERVICE_RELEASE = "release"
SERVICE_CHECK_NOW = "check_now"

ATTR_CHANNEL = "channel"
ATTR_DIRECTION = "direction"
ATTR_ROOM = "room"
