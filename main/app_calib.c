#include "app_calib.h"

#include <stdlib.h>
#include <string.h>

#include "app_control.h"
#include "bemf.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "calib";

/* Hoechstdauer je Fahrtrichtung. Grosszuegig gewaehlt: die Kalibrierung soll
 * auch dann ein Ergebnis liefern, wenn die bisher hinterlegte Fahrzeit zu kurz
 * eingetragen war. */
#define PHASE_TIMEOUT_MS   90000
#define PAUSE_MS           3000
#define BASELINE_WINDOW_MS 2000
#define LEVEL_WINDOW       8    /* 400 ms gleitender Median */
#define MIN_RISE_MV        60   /* Mindestanstieg gegenueber dem Fahrniveau */
#define SUSTAIN_SAMPLES    4    /* so lange muss der Anstieg anhalten */

#define MAX_SAMPLES (2 * PHASE_TIMEOUT_MS / BEMF_SAMPLE_PERIOD_MS + 200)

static SemaphoreHandle_t s_mtx;
static calib_status_t s_st;
static uint16_t *s_samples;
static volatile uint16_t s_write_idx;
static volatile bool s_abort;
static TaskHandle_t s_task;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */

static uint16_t median_of(uint16_t *v, size_t n)
{
    for (size_t i = 1; i < n; i++) {
        uint16_t x = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    return v[n / 2];
}

static uint16_t median_range(uint16_t from, uint16_t to)
{
    if (to <= from) {
        return 0;
    }
    size_t n = to - from;
    if (n > 64) {
        n = 64; /* mehr braucht es fuer eine stabile Mitte nicht */
    }
    uint16_t tmp[64];
    uint16_t step = (uint16_t)((to - from) / n);
    if (step == 0) {
        step = 1;
    }
    size_t k = 0;
    for (uint16_t i = from; i < to && k < n; i += step) {
        tmp[k++] = s_samples[i];
    }
    return median_of(tmp, k ? k : 1);
}

/* Messwerte der eigenen Messgruppe mitschreiben. Laeuft im Kontext der
 * BEMF-Task, deshalb nur anhaengen und nichts auswerten. */
static void on_sample(uint8_t group, uint16_t raw_mv, uint16_t median_mv, uint32_t t_ms, void *ctx)
{
    (void)median_mv;
    (void)t_ms;
    (void)ctx;

    if (group != s_st.group || s_samples == NULL) {
        return;
    }
    uint16_t idx = s_write_idx;
    if (idx >= MAX_SAMPLES) {
        return;
    }
    s_samples[idx] = raw_mv;
    s_write_idx = (uint16_t)(idx + 1);
}

/*
 * Faehrt eine Richtung und wartet auf das Blockieren.
 *
 * Rueckgabe: Index, an dem das Blockieren erkannt wurde, oder 0 bei Zeitablauf
 * beziehungsweise Abbruch. baseline und stall werden immer gesetzt, soweit
 * ermittelbar.
 */
static uint16_t run_phase(hw_drive_t drive, uint32_t blank_ms, uint16_t *baseline_out,
                          uint16_t *stall_out, uint32_t *duration_ms)
{
    uint32_t start = now_ms();
    control_raw_drive(s_st.channel, drive);

    uint16_t baseline = 0;
    bool baseline_ready = false;
    uint16_t baseline_from = 0;
    uint16_t window[LEVEL_WINDOW];
    uint8_t sustain = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BEMF_SAMPLE_PERIOD_MS));

        uint32_t elapsed = now_ms() - start;
        uint16_t idx = s_write_idx;

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_st.sample_count = idx;
        xSemaphoreGive(s_mtx);

        if (s_abort) {
            break;
        }

        if (elapsed < blank_ms) {
            continue; /* Anlaufstrom */
        }
        if (baseline_from == 0) {
            baseline_from = idx;
        }

        if (!baseline_ready) {
            if (elapsed >= blank_ms + BASELINE_WINDOW_MS) {
                baseline = median_range(baseline_from, idx);
                baseline_ready = true;
                if (baseline_out) {
                    *baseline_out = baseline;
                }
                ESP_LOGI(TAG, "CH%u %s: Fahrniveau %u mV", s_st.channel,
                         drive == HW_DRIVE_CLOSE ? "schliessen" : "oeffnen", baseline);
            }
            continue;
        }

        if (idx < LEVEL_WINDOW) {
            continue;
        }
        memcpy(window, &s_samples[idx - LEVEL_WINDOW], sizeof(window));
        uint16_t level = median_of(window, LEVEL_WINDOW);

        uint16_t rise = baseline > MIN_RISE_MV ? baseline : MIN_RISE_MV;
        if (level >= baseline + rise) {
            if (++sustain >= SUSTAIN_SAMPLES) {
                control_raw_drive(s_st.channel, HW_DRIVE_OFF);
                if (stall_out) {
                    *stall_out = level;
                }
                if (duration_ms) {
                    *duration_ms = elapsed;
                }
                return idx;
            }
        } else {
            sustain = 0;
        }

        if (elapsed >= PHASE_TIMEOUT_MS) {
            break;
        }
    }

    control_raw_drive(s_st.channel, HW_DRIVE_OFF);
    if (duration_ms) {
        *duration_ms = now_ms() - start;
    }
    return 0;
}

/* Ruhephase mitschreiben, damit im Diagramm sichtbar ist, wo der Motor
 * steht. */
static void record_pause(uint32_t ms)
{
    uint32_t start = now_ms();
    while (now_ms() - start < ms && !s_abort) {
        vTaskDelay(pdMS_TO_TICKS(BEMF_SAMPLE_PERIOD_MS));
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_st.sample_count = s_write_idx;
        xSemaphoreGive(s_mtx);
    }
}

static void finish(calib_state_t state, const char *msg)
{
    control_raw_drive(s_st.channel, HW_DRIVE_OFF);
    bemf_set_sample_cb(NULL, NULL);
    control_release(s_st.channel);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_st.state = state;
    s_st.phase = CALIB_PHASE_NONE;
    s_st.sample_count = s_write_idx;
    snprintf(s_st.message, sizeof(s_st.message), "%s", msg);
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "CH%u: %s", s_st.channel, msg);
}

static void calib_task(void *arg)
{
    (void)arg;

    app_config_t cfg;
    cfg_copy(&cfg);
    uint32_t blank_ms = cfg.channels[s_st.channel - 1].blank_ms;
    if (blank_ms < 1000) {
        blank_ms = 1000;
    }

    /* --- Schliessen --- */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_st.phase = CALIB_PHASE_CLOSE;
    s_st.close_from = s_write_idx;
    xSemaphoreGive(s_mtx);

    uint32_t close_ms = 0;
    uint16_t stall = run_phase(HW_DRIVE_CLOSE, blank_ms, &s_st.baseline_close_mv,
                               &s_st.stall_close_mv, &close_ms);
    s_st.close_to = s_write_idx;
    s_st.close_stalled = stall != 0;

    if (s_abort) {
        finish(CALIB_FAILED, "Vom Anwender abgebrochen");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (!s_st.close_stalled) {
        finish(CALIB_FAILED,
               "Beim Schliessen wurde keine Endlage erkannt. Verdrahtung und Shunt pruefen.");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* --- Ruhe --- */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_st.phase = CALIB_PHASE_PAUSE;
    xSemaphoreGive(s_mtx);
    record_pause(PAUSE_MS);

    /* --- Oeffnen --- */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_st.phase = CALIB_PHASE_OPEN;
    s_st.open_from = s_write_idx;
    xSemaphoreGive(s_mtx);

    uint32_t open_ms = 0;
    stall = run_phase(HW_DRIVE_OPEN, blank_ms, &s_st.baseline_open_mv, &s_st.stall_open_mv,
                      &open_ms);
    s_st.open_to = s_write_idx;
    s_st.open_stalled = stall != 0;

    if (s_abort) {
        finish(CALIB_FAILED, "Vom Anwender abgebrochen");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (!s_st.open_stalled) {
        finish(CALIB_FAILED,
               "Beim Oeffnen wurde keine Endlage erkannt. Verdrahtung und Shunt pruefen.");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* --- Auswertung --- */
    uint16_t base = (uint16_t)((s_st.baseline_close_mv + s_st.baseline_open_mv) / 2);
    uint16_t peak = s_st.stall_close_mv < s_st.stall_open_mv ? s_st.stall_close_mv
                                                            : s_st.stall_open_mv;
    if (peak <= base) {
        finish(CALIB_FAILED, "Kein auswertbarer Anstieg zwischen Fahrt und Endlage");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint16_t span = (uint16_t)(peak - base);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_st.suggest_close_ms = close_ms;
    s_st.suggest_open_ms = open_ms;
    uint32_t longest = close_ms > open_ms ? close_ms : open_ms;
    s_st.suggest_max_ms = longest + longest / 6; /* rund 15 Prozent Reserve */
    s_st.suggest_bemf_mv = (uint16_t)(base + span / 2);
    s_st.suggest_hyst_mv = (uint16_t)(span / 4);
    if (s_st.suggest_hyst_mv < 10) {
        s_st.suggest_hyst_mv = 10;
    }
    s_st.suggestion_valid = true;
    xSemaphoreGive(s_mtx);

    char msg[96];
    snprintf(msg, sizeof(msg), "Fertig: zu %.1f s, auf %.1f s, Schwelle %u mV",
             close_ms / 1000.0f, open_ms / 1000.0f, s_st.suggest_bemf_mv);
    finish(CALIB_DONE, msg);

    s_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */

esp_err_t calib_start(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mtx == NULL) {
        s_mtx = xSemaphoreCreateMutex();
        if (s_mtx == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_st.state == CALIB_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!control_reserve(channel)) {
        return ESP_ERR_NOT_FINISHED; /* Kanal oder Messgruppe belegt */
    }

    uint16_t *buf = calloc(MAX_SAMPLES, sizeof(uint16_t));
    if (buf == NULL) {
        control_release(channel);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    free(s_samples);
    s_samples = buf;
    s_write_idx = 0;
    s_abort = false;
    memset(&s_st, 0, sizeof(s_st));
    s_st.state = CALIB_RUNNING;
    s_st.channel = channel;
    s_st.group = hw_group_of_channel(channel);
    s_st.sample_period_ms = BEMF_SAMPLE_PERIOD_MS;
    snprintf(s_st.message, sizeof(s_st.message), "Kalibrierung laeuft");
    xSemaphoreGive(s_mtx);

    bemf_set_sample_cb(on_sample, NULL);

    if (xTaskCreate(calib_task, "calib", 4096, NULL, 5, &s_task) != pdPASS) {
        bemf_set_sample_cb(NULL, NULL);
        control_release(channel);
        s_st.state = CALIB_FAILED;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Kalibrierung CH%u gestartet (Messgruppe %u)", channel, s_st.group + 1);
    return ESP_OK;
}

void calib_abort(void)
{
    s_abort = true;
}

void calib_get_status(calib_status_t *out)
{
    if (s_mtx == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_st;
    out->sample_count = s_write_idx;
    xSemaphoreGive(s_mtx);
}

size_t calib_get_samples(uint16_t from, uint16_t *dst, size_t max)
{
    if (s_samples == NULL) {
        return 0;
    }
    uint16_t have = s_write_idx;
    if (from >= have) {
        return 0;
    }
    size_t n = have - from;
    if (n > max) {
        n = max;
    }
    memcpy(dst, &s_samples[from], n * sizeof(uint16_t));
    return n;
}

esp_err_t calib_accept(char *err, size_t err_len)
{
    if (s_st.state != CALIB_DONE || !s_st.suggestion_valid) {
        snprintf(err, err_len, "Es liegt kein bestaetigungsfaehiges Ergebnis vor");
        return ESP_ERR_INVALID_STATE;
    }

    app_config_t cfg;
    cfg_copy(&cfg);
    cfg_channel_t *c = &cfg.channels[s_st.channel - 1];
    c->close_ms = s_st.suggest_close_ms;
    c->open_ms = s_st.suggest_open_ms;
    c->max_ms = s_st.suggest_max_ms;
    c->bemf_mv = s_st.suggest_bemf_mv;
    c->bemf_hyst_mv = s_st.suggest_hyst_mv;
    c->calibrated = true;

    esp_err_t rc = cfg_set(&cfg, err, err_len);
    if (rc == ESP_OK) {
        control_config_changed();
        ESP_LOGI(TAG, "CH%u kalibriert uebernommen", s_st.channel);
    }
    return rc;
}

void calib_discard(void)
{
    if (s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_st.state != CALIB_RUNNING) {
        free(s_samples);
        s_samples = NULL;
        s_write_idx = 0;
        memset(&s_st, 0, sizeof(s_st));
    }
    xSemaphoreGive(s_mtx);
}
