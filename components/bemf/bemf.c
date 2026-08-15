#include "bemf.h"

#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bemf";

typedef struct {
    uint16_t window[BEMF_MEDIAN_WINDOW];
    uint8_t fill;
    uint8_t next;
    uint16_t median_mv;
    uint16_t threshold_mv;
    uint16_t hyst_mv;
    bool armed;
    bool tripped; /* haelt bis zum Unterschreiten von Schwelle minus Hysterese */
} group_state_t;

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;
static group_state_t s_groups[HW_BEMF_GROUP_COUNT];

static bemf_endstop_cb_t s_endstop_cb;
static void *s_endstop_ctx;
static bemf_sample_cb_t s_sample_cb;
static void *s_sample_ctx;

/* Median eines kleinen Fensters. Bei fuenf Werten ist Sortieren durch
 * Einfuegen guenstiger als jeder ausgefeiltere Weg. */
static uint16_t median_of(const uint16_t *src, uint8_t n)
{
    uint16_t tmp[BEMF_MEDIAN_WINDOW];
    memcpy(tmp, src, n * sizeof(uint16_t));
    for (uint8_t i = 1; i < n; i++) {
        uint16_t v = tmp[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && tmp[j] > v) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = v;
    }
    return tmp[n / 2];
}

static uint16_t read_group_mv(const hw_bemf_group_t *g)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, (adc_channel_t)g->adc_channel, &raw) != ESP_OK) {
        return 0;
    }
    int mv = raw;
    if (s_cali_ok) {
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
            mv = raw;
        }
    }
    if (mv < 0) {
        mv = 0;
    }
    if (mv > 65535) {
        mv = 65535;
    }
    return (uint16_t)mv;
}

static void bemf_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        for (uint8_t gi = 0; gi < HW_BEMF_GROUP_COUNT; gi++) {
            const hw_bemf_group_t *g = hw_bemf_group(gi);
            group_state_t *st = &s_groups[gi];

            uint16_t mv = read_group_mv(g);

            st->window[st->next] = mv;
            st->next = (uint8_t)((st->next + 1) % BEMF_MEDIAN_WINDOW);
            if (st->fill < BEMF_MEDIAN_WINDOW) {
                st->fill++;
            }
            st->median_mv = median_of(st->window, st->fill);

            if (s_sample_cb) {
                s_sample_cb(gi, mv, st->median_mv, now, s_sample_ctx);
            }

            if (!st->armed || st->threshold_mv == 0) {
                st->tripped = false;
                continue;
            }

            if (!st->tripped && st->median_mv >= st->threshold_mv) {
                st->tripped = true;
                if (s_endstop_cb) {
                    s_endstop_cb(gi, st->median_mv, s_endstop_ctx);
                }
            } else if (st->tripped && st->median_mv + st->hyst_mv < st->threshold_mv) {
                st->tripped = false;
            }
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(BEMF_SAMPLE_PERIOD_MS));
    }
}

esp_err_t bemf_start(void)
{
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit, &s_adc);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_6, /* rund 0..1750 mV, wie im ESPHome-Aufbau */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    for (uint8_t gi = 0; gi < HW_BEMF_GROUP_COUNT; gi++) {
        const hw_bemf_group_t *g = hw_bemf_group(gi);
        err = adc_oneshot_config_channel(s_adc, (adc_channel_t)g->adc_channel, &chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Gruppe %u (GPIO%u) liess sich nicht einrichten: %s", gi + 1, g->gpio,
                     esp_err_to_name(err));
            return err;
        }
    }

    /* Der ESP32 kennt nur die Geradenanpassung. */
    adc_cali_line_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali, &s_cali) == ESP_OK) {
        s_cali_ok = true;
    } else {
        ESP_LOGW(TAG, "Keine ADC-Kalibrierung verfuegbar, Rohwerte werden verwendet");
    }

    BaseType_t ok = xTaskCreate(bemf_task, "bemf", 3072, NULL, 6, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void bemf_set_threshold(uint8_t group, uint16_t mv, uint16_t hyst_mv)
{
    if (group >= HW_BEMF_GROUP_COUNT) {
        return;
    }
    s_groups[group].threshold_mv = mv;
    s_groups[group].hyst_mv = hyst_mv;
}

void bemf_set_endstop_cb(bemf_endstop_cb_t cb, void *ctx)
{
    s_endstop_ctx = ctx;
    s_endstop_cb = cb;
}

void bemf_set_sample_cb(bemf_sample_cb_t cb, void *ctx)
{
    s_sample_ctx = ctx;
    s_sample_cb = cb;
}

void bemf_arm(uint8_t group, bool armed)
{
    if (group >= HW_BEMF_GROUP_COUNT) {
        return;
    }
    s_groups[group].armed = armed;
    if (!armed) {
        s_groups[group].tripped = false;
    }
}

uint16_t bemf_mv(uint8_t group)
{
    if (group >= HW_BEMF_GROUP_COUNT) {
        return 0;
    }
    return s_groups[group].median_mv;
}
