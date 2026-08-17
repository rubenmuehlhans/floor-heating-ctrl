/*
 * Auswertung der Protokolle.
 *
 * Bindeglied zwischen den beiden Ringen im NVS und den Rechenmodulen `trend`
 * und `flue`: Es liest die Saetze, fuettert die Summen und haelt das Ergebnis
 * bereit.
 *
 * Gerechnet wird nicht laufend, sondern beim Start und danach nur, wenn ein
 * Tag oder eine Ladung hinzugekommen ist. Der Durchgang belegt kurz die acht
 * Kilobyte der Protokolle; wenige Male am Tag ist das keiner Rede wert, im
 * Sekundentakt waere es eine.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "flue.h"
#include "trend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Verbrauchslinie: Brennerlaufzeit ueber Heizgradtagen. */
typedef struct {
    trend_fit_t fit;
    /* Der letzte abgeschlossene Tag, gegen die Linie gehalten. Der laufende
     * Tag bleibt aussen vor -- er ist noch nicht zu Ende. */
    bool last_day_valid;
    float last_gradtage;
    float last_hours;
    float last_expected;
    float last_sigma_off;
    /* Tage, die wegen fehlender Aussentemperatur uebergangen wurden. */
    uint32_t skipped;
} trend_status_t;

/* Abgas-Vorlauf-Abstand als Verschmutzungsmass. */
typedef struct {
    flue_result_t res;
    bool wartung_gesetzt;
    uint32_t wartung_epoch;
    /* Ladungen ohne brauchbare Hoechstwerte. */
    uint32_t skipped;
} flue_status_t;

void analyse_start(void);

/* Neu rechnen, wenn seit dem letzten Durchgang etwas dazugekommen ist. */
void analyse_poll(void);

void analyse_trend(trend_status_t *out);
void analyse_flue(flue_status_t *out);

/* Der letzte Tag liegt mehr als TREND_SIGMA_ALERT ueber der Linie. */
bool analyse_day_alert(const trend_status_t *st);

#ifdef __cplusplus
}
#endif
