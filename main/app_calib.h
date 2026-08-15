/*
 * Autokalibrierung der Endlagenerkennung.
 *
 * Der Kanal wird einmal ganz zu und einmal ganz auf gefahren, waehrend die
 * Spannung am Shunt der Messgruppe mitgeschrieben wird. Aus dem Verlauf ergeben
 * sich Fahrzeiten und Ausloeseschwelle.
 *
 * Die Erkennung des Blockierens arbeitet ausschliesslich relativ zum
 * gemessenen Fahrniveau - eine vorhandene Schwelle wird nicht benutzt, sonst
 * wuerde die Kalibrierung ihr eigenes Ergebnis voraussetzen.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CALIB_IDLE = 0,
    CALIB_RUNNING,
    CALIB_DONE,
    CALIB_FAILED,
} calib_state_t;

typedef enum {
    CALIB_PHASE_NONE = 0,
    CALIB_PHASE_CLOSE,
    CALIB_PHASE_PAUSE,
    CALIB_PHASE_OPEN,
} calib_phase_t;

typedef struct {
    calib_state_t state;
    calib_phase_t phase;
    uint8_t channel;
    uint8_t group;

    uint16_t sample_count;
    uint16_t sample_period_ms;

    /* Abschnitte im Messverlauf, als Index in die Messreihe */
    uint16_t close_from, close_to;
    uint16_t open_from, open_to;
    bool close_stalled;
    bool open_stalled;

    uint16_t baseline_close_mv;
    uint16_t stall_close_mv;
    uint16_t baseline_open_mv;
    uint16_t stall_open_mv;

    /* Vorschlag, erst nach Bestaetigung uebernommen */
    uint32_t suggest_close_ms;
    uint32_t suggest_open_ms;
    uint32_t suggest_max_ms;
    uint16_t suggest_bemf_mv;
    uint16_t suggest_hyst_mv;
    bool suggestion_valid;

    char message[96];
} calib_status_t;

/* Startet die Kalibrierung eines Kanals. Schlaegt fehl, wenn bereits eine
 * laeuft oder die Messgruppe belegt ist. */
esp_err_t calib_start(uint8_t channel);

/* Bricht den laufenden Vorgang ab und schaltet den Motor aus. */
void calib_abort(void);

void calib_get_status(calib_status_t *out);

/* Liefert Messwerte ab Index from, hoechstens max Stueck. */
size_t calib_get_samples(uint16_t from, uint16_t *dst, size_t max);

/* Uebernimmt den Vorschlag in die Konfiguration. */
esp_err_t calib_accept(char *err, size_t err_len);

/* Verwirft das Ergebnis und gibt den Messspeicher frei. */
void calib_discard(void);

#ifdef __cplusplus
}
#endif
