/*
 * Anbindung an Home Assistant ueber MQTT.
 *
 * Die Entities werden ueber die MQTT-Discovery angemeldet und bei jeder
 * Konfigurationsaenderung neu berechnet; entfallene Raeume werden mit leerem Retained-Payload wieder
 * entfernt.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_start(void);

/* Nach einer Konfigurationsaenderung: Discovery neu aufbauen. */
void mqtt_config_changed(void);

#ifdef __cplusplus
}
#endif
