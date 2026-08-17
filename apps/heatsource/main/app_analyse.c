#include "app_analyse.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "app_log.h"
#include "config_store.h"

static const char *TAG = "auswert";

static SemaphoreHandle_t s_mtx;
static trend_status_t s_trend;
static flue_status_t s_flue;

/* Stand der Protokolle beim letzten Durchgang. */
static size_t s_tage_bei = SIZE_MAX;
static size_t s_ladungen_bei = SIZE_MAX;
static uint32_t s_wartung_bei;

/*
 * Beide Protokolle geben den juengsten Satz unter Index 0 zurueck. Beim
 * Tagesprotokoll steht dort sogar der laufende Tag, sofern die Uhr steht --
 * deshalb wird rueckwaerts gelaufen und der laufende Tag uebergangen.
 */
static void tage_rechnen(size_t n)
{
    trend_t akk;
    trend_init(&akk);
    trend_status_t neu;
    memset(&neu, 0, sizeof(neu));

    /*
     * Liegt hinter dem letzten abgeschlossenen Tag noch ein Satz, dann ist
     * Index 0 der laufende Tag und die abgeschlossenen stehen bei 1..n.
     */
    day_log_t d;
    size_t erster = applog_day(n, &d) ? 1 : 0;

    bool letzter_da = false;
    /* Rueckwaerts, damit die aeltesten Tage zuerst einfliessen. */
    for (size_t k = 0; k < n; k++) {
        size_t i = erster + (n - 1 - k);
        if (!applog_day(i, &d)) {
            continue;
        }
        /*
         * Tage ohne Aussentemperatur werden uebergangen. Ihre Gradtage stehen
         * auf null, was einen kalten Tag wie einen warmen aussehen liesse --
         * das zoege die Linie nach unten und machte jeden folgenden Wintertag
         * zum Ausreisser.
         */
        if (d.aussen_min_dc == LOG_NONE) {
            neu.skipped++;
            continue;
        }
        trend_add(&akk, (float)d.gradtage_dc / 10.0f, (float)d.runtime_s / 3600.0f);
        /* Der zuletzt eingetragene ist der juengste abgeschlossene Tag. */
        neu.last_gradtage = (float)d.gradtage_dc / 10.0f;
        neu.last_hours = (float)d.runtime_s / 3600.0f;
        letzter_da = true;
    }

    trend_fit(&akk, &neu.fit);
    if (letzter_da) {
        neu.last_day_valid = true;
        trend_expected(&neu.fit, neu.last_gradtage, &neu.last_expected);
        trend_sigma_off(&neu.fit, neu.last_gradtage, neu.last_hours, &neu.last_sigma_off);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_trend = neu;
    xSemaphoreGive(s_mtx);

    if (neu.fit.valid) {
        ESP_LOGI(TAG, "Verbrauchslinie aus %u Tagen: %.3f h je Gradtag, %.2f h Grundlast, "
                      "Streuung %.2f h",
                 (unsigned)neu.fit.n, neu.fit.slope, neu.fit.intercept, neu.fit.sigma);
    } else {
        ESP_LOGI(TAG, "noch keine Verbrauchslinie: %u Tage, %u ohne Aussenwert",
                 (unsigned)neu.fit.n, (unsigned)neu.skipped);
    }
}

static void ladungen_rechnen(size_t n, uint32_t wartung)
{
    static flue_acc_t akk; /* vierhundert Byte -- nicht auf den Stapel */
    flue_init(&akk);
    flue_status_t neu;
    memset(&neu, 0, sizeof(neu));
    neu.wartung_epoch = wartung;
    neu.wartung_gesetzt = wartung != 0;

    /* Rueckwaerts, damit die aeltesten Ladungen zuerst einfliessen: nur so
     * stehen im Bezugsfenster die ersten nach der Reinigung. */
    for (size_t k = 0; k < n; k++) {
        charge_log_t c;
        if (!applog_charge(n - 1 - k, &c)) {
            continue;
        }
        if (c.abgas_max_dc == LOG_NONE || c.kessel_vl_max_dc == LOG_NONE) {
            neu.skipped++;
            continue;
        }
        float abstand = (float)(c.abgas_max_dc - c.kessel_vl_max_dc) / 10.0f;
        /*
         * Ohne Zeitstempel laesst sich die Ladung keiner Seite der Reinigung
         * zuordnen. Sie zaehlt dann fuer den laufenden Median mit, aber nicht
         * fuer den Bezug -- lieber kein Bezug als ein falscher.
         */
        bool nach = wartung != 0 && c.start_epoch != 0 && c.start_epoch >= wartung;
        flue_add(&akk, nach, abstand);
    }

    flue_eval(&akk, &neu.res);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_flue = neu;
    xSemaphoreGive(s_mtx);

    if (neu.res.delta_valid) {
        ESP_LOGI(TAG, "Abgasabstand: %.1f K jetzt, %.1f K nach der Reinigung, %+.1f K",
                 neu.res.now_k, neu.res.ref_k, neu.res.delta_k);
    } else {
        ESP_LOGI(TAG, "Abgasabstand: %u Ladungen, kein Vergleich moeglich",
                 (unsigned)neu.res.now_n);
    }
}

static void rechnen(void)
{
    static app_config_t cfg;
    cfg_copy(&cfg);
    uint32_t wartung = cfg.burner.wartung_epoch;

    size_t tage = applog_day_count();
    size_t ladungen = applog_charge_count();

    if (!applog_open()) {
        ESP_LOGW(TAG, "Protokolle liessen sich nicht lesen");
        return;
    }
    tage_rechnen(tage);
    ladungen_rechnen(ladungen, wartung);
    applog_close();

    s_tage_bei = tage;
    s_ladungen_bei = ladungen;
    s_wartung_bei = wartung;
}

void analyse_start(void)
{
    if (s_mtx != NULL) {
        return;
    }
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        ESP_LOGE(TAG, "Sperre liess sich nicht anlegen");
        return;
    }
    rechnen();
}

void analyse_poll(void)
{
    if (s_mtx == NULL) {
        return;
    }
    static app_config_t cfg;
    cfg_copy(&cfg);
    /* Auch das Reinigungsdatum aendert das Ergebnis, ohne dass ein Satz
     * dazugekommen waere. */
    if (applog_day_count() != s_tage_bei || applog_charge_count() != s_ladungen_bei ||
        cfg.burner.wartung_epoch != s_wartung_bei) {
        rechnen();
    }
}

void analyse_trend(trend_status_t *out)
{
    if (s_mtx == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_trend;
    xSemaphoreGive(s_mtx);
}

void analyse_flue(flue_status_t *out)
{
    if (s_mtx == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_flue;
    xSemaphoreGive(s_mtx);
}

bool analyse_day_alert(const trend_status_t *st)
{
    /* Nur nach oben. Weniger Verbrauch als erwartet ist kein Fehler. */
    return st->fit.valid && st->last_day_valid && st->last_sigma_off > TREND_SIGMA_ALERT;
}
