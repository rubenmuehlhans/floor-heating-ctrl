/*
 * Empfang der Raumthermometer ueber Bluetooth Low Energy.
 *
 * Die Geraete senden ihre Messwerte offen als Rundruf; das Geraet hoert nur
 * mit (Observer, keine Verbindung, kein eigenes Senden). Unterstuetzt werden
 * drei Formate:
 *
 *   - atc1441: 13 Byte Dienstdaten unter UUID 0x181A, Werte in Big Endian
 *   - pvvx:    15 Byte Dienstdaten unter UUID 0x181A, Werte in Little Endian
 *   - Ruuvi:   24 Byte Herstellerdaten der Kennung 0x0499, Datensatz 5 (RAWv2)
 *
 * Die ersten beiden sind die Xiaomi-Raumthermometer mit freier Firmware, das
 * dritte der RuuviTag als Aussenfuehler. Ruuvi sendet nicht unter 0x181A,
 * sondern als Herstellerdaten -- ein anderes Feld desselben Rundrufs.
 *
 * Damit ist die Regelung unabhaengig von Home Assistant; frueher kam
 * die Temperatur ueber die Home-Assistant-API herein.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atc_decode.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATC_MAX_DEVICES 24

typedef void (*atc_cb_t)(const atc_device_t *dev, void *ctx);

esp_err_t atc_ble_start(atc_cb_t cb, void *ctx);

/* Kopiert die bekannten Geraete, neueste zuerst. */
size_t atc_ble_devices(atc_device_t *out, size_t max);

/*
 * Suche anhalten und wieder aufnehmen.
 *
 * Bluetooth und WLAN teilen sich einen Funkteil. Solange der
 * Einrichtungs-Zugangspunkt die einzige Zugangsmoeglichkeit ist, bringt die
 * Suche nichts -- es ist noch kein Raum eingerichtet, dem ein Messwert
 * zugeordnet werden koennte -- und sie kostet genau die Funkzeit, die fuer die
 * Anmeldung am Zugangspunkt gebraucht wird.
 */
void atc_ble_pause(bool pausieren);

#ifdef __cplusplus
}
#endif
