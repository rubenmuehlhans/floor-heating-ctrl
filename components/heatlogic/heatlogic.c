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

/* ------------------------------------------------------------------ */
/* Brennererkennung                                                    */
/* ------------------------------------------------------------------ */

/* Laenge des Fensters fuer die Bezugslinie. */
#define BASELINE_WINDOW_MS (24UL * 60UL * 60UL * 1000UL)

void burner_defaults(burner_cfg_t *cfg)
{
    cfg->delta_on_k = 12.0f;
    cfg->delta_off_k = 6.0f;
    cfg->on_hold_s = 60;
    cfg->off_hold_s = 300;
    cfg->duese_l_h = 2.2f;
}

void burner_init(burner_state_t *st)
{
    memset(st, 0, sizeof(*st));
}

void burner_new_day(burner_state_t *st)
{
    st->runtime_today_s = 0;
    st->starts_today = 0;
}

float burner_litres_today(const burner_state_t *st, const burner_cfg_t *cfg)
{
    if (cfg->duese_l_h <= 0.0f) {
        return 0.0f;
    }
    return (float)st->runtime_today_s / 3600.0f * cfg->duese_l_h;
}

void burner_tick(burner_state_t *st, const burner_cfg_t *cfg, bool abgas_valid, float abgas_c,
                 uint32_t now_ms)
{
    if (!abgas_valid) {
        st->known = false;
        st->last_ms = now_ms;
        return;
    }

    if (!st->started) {
        st->started = true;
        st->min_seen_c = abgas_c;
        st->baseline_c = abgas_c;
        st->window_ms = now_ms;
        st->since_ms = now_ms;
        st->cond_since_ms = now_ms;
        st->last_ms = now_ms;
    }

    uint32_t dt = now_ms - st->last_ms;
    st->last_ms = now_ms;
    st->known = true;
    st->abgas_c = abgas_c;

    /*
     * Bezugslinie: das Minimum ueber ein wanderndes Fenster. Statt alle
     * Messwerte eines Tages vorzuhalten, wird das kleinste Vorkommen im
     * laufenden Fenster mitgefuehrt und das Fenster nach 24 Stunden mit dem
     * aktuellen Wert neu begonnen. Das ist gegenueber einem echten gleitenden
     * Minimum traege, aber die Rohrtemperatur aendert sich ueber Wochen, nicht
     * ueber Stunden.
     */
    if (abgas_c < st->min_seen_c) {
        st->min_seen_c = abgas_c;
    }
    if (now_ms - st->window_ms >= BASELINE_WINDOW_MS) {
        st->baseline_c = st->min_seen_c;
        st->min_seen_c = abgas_c;
        st->window_ms = now_ms;
    }
    /* Solange das erste Fenster laeuft, folgt die Bezugslinie dem Minimum
     * unmittelbar -- sonst haette die Erkennung einen Tag lang keinen Bezug. */
    if (st->min_seen_c < st->baseline_c) {
        st->baseline_c = st->min_seen_c;
    }

    bool bedingung = st->running ? (abgas_c < st->baseline_c + cfg->delta_off_k)
                                 : (abgas_c > st->baseline_c + cfg->delta_on_k);

    if (bedingung != st->cond) {
        st->cond = bedingung;
        st->cond_since_ms = now_ms;
    }

    if (st->running) {
        /* Der Rest wird mitgefuehrt: bei einem Takt, der nicht genau eine
         * Sekunde trifft, summierte sich der Fehler sonst ueber den Tag. */
        st->runtime_rest_ms += dt;
        st->runtime_today_s += st->runtime_rest_ms / 1000;
        st->runtime_rest_ms %= 1000;
    }

    uint32_t haltezeit = st->running ? cfg->off_hold_s : cfg->on_hold_s;
    if (bedingung && (now_ms - st->cond_since_ms) >= haltezeit * 1000UL) {
        st->running = !st->running;
        st->since_ms = now_ms;
        st->cond = false;
        st->cond_since_ms = now_ms;
        if (st->running) {
            st->starts_today++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ladezustand des Pufferspeichers                                     */
/* ------------------------------------------------------------------ */

void charge_defaults(charge_cfg_t *cfg)
{
    cfg->spread_full_k = 8.0f;
    cfg->spread_hold_s = 300;
    cfg->voll_c = 62.0f;
    cfg->leer_c = 35.0f;
    cfg->warn_c = 40.0f;
    cfg->kessel_hot_c = 60.0f;
}

void charge_init(charge_state_t *st)
{
    memset(st, 0, sizeof(*st));
}

const char *charge_phase_text(charge_phase_t p)
{
    switch (p) {
    case CHARGE_IDLE:
        return "keine Ladung";
    case CHARGE_LOADING:
        return "wird geladen";
    case CHARGE_FULL:
        return "geladen";
    default:
        return "unbekannt";
    }
}

static void charge_set(charge_state_t *st, charge_phase_t p, uint32_t now_ms)
{
    if (st->phase != p) {
        st->phase = p;
        st->since_ms = now_ms;
    }
}

void charge_tick(charge_state_t *st, const charge_cfg_t *cfg, const charge_input_t *in,
                 uint32_t now_ms)
{
    if (!st->started) {
        st->started = true;
        st->since_ms = now_ms;
        st->cond_since_ms = now_ms;
    }

    /* Schaetzung aus dem Pufferfuehler. */
    st->level_valid = in->puffer_valid;
    if (in->puffer_valid) {
        float spanne = cfg->voll_c - cfg->leer_c;
        float v = spanne > 0.1f ? (in->puffer_c - cfg->leer_c) / spanne : 0.0f;
        st->level = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        st->warn_dhw = in->puffer_c < cfg->warn_c;
    } else {
        st->level = 0.0f;
        st->warn_dhw = false;
    }

    st->spread_valid = in->kessel_valid;
    st->spread_k = in->kessel_valid ? in->kessel_vl_c - in->kessel_rl_c : 0.0f;

    /* Ohne Kesselwerte oder ohne Brennerzustand bleibt nur die Schaetzung. */
    st->limited = !in->kessel_valid || !in->burner_known;

    if (st->limited) {
        /* Die Phase laesst sich dann nicht bestimmen; der Fuellstand steht
         * trotzdem. Ein einmal erreichtes "geladen" bleibt stehen, bis der
         * Speicher merklich abkuehlt -- sonst spraenge die Anzeige, sobald das
         * andere Geraet kurz nicht antwortet. */
        if (st->phase == CHARGE_FULL && st->level_valid && st->level < 0.85f) {
            charge_set(st, CHARGE_IDLE, now_ms);
        } else if (st->phase == CHARGE_UNKNOWN) {
            charge_set(st, CHARGE_UNKNOWN, now_ms);
        }
        st->last_burner_running = in->burner_running;
        return;
    }

    st->had_burner = true;

    if (in->burner_running) {
        /*
         * Der Ruecklauf naehert sich dem Vorlauf: der Speicher nimmt keine
         * Waerme mehr auf. Die Haltezeit trennt das vom kurzen Angleichen
         * beim Anfahren, wenn der ganze Kessel noch kalt ist.
         */
        bool eng = st->spread_k < cfg->spread_full_k;
        if (eng != st->cond) {
            st->cond = eng;
            st->cond_since_ms = now_ms;
        }
        if (eng && (now_ms - st->cond_since_ms) >= cfg->spread_hold_s * 1000UL) {
            charge_set(st, CHARGE_FULL, now_ms);
        } else if (st->phase != CHARGE_FULL) {
            charge_set(st, CHARGE_LOADING, now_ms);
        }
    } else {
        /*
         * Schaltet der Brenner bei hohem Vorlauf von selbst ab, hat die
         * Regelung des Kessels die Ladung fuer beendet erklaert.
         */
        if (st->last_burner_running && in->kessel_vl_c > cfg->kessel_hot_c) {
            charge_set(st, CHARGE_FULL, now_ms);
        } else if (st->phase != CHARGE_FULL) {
            charge_set(st, CHARGE_IDLE, now_ms);
        }
        /* "Geladen" haelt, bis der Speicher merklich abgibt. */
        if (st->phase == CHARGE_FULL && st->level_valid && st->level < 0.85f) {
            charge_set(st, CHARGE_IDLE, now_ms);
        }
        st->cond = false;
        st->cond_since_ms = now_ms;
    }

    st->last_burner_running = in->burner_running;
}
