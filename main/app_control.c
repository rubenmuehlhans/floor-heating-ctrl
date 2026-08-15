#include "app_control.h"

#include <math.h>
#include <string.h>

#include "bemf.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "roomctrl.h"
#include "sr74hc595.h"

static const char *TAG = "ctl";

#define TICK_MS            50
#define POSITION_SAVE_MS   60000
#define FIRST_CHECK_DELAY_MS 50000 /* wie im ESPHome-Aufbau: erst einlaufen lassen */

typedef struct {
    valve_t valve;
    bool reserved;
    bool req_valid;
    float req_target;
    float req_min_delta;
} channel_rt_t;

typedef struct {
    bool valid;
    float temp_c;
    float humidity;
    uint8_t battery;
    uint32_t ts_ms;
} sensor_rt_t;

typedef struct {
    uint32_t last_check_ms;
    float target_position;
    uint16_t pending_mask; /* Kanaele, die noch angefahren werden muessen */
    sensor_rt_t sensor;
} room_rt_t;

static SemaphoreHandle_t s_mtx;
static app_config_t s_cfg;
static channel_rt_t s_ch[HW_CHANNEL_COUNT];
static room_rt_t s_room[CFG_MAX_ROOMS];
static uint32_t s_revision;
static bool s_positions_dirty;
static uint32_t s_positions_saved_ms;
static uint32_t s_boot_ms;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline void lock(void)
{
    xSemaphoreTakeRecursive(s_mtx, portMAX_DELAY);
}

static inline void unlock(void)
{
    xSemaphoreGiveRecursive(s_mtx);
}

/* ------------------------------------------------------------------ */
/* Ausgabestufe und Gruppensperre                                      */
/* ------------------------------------------------------------------ */

/* Schreibt den Wunschzustand eines Kanals nach aussen und schaltet die
 * Endlagenerkennung der Messgruppe passend scharf. */
static void apply_channel(uint8_t idx)
{
    uint8_t channel = idx + 1;
    valve_t *v = &s_ch[idx].valve;

    sr595_set_drive(channel, valve_drive(v));

    /* Die Ausloeseschwelle gilt je Messgruppe, kalibriert wird aber je Kanal.
     * Sie wird deshalb beim Anfahren auf den Wert des fahrenden Kanals
     * gesetzt - innerhalb einer Gruppe faehrt ohnehin immer nur einer. */
    uint8_t group = hw_group_of_channel(channel);
    const hw_bemf_group_t *g = hw_bemf_group(group);
    uint8_t moving_channel = 0;
    for (int i = 0; i < 2; i++) {
        uint8_t c = g->channels[i];
        if (c != 0 && valve_is_moving(&s_ch[c - 1].valve)) {
            moving_channel = c;
        }
    }
    if (moving_channel) {
        const cfg_channel_t *cc = &s_cfg.channels[moving_channel - 1];
        bemf_set_threshold(group, cc->bemf_mv, cc->bemf_hyst_mv);
    }
    bemf_arm(group, moving_channel != 0);
}

/* Faehrt in der Messgruppe bereits ein anderer Kanal? Waehrend einer Fahrt
 * liegt die gemessene Gegen-EMK beider Kanaele auf demselben Eingang und liesse
 * sich nicht mehr zuordnen. */
static bool group_busy(uint8_t channel)
{
    uint8_t group = hw_group_of_channel(channel);
    const hw_bemf_group_t *g = hw_bemf_group(group);
    for (int i = 0; i < 2; i++) {
        uint8_t c = g->channels[i];
        if (c == 0 || c == channel) {
            continue;
        }
        if (valve_is_moving(&s_ch[c - 1].valve) || s_ch[c - 1].reserved) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Konfiguration uebernehmen                                           */
/* ------------------------------------------------------------------ */

/*
 * app_config_t ist rund 1,5 kB gross. Auf einem Task-Stack hat es nichts
 * verloren - genau daran ist die Bedien-Task beim ersten Hardwaretest
 * gescheitert. Alle groesseren Kopien liegen deshalb auf dem Heap.
 */
static void load_config_locked(void)
{
    app_config_t *fresh = malloc(sizeof(*fresh));
    if (fresh == NULL) {
        ESP_LOGE(TAG, "Kein Speicher zum Einlesen der Konfiguration");
        return;
    }
    cfg_copy(fresh);

    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        const cfg_channel_t *c = &fresh->channels[i];
        valve_cfg_t vc = {
            .open_ms = c->open_ms,
            .close_ms = c->close_ms,
            .max_ms = c->max_ms,
            .blank_ms = c->blank_ms,
        };
        valve_set_cfg(&s_ch[i].valve, &vc);
    }

    /* Raumlaufzeitdaten anhand der Kennung uebernehmen, damit ein
     * umbenannter Raum seinen Messwert behaelt. */
    static room_rt_t merged[CFG_MAX_ROOMS]; /* nur unter dem Mutex benutzt */
    memset(merged, 0, sizeof(merged));
    for (int i = 0; i < fresh->room_count; i++) {
        for (int j = 0; j < s_cfg.room_count; j++) {
            if (s_cfg.rooms[j].id == fresh->rooms[i].id) {
                merged[i] = s_room[j];
                break;
            }
        }
    }
    memcpy(s_room, merged, sizeof(s_room));

    s_cfg = *fresh;
    free(fresh);
    s_revision++;
}

/* ------------------------------------------------------------------ */
/* Endlagenmeldung                                                     */
/* ------------------------------------------------------------------ */

static void on_endstop(uint8_t group, uint16_t mv, void *ctx)
{
    (void)ctx;
    const hw_bemf_group_t *g = hw_bemf_group(group);

    lock();
    uint32_t t = now_ms();
    for (int i = 0; i < 2; i++) {
        uint8_t c = g->channels[i];
        if (c == 0) {
            continue;
        }
        channel_rt_t *rt = &s_ch[c - 1];
        if (!valve_is_moving(&rt->valve)) {
            continue;
        }
        if (valve_endstop(&rt->valve, t)) {
            ESP_LOGI(TAG, "CH%u Endlage bei %u mV, Position %.2f", c, mv, rt->valve.position);
            apply_channel(c - 1);
            s_positions_dirty = true;
            s_revision++;
        }
    }
    unlock();
}

/* ------------------------------------------------------------------ */
/* Regelung                                                            */
/* ------------------------------------------------------------------ */

static void room_run_check(int ri, uint32_t t)
{
    const cfg_room_t *r = &s_cfg.rooms[ri];
    room_rt_t *rt = &s_room[ri];

    rt->last_check_ms = t;

    if (!r->mode_heat) {
        rt->pending_mask = 0;
        return;
    }
    if (!rt->sensor.valid) {
        ESP_LOGW(TAG, "%s: kein Messwert, Regelung ausgesetzt", r->name);
        rt->pending_mask = 0;
        return;
    }
    uint32_t age_s = (t - rt->sensor.ts_ms) / 1000;
    if (age_s > s_cfg.sensor_timeout_s) {
        ESP_LOGW(TAG, "%s: Messwert ist %u s alt, Regelung ausgesetzt", r->name, age_s);
        rt->pending_mask = 0;
        return;
    }

    float target = roomctrl_target_position(r->target_c, rt->sensor.temp_c, r->p_band_k, r->step);
    rt->target_position = target;

    uint16_t pending = 0;
    for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
        if (!(r->channel_mask & (1U << (n - 1)))) {
            continue;
        }
        channel_rt_t *ch = &s_ch[n - 1];
        if (ch->reserved) {
            continue;
        }
        if (!ch->valve.position_known ||
            roomctrl_needs_move(ch->valve.position, target, r->min_delta)) {
            pending |= 1U << (n - 1);
        }
    }
    rt->pending_mask = pending;

    ESP_LOGI(TAG, "%s: ist %.2f, soll %.2f, Zielstellung %.0f%%%s", r->name, rt->sensor.temp_c,
             r->target_c, target * 100.0f, pending ? "" : " (keine Fahrt noetig)");
}

/* Versucht, offene Fahrbefehle zu starten - sowohl aus der Regelung als auch
 * aus der Handbedienung. */
static void serve_requests(uint32_t t)
{
    /* Regelung: pro Raum die noch offenen Kanaele. */
    for (int ri = 0; ri < s_cfg.room_count; ri++) {
        room_rt_t *rt = &s_room[ri];
        if (rt->pending_mask == 0) {
            continue;
        }
        const cfg_room_t *r = &s_cfg.rooms[ri];

        for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
            uint16_t bit = 1U << (n - 1);
            if (!(rt->pending_mask & bit)) {
                continue;
            }
            channel_rt_t *ch = &s_ch[n - 1];
            if (ch->reserved || ch->req_valid) {
                rt->pending_mask &= (uint16_t)~bit;
                continue;
            }
            if (valve_is_moving(&ch->valve) || group_busy(n)) {
                continue; /* naechster Durchlauf */
            }
            if (valve_goto(&ch->valve, rt->target_position, r->min_delta, t)) {
                ESP_LOGI(TAG, "CH%d faehrt auf %.0f%%", n, rt->target_position * 100.0f);
                apply_channel(n - 1);
                s_revision++;
            }
            rt->pending_mask &= (uint16_t)~bit;
        }
    }

    /* Handbedienung. */
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        channel_rt_t *ch = &s_ch[i];
        if (!ch->req_valid || ch->reserved) {
            continue;
        }
        if (valve_is_moving(&ch->valve) || group_busy(i + 1)) {
            continue;
        }
        bool started = valve_goto(&ch->valve, ch->req_target, ch->req_min_delta, t);
        ch->req_valid = false;
        if (started) {
            apply_channel(i);
            s_revision++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Steuertask                                                          */
/* ------------------------------------------------------------------ */

static void control_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        uint32_t t = now_ms();

        lock();

        for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
            if (valve_tick(&s_ch[i].valve, t)) {
                apply_channel(i);
                s_positions_dirty = true;
                s_revision++;
            }
        }

        for (int ri = 0; ri < s_cfg.room_count; ri++) {
            room_rt_t *rt = &s_room[ri];
            uint32_t interval_ms = (uint32_t)s_cfg.rooms[ri].interval_s * 1000;
            if (rt->last_check_ms == 0) {
                /* Erster Durchlauf erst nach der Anlaufzeit. */
                if (t - s_boot_ms >= FIRST_CHECK_DELAY_MS) {
                    room_run_check(ri, t);
                }
            } else if (t - rt->last_check_ms >= interval_ms) {
                room_run_check(ri, t);
            }
        }

        serve_requests(t);

        if (s_positions_dirty && (t - s_positions_saved_ms) >= POSITION_SAVE_MS) {
            cfg_positions_t pos;
            for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
                pos.position[i] = s_ch[i].valve.position;
                pos.known[i] = s_ch[i].valve.position_known;
            }
            unlock();
            cfg_save_positions(&pos);
            lock();
            s_positions_dirty = false;
            s_positions_saved_ms = t;
        }

        unlock();

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t control_start(void)
{
    s_mtx = xSemaphoreCreateRecursiveMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_boot_ms = now_ms();
    s_positions_saved_ms = s_boot_ms;

    valve_cfg_t vc = {.open_ms = 39000, .close_ms = 40000, .max_ms = 45000, .blank_ms = 2000};
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        valve_init(&s_ch[i].valve, &vc);
    }

    lock();
    memset(&s_cfg, 0, sizeof(s_cfg));
    load_config_locked();
    unlock();

    cfg_positions_t pos;
    if (cfg_load_positions(&pos) == ESP_OK) {
        int restored = 0;
        for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
            if (pos.known[i]) {
                valve_restore(&s_ch[i].valve, pos.position[i]);
                restored++;
            }
        }
        ESP_LOGI(TAG, "%d Ventilstellungen aus dem Speicher uebernommen", restored);
    } else {
        ESP_LOGW(TAG, "Keine Ventilstellungen gespeichert, Kanaele fahren einmal auf Anschlag");
    }

    bemf_set_endstop_cb(on_endstop, NULL);

    BaseType_t ok = xTaskCreate(control_task, "control", 4096, NULL, 7, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

/* ------------------------------------------------------------------ */
/* Schnittstelle nach aussen                                           */
/* ------------------------------------------------------------------ */

static void request(uint8_t channel, float target, float min_delta)
{
    if (!hw_channel_valid(channel)) {
        return;
    }
    lock();
    channel_rt_t *ch = &s_ch[channel - 1];
    if (!ch->reserved) {
        ch->req_valid = true;
        ch->req_target = target;
        ch->req_min_delta = min_delta;
        s_revision++;
    }
    unlock();
}

void control_cmd_position(uint8_t channel, float position)
{
    request(channel, position, 0.005f);
}

void control_cmd_open(uint8_t channel)
{
    request(channel, 1.0f, 0.0f);
}

void control_cmd_close(uint8_t channel)
{
    request(channel, 0.0f, 0.0f);
}

void control_cmd_stop(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return;
    }
    lock();
    channel_rt_t *ch = &s_ch[channel - 1];
    ch->req_valid = false;
    valve_stop(&ch->valve, now_ms());
    apply_channel(channel - 1);
    s_positions_dirty = true;
    s_revision++;
    unlock();
}

static int room_index(uint8_t room_id)
{
    for (int i = 0; i < s_cfg.room_count; i++) {
        if (s_cfg.rooms[i].id == room_id) {
            return i;
        }
    }
    return -1;
}

/* Aendert einen Raum in der gespeicherten Konfiguration und uebernimmt ihn. */
static void room_patch(uint8_t room_id, float target_c, const bool *mode_heat)
{
    app_config_t *next = malloc(sizeof(*next));
    if (next == NULL) {
        ESP_LOGE(TAG, "Kein Speicher fuer die Raumaenderung");
        return;
    }
    cfg_copy(next);

    cfg_room_t *r = NULL;
    for (int i = 0; i < next->room_count; i++) {
        if (next->rooms[i].id == room_id) {
            r = &next->rooms[i];
            break;
        }
    }
    if (r == NULL) {
        free(next);
        return;
    }
    if (!isnan(target_c)) {
        r->target_c = target_c;
    }
    if (mode_heat) {
        r->mode_heat = *mode_heat;
    }

    char err[128];
    esp_err_t rc = cfg_set(next, err, sizeof(err));
    free(next);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Raumaenderung abgelehnt: %s", err);
        return;
    }
    control_config_changed();
    control_room_check_now(room_id);
}

void control_room_set_target(uint8_t room_id, float target_c)
{
    room_patch(room_id, target_c, NULL);
}

void control_room_set_mode(uint8_t room_id, bool heat)
{
    room_patch(room_id, NAN, &heat);
}

void control_room_check_now(uint8_t room_id)
{
    lock();
    int ri = room_index(room_id);
    if (ri >= 0) {
        room_run_check(ri, now_ms());
        s_revision++;
    }
    unlock();
}

void control_config_changed(void)
{
    lock();
    load_config_locked();
    unlock();
}

void control_temperature(const uint8_t mac[6], float temp_c, float humidity, uint8_t battery)
{
    lock();
    for (int i = 0; i < s_cfg.room_count; i++) {
        const cfg_room_t *r = &s_cfg.rooms[i];
        if (!r->sensor_set || memcmp(r->sensor_mac, mac, 6) != 0) {
            continue;
        }
        room_rt_t *rt = &s_room[i];
        bool first = !rt->sensor.valid;
        rt->sensor.valid = true;
        rt->sensor.temp_c = temp_c;
        rt->sensor.humidity = humidity;
        rt->sensor.battery = battery;
        rt->sensor.ts_ms = now_ms();
        s_revision++;
        if (first) {
            ESP_LOGI(TAG, "%s: erster Messwert %.2f Grad", r->name, temp_c);
        }
    }
    unlock();
}

bool control_reserve(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return false;
    }
    bool ok = false;
    lock();
    channel_rt_t *ch = &s_ch[channel - 1];
    if (!ch->reserved && !valve_is_moving(&ch->valve) && !group_busy(channel)) {
        ch->reserved = true;
        ch->req_valid = false;
        ok = true;
        s_revision++;
    }
    unlock();
    return ok;
}

void control_release(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return;
    }
    lock();
    channel_rt_t *ch = &s_ch[channel - 1];
    ch->reserved = false;
    valve_stop(&ch->valve, now_ms());
    apply_channel(channel - 1);
    s_positions_dirty = true;
    s_revision++;
    unlock();
}

bool control_raw_drive(uint8_t channel, hw_drive_t drive)
{
    if (!hw_channel_valid(channel)) {
        return false;
    }
    bool ok = false;
    lock();
    if (s_ch[channel - 1].reserved) {
        ok = sr595_set_drive(channel, drive) == ESP_OK;
        s_revision++;
    }
    unlock();
    return ok;
}

bool control_busy(void)
{
    bool busy = false;
    lock();
    for (int i = 0; i < HW_CHANNEL_COUNT && !busy; i++) {
        busy = valve_is_moving(&s_ch[i].valve) || s_ch[i].reserved || s_ch[i].req_valid;
    }
    unlock();
    return busy;
}

uint32_t control_revision(void)
{
    return s_revision;
}

void control_snapshot(ctl_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    lock();
    uint32_t t = now_ms();

    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        const channel_rt_t *rt = &s_ch[i];
        out->ch[i].position = rt->valve.position;
        out->ch[i].position_known = rt->valve.position_known;
        out->ch[i].op = rt->valve.op;
        out->ch[i].group = hw_group_of_channel(i + 1);
        out->ch[i].reserved = rt->reserved;
        out->ch[i].request_open = rt->req_valid;
        out->ch[i].request_target = rt->req_target;
        out->ch[i].last_move_ms = rt->valve.last_move_ms;
        out->ch[i].last_stop_reason = rt->valve.last_stop_reason;
    }

    out->room_count = s_cfg.room_count;
    for (int i = 0; i < s_cfg.room_count; i++) {
        const cfg_room_t *r = &s_cfg.rooms[i];
        const room_rt_t *rt = &s_room[i];
        ctl_room_t *o = &out->rooms[i];
        o->id = r->id;
        snprintf(o->name, sizeof(o->name), "%s", r->name);
        o->channel_mask = r->channel_mask;
        o->mode_heat = r->mode_heat;
        o->target_c = r->target_c;
        o->sensor_set = r->sensor_set;
        o->temp_c = rt->sensor.temp_c;
        o->humidity = rt->sensor.humidity;
        o->battery = rt->sensor.battery;
        o->temp_age_s = rt->sensor.valid ? (t - rt->sensor.ts_ms) / 1000 : 0;
        o->temp_valid = rt->sensor.valid && o->temp_age_s <= s_cfg.sensor_timeout_s;
        o->target_position = rt->target_position;
        uint32_t interval_ms = (uint32_t)r->interval_s * 1000;
        uint32_t elapsed = rt->last_check_ms ? (t - rt->last_check_ms) : 0;
        o->next_check_s = elapsed >= interval_ms ? 0 : (interval_ms - elapsed) / 1000;
    }

    for (int g = 0; g < HW_BEMF_GROUP_COUNT; g++) {
        out->bemf_mv[g] = bemf_mv(g);
    }
    out->uptime_s = (t - s_boot_ms) / 1000;

    unlock();
}
