#include "plausi.h"

#include <string.h>

void plausi_defaults(plausi_cfg_t *cfg)
{
    cfg->hold_s = 1800;
    cfg->margin_k = 1.0f;
    cfg->min_buffer_c = 35.0f;
    cfg->max_error_ratio = 0.05f;
}

void plausi_init(plausi_finding_t *f)
{
    memset(f, 0, sizeof(*f));
}

/*
 * Gemeinsamer Teil beider Pruefungen: Eine Bedingung, die anliegt, laesst die
 * Uhr laufen; eine, die abfaellt, setzt sie zurueck. Gemeldet wird erst nach
 * der Haltezeit.
 *
 * urteilbar trennt "die Bedingung liegt nicht an" von "es laesst sich gerade
 * nicht beurteilen". Im zweiten Fall bleibt der Zaehler stehen, statt von vorn
 * zu beginnen -- sonst kaeme eine Meldung nie zustande, wenn die Pumpe alle
 * zwanzig Minuten fuer fuenf laeuft.
 */
static void schritt(plausi_finding_t *f, const plausi_cfg_t *cfg, bool urteilbar,
                    bool verletzt, uint32_t now_ms)
{
    if (!urteilbar) {
        return;
    }
    if (!verletzt) {
        f->since_ms = 0;
        f->held_s = 0;
        f->active = false;
        return;
    }
    if (f->since_ms == 0) {
        f->since_ms = now_ms;
    }
    f->held_s = (now_ms - f->since_ms) / 1000;
    if (f->held_s >= cfg->hold_s) {
        f->active = true;
    }
}

void plausi_flow_tick(plausi_finding_t *f, const plausi_cfg_t *cfg, bool pump_on,
                      bool buffer_valid, float buffer_c, bool vl_valid, float vl_c,
                      bool rl_valid, float rl_c, uint32_t now_ms)
{
    bool urteilbar = pump_on && vl_valid && rl_valid && buffer_valid &&
                     buffer_c >= cfg->min_buffer_c;
    schritt(f, cfg, urteilbar, urteilbar && vl_c < rl_c - cfg->margin_k, now_ms);
}

void plausi_buffer_tick(plausi_finding_t *f, const plausi_cfg_t *cfg, bool loading,
                        bool buffer_valid, float buffer_c, bool vl_valid, float vl_c,
                        uint32_t now_ms)
{
    bool urteilbar = loading && buffer_valid && vl_valid;
    schritt(f, cfg, urteilbar, urteilbar && buffer_c > vl_c + cfg->margin_k, now_ms);
}

bool plausi_probe_bad(const plausi_cfg_t *cfg, uint32_t reads, uint32_t errors)
{
    /* Unter hundert Messungen ist der Anteil noch nicht aussagekraeftig. */
    uint32_t gesamt = reads + errors;
    if (gesamt < 100) {
        return false;
    }
    return (float)errors / (float)gesamt > cfg->max_error_ratio;
}

const char *plausi_text(plausi_code_t c)
{
    switch (c) {
    case PLAUSI_FLOW_SWAPPED:
        return "Vorlauf und Ruecklauf sind vermutlich vertauscht";
    case PLAUSI_BUFFER_ABOVE_BOILER:
        return "Der Speicher meldet mehr als der Kesselvorlauf";
    case PLAUSI_PROBE_ERRORS:
        return "Ein Fuehler verwirft auffaellig viele Messungen";
    default:
        return "";
    }
}
