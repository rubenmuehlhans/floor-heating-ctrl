/*
 * Woechentlicher Termin.
 *
 * Reines Rechenmodul wie valve, roomctrl und heatlogic: die Zeit kommt als
 * Parameter herein, es wird nichts geschaltet und nichts protokolliert.
 *
 * Gemessen wird an der Uhr, nicht an der Laufzeit seit dem Einschalten. Das
 * ist der Unterschied, auf den es ankommt: Ein Geraet mit taeglichem Neustart
 * erreicht nie sieben Tage Laufzeit -- ein Termin, der daran haengt, kaeme nie.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Tag des Jahres und Jahr des letzten Ausloesens, -1 = noch nie. */
    int16_t last_yday;
    int16_t last_year;
} sched_weekly_t;

void sched_weekly_init(sched_weekly_t *st);

/*
 * Liefert true genau einmal an dem Tag, an dem der Termin faellt, sobald die
 * Stunde erreicht ist.
 *
 * weekday und hour sind der eingestellte Termin: 0 = Sonntag bis 6 = Samstag,
 * weekday < 0 schaltet ihn ab. Die uebrigen Werte beschreiben den Augenblick,
 * wie ihn localtime liefert. Ohne gestellte Uhr (year_now < 0) faellt nichts
 * an -- sonst laege der Termin nach jedem Neustart im Jahr 1970.
 *
 * Verpasst wird nichts: Wer zur Terminstunde aus war und danach angeht, loest
 * am selben Tag noch aus. Erst mit dem Tageswechsel ist der Termin vorbei.
 */
bool sched_weekly_due(sched_weekly_t *st, int8_t weekday, int8_t hour, int wday_now,
                      int hour_now, int yday_now, int year_now);

/*
 * Tage bis zum naechsten Termin, 0 wenn er heute noch ansteht. Nur fuer die
 * Anzeige; -1, wenn kein Termin eingestellt oder die Uhr nicht gestellt ist.
 */
int sched_weekly_days_left(const sched_weekly_t *st, int8_t weekday, int8_t hour, int wday_now,
                           int hour_now, int yday_now, int year_now);

#ifdef __cplusplus
}
#endif
