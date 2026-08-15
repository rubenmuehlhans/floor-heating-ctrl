#include "valve.h"

#include <math.h>
#include <string.h>

#define POS_EPS 0.002f

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static void finish(valve_t *v, uint32_t now_ms, int reason)
{
    if (v->op != VALVE_IDLE) {
        v->last_move_ms = now_ms - v->move_start_ms;
    }
    v->op = VALVE_IDLE;
    v->last_stop_reason = reason;
}

static void start_move(valve_t *v, float target, uint32_t now_ms)
{
    v->target = clamp01(target);
    v->op = (v->target > v->position) ? VALVE_OPENING : VALVE_CLOSING;
    v->move_start_ms = now_ms;
    v->last_ms = now_ms;
}

void valve_init(valve_t *v, const valve_cfg_t *cfg)
{
    memset(v, 0, sizeof(*v));
    v->cfg = *cfg;
    if (v->cfg.blank_ms == 0) {
        v->cfg.blank_ms = VALVE_DEFAULT_BLANK_MS;
    }
    v->position = 0.0f;
    v->position_known = false;
    v->target = 0.0f;
}

void valve_set_cfg(valve_t *v, const valve_cfg_t *cfg)
{
    v->cfg = *cfg;
    if (v->cfg.blank_ms == 0) {
        v->cfg.blank_ms = VALVE_DEFAULT_BLANK_MS;
    }
}

void valve_restore(valve_t *v, float position)
{
    v->position = clamp01(position);
    v->position_known = true;
}

bool valve_goto(valve_t *v, float target, float min_delta, uint32_t now_ms)
{
    target = clamp01(target);

    if (!v->position_known) {
        /* Ohne bekannte Position hilft nur eine Referenzfahrt gegen die untere
         * Endlage. Das eigentliche Ziel wird danach angefahren. */
        v->pending_target = target;
        v->pending_valid = true;
        v->referencing = true;
        v->position = 1.0f; /* schlimmster Fall: Ventil steht ganz offen */
        start_move(v, 0.0f, now_ms);
        return true;
    }

    if (v->op != VALVE_IDLE && fabsf(v->target - target) <= POS_EPS) {
        return false; /* faehrt bereits dorthin */
    }

    if (v->op == VALVE_IDLE && fabsf(v->position - target) <= min_delta) {
        return false; /* Abweichung zu klein, kein Fahrbefehl */
    }

    v->referencing = false;
    v->pending_valid = false;
    start_move(v, target, now_ms);
    return true;
}

void valve_open(valve_t *v, uint32_t now_ms)
{
    v->referencing = false;
    v->pending_valid = false;
    v->target = 1.0f;
    v->op = VALVE_OPENING;
    v->move_start_ms = now_ms;
    v->last_ms = now_ms;
}

void valve_close(valve_t *v, uint32_t now_ms)
{
    v->referencing = false;
    v->pending_valid = false;
    v->target = 0.0f;
    v->op = VALVE_CLOSING;
    v->move_start_ms = now_ms;
    v->last_ms = now_ms;
}

void valve_stop(valve_t *v, uint32_t now_ms)
{
    v->referencing = false;
    v->pending_valid = false;
    finish(v, now_ms, VALVE_STOP_COMMAND);
}

/* Uebernimmt nach einer Referenzfahrt das gemerkte Ziel. */
static bool resume_pending(valve_t *v, uint32_t now_ms)
{
    if (!v->pending_valid) {
        return false;
    }
    float target = v->pending_target;
    v->pending_valid = false;
    v->referencing = false;
    if (fabsf(v->position - target) <= POS_EPS) {
        return false;
    }
    start_move(v, target, now_ms);
    return true;
}

bool valve_tick(valve_t *v, uint32_t now_ms)
{
    if (v->op == VALVE_IDLE) {
        return false;
    }

    uint32_t dt = now_ms - v->last_ms;
    v->last_ms = now_ms;

    uint32_t travel_ms = (v->op == VALVE_OPENING) ? v->cfg.open_ms : v->cfg.close_ms;
    if (travel_ms == 0) {
        travel_ms = 1;
    }
    float step = (float)dt / (float)travel_ms;

    if (v->op == VALVE_OPENING) {
        v->position = clamp01(v->position + step);
        if (v->position >= v->target - POS_EPS) {
            v->position = v->target;
            finish(v, now_ms, VALVE_STOP_TARGET);
            return true;
        }
    } else {
        v->position = clamp01(v->position - step);
        if (v->position <= v->target + POS_EPS) {
            v->position = v->target;
            finish(v, now_ms, VALVE_STOP_TARGET);
            bool resumed = resume_pending(v, now_ms);
            if (v->referencing || resumed) {
                v->position_known = true;
            }
            return true;
        }
    }

    if (v->cfg.max_ms > 0 && (now_ms - v->move_start_ms) >= v->cfg.max_ms) {
        /* Endlage ausgeblieben. Bei einer Fahrt auf einen Anschlag steht das
         * Ventil mechanisch trotzdem dort - die Position wird entsprechend
         * gesetzt, damit die Schaetzung nicht wegdriftet. */
        if (v->target <= POS_EPS) {
            v->position = 0.0f;
            v->position_known = true;
        } else if (v->target >= 1.0f - POS_EPS) {
            v->position = 1.0f;
            v->position_known = true;
        }
        bool was_referencing = v->referencing;
        finish(v, now_ms, VALVE_STOP_TIMEOUT);
        if (was_referencing) {
            resume_pending(v, now_ms);
        }
        return true;
    }

    return false;
}

bool valve_endstop(valve_t *v, uint32_t now_ms)
{
    if (v->op == VALVE_IDLE) {
        return false;
    }
    if ((now_ms - v->move_start_ms) < v->cfg.blank_ms) {
        return false; /* Anlaufstrom, keine Endlage */
    }

    if (v->op == VALVE_OPENING) {
        v->position = 1.0f;
    } else {
        v->position = 0.0f;
    }
    v->position_known = true;

    bool was_referencing = v->referencing;
    finish(v, now_ms, VALVE_STOP_ENDSTOP);
    if (was_referencing) {
        v->referencing = false;
        resume_pending(v, now_ms);
    }
    return true;
}

hw_drive_t valve_drive(const valve_t *v)
{
    switch (v->op) {
    case VALVE_OPENING:
        return HW_DRIVE_OPEN;
    case VALVE_CLOSING:
        return HW_DRIVE_CLOSE;
    default:
        return HW_DRIVE_OFF;
    }
}
