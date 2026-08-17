/*
 * Verbrauchslinie: Brennerlaufzeit ueber Heizgradtagen.
 *
 * Reines Rechenmodul wie der Rest von heatlogic: es bekommt Zahlenpaare,
 * rechnet und gibt zurueck. Keine Zeit, kein Zustand ausser den Summen.
 *
 * Ein kalter Januar braucht mehr Oel als ein milder, ohne dass an der Anlage
 * etwas anders waere. Verbrauch ist deshalb erst vergleichbar, wenn er auf die
 * Aussenlage bezogen wird. Genau das leistet eine Gerade durch die Tagespunkte:
 *
 *   Laufzeit = Steigung * Gradtage + Achsenabschnitt
 *
 * Die Steigung ist der Waermebedarf des Hauses je Gradtag, der Achsenabschnitt
 * der Grundverbrauch fuer Warmwasser -- beides Kennzahlen, die man sonst
 * schaetzt. Die Streuung der Abweichungen sagt, wie eng die Tage an der Linie
 * liegen, und macht damit erst beurteilbar, ob ein einzelner Tag auffaellt.
 *
 * Gerechnet wird ueber Summen, nicht ueber die gespeicherten Punkte: sechs
 * Zahlen genuegen fuer Steigung, Achsenabschnitt und Streuung, gleich wie viele
 * Tage einfliessen.
 *
 * Die Grenzen des Verfahrens sind bewusst eng gezogen. Im Sommer liegen alle
 * Tage bei null Gradtagen; eine Gerade durch eine senkrechte Punktwolke hat
 * keine sinnvolle Steigung. Deshalb wird eine Mindestspreizung der Gradtage
 * verlangt, und ohne sie gibt es keine Linie statt einer falschen.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Summen fuer die Anpassung. Rund vierzig Byte, unabhaengig von der Tagezahl. */
typedef struct {
    uint32_t n;
    double sx, sy, sxx, sxy, syy;
} trend_t;

typedef struct {
    bool valid;
    uint32_t n;
    float slope;      /* Stunden Brennerlauf je Gradtag */
    float intercept;  /* Stunden je Tag ohne Heizbedarf, also Warmwasser */
    float sigma;      /* Streuung der Abweichungen, in Stunden */
    float r2;         /* Anteil erklaerter Streuung, 0 bis 1 */
} trend_fit_t;

/*
 * Mindestzahl an Tagen und Mindestspreizung der Gradtage. Unter beidem gibt es
 * keine Linie: zu wenige Punkte tragen keine Streuungsangabe, und ohne
 * Spreizung in der Aussenlage ist die Steigung nicht bestimmt.
 */
#define TREND_MIN_DAYS 14
#define TREND_MIN_SPREAD_K 3.0f

/* Ab dieser Abweichung nach oben gilt ein Tag als auffaellig. */
#define TREND_SIGMA_ALERT 3.0f

void trend_init(trend_t *t);

/* Ein Tag: Gradtage und Brennerlaufzeit in Stunden. */
void trend_add(trend_t *t, float gradtage, float stunden);

/*
 * Passt die Gerade an. Rueckgabe false, wenn zu wenige Tage vorliegen oder die
 * Gradtage zu eng beieinander -- dann ist out->valid ebenfalls false und die
 * Koeffizienten sind null.
 */
bool trend_fit(const trend_t *t, trend_fit_t *out);

/*
 * Abweichung eines Tages von der Linie, in Streuungen. Positiv heisst mehr
 * Verbrauch als erwartet. Rueckgabe false, wenn keine Linie vorliegt oder die
 * Streuung null ist.
 */
bool trend_sigma_off(const trend_fit_t *f, float gradtage, float stunden, float *out);

/* Erwartete Laufzeit fuer eine Aussenlage, in Stunden. */
bool trend_expected(const trend_fit_t *f, float gradtage, float *out);

#ifdef __cplusplus
}
#endif
