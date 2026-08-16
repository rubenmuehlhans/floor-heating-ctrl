/*
 * Empfang der Raumthermometer ueber Bluetooth Low Energy.
 *
 * Die Geraete senden ihre Messwerte offen als Rundruf; das Geraet hoert nur
 * mit (Observer, keine Verbindung, kein eigenes Senden). Unterstuetzt werden
 * beide gaengigen Formate der ATC-Firmware fuer die Xiaomi-Thermometer:
 *
 *   - atc1441: 13 Byte Dienstdaten unter UUID 0x181A, Werte in Big Endian
 *   - pvvx:    15 Byte Dienstdaten unter UUID 0x181A, Werte in Little Endian
 *
 * Damit ist die Regelung unabhaengig von Home Assistant; im ESPHome-Aufbau kam
 * die Temperatur ueber die Home-Assistant-API herein.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATC_MAX_DEVICES 24

typedef struct {
    uint8_t mac[6];
    /* Name aus dem Rundruf. Die pvvx-Firmware laesst ihn frei setzen, die
     * atc1441-Fassung sendet ATC_<letzte drei MAC-Bytes>. Leer, wenn das
     * Geraet keinen Namen mitsendet. */
    char name[24];
    int8_t rssi;
    float temp_c;
    float humidity;
    uint8_t battery;     /* Prozent */
    uint16_t battery_mv;
    uint32_t last_seen_ms;
    uint32_t packets;
    bool pvvx; /* true = erweitertes Format */
} atc_device_t;

typedef void (*atc_cb_t)(const atc_device_t *dev, void *ctx);

esp_err_t atc_ble_start(atc_cb_t cb, void *ctx);

/* Kopiert die bekannten Geraete, neueste zuerst. */
size_t atc_ble_devices(atc_device_t *out, size_t max);

#ifdef __cplusplus
}
#endif
