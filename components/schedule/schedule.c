#include "schedule.h"

#include <string.h>

void sched_weekly_init(sched_weekly_t *st)
{
    st->last_yday = -1;
    st->last_year = -1;
}

bool sched_weekly_due(sched_weekly_t *st, int8_t weekday, int8_t hour, int wday_now,
                      int hour_now, int yday_now, int year_now)
{
    if (weekday < 0 || weekday > 6 || year_now < 0) {
        return false;
    }
    if (wday_now != weekday || hour_now < hour) {
        return false;
    }
    /* Am selben Tag nur einmal. Der Vergleich nimmt das Jahr dazu, sonst
     * fiele der Termin an einem 1. Januar mit dem des Vorjahres zusammen. */
    if (st->last_yday == (int16_t)yday_now && st->last_year == (int16_t)year_now) {
        return false;
    }
    st->last_yday = (int16_t)yday_now;
    st->last_year = (int16_t)year_now;
    return true;
}

int sched_weekly_days_left(const sched_weekly_t *st, int8_t weekday, int8_t hour, int wday_now,
                           int hour_now, int yday_now, int year_now)
{
    if (weekday < 0 || weekday > 6 || year_now < 0) {
        return -1;
    }
    int tage = (weekday - wday_now + 7) % 7;
    if (tage != 0) {
        return tage;
    }
    /* Heute ist der Tag: entweder steht er noch an oder er ist erledigt. */
    (void)hour;
    (void)hour_now;
    bool heute_schon = st->last_yday == (int16_t)yday_now && st->last_year == (int16_t)year_now;
    return heute_schon ? 7 : 0;
}
