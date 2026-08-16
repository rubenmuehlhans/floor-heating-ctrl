/*
 * Brennererkennung, Tagesstatistik und Aufzeichnung einer Ladung.
 *
 * Die Erkennung selbst liegt in components/heatlogic und ist dort ohne
 * Hardware geprueft. Hier steht, was ohne ESP-IDF nicht geht: den Abgaswert
 * holen, die Tageswerte ueber einen Neustart retten und die hochaufgeloeste
 * Aufzeichnung fuehren.
 *
 * Die Aufzeichnung dient dazu, die Schwellwerte fuer den Ladezustand aus
 * gemessenen Kurven abzuleiten statt sie zu schaetzen -- dasselbe Vorgehen wie
 * bei der Messfahrt der Verteilerplatine.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"
#include "heatlogic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 5-Sekunden-Raster ueber gut zwei Stunden. */
#define REC_SLOTS 1600

typedef struct {
    bool known;
    bool running;
    float abgas_c;
    float baseline_c;
    uint32_t since_s;          /* seit dem letzten Wechsel */
    uint32_t runtime_today_s;
    uint32_t starts_today;
    uint32_t runtime_yesterday_s;
    uint32_t starts_yesterday;
    float litres_today;
    bool short_cycling;        /* viele Starts bei geringer Laufzeit */
} burner_status_t;

typedef enum {
    REC_IDLE = 0,
    REC_RUNNING,
    REC_DONE,
} rec_state_t;

typedef struct {
    rec_state_t state;
    uint16_t samples;
    uint16_t period_s;
    uint32_t started_epoch;
    /* Nur die beim Start belegten Messstellen werden aufgezeichnet. Das spart
     * Arbeitsspeicher und laesst die CSV-Datei ohne leere Spalten. */
    uint8_t cols;
    probe_role_t role[ROLE_COUNT];
    uint32_t bytes;   /* belegter Arbeitsspeicher */
} rec_status_t;

esp_err_t burner_start(void);
void burner_config_changed(void);
void burner_get(burner_status_t *out);

/* Aufzeichnung einer Ladung. */
esp_err_t rec_start(void);
void rec_stop(void);

/* Verwirft die Aufzeichnung und gibt ihren Speicher zurueck. */
void rec_discard(void);
void rec_get_status(rec_status_t *out);

/*
 * Eine Zeile der Aufzeichnung. index 0 ist der aelteste Eintrag, damit sich
 * die Ausgabe in Leserichtung schreiben laesst. Werte in Zehntelgrad,
 * SENS_HIST_NONE fuer "kein Messwert".
 */
bool rec_row(size_t index, int16_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
