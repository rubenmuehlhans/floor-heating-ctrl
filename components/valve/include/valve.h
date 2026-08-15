/*
 * Zustandsmaschine eines Ventilkanals.
 *
 * Bewusst frei von IDF-Abhaengigkeiten: die Zeit kommt als Parameter herein,
 * die Ansteuerung geht als Wunschzustand heraus. Dadurch laesst sich das
 * Fahrverhalten auf dem Rechner testen.
 *
 * Position 0.0 = geschlossen, 1.0 = geoeffnet - wie im ESPHome-Aufbau.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hw_map.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VALVE_IDLE = 0,
    VALVE_OPENING,
    VALVE_CLOSING,
} valve_op_t;

typedef struct {
    uint32_t open_ms;  /* Fahrzeit von zu nach auf */
    uint32_t close_ms; /* Fahrzeit von auf nach zu */
    uint32_t max_ms;   /* Notabschaltung, wenn die Endlage ausbleibt */
    uint32_t blank_ms; /* Sperrzeit nach dem Anlauf, in der die Endlagenmeldung
                        * ignoriert wird - der Einschaltstrom sieht sonst wie
                        * ein Blockieren aus */
} valve_cfg_t;

typedef struct {
    valve_cfg_t cfg;

    valve_op_t op;
    float position;      /* 0..1 */
    bool position_known; /* false bis zur ersten Referenzfahrt */

    float target;         /* Zielposition der laufenden Fahrt */
    float pending_target; /* Ziel nach Abschluss der Referenzfahrt */
    bool referencing;     /* Referenzfahrt gegen die untere Endlage laeuft */
    bool pending_valid;

    uint32_t move_start_ms;
    uint32_t last_ms;

    /* Diagnose: Grund der letzten Beendigung */
    enum {
        VALVE_STOP_NONE = 0,
        VALVE_STOP_TARGET,
        VALVE_STOP_ENDSTOP,
        VALVE_STOP_TIMEOUT,
        VALVE_STOP_COMMAND,
    } last_stop_reason;
    uint32_t last_move_ms; /* Dauer der letzten Fahrt */
} valve_t;

#define VALVE_DEFAULT_BLANK_MS 2000

void valve_init(valve_t *v, const valve_cfg_t *cfg);
void valve_set_cfg(valve_t *v, const valve_cfg_t *cfg);

/* Setzt eine bekannte Position, etwa beim Start aus dem NVS. */
void valve_restore(valve_t *v, float position);

/*
 * Faehrt auf die Zielposition. min_delta ist die Mindestabweichung, ab der
 * ueberhaupt gefahren wird. Liefert true, wenn eine Fahrt beginnt.
 *
 * Ist die Position unbekannt, laeuft zuerst eine Referenzfahrt gegen die
 * untere Endlage; das eigentliche Ziel wird danach automatisch angefahren.
 */
bool valve_goto(valve_t *v, float target, float min_delta, uint32_t now_ms);

void valve_open(valve_t *v, uint32_t now_ms);
void valve_close(valve_t *v, uint32_t now_ms);
void valve_stop(valve_t *v, uint32_t now_ms);

/* Rechnet die Position fort und beendet die Fahrt bei Ziel oder Zeitablauf.
 * Liefert true, wenn sich der Fahrzustand geaendert hat. */
bool valve_tick(valve_t *v, uint32_t now_ms);

/* Meldung der Endlagenerkennung. Liefert true, wenn sie ausgewertet wurde. */
bool valve_endstop(valve_t *v, uint32_t now_ms);

/* Gewuenschter Zustand der Ausgangsstufe. */
hw_drive_t valve_drive(const valve_t *v);

static inline bool valve_is_moving(const valve_t *v)
{
    return v->op != VALVE_IDLE;
}

#ifdef __cplusplus
}
#endif
