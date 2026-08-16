#include "app_burner.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_sensors.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "burner";

#define TICK_MS 1000
#define REC_PERIOD_S 5
#define NVS_NAMESPACE "heiz"
#define NVS_KEY_STATS "stats"
/* Ab so vielen Starts bei so wenig Laufzeit je Start gilt der Betrieb als
 * taktend -- der Kessel geht dann haeufiger an, als er Waerme abgibt. */
#define TAKT_STARTS 12
#define TAKT_LAUF_S 300

typedef struct {
    uint32_t runtime_today_s;
    uint32_t starts_today;
    uint32_t runtime_yesterday_s;
    uint32_t starts_yesterday;
    int16_t tag; /* Tag des Jahres, zu dem die Werte gehoeren */
} stats_t;

static burner_cfg_t s_cfg;
static burner_state_t s_st;
static stats_t s_stats;
static SemaphoreHandle_t s_mtx;
static volatile bool s_cfg_dirty;

/* Aufzeichnung. Belegt werden nur die beim Start zugeordneten Messstellen:
 * am Kessel sind das drei, am Speicher sechs -- statt aller vierzehn Rollen. */
static int16_t *s_rec;
static probe_role_t s_rec_role[ROLE_COUNT];
static uint8_t s_rec_cols;
static uint16_t s_rec_len;
static rec_state_t s_rec_state;
static uint32_t s_rec_started_epoch;
static uint32_t s_rec_last_ms;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */
/* Tageswerte                                                          */
/* ------------------------------------------------------------------ */

/*
 * Laufzeit und Starts ueberdauern einen Neustart. Geschrieben wird nur alle
 * paar Minuten und beim Tageswechsel: das Flash haelt begrenzt viele
 * Schreibvorgaenge aus, und ein verlorener Wert von wenigen Minuten faellt in
 * einer Tagesbilanz nicht ins Gewicht.
 */
static void stats_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_stats);
    if (nvs_get_blob(h, NVS_KEY_STATS, &s_stats, &len) != ESP_OK || len != sizeof(s_stats)) {
        memset(&s_stats, 0, sizeof(s_stats));
        s_stats.tag = -1;
    }
    nvs_close(h);
}

static void stats_store(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, NVS_KEY_STATS, &s_stats, sizeof(s_stats));
    nvs_commit(h);
    nvs_close(h);
}

/* Tag des Jahres, oder -1 solange die Uhr nicht gestellt ist. */
static int heute(void)
{
    time_t jetzt = time(NULL);
    if (jetzt < 1700000000) {
        return -1;
    }
    struct tm tm;
    localtime_r(&jetzt, &tm);
    return tm.tm_yday;
}

/* ------------------------------------------------------------------ */
/* Aufzeichnung                                                        */
/* ------------------------------------------------------------------ */

void rec_discard(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    free(s_rec);
    s_rec = NULL;
    s_rec_len = 0;
    s_rec_cols = 0;
    s_rec_state = REC_IDLE;
    xSemaphoreGive(s_mtx);
}

esp_err_t rec_start(void)
{
    /* Welche Messstellen belegt sind, entscheidet ueber die Spalten. */
    static sens_snapshot_t snap;
    sensors_get(&snap);

    probe_role_t rollen[ROLE_COUNT];
    uint8_t n = 0;
    for (int r = 1; r < ROLE_COUNT; r++) {
        for (size_t i = 0; i < snap.count; i++) {
            if (snap.probes[i].role == (probe_role_t)r) {
                rollen[n++] = (probe_role_t)r;
                break;
            }
        }
    }
    if (n == 0) {
        ESP_LOGW(TAG, "Keine zugeordnete Messstelle, nichts aufzuzeichnen");
        return ESP_ERR_NOT_FOUND;
    }

    /* Ein vorheriger Lauf wird verworfen; sonst haetten zwei Aufzeichnungen
     * unterschiedliche Spalten im selben Puffer. */
    free(s_rec);
    s_rec = calloc((size_t)REC_SLOTS * n, sizeof(int16_t));
    if (s_rec == NULL) {
        s_rec_cols = 0;
        ESP_LOGW(TAG, "Kein Speicher fuer die Aufzeichnung");
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(s_rec_role, rollen, sizeof(rollen));
    s_rec_cols = n;
    s_rec_len = 0;
    s_rec_state = REC_RUNNING;
    s_rec_last_ms = 0;
    time_t jetzt = time(NULL);
    s_rec_started_epoch = jetzt > 1700000000 ? (uint32_t)jetzt : 0;
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "Aufzeichnung gestartet, %u Messstellen, %u Byte", (unsigned)n,
             (unsigned)((size_t)REC_SLOTS * n * sizeof(int16_t)));
    return ESP_OK;
}

void rec_stop(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_rec_state == REC_RUNNING) {
        s_rec_state = REC_DONE;
        ESP_LOGI(TAG, "Aufzeichnung beendet, %u Zeilen", (unsigned)s_rec_len);
    }
    xSemaphoreGive(s_mtx);
}

void rec_get_status(rec_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    out->state = s_rec_state;
    out->samples = s_rec_len;
    out->period_s = REC_PERIOD_S;
    out->started_epoch = s_rec_started_epoch;
    out->cols = s_rec_cols;
    memcpy(out->role, s_rec_role, sizeof(out->role));
    out->bytes = s_rec ? (uint32_t)REC_SLOTS * s_rec_cols * sizeof(int16_t) : 0;
    xSemaphoreGive(s_mtx);
}

bool rec_row(size_t index, int16_t *out, size_t out_len)
{
    bool ok = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_rec != NULL && index < s_rec_len && out_len >= s_rec_cols) {
        memcpy(out, &s_rec[index * s_rec_cols], s_rec_cols * sizeof(int16_t));
        ok = true;
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

/* Schreibt eine Zeile mit allen zugeordneten Messstellen. */
static void rec_append(const sens_snapshot_t *snap)
{
    if (s_rec == NULL || s_rec_state != REC_RUNNING || s_rec_len >= REC_SLOTS) {
        if (s_rec_state == REC_RUNNING && s_rec_len >= REC_SLOTS) {
            s_rec_state = REC_DONE;
            ESP_LOGI(TAG, "Aufzeichnung voll, beendet");
        }
        return;
    }
    int16_t *row = &s_rec[(size_t)s_rec_len * s_rec_cols];
    for (uint8_t c = 0; c < s_rec_cols; c++) {
        row[c] = SENS_HIST_NONE;
        for (size_t i = 0; i < snap->count; i++) {
            const sens_probe_t *p = &snap->probes[i];
            if (p->role != s_rec_role[c] || !p->valid) {
                continue;
            }
            float v = p->temp_c * 10.0f;
            if (v <= 32000.0f && v >= -32000.0f) {
                row[c] = (int16_t)lroundf(v);
            }
            break;
        }
    }
    s_rec_len++;
}

/* ------------------------------------------------------------------ */
/* Aufgabe                                                             */
/* ------------------------------------------------------------------ */

static void apply_config(void)
{
    static app_config_t cfg;
    cfg_copy(&cfg);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cfg.delta_on_k = cfg.burner.delta_on_k;
    s_cfg.delta_off_k = cfg.burner.delta_off_k;
    s_cfg.on_hold_s = cfg.burner.on_hold_s;
    s_cfg.off_hold_s = cfg.burner.off_hold_s;
    s_cfg.duese_l_h = cfg.burner.duese_l_h;
    xSemaphoreGive(s_mtx);
}

static void burner_task(void *arg)
{
    (void)arg;
    apply_config();
    stats_load();

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_st.runtime_today_s = s_stats.runtime_today_s;
    s_st.starts_today = s_stats.starts_today;
    xSemaphoreGive(s_mtx);

    static sens_snapshot_t snap;
    uint32_t letzte_sicherung = now_ms();
    int letzter_tag = s_stats.tag;

    for (;;) {
        if (s_cfg_dirty) {
            s_cfg_dirty = false;
            apply_config();
        }

        float abgas = 0.0f;
        bool gueltig = sensors_role_value(ROLE_ABGAS, &abgas, NULL);
        uint32_t t = now_ms();

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        burner_tick(&s_st, &s_cfg, gueltig, abgas, t);
        xSemaphoreGive(s_mtx);

        /* Tageswechsel. Ohne gestellte Uhr wird nicht umgeschaltet -- sonst
         * fiele der Wechsel mit jedem Neustart zusammen. */
        int tag = heute();
        if (tag >= 0 && letzter_tag >= 0 && tag != letzter_tag) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_stats.runtime_yesterday_s = s_st.runtime_today_s;
            s_stats.starts_yesterday = s_st.starts_today;
            burner_new_day(&s_st);
            s_stats.runtime_today_s = 0;
            s_stats.starts_today = 0;
            s_stats.tag = (int16_t)tag;
            xSemaphoreGive(s_mtx);
            stats_store();
            ESP_LOGI(TAG, "Tageswechsel: gestern %u s Laufzeit, %u Starts",
                     (unsigned)s_stats.runtime_yesterday_s, (unsigned)s_stats.starts_yesterday);
        }
        if (tag >= 0) {
            letzter_tag = tag;
            s_stats.tag = (int16_t)tag;
        }

        /* Alle fuenf Minuten sichern. */
        if (t - letzte_sicherung >= 300000) {
            letzte_sicherung = t;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_stats.runtime_today_s = s_st.runtime_today_s;
            s_stats.starts_today = s_st.starts_today;
            xSemaphoreGive(s_mtx);
            stats_store();
        }

        /* Aufzeichnung im festen Raster. */
        if (s_rec_state == REC_RUNNING &&
            (s_rec_last_ms == 0 || t - s_rec_last_ms >= REC_PERIOD_S * 1000UL)) {
            s_rec_last_ms = t;
            sensors_get(&snap);
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            rec_append(&snap);
            xSemaphoreGive(s_mtx);
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ------------------------------------------------------------------ */

esp_err_t burner_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    burner_defaults(&s_cfg);
    burner_init(&s_st);
    s_stats.tag = -1;

    if (xTaskCreate(burner_task, "burner", 4096, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void burner_config_changed(void)
{
    s_cfg_dirty = true;
}

void burner_get(burner_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    out->known = s_st.known;
    out->running = s_st.running;
    out->abgas_c = s_st.abgas_c;
    out->baseline_c = s_st.baseline_c;
    out->since_s = s_st.started ? (now_ms() - s_st.since_ms) / 1000 : 0;
    out->runtime_today_s = s_st.runtime_today_s;
    out->starts_today = s_st.starts_today;
    out->runtime_yesterday_s = s_stats.runtime_yesterday_s;
    out->starts_yesterday = s_stats.starts_yesterday;
    out->litres_today = burner_litres_today(&s_st, &s_cfg);
    /* Haeufige kurze Starts: der Kessel taktet, statt durchzuheizen. */
    out->short_cycling = s_st.starts_today >= TAKT_STARTS &&
                         s_st.runtime_today_s / (s_st.starts_today ? s_st.starts_today : 1) <
                             TAKT_LAUF_S;
    xSemaphoreGive(s_mtx);
}
