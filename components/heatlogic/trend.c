#include "trend.h"

#include <math.h>
#include <string.h>

void trend_init(trend_t *t)
{
    memset(t, 0, sizeof(*t));
}

void trend_add(trend_t *t, float gradtage, float stunden)
{
    double x = gradtage;
    double y = stunden;
    t->n++;
    t->sx += x;
    t->sy += y;
    t->sxx += x * x;
    t->sxy += x * y;
    t->syy += y * y;
}

bool trend_fit(const trend_t *t, trend_fit_t *out)
{
    memset(out, 0, sizeof(*out));
    out->n = t->n;

    if (t->n < TREND_MIN_DAYS) {
        return false;
    }

    double n = (double)t->n;
    /*
     * Streuungsquadrate um den Mittelwert. In dieser Form gerechnet, nicht als
     * Differenz zweier grosser Summen -- bei 365 Tagen mit dreistelligen
     * Gradtagen loeschen sich sonst die fuehrenden Stellen aus.
     */
    double mx = t->sx / n;
    double my = t->sy / n;
    double sxx = t->sxx - n * mx * mx;
    double syy = t->syy - n * my * my;
    double sxy = t->sxy - n * mx * my;

    /*
     * Ohne Spreizung in der Aussenlage gibt es keine Steigung. Die Streuung ist
     * das Quadrat, also wird gegen das Quadrat der Mindestspreizung geprueft.
     */
    if (sxx <= (double)TREND_MIN_SPREAD_K * TREND_MIN_SPREAD_K) {
        return false;
    }

    double b = sxy / sxx;
    double a = my - b * mx;

    /*
     * Reststreuung mit zwei Freiheitsgraden weniger: die Gerade wurde aus
     * denselben Punkten bestimmt, gegen die hier gemessen wird.
     */
    double rest = syy - b * sxy;
    if (rest < 0.0) {
        rest = 0.0; /* nur Rundung */
    }
    double var = rest / (n - 2.0);

    out->valid = true;
    out->slope = (float)b;
    out->intercept = (float)a;
    out->sigma = (float)sqrt(var);
    out->r2 = syy > 0.0 ? (float)(1.0 - rest / syy) : 0.0f;
    return true;
}

bool trend_expected(const trend_fit_t *f, float gradtage, float *out)
{
    if (!f->valid) {
        return false;
    }
    float e = f->intercept + f->slope * gradtage;
    *out = e < 0.0f ? 0.0f : e; /* negative Laufzeit gibt es nicht */
    return true;
}

bool trend_sigma_off(const trend_fit_t *f, float gradtage, float stunden, float *out)
{
    float erwartet;
    if (!trend_expected(f, gradtage, &erwartet) || f->sigma <= 0.0f) {
        return false;
    }
    *out = (stunden - erwartet) / f->sigma;
    return true;
}
