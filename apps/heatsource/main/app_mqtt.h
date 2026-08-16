/*
 * Anmeldung bei Home Assistant und Veroeffentlichung der Zustaende.
 *
 * Die Entitaeten meldet das Geraet selbst ueber MQTT-Discovery an. Welche
 * entstehen, richtet sich nach dem, was das Geraet wirklich fuehrt: ein Geraet
 * ohne Heizkreise bekommt keine Pumpen-Entitaeten, eines ohne Abgasfuehler
 * keine Brenner-Entitaeten.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hamqtt_start(void);

/* Rueckruf fuer mqttc: erneuert Abos und Anmeldung nach jedem
 * Verbindungsaufbau. */
#include "mqttc.h"
mqttc_connected_cb_t hamqtt_on_connected(void);

/* Nach einer Konfigurationsaenderung die Anmeldung neu aufbauen. */
void hamqtt_config_changed(void);

#ifdef __cplusplus
}
#endif
