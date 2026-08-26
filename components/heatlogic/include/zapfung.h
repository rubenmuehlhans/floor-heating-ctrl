/*
 * Warmwasserzapfung am Verlauf der Speichertemperatur.
 *
 * Reines Rechenmodul wie der Rest von heatlogic.
 *
 * Im Sommer verbraucht die Anlage taeglich Oel, ohne dass ein Raum Waerme
 * abruft. Darin stecken zwei Dinge, die sich bisher nicht trennen liessen: das
 * Warmwasser und der Stillstandsverlust des Speichers. Sie verhalten sich aber
 * ganz verschieden -- der Verlust ist ein langsames, stetiges Absinken, eine
 * Zapfung ein steiler Einbruch.
 *
 * An der Anlage gemessen: Der Stillstandsverlust betraegt rund 0,8 Kelvin je
 * Stunde. Ein Vollbad liess den Speicher in dreissig Minuten um 6,5 Kelvin
 * fallen -- das Achtfache. Die beiden auseinanderzuhalten braucht keine
 * Feinheit, nur eine Schwelle.
 *
 * Gezaehlt wird in Kelvin, nicht in Kilowattstunden: Wie viel Waerme ein Kelvin
 * ist, haengt am Inhalt des Speichers, und der ist eine Angabe ueber die
 * Anlage, keine Messung. Wer ihn eintraegt, bekommt die Umrechnung dazu.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* So weit muss der Speicher fallen, damit es als Zapfung gilt. */
    float drop_k;
    /* In diesem Fenster gemessen. */
    uint32_t win_s;
} zapf_cfg_t;

typedef struct {
    bool active;         /* gerade laeuft eine Zapfung */
    float start_c;       /* Speicherwert zu ihrem Beginn */
    float tief_c;        /* tiefster Wert waehrend ihr */
    uint32_t begin_ms;

    /* Bezugspunkt des wandernden Fensters. */
    bool have;
    float ref_c;
    uint32_t ref_ms;

    /* Bilanz, von der Anwendung zum Tageswechsel zurueckgesetzt. */
    uint32_t count;      /* Zapfungen */
    float sum_k;         /* zusammen entnommene Kelvin */
    float last_k;        /* Hoehe der letzten Zapfung */
} zapf_state_t;

/* Vorgabe: 2 K Einbruch innerhalb von 15 Minuten. Der Stillstandsverlust
 * schafft in dieser Zeit rund 0,2 K. */
void zapf_defaults(zapf_cfg_t *cfg);
void zapf_init(zapf_state_t *st);

/*
 * Ein Zeitschritt. Gezaehlt wird nur bei stehendem Brenner -- waehrend einer
 * Ladung steigt der Speicher, und was dabei gleichzeitig gezapft wird, laesst
 * sich am Fuehler nicht abtrennen.
 */
void zapf_tick(zapf_state_t *st, const zapf_cfg_t *cfg, bool burner_running,
               bool puffer_valid, float puffer_c, uint32_t now_ms);

/* Tageswechsel: Zaehler und Summe zuruecksetzen. */
void zapf_new_day(zapf_state_t *st);

/* Kelvin in Kilowattstunden, wenn der Inhalt des Speichers bekannt ist.
 * Rueckgabe false bei volumen_l <= 0. */
bool zapf_kwh(float kelvin, float volumen_l, float *out_kwh);

#ifdef __cplusplus
}
#endif
