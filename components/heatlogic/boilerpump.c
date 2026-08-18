#include "boilerpump.h"

#include <string.h>

void bp_defaults(bp_cfg_t *cfg)
{
    cfg->on_k = 1.0f;
    cfg->off_k = 0.5f;
    cfg->hold_s = 120;
    cfg->min_run_s = 180;
    cfg->min_pause_s = 180;
    cfg->emergency_c = 85.0f;
    cfg->enabled = false; /* nur wo eine Kesselkreispumpe haengt */
}

void bp_init(bp_state_t *st, bp_mode_t mode)
{
    memset(st, 0, sizeof(*st));
    st->mode = mode;
}

static void schalten(bp_state_t *st, bool on, bp_reason_t grund, uint32_t now_ms)
{
    if (st->on != on || !st->started) {
        if (st->started) {
            st->switched = true;
        }
        st->on = on;
        st->since_ms = now_ms;
    }
    st->reason = grund;
    st->started = true;
}

void bp_set_mode(bp_state_t *st, bp_mode_t mode, uint32_t now_ms)
{
    if (st->mode == mode) {
        return;
    }
    st->mode = mode;
    /*
     * Die Mindestzeiten zaehlen ab dem letzten Schaltvorgang, nicht ab dem
     * Wechsel der Betriebsart -- sonst muesste man nach jedem Umschalten
     * warten, ohne dass die Pumpe etwas getan haette.
     */
    (void)now_ms;
}

static uint32_t verstrichen(uint32_t now_ms, uint32_t seit_ms)
{
    return now_ms - seit_ms;
}

void bp_tick(bp_state_t *st, const bp_cfg_t *cfg, const bp_input_t *in, uint32_t now_ms)
{
    if (!cfg->enabled) {
        schalten(st, false, BP_REASON_DISABLED, now_ms);
        return;
    }
    if (st->mode == BP_MODE_ON) {
        schalten(st, true, BP_REASON_MANUAL, now_ms);
        return;
    }
    if (st->mode == BP_MODE_OFF) {
        schalten(st, false, BP_REASON_MANUAL, now_ms);
        return;
    }

    /*
     * Notabfuhr hat Vorrang vor allem, auch vor den Mindestzeiten. Ein Kessel
     * ueber der Notgrenze muss seine Waerme loswerden.
     */
    if (in->valid && in->vl_c >= cfg->emergency_c) {
        schalten(st, true, BP_REASON_EMERGENCY, now_ms);
        return;
    }

    /* Ohne Messwerte laeuft sie. Nicht zu foerdern waere hier das groessere
     * Risiko als zu viel zu foerdern. */
    if (!in->valid) {
        schalten(st, true, BP_REASON_NO_READING, now_ms);
        return;
    }

    /*
     * Zweipunktverhalten mit Haltezeit: Der Kessel gilt als abgebend, wenn der
     * Vorlauf den Bezug um on_k uebersteigt, und als aufnehmend, wenn er
     * darunter faellt. Dazwischen bleibt es, wie es war -- sonst schaltete die
     * Pumpe im Minutentakt, wenn die Spreizung um null pendelt.
     *
     * Bezug ist die Speichertemperatur, ersatzweise der Ruecklauf. Bei
     * stehender Pumpe fliesst nichts: Vor- und Ruecklauf gleichen sich der
     * Kesseltemperatur an, ihre Differenz geht gegen null, und ein Kessel mit
     * Restwaerme bliebe stehen, obwohl der Speicher kaelter ist.
     */
    float bezug = in->buffer_valid ? in->buffer_c : in->rl_c;
    float spreizung = in->vl_c - bezug;
    bool transfer = st->on ? spreizung > cfg->off_k : spreizung >= cfg->on_k;

    if (transfer != st->cond_transfer || !st->started) {
        st->cond_transfer = transfer;
        st->cond_since_ms = now_ms;
    }
    bool gehalten = verstrichen(now_ms, st->cond_since_ms) >= cfg->hold_s * 1000UL;

    bool soll = st->started ? st->on : transfer;
    if (gehalten) {
        soll = transfer;
    }

    /* Mindestzeiten, aber erst nachdem wirklich einmal geschaltet wurde. */
    if (st->switched && soll != st->on) {
        uint32_t seit = verstrichen(now_ms, st->since_ms);
        if (st->on && seit < cfg->min_run_s * 1000UL) {
            schalten(st, true, BP_REASON_MIN_RUN, now_ms);
            return;
        }
        if (!st->on && seit < cfg->min_pause_s * 1000UL) {
            schalten(st, false, BP_REASON_MIN_PAUSE, now_ms);
            return;
        }
    }

    /*
     * Der Grund muss den tatsaechlichen Anlass nennen, nicht den Zustand.
     * Weicht die Sollstellung von der gerade anliegenden Bedingung ab, laeuft
     * die Pumpe nur noch, weil die Haltezeit nicht abgelaufen ist -- dann
     * "Kessel gibt Waerme ab" zu melden waere schlicht falsch.
     */
    bp_reason_t grund = soll ? BP_REASON_TRANSFER : BP_REASON_NO_TRANSFER;
    if (soll != transfer) {
        grund = BP_REASON_HOLD;
    }
    schalten(st, soll, grund, now_ms);
}

const char *bp_reason_text(bp_reason_t r)
{
    switch (r) {
    case BP_REASON_TRANSFER:     return "Kessel gibt Waerme ab";
    case BP_REASON_NO_TRANSFER:  return "Ruecklauf waermer als Vorlauf";
    case BP_REASON_EMERGENCY:    return "Notabfuhr, Kessel ueber der Grenze";
    case BP_REASON_NO_READING:   return "keine Kesselwerte, Pumpe laeuft sicherheitshalber";
    case BP_REASON_HOLD:         return "Haltezeit, Bedingung eben erst gewechselt";
    case BP_REASON_MIN_RUN:      return "Mindestlaufzeit";
    case BP_REASON_MIN_PAUSE:    return "Mindestpause";
    case BP_REASON_MANUAL:       return "Handbetrieb";
    case BP_REASON_DISABLED:     return "keine Kesselkreispumpe eingerichtet";
    default:                     return "";
    }
}

const char *bp_reason_key(bp_reason_t r)
{
    switch (r) {
    case BP_REASON_TRANSFER:     return "transfer";
    case BP_REASON_NO_TRANSFER:  return "no_transfer";
    case BP_REASON_EMERGENCY:    return "emergency";
    case BP_REASON_NO_READING:   return "no_reading";
    case BP_REASON_HOLD:         return "hold";
    case BP_REASON_MIN_RUN:      return "min_run";
    case BP_REASON_MIN_PAUSE:    return "min_pause";
    case BP_REASON_MANUAL:       return "manual";
    case BP_REASON_DISABLED:     return "disabled";
    default:                     return "";
    }
}
