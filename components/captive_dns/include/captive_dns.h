/*
 * Namensaufloesung fuer das Captive Portal.
 *
 * Solange der Einrichtungs-Zugangspunkt offen ist, beantwortet dieser
 * Namensdienst jede Anfrage mit der eigenen Adresse. Zusammen mit der
 * Umleitung im Webserver oeffnet sich die Einrichtungsseite dadurch von
 * selbst, sobald sich ein Geraet verbindet.
 *
 * Der Dienst laeuft nur im Zugangspunkt-Betrieb. Sobald die WLAN-Verbindung
 * steht, wird er beendet - sonst wuerde er die reguläre Namensaufloesung im
 * Heimnetz stoeren.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Startet den Dienst und beantwortet alle Anfragen mit der uebergebenen
 * Adresse in Netzwerkbyte-Reihenfolge. */
esp_err_t captive_dns_start(uint32_t answer_ip);

void captive_dns_stop(void);

bool captive_dns_running(void);

#ifdef __cplusplus
}
#endif
