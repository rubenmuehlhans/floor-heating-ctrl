#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_control.h"
#include "config_store.h"
#include "driver/touch_sens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "sensors_local.h"
#include "ssd1327.h"

static const char *TAG = "ui";

#define TICK_MS        50
#define DRAW_PERIOD_MS 1000
#define SETPOINT_STEP  0.5f

/* Graustufen der Anzeige. Der Baustein kann 16 Stufen; mehr als drei braucht
 * die Darstellung nicht. */
#define GRAY_OFF 0
#define GRAY_DIM 4
#define GRAY_ON  15

/* Reihenfolge wie in hw_map: herunter, herauf, auswaehlen. */
static const int s_touch_channel[HW_TOUCH_COUNT] = {0, 2, 3}; /* GPIO4, GPIO2, GPIO15 */

static touch_sensor_handle_t s_touch;
static touch_channel_handle_t s_chan[HW_TOUCH_COUNT];
static uint32_t s_raw[HW_TOUCH_COUNT];
static bool s_pressed[HW_TOUCH_COUNT];
/* Eine Taste zaehlt erst, nachdem sie einmal sauber als losgelassen gemessen
 * wurde. Direkt nach dem Einschalten hat der Treiber seinen Bezugswert noch
 * nicht gebildet und meldet Werte unterhalb der Schwelle - beim ersten
 * Hardwaretest hat das sofort einen Sollwert verstellt. */
static bool s_armed[HW_TOUCH_COUNT];
static uint32_t s_touch_started_ms;
#define TOUCH_SETTLE_MS 2000

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint8_t s_selected; /* Index in die Raumliste */
static bool s_config_dirty;

/* ------------------------------------------------------------------ */
/* Anzeige                                                             */
/* ------------------------------------------------------------------ */

/* Balken eines Kanals: Rahmen ueber die volle Hoehe, Fuellung von unten. */
static void draw_channel_bar(int x, int y, int w, int h, float position, bool moving)
{
    ssd1327_rect(x, y, w, h, moving ? GRAY_ON : GRAY_DIM);
    int fill = (int)(position * (h - 2) + 0.5f);
    if (fill > 0) {
        ssd1327_fill_rect(x + 1, y + h - 1 - fill, w - 2, fill, GRAY_ON);
    }
}

/* Kuerzt einen Namen auf die verfuegbare Breite. */
static void fit_text(char *dst, size_t dst_len, const char *src, int max_px)
{
    int max_chars = max_px / 6;
    if (max_chars < 1) {
        max_chars = 1;
    }
    size_t n = 0, chars = 0;
    while (src[n] && (int)chars < max_chars) {
        unsigned char c = (unsigned char)src[n];
        size_t step = (c < 0x80) ? 1 : 2;
        if (n + step >= dst_len) {
            break;
        }
        memcpy(dst + n, src + n, step);
        n += step;
        chars++;
    }
    dst[n < dst_len ? n : dst_len - 1] = '\0';
}

static void draw_overview(const ctl_snapshot_t *snap)
{
    if (snap->room_count == 0) {
        ssd1327_text(4, 24, "Kein Raum", GRAY_ON, 2);
        ssd1327_text(4, 48, "eingerichtet.", GRAY_ON, 1);
        ssd1327_text(4, 62, "Bitte ueber die", GRAY_DIM, 1);
        ssd1327_text(4, 72, "Weboberflaeche", GRAY_DIM, 1);
        ssd1327_text(4, 82, "anlegen.", GRAY_DIM, 1);
        return;
    }

    int col_w = SSD1327_WIDTH / snap->room_count;

    for (int i = 0; i < snap->room_count; i++) {
        const ctl_room_t *r = &snap->rooms[i];
        int x0 = i * col_w;

        /* Kanalbalken, gleichmaessig ueber die Spalte verteilt. */
        int count = 0;
        for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
            if (r->channel_mask & (1U << (n - 1))) {
                count++;
            }
        }
        if (count > 0) {
            int gap = 2;
            int bar_w = (col_w - 4 - (count - 1) * gap) / count;
            if (bar_w < 3) {
                bar_w = 3;
            }
            int bx = x0 + 2;
            for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
                if (!(r->channel_mask & (1U << (n - 1)))) {
                    continue;
                }
                const ctl_channel_t *c = &snap->ch[n - 1];
                draw_channel_bar(bx, 2, bar_w, 34, c->position, c->op != VALVE_IDLE);
                bx += bar_w + gap;
            }
        }

        /* Auswahlmarke: der gewaehlte Raum bekommt den dickeren Streifen. */
        if (i == s_selected) {
            ssd1327_fill_rect(x0 + 1, 38, col_w - 2, 5, GRAY_ON);
        } else {
            ssd1327_fill_rect(x0 + 1, 40, col_w - 2, 2, GRAY_DIM);
        }

        char name[24];
        fit_text(name, sizeof(name), r->name, col_w - 2);
        ssd1327_text(x0 + 1, 45, name, i == s_selected ? GRAY_ON : GRAY_DIM, 1);

        char val[12];
        if (r->temp_valid) {
            snprintf(val, sizeof(val), "%.1f", r->temp_c);
        } else {
            snprintf(val, sizeof(val), "--");
        }
        ssd1327_text(x0 + 1, 55, val, GRAY_ON, 1);
    }
}

static void draw_detail(const ctl_snapshot_t *snap)
{
    ssd1327_fill_rect(0, 66, SSD1327_WIDTH, 1, GRAY_DIM);

    if (snap->room_count == 0) {
        return;
    }
    const ctl_room_t *r = &snap->rooms[s_selected];

    char line[32];
    fit_text(line, sizeof(line), r->name, SSD1327_WIDTH - 4);
    ssd1327_text(2, 70, line, GRAY_ON, 1);

    if (r->temp_valid) {
        snprintf(line, sizeof(line), "%.1f", r->temp_c);
    } else {
        snprintf(line, sizeof(line), "--.-");
    }
    ssd1327_text(2, 82, line, GRAY_ON, 2);
    ssd1327_text(2 + ssd1327_text_width(line, 2) + 2, 90, "\xC2\xB0" "C", GRAY_DIM, 1);

    snprintf(line, sizeof(line), "Soll %.1f", r->target_c);
    ssd1327_text(2, 102, line, GRAY_ON, 1);

    if (!r->mode_heat) {
        ssd1327_text(2, 112, "Regelung aus", GRAY_DIM, 1);
    } else if (!r->sensor_set) {
        ssd1327_text(2, 112, "kein Fuehler", GRAY_DIM, 1);
    } else if (!r->temp_valid) {
        ssd1327_text(2, 112, "Messwert alt", GRAY_DIM, 1);
    } else {
        snprintf(line, sizeof(line), "Ventil %.0f%%  in %lus", r->target_position * 100.0f,
                 (unsigned long)r->next_check_s);
        ssd1327_text(2, 112, line, GRAY_DIM, 1);
    }

    /* Vorlauftemperatur des ersten Fuehlers, sofern vorhanden. */
    static sensors_snapshot_t sens; /* nur aus der ui-Task benutzt */
    sensors_get(&sens);
    for (int i = 0; i < sens.ds_count; i++) {
        if (!sens.ds[i].valid) {
            continue;
        }
        snprintf(line, sizeof(line), "VL %.1f", sens.ds[i].temp_c);
        ssd1327_text(SSD1327_WIDTH - ssd1327_text_width(line, 1) - 2, 70, line, GRAY_DIM, 1);
        break;
    }
}

static void draw(void)
{
    /* Über 1 kB gross - gehoert nicht auf den Stack. Die Anzeige laeuft
     * ausschliesslich in der ui-Task, ein statischer Puffer genuegt. */
    static ctl_snapshot_t snap;
    control_snapshot(&snap);

    if (snap.room_count > 0 && s_selected >= snap.room_count) {
        s_selected = 0;
    }

    ssd1327_clear(GRAY_OFF);
    draw_overview(&snap);
    draw_detail(&snap);
    ssd1327_flush();
}

/* ------------------------------------------------------------------ */
/* Tasten                                                              */
/* ------------------------------------------------------------------ */

static void adjust_setpoint(float delta)
{
    static ctl_snapshot_t snap;
    control_snapshot(&snap);
    if (snap.room_count == 0 || s_selected >= snap.room_count) {
        return;
    }
    const ctl_room_t *r = &snap.rooms[s_selected];
    float target = r->target_c + delta;
    if (target < 5.0f) {
        target = 5.0f;
    }
    if (target > 35.0f) {
        target = 35.0f;
    }
    ESP_LOGI(TAG, "%s: Sollwert von Hand auf %.1f Grad", r->name, target);
    control_room_set_target(r->id, target);
}

static void on_press(int index)
{
    static ctl_snapshot_t snap;
    switch (index) {
    case 0:
        adjust_setpoint(-SETPOINT_STEP);
        break;
    case 1:
        adjust_setpoint(+SETPOINT_STEP);
        break;
    case 2:
        control_snapshot(&snap);
        if (snap.room_count > 0) {
            s_selected = (uint8_t)((s_selected + 1) % snap.room_count);
        }
        break;
    default:
        break;
    }
}

static esp_err_t touch_init(const app_config_t *cfg)
{
    touch_sensor_sample_config_t sample =
        TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(1, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V7);
    touch_sensor_config_t ctrl = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, &sample);

    esp_err_t err = touch_sensor_new_controller(&ctrl, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Tastenauswertung nicht startbar: %s", esp_err_to_name(err));
        return err;
    }

    touch_sensor_filter_config_t filter = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    touch_sensor_config_filter(s_touch, &filter);

    for (int i = 0; i < HW_TOUCH_COUNT; i++) {
        touch_channel_config_t chan = {
            .abs_active_thresh = {cfg->touch_thresh[i]},
            .charge_speed = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
            .group = TOUCH_CHAN_TRIG_GROUP_BOTH,
        };
        err = touch_sensor_new_channel(s_touch, s_touch_channel[i], &chan, &s_chan[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Taste %d liess sich nicht einrichten: %s", i + 1, esp_err_to_name(err));
            return err;
        }
    }

    touch_sensor_enable(s_touch);
    touch_sensor_start_continuous_scanning(s_touch);
    s_touch_started_ms = now_ms();
    ESP_LOGI(TAG, "Tastenauswertung laeuft");
    return ESP_OK;
}

/*
 * Ausgewertet wird durch Abfrage statt ueber die Ereignisse des Treibers: so
 * liegen die Rohwerte fuer den Abgleich in der Oberflaeche ohnehin vor, und die
 * Auswertung laeuft ausserhalb des Interrupts.
 */
static void poll_touch(const app_config_t *cfg)
{
    bool settled = (now_ms() - s_touch_started_ms) >= TOUCH_SETTLE_MS;

    for (int i = 0; i < HW_TOUCH_COUNT; i++) {
        if (s_chan[i] == NULL) {
            continue;
        }
        uint32_t value = 0;
        if (touch_channel_read_data(s_chan[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, &value) != ESP_OK) {
            continue;
        }
        s_raw[i] = value;

        /* Beim ESP32 sinkt der Messwert bei Beruehrung. */
        bool now_pressed = value > 0 && value < cfg->touch_thresh[i];

        if (!s_armed[i]) {
            /* Erst wenn die Taste einmal eindeutig losgelassen war, wird sie
             * scharf. Damit fallen weder der unruhige Anlauf noch eine zu hoch
             * eingestellte Schwelle als Dauerdruck ins Gewicht. */
            if (settled && !now_pressed) {
                s_armed[i] = true;
                ESP_LOGD(TAG, "Taste %d scharf, Ruhewert %lu", i + 1, (unsigned long)value);
            }
            s_pressed[i] = now_pressed;
            continue;
        }

        if (now_pressed && !s_pressed[i]) {
            on_press(i);
        }
        s_pressed[i] = now_pressed;
    }
}

/* ------------------------------------------------------------------ */

static void ui_task(void *arg)
{
    (void)arg;

    static app_config_t cfg; /* rund 1,5 kB, nicht auf den Stack */
    cfg_copy(&cfg);

    bool display_ok = ssd1327_init() == ESP_OK;
    if (display_ok) {
        ssd1327_set_brightness(cfg.display_brightness);
    }

    bool touch_ok = false;
    if (cfg.touch_enabled) {
        touch_ok = touch_init(&cfg) == ESP_OK;
    }

    if (!display_ok && !touch_ok) {
        ESP_LOGW(TAG, "Weder Anzeige noch Tasten vorhanden, Bedienung am Geraet entfaellt");
        vTaskDelete(NULL);
        return;
    }

    uint32_t since_draw = DRAW_PERIOD_MS;

    for (;;) {
        if (s_config_dirty) {
            s_config_dirty = false;
            cfg_copy(&cfg);
            if (display_ok) {
                ssd1327_set_brightness(cfg.display_brightness);
            }
        }

        if (touch_ok) {
            poll_touch(&cfg);
        }

        since_draw += TICK_MS;
        if (display_ok && since_draw >= DRAW_PERIOD_MS) {
            since_draw = 0;
            draw();
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t ui_start(void)
{
    BaseType_t ok = xTaskCreate(ui_task, "ui", 6144, NULL, 3, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void ui_touch_raw(uint32_t out[HW_TOUCH_COUNT])
{
    memcpy(out, s_raw, sizeof(s_raw));
}

void ui_config_changed(void)
{
    s_config_dirty = true;
}
