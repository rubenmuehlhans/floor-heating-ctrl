#include "heatlogic.h"

#include <string.h>

#define MS_PRO_TAG 86400000UL

void pump_defaults(pump_cfg_t *cfg)
{
    cfg->overrun_s = 300;
    cfg->min_run_s = 180;
    cfg->min_pause_s = 180;
    cfg->min_buffer_c = 40.0f;
    cfg->frost_c = 6.0f;
    cfg->seize_days = 7;
    cfg->seize_run_s = 180;
}

void pump_init(pump_state_t *st, pump_mode_t mode)
{
    memset(st, 0, sizeof(*st));
    st->mode = mode;
    st->reason = PUMP_REASON_NONE;
}

void pump_set_mode(pump_state_t *st, pump_mode_t mode, uint32_t now_ms)
{
    if (st->mode == mode) {
        return;
    }
    st->mode = mode;
    /* Der Wechsel der Betriebsart hebt Mindestlaufzeit und Mindestpause auf:
     * wer von Hand eingreift, will nicht warten. */
    st->switched = false;
    st->since_ms = now_ms;
}

static void schalten(pump_state_t *st, bool ein, pump_reason_t grund, uint32_t now_ms)
{
    if (st->on != ein) {
        st->on = ein;
        st->since_ms = now_ms;
        st->switched = true;
    }
    st->reason = grund;
    if (ein) {
        st->last_run_ms = now_ms;
    }
}

static uint32_t verstrichen(uint32_t now_ms, uint32_t seit)
{
    return now_ms - seit; /* Ueberlauf nach 49 Tagen ist durch die Subtraktion abgedeckt */
}

/* Ueberlaufsicherer Vergleich: ist der Zeitpunkt erreicht? */
static bool erreicht(uint32_t now_ms, uint32_t zeitpunkt)
{
    return (int32_t)(now_ms - zeitpunkt) >= 0;
}

void pump_tick(pump_state_t *st, const pump_cfg_t *cfg, const pump_input_t *in, uint32_t now_ms)
{
    if (!st->started) {
        st->started = true;
        st->since_ms = now_ms;
        st->last_run_ms = now_ms; /* frisch gestartet gilt nicht als festsitzend */
    }

    if (in->demand) {
        st->last_demand_ms = now_ms;
    }
    if (st->on) {
        /* Auch waehrend einer Mindestlaufzeit gilt die Pumpe als gelaufen,
         * sonst zaehlt der Schutzlauf falsch. */
        st->last_run_ms = now_ms;
    }

    /* Handbetrieb geht vor, aber nicht vor dem Frostschutz. */
    bool frost = (in->room_valid && in->min_room_c < cfg->frost_c) ||
                 (in->flow_valid && in->flow_c < 8.0f);

    if (frost) {
        schalten(st, true, PUMP_REASON_FROST, now_ms);
        return;
    }

    if (st->mode == PUMP_MODE_ON) {
        schalten(st, true, PUMP_REASON_MANUAL, now_ms);
        return;
    }
    if (st->mode == PUMP_MODE_OFF) {
        schalten(st, false, PUMP_REASON_MANUAL, now_ms);
        return;
    }

    /* Schutzlauf: steht die Pumpe zu lange, laeuft sie kurz an. Er laeuft
     * unabhaengig von Bedarf und Speichertemperatur -- er dient dem Lager,
     * nicht der Waerme. */
    if (st->seize_until_ms != 0) {
        if (!erreicht(now_ms, st->seize_until_ms)) {
            schalten(st, true, PUMP_REASON_SEIZE, now_ms);
            return;
        }
        st->seize_until_ms = 0;
    }
    if (!st->on && cfg->seize_days > 0 &&
        verstrichen(now_ms, st->last_run_ms) >= cfg->seize_days * MS_PRO_TAG) {
        st->seize_until_ms = now_ms + cfg->seize_run_s * 1000UL;
        schalten(st, true, PUMP_REASON_SEIZE, now_ms);
        return;
    }

    /* Ohne ausreichend warmen Speicher bringt das Umwaelzen nichts. Der Wert
     * liegt bewusst ueber der noetigen Vorlauftemperatur: der Mischer kann nur
     * herunterregeln, nicht anheben. */
    bool speicher_kalt = in->buffer_valid && in->buffer_c < cfg->min_buffer_c;

    bool soll_laufen;
    pump_reason_t grund;

    if (in->demand && !speicher_kalt) {
        soll_laufen = true;
        grund = PUMP_REASON_DEMAND;
    } else if (speicher_kalt) {
        soll_laufen = false;
        grund = PUMP_REASON_BUFFER_COLD;
    } else if (st->last_demand_ms != 0 &&
               verstrichen(now_ms, st->last_demand_ms) < cfg->overrun_s * 1000UL) {
        soll_laufen = true;
        grund = PUMP_REASON_OVERRUN;
    } else {
        soll_laufen = false;
        grund = PUMP_REASON_NO_DEMAND;
    }

    /* Mindestzeiten. Sie halten den bestehenden Zustand, aendern aber nichts
     * am Grund, der ohne sie gelten wuerde. */
    if (soll_laufen != st->on && st->switched) {
        uint32_t im_zustand = verstrichen(now_ms, st->since_ms);
        if (st->on && im_zustand < cfg->min_run_s * 1000UL) {
            st->reason = PUMP_REASON_MIN_RUN;
            return;
        }
        if (!st->on && im_zustand < cfg->min_pause_s * 1000UL) {
            st->reason = PUMP_REASON_MIN_PAUSE;
            return;
        }
    }

    schalten(st, soll_laufen, grund, now_ms);
}

const char *pump_reason_text(pump_reason_t r)
{
    switch (r) {
    case PUMP_REASON_DEMAND:
        return "Abnehmer vorhanden";
    case PUMP_REASON_OVERRUN:
        return "Nachlauf";
    case PUMP_REASON_MIN_RUN:
        return "Mindestlaufzeit";
    case PUMP_REASON_MIN_PAUSE:
        return "Mindestpause";
    case PUMP_REASON_FROST:
        return "Frostschutz";
    case PUMP_REASON_SEIZE:
        return "Schutzlauf";
    case PUMP_REASON_NO_DEMAND:
        return "kein Abnehmer";
    case PUMP_REASON_BUFFER_COLD:
        return "Speicher zu kalt";
    case PUMP_REASON_MANUAL:
        return "Handbetrieb";
    default:
        return "";
    }
}

void demand_evaluate(const demand_source_t *src, uint32_t count, uint32_t timeout_s,
                     demand_result_t *out)
{
    memset(out, 0, sizeof(*out));

    for (uint32_t i = 0; i < count; i++) {
        const demand_source_t *s = &src[i];
        if (!s->seen) {
            continue; /* hat seit dem Start nie geantwortet */
        }
        out->any_seen = true;

        if (s->age_s > timeout_s) {
            /* Frueher erreichbar, jetzt verstummt: im Zweifel heizen. */
            out->stale = true;
            out->demand = true;
            continue;
        }
        if (s->demand) {
            out->demand = true;
        }
        if (s->room_valid) {
            if (!out->room_valid || s->min_room_c < out->min_room_c) {
                out->min_room_c = s->min_room_c;
                out->room_valid = true;
            }
        }
    }
}
