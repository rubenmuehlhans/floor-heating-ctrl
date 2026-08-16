#include "app_control.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "bemf.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "roomctrl.h"
#include "schedule.h"
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
    bool req_force;    /* Notfahrt bis zum Anschlag statt auf eine Stellung */
    bool manual_hold;  /* die Regelung laesst diesen Kreis in Ruhe */

    /*
     * Schutzfahrt: 0 = keine, 1 = auf Anschlag auf, 2 = auf Anschlag zu,
     * 3 = zurueck auf die vorherige Stellung.
     */
    uint8_t seize_step;
    float seize_back;
    bool moved_since_seize;
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
static sched_weekly_t s_seize_sched;
static uint8_t s_seize_done;

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
        if (ch->reserved || ch->manual_hold || ch->seize_step != 0) {
            continue; /* Kalibrierung, Notstellung von Hand oder Schutzfahrt */
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

/* ------------------------------------------------------------------ */
/* Schutzfahrt                                                         */
/* ------------------------------------------------------------------ */

/*
 * Im Sommer stehen die Ventile ueber Monate geschlossen. Damit sie nicht
 * festsitzen, faehrt jeder Kanal einmal in der Woche auf Anschlag auf, wieder
 * zu und danach auf seine vorherige Stellung zurueck.
 *
 * Gefahren wird auf Anschlag, nicht auf eine Stellung: die Schutzfahrt soll
 * den ganzen Weg abdecken, und nebenbei ist die Stellung danach wieder
 * gesichert statt geschaetzt.
 *
 * Uebergangen wird, wer in der Zwischenzeit ohnehin gefahren ist -- ein Kanal,
 * der taeglich regelt, sitzt nicht fest, und die Fahrt kaeme dem Raum in die
 * Quere. Uebergangen wird auch, wer von Hand gehalten oder von der Messfahrt
 * belegt ist.
 */
static uint8_t seize_arm_locked(bool alle)
{
    uint8_t n = 0;
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        channel_rt_t *ch = &s_ch[i];
        if (ch->reserved || ch->manual_hold || ch->seize_step != 0) {
            continue;
        }
        if (!alle && ch->moved_since_seize) {
            continue;
        }
        ch->seize_step = 1;
        ch->seize_back = ch->valve.position_known ? ch->valve.position : 0.0f;
        n++;
    }
    /* Der Zaehler beginnt mit jedem Durchgang von vorn. */
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        s_ch[i].moved_since_seize = false;
    }
    s_seize_done = 0;
    return n;
}

/* Faellt der Termin? Gefragt wird die Uhr, nicht die Laufzeit. */
static void seize_check_due(void)
{
    if (s_cfg.seize_weekday < 0) {
        return;
    }
    time_t jetzt = time(NULL);
    if (jetzt < 1700000000) {
        return; /* Uhr nicht gestellt */
    }
    struct tm tm;
    localtime_r(&jetzt, &tm);
    if (!sched_weekly_due(&s_seize_sched, s_cfg.seize_weekday, s_cfg.seize_hour, tm.tm_wday,
                          tm.tm_hour, tm.tm_yday, tm.tm_year)) {
        return;
    }
    uint8_t n = seize_arm_locked(false);
    ESP_LOGI(TAG, "Schutzfahrt faellig, %u Kanaele vorgemerkt", n);
}

/* Arbeitet die vorgemerkten Kanaele ab, einen je Messgruppe. */
static void seize_serve(uint32_t t)
{
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        channel_rt_t *ch = &s_ch[i];
        if (ch->seize_step == 0 || ch->reserved) {
            continue;
        }
        if (ch->manual_hold) {
            /* Jemand hat den Kreis in der Zwischenzeit von Hand uebernommen.
             * Die Schutzfahrt tritt zurueck, statt gegen ihn anzufahren. */
            ch->seize_step = 0;
            ESP_LOGI(TAG, "CH%d Schutzfahrt entfaellt, Kreis ist von Hand gehalten", i + 1);
            continue;
        }
        if (valve_is_moving(&ch->valve) || group_busy(i + 1) || ch->req_valid) {
            continue;
        }
        switch (ch->seize_step) {
        case 1:
            valve_force(&ch->valve, true, t);
            ch->seize_step = 2;
            ESP_LOGI(TAG, "CH%d Schutzfahrt: auf", i + 1);
            break;
        case 2:
            valve_force(&ch->valve, false, t);
            ch->seize_step = 3;
            ESP_LOGI(TAG, "CH%d Schutzfahrt: zu", i + 1);
            break;
        default:
            /* Zurueck auf die Stellung von vorher. Die Regelung uebernimmt
             * beim naechsten Raumdurchlauf ohnehin wieder. */
            valve_goto(&ch->valve, ch->seize_back, 0.01f, t);
            ch->seize_step = 0;
            ch->moved_since_seize = false;
            s_seize_done++;
            ESP_LOGI(TAG, "CH%d Schutzfahrt beendet, zurueck auf %.0f%%", i + 1,
                     ch->seize_back * 100.0f);
            break;
        }
        apply_channel(i);
        s_revision++;
    }
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
                ch->moved_since_seize = true;
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
        bool started;
        if (ch->req_force) {
            valve_force(&ch->valve, ch->req_target >= 0.5f, t);
            started = true;
            ESP_LOGW(TAG, "CH%d Notfahrt %s", i + 1, ch->req_target >= 0.5f ? "auf" : "zu");
        } else {
            started = valve_goto(&ch->valve, ch->req_target, ch->req_min_delta, t);
        }
        ch->req_valid = false;
        ch->req_force = false;
        if (started) {
            ch->moved_since_seize = true;
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

        seize_check_due();
        serve_requests(t);
        seize_serve(t);

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
    sched_weekly_init(&s_seize_sched);

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

static void request(uint8_t channel, float target, float min_delta, bool force)
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
        ch->req_force = force;
        if (force) {
            /* Eine von Hand erzwungene Stellung soll stehen bleiben. Ohne
             * diesen Halt haette der naechste Regeldurchlauf sie nach
             * spaetestens einem Pruefintervall wieder verworfen. */
            ch->manual_hold = true;
        }
        s_revision++;
    }
    unlock();
}

void control_cmd_position(uint8_t channel, float position)
{
    request(channel, position, 0.005f, false);
}

void control_cmd_open(uint8_t channel)
{
    request(channel, 1.0f, 0.0f, true);
}

void control_cmd_close(uint8_t channel)
{
    request(channel, 0.0f, 0.0f, true);
}

void control_cmd_auto(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return;
    }
    lock();
    s_ch[channel - 1].manual_hold = false;
    s_revision++;
    unlock();
    ESP_LOGI(TAG, "CH%u wieder unter Regelung", channel);

    /* Den zugehoerigen Raum gleich neu bewerten, statt bis zum naechsten
     * Pruefintervall zu warten. */
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (cfg == NULL) {
        return;
    }
    cfg_copy(cfg);
    const cfg_room_t *r = cfg_room_of_channel(cfg, channel);
    uint8_t room_id = r ? r->id : 0;
    free(cfg);
    if (room_id) {
        control_room_check_now(room_id);
    }
}

void control_cmd_all(bool open)
{
    ESP_LOGW(TAG, "Notfahrt aller Kreise: %s", open ? "auf" : "zu");
    for (uint8_t n = 1; n <= HW_CHANNEL_COUNT; n++) {
        request(n, open ? 1.0f : 0.0f, 0.0f, true);
    }
}

void control_cmd_all_auto(void)
{
    for (uint8_t n = 1; n <= HW_CHANNEL_COUNT; n++) {
        control_cmd_auto(n);
    }
}

void control_cmd_stop(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return;
    }
    lock();
    channel_rt_t *ch = &s_ch[channel - 1];
    ch->req_valid = false;
    ch->req_force = false;
    ch->manual_hold = true; /* von Hand gestoppt bleibt von Hand gestoppt */
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

uint8_t control_seize_start(bool alle)
{
    lock();
    uint8_t n = seize_arm_locked(alle);
    unlock();
    ESP_LOGI(TAG, "Schutzfahrt von Hand angestossen, %u Kanaele", n);
    return n;
}

void control_seize_abort(void)
{
    lock();
    uint8_t offen = 0;
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        /* Eine angefangene Fahrt laeuft zu Ende -- ein Ventil auf halbem Weg
         * stehen zu lassen waere schlechter als die Fahrt abzuschliessen. */
        if (s_ch[i].seize_step != 0 && !valve_is_moving(&s_ch[i].valve)) {
            s_ch[i].seize_step = 0;
            offen++;
        }
    }
    unlock();
    ESP_LOGI(TAG, "Schutzfahrt abgebrochen, %u Kanaele gestrichen", offen);
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
        out->ch[i].manual_hold = rt->manual_hold;
        out->ch[i].request_open = rt->req_valid;
        out->ch[i].request_target = rt->req_target;
        out->ch[i].last_move_ms = rt->valve.last_move_ms;
        out->ch[i].last_stop_reason = rt->valve.last_stop_reason;
        out->ch[i].seize_step = rt->seize_step;
        out->ch[i].moved_since_seize = rt->moved_since_seize;
    }

    out->seize.weekday = s_cfg.seize_weekday;
    out->seize.hour = s_cfg.seize_hour;
    out->seize.done = s_seize_done;
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        if (s_ch[i].seize_step != 0) {
            out->seize.pending++;
            if (valve_is_moving(&s_ch[i].valve)) {
                out->seize.running = true;
            }
        }
    }
    out->seize.days_left = -1;
    time_t jetzt = time(NULL);
    if (jetzt >= 1700000000) {
        struct tm tm;
        localtime_r(&jetzt, &tm);
        out->seize.days_left =
            sched_weekly_days_left(&s_seize_sched, s_cfg.seize_weekday, s_cfg.seize_hour,
                                   tm.tm_wday, tm.tm_hour, tm.tm_yday, tm.tm_year);
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
