/*
 * Ladungs- und Tagesprotokoll.
 *
 * Der Verlauf im Arbeitsspeicher deckt vierundzwanzig Stunden ab und ist nach
 * einem Neustart weg; die Ladungsaufzeichnung haelt genau eine Ladung und nur
 * dann, wenn jemand sie vorher scharf geschaltet hat. Damit hinterlaesst der
 * Betrieb keine Reihe, an der sich etwas ablesen liesse.
 *
 * Hier stehen deshalb zwei knappe Protokolle im NVS:
 *
 *   - je abgeschlossener Ladung ein Satz von 22 Byte, 64 im Ring
 *   - je Tag ein Satz von 18 Byte, 365 im Ring
 *
 * Zusammen gut acht Kilobyte. Geschrieben wird einmal je Ladung und einmal je
 * Tag -- wenige Male am Tag also, was der NVS vertraegt. Ein Fuenfminutentakt
 * waere das Gegenteil: der Bereich liefe voll, wuerde geleert, und die gesamte
 * Einrichtung waere weg.
 *
 * Beide Protokolle sind bewusst schmal. Sie ersetzen nicht die Aufzeichnung
 * einer einzelnen Ladung, sondern tragen den langen Verlauf: Wie viel Oel bei
 * welcher Aussenlage, und ob der Kessel schlechter wird.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_CHARGES 64
#define LOG_DAYS    365

/* Fehlender Messwert. Dieselbe Kennung wie im Verlauf. */
#define LOG_NONE INT16_MIN

/*
 * Eine abgeschlossene Ladung. Temperaturen in Zehntelgrad, Oel in Millilitern
 * -- damit passt alles in ganze Zahlen und der Satz bleibt klein.
 */
typedef struct {
    uint32_t start_epoch;      /* Beginn, 0 wenn die Uhr nicht stand */
    uint16_t duration_s;       /* bis zum Ende des Nachlaufs */
    uint16_t burner_s;         /* davon mit laufendem Brenner */
    uint8_t starts;            /* Brennerstarts in dieser Ladung */
    uint8_t reserved;
    int16_t puffer_start_dc;
    int16_t puffer_end_dc;
    int16_t kessel_vl_max_dc;
    int16_t abgas_max_dc;
    int16_t aussen_mean_dc;
    uint16_t litres_ml;
} charge_log_t;

/* Ein Tag. */
typedef struct {
    int16_t year;              /* wie struct tm: Jahre seit 1900 */
    uint16_t yday;
    uint32_t runtime_s;
    uint16_t starts;
    uint16_t litres_ml;
    /*
     * Heizgradtage in Zehnteln: je Stunde der positive Anteil von 20 °C minus
     * Aussentemperatur, aufsummiert und durch 24 geteilt. Ohne diese Groesse
     * ist Verbrauch nicht vergleichbar -- ein kalter Januar braucht mehr als
     * ein milder, ohne dass an der Anlage etwas anders waere.
     */
    int16_t gradtage_dc;
    int16_t aussen_min_dc;
    int16_t aussen_max_dc;
} day_log_t;

esp_err_t applog_init(void);

/* Traegt eine abgeschlossene Ladung ein und sichert sie. */
void applog_add_charge(const charge_log_t *rec);

/*
 * Schreibt die Werte des laufenden Tages fort. Beim Tageswechsel wird der
 * abgeschlossene Tag gesichert und ein neuer begonnen.
 */
void applog_tick_day(int year, int yday, uint32_t runtime_s, uint32_t starts,
                     uint16_t litres_ml, bool aussen_valid, float aussen_c);

/* Stuendlicher Beitrag zu den Heizgradtagen. */
void applog_tick_hour(bool aussen_valid, float aussen_c);

/* Anzahl der vorliegenden Saetze; dafuer wird nichts belegt. */
size_t applog_charge_count(void);
size_t applog_day_count(void);

/*
 * Zum Lesen wird das Protokoll einmal geoeffnet und danach geschlossen.
 * Dazwischen ist es gesperrt und belegt rund acht Kilobyte -- deshalb nur so
 * lange wie noetig. applog_charge und applog_day sind nur zwischen open und
 * close gueltig.
 */
bool applog_open(void);
void applog_close(void);
bool applog_charge(size_t index, charge_log_t *out);
bool applog_day(size_t index, day_log_t *out);

/* Verwirft beide Protokolle. */
void applog_clear(void);

#ifdef __cplusplus
}
#endif
