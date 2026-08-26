#include "zapfung.h"

#include <string.h>

void zapf_defaults(zapf_cfg_t *cfg)
{
    cfg->drop_k = 2.0f;
    cfg->win_s = 900;
}

void zapf_init(zapf_state_t *st)
{
    memset(st, 0, sizeof(*st));
}

void zapf_new_day(zapf_state_t *st)
{
    st->count = 0;
    st->sum_k = 0.0f;
}

static void abschliessen(zapf_state_t *st)
{
    if (!st->active) {
        return;
    }
    float hoehe = st->start_c - st->tief_c;
    st->active = false;
    st->last_k = hoehe;
    st->sum_k += hoehe;
    st->count++;
}

void zapf_tick(zapf_state_t *st, const zapf_cfg_t *cfg, bool burner_running,
               bool puffer_valid, float puffer_c, uint32_t now_ms)
{
    if (!puffer_valid || burner_running) {
        /*
         * Waehrend einer Ladung steigt der Speicher. Eine laufende Zapfung
         * wird abgeschlossen und gezaehlt, danach beginnt der Bezug von vorn.
         */
        abschliessen(st);
        st->have = false;
        return;
    }

    if (!st->have) {
        st->have = true;
        st->ref_c = puffer_c;
        st->ref_ms = now_ms;
        return;
    }

    /*
     * Wandernder Bezug: Steigt der Speicher oder ist das Fenster abgelaufen,
     * wird er nachgezogen. So misst der Vergleich immer den Einbruch der
     * letzten win_s und nicht den Abstand zu irgendeinem alten Hoechstwert.
     */
    if (puffer_c > st->ref_c || (now_ms - st->ref_ms) >= cfg->win_s * 1000UL) {
        if (!st->active) {
            st->ref_c = puffer_c;
            st->ref_ms = now_ms;
        }
    }

    float einbruch = st->ref_c - puffer_c;

    if (!st->active) {
        if (einbruch >= cfg->drop_k) {
            st->active = true;
            st->start_c = st->ref_c;
            st->tief_c = puffer_c;
            st->begin_ms = now_ms;
        }
        return;
    }

    if (puffer_c < st->tief_c) {
        st->tief_c = puffer_c;
    }
    /*
     * Vorbei ist sie, wenn der Speicher wieder steigt oder sich beruhigt --
     * gemessen daran, dass er den Tiefstwert um ein Zehntel Kelvin ueberholt
     * oder das Fenster ohne neuen Tiefstwert verstreicht.
     */
    if (puffer_c > st->tief_c + 0.1f || (now_ms - st->begin_ms) >= cfg->win_s * 2000UL) {
        abschliessen(st);
        st->ref_c = puffer_c;
        st->ref_ms = now_ms;
    }
}

bool zapf_kwh(float kelvin, float volumen_l, float *out_kwh)
{
    if (volumen_l <= 0.0f) {
        return false;
    }
    /* 1,163 Wattstunden je Kilogramm und Kelvin. */
    *out_kwh = kelvin * volumen_l * 1.163f / 1000.0f;
    return true;
}
