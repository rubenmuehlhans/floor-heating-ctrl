/*
 * Sensoren auf der Platine: DS18B20-Vorlauffuehler am 1-Wire-Bus und der
 * HDC1080 fuer Temperatur und Luftfeuchte im Schaltschrank.
 *
 * Die Fuehler werden zyklisch abgefragt; der Wert 85 Grad ist der
 * Ruecksetzwert des DS18B20 und wird verworfen.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSORS_MAX_DS18B20 8

typedef struct {
    uint64_t address;
    float temp_c;
    bool valid;
} ds18b20_reading_t;

typedef struct {
    ds18b20_reading_t ds[SENSORS_MAX_DS18B20];
    uint8_t ds_count;

    bool hdc_valid;
    float hdc_temp_c;
    float hdc_humidity;
} sensors_snapshot_t;

esp_err_t sensors_start(void);
void sensors_get(sensors_snapshot_t *out);

#ifdef __cplusplus
}
#endif
