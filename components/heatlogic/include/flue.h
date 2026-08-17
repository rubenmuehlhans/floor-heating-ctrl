/*
 * Abgas-Vorlauf-Abstand als Verschmutzungsmass des Kessels.
 *
 * Reines Rechenmodul wie der Rest von heatlogic.
 *
 * Der Abstand zwischen hoechster Abgastemperatur und hoechstem Kesselvorlauf
 * einer Ladung sagt, wie viel Waerme durch den Schornstein geht statt ins
 * Wasser. Bei sauberem Kessel ist er klein und stabil; Russablagerungen im
 * Waermetauscher heben ihn ueber Wochen an, weil die Waerme nicht mehr
 * uebertritt und stattdessen mit dem Abgas hinausgeht.
 *
 * Verglichen wird der Median der juengsten Ladungen mit dem Median der ersten
 * Ladungen nach der letzten Reinigung. Damit ist der Vergleich einer mit dem
 * eigenen Kessel im sauberen Zustand, nicht mit einem Katalogwert.
 *
 * Der Median, nicht der Mittelwert: Eine einzelne kurze Ladung mit hohem
 * Abgaswert -- etwa ein Start in den kalten Kessel -- soll das Bild nicht
 * verschieben.
 *
 * Zur Groessenordnung: Grob zwanzig Kelvin Abgastemperatur entsprechen einem
 * Prozentpunkt Wirkungsgrad. Der Zeitpunkt der naechsten Reinigung wird damit
 * eine Messung statt eines Kalendereintrags.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ladungen je Fenster: die ersten nach der Reinigung, die juengsten insgesamt. */
#define FLUE_WINDOW 50

/* Darunter wird kein Median gebildet -- zu wenige Ladungen tragen keine Aussage. */
#define FLUE_MIN_CHARGES 10

/*
 * Ab dieser Verschlechterung wird gemeldet. Fuenfzehn Kelvin sind knapp ein
 * Prozentpunkt Wirkungsgrad; darunter liegt man im Bereich der Streuung
 * zwischen einzelnen Ladungen.
 */
#define FLUE_ALERT_K 15.0f

typedef struct {
    /* Die ersten Ladungen nach der Reinigung, in der Reihenfolge des Anfalls. */
    float ref[FLUE_WINDOW];
    uint32_t ref_n;
    /* Die juengsten Ladungen, als Ring. */
    float now[FLUE_WINDOW];
    uint32_t now_n;
    uint32_t now_pos;
} flue_acc_t;

typedef struct {
    bool ref_valid;
    float ref_k;        /* Median nach der Reinigung */
    uint32_t ref_n;
    bool now_valid;
    float now_k;        /* Median der juengsten Ladungen */
    uint32_t now_n;
    bool delta_valid;
    float delta_k;      /* now_k - ref_k, positiv heisst schlechter */
} flue_result_t;

void flue_init(flue_acc_t *a);

/*
 * Eine Ladung. `abstand_k` ist Abgashoechstwert minus Kesselvorlauf-Hoechstwert.
 * `nach_wartung` sagt, ob die Ladung nach der letzten Reinigung liegt; nur
 * solche fuellen das Bezugsfenster, und dort nur die ersten FLUE_WINDOW.
 *
 * Die Ladungen muessen in zeitlicher Reihenfolge kommen, aelteste zuerst --
 * sonst stuenden im Bezugsfenster nicht die ersten nach der Reinigung.
 */
void flue_add(flue_acc_t *a, bool nach_wartung, float abstand_k);

void flue_eval(const flue_acc_t *a, flue_result_t *out);

/* Der Kessel ist deutlich schlechter geworden als nach der letzten Reinigung. */
bool flue_alert(const flue_result_t *r);

#ifdef __cplusplus
}
#endif
