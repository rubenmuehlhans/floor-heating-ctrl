#include "flue.h"

#include <string.h>

void flue_init(flue_acc_t *a)
{
    memset(a, 0, sizeof(*a));
}

void flue_add(flue_acc_t *a, bool nach_wartung, float abstand_k)
{
    if (nach_wartung && a->ref_n < FLUE_WINDOW) {
        a->ref[a->ref_n++] = abstand_k;
    }
    a->now[a->now_pos] = abstand_k;
    a->now_pos = (a->now_pos + 1) % FLUE_WINDOW;
    if (a->now_n < FLUE_WINDOW) {
        a->now_n++;
    }
}

/*
 * Median ueber eine Kopie. Einfuegesortierung: bei fuenfzig Werten ist sie
 * schnell genug, und sie braucht weder Vergleichsfunktion noch Bibliothek.
 */
static bool median(const float *werte, uint32_t n, float *out)
{
    if (n < FLUE_MIN_CHARGES) {
        return false;
    }
    float s[FLUE_WINDOW];
    for (uint32_t i = 0; i < n; i++) {
        float v = werte[i];
        uint32_t j = i;
        while (j > 0 && s[j - 1] > v) {
            s[j] = s[j - 1];
            j--;
        }
        s[j] = v;
    }
    *out = (n % 2) ? s[n / 2] : 0.5f * (s[n / 2 - 1] + s[n / 2]);
    return true;
}

void flue_eval(const flue_acc_t *a, flue_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ref_n = a->ref_n;
    out->now_n = a->now_n;

    out->ref_valid = median(a->ref, a->ref_n, &out->ref_k);
    /*
     * Der Ring wird unsortiert uebergeben. Fuer einen Median spielt die
     * Reihenfolge keine Rolle, also muss der Umbruch nicht aufgeloest werden.
     */
    out->now_valid = median(a->now, a->now_n, &out->now_k);

    if (out->ref_valid && out->now_valid) {
        out->delta_valid = true;
        out->delta_k = out->now_k - out->ref_k;
    }
}

bool flue_alert(const flue_result_t *r)
{
    /* Nur nach oben: ein kleinerer Abstand als nach der Reinigung ist kein
     * Mangel, sondern hoechstens ein Zeichen milderer Ladungen. */
    return r->delta_valid && r->delta_k > FLUE_ALERT_K;
}
