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

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATC_MAX_DEVICES 24

typedef enum {
    ATC_FMT_ATC1441 = 0,
    ATC_FMT_PVVX,
    ATC_FMT_RUUVI,
} atc_format_t;

typedef struct {
    uint8_t mac[6];
    /* Name aus dem Rundruf. Die pvvx-Firmware laesst ihn frei setzen, die
     * atc1441-Fassung sendet ATC_<letzte drei MAC-Bytes>. Leer, wenn das
     * Geraet keinen Namen mitsendet. */
    char name[24];
    int8_t rssi;
    float temp_c;
    float humidity;
    uint8_t battery;     /* Prozent; der RuuviTag meldet keine, dort 0 */
    uint16_t battery_mv;
    /* Luftdruck in Hektopascal, 0 wenn das Format keinen liefert. Nur der
     * RuuviTag misst ihn. */
    float pressure_hpa;
    uint32_t last_seen_ms;
    uint32_t packets;
    atc_format_t format;
} atc_device_t;

/* Kurzname des Formats, fuer Oberflaeche und Protokoll. */
const char *atc_format_name(atc_format_t f);

typedef void (*atc_cb_t)(const atc_device_t *dev, void *ctx);

esp_err_t atc_ble_start(atc_cb_t cb, void *ctx);

/* Kopiert die bekannten Geraete, neueste zuerst. */
size_t atc_ble_devices(atc_device_t *out, size_t max);

#ifdef __cplusplus
}
#endif
