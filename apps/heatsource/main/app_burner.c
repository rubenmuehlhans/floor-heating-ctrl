#include "app_burner.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_remote.h"
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
static charge_cfg_t s_ccfg;
static charge_state_t s_cst;
static bool s_kessel_remote;
static stats_t s_stats;
static SemaphoreHandle_t s_mtx;
static volatile bool s_cfg_dirty;

/* Aufzeichnung. Belegt werden nur die beim Start zugeordneten Messstellen:
 * am Kessel sind das drei, am Speicher sechs -- statt aller vierzehn Rollen. */
static int16_t *s_rec;
static probe_role_t s_rec_role[ROLE_COUNT];
static uint8_t s_rec_cols;
static uint16_t s_rec_len;
static uint32_t s_rec_started_epoch;
static uint32_t s_rec_last_ms;

/* Wann begonnen und wann beendet wird, steht in components/heatlogic und ist
 * dort ohne Hardware geprueft. */
static rec_trigger_t s_trig;
static rec_trig_cfg_t s_tcfg;
static rec_input_t s_tin;       /* letzte Zeichen: Brenner und Speicher */

/* Brennerzustand, wie er tatsaechlich gilt: eigener Abgasfuehler, sonst das
 * Geraet am Kessel. */
static bool s_eff_known, s_eff_running, s_eff_remote;

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
    rec_trigger_init(&s_trig);
    xSemaphoreGive(s_mtx);
}

/*
 * Belegt den Puffer und legt die Spalten fest: aufgezeichnet wird nur, was
 * gerade einen Messwert liefert. Der Speicher wird schon beim Scharfschalten
 * geholt, damit ein Fehlschlag sofort auffaellt und nicht erst dann, wenn der
 * Brenner anspringt.
 */
static esp_err_t rec_alloc(void)
{
    probe_role_t rollen[ROLE_COUNT] = {0};
    uint8_t n = 0;
    for (int r = 1; r < ROLE_COUNT; r++) {
        float wert = 0.0f;
        if (any_role_value((probe_role_t)r, &wert, NULL)) {
            rollen[n++] = (probe_role_t)r;
        }
    }
    if (n == 0) {
        ESP_LOGW(TAG, "Keine zugeordnete Messstelle, nichts aufzuzeichnen");
        return ESP_ERR_NOT_FOUND;
    }

    int16_t *puffer = calloc((size_t)REC_SLOTS * n, sizeof(int16_t));
    if (puffer == NULL) {
        ESP_LOGW(TAG, "Kein Speicher fuer die Aufzeichnung");
        return ESP_ERR_NO_MEM;
    }

    /* Ein vorheriger Lauf wird verworfen; sonst haetten zwei Aufzeichnungen
     * unterschiedliche Spalten im selben Puffer. */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    free(s_rec);
    s_rec = puffer;
    memcpy(s_rec_role, rollen, sizeof(rollen));
    s_rec_cols = n;
    s_rec_len = 0;
    s_rec_last_ms = 0;
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

/* Setzt die eben begonnene Aufzeichnung auf null. Der Aufrufer haelt die Sperre. */
static void rec_begin_locked(void)
{
    s_rec_len = 0;
    s_rec_last_ms = 0;
    time_t jetzt = time(NULL);
    s_rec_started_epoch = jetzt > 1700000000 ? (uint32_t)jetzt : 0;
}

esp_err_t rec_start(void)
{
    esp_err_t rc = rec_alloc();
    if (rc != ESP_OK) {
        return rc;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    rec_trigger_manual(&s_trig);
    rec_begin_locked();
    uint8_t n = s_rec_cols;
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "Aufzeichnung gestartet, %u Messstellen, %u Byte", (unsigned)n,
             (unsigned)((size_t)REC_SLOTS * n * sizeof(int16_t)));
    return ESP_OK;
}

esp_err_t rec_arm(void)
{
    esp_err_t rc = rec_alloc();
    if (rc != ESP_OK) {
        return rc;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    rec_trigger_arm(&s_trig, &s_tin, now_ms());
    s_rec_started_epoch = 0;
    bool warten = s_trig.wait_off;
    rec_source_t quelle = s_trig.source;
    uint8_t n = s_rec_cols;
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "Aufzeichnung scharf, %u Messstellen, Zeichen: %s%s", (unsigned)n,
             quelle == REC_SRC_BURNER ? "Brennerzustand" : "Anstieg der Speichertemperatur",
             warten ? " -- der Brenner laeuft noch, gewartet wird auf den naechsten Start" : "");
    return ESP_OK;
}

/* Woran sich der Beginn einer Ladung hier erkennen liesse. */
rec_source_t rec_available_source(void)
{
    if (s_mtx == NULL) {
        return REC_SRC_NONE;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    rec_source_t q = rec_trigger_source(&s_tin);
    xSemaphoreGive(s_mtx);
    return q;
}

void rec_stop(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool war_scharf = s_trig.phase == REC_TRIG_ARMED;
    bool lief = s_trig.phase == REC_TRIG_RUN;
    rec_trigger_stop(&s_trig);
    if (war_scharf) {
        /* Aufgezeichnet wurde noch nichts, der Speicher wird gleich frei. */
        free(s_rec);
        s_rec = NULL;
        s_rec_cols = 0;
        ESP_LOGI(TAG, "Aufzeichnung abgebrochen");
    } else if (lief) {
        ESP_LOGI(TAG, "Aufzeichnung beendet, %u Zeilen", (unsigned)s_rec_len);
    }
    xSemaphoreGive(s_mtx);
}

/*
 * Uebergaenge der selbsttaetigen Aufzeichnung, einmal je Sekunde.
 *
 * Der Brennerzustand kommt vom eigenen Abgasfuehler oder, wenn dieses Geraet
 * keinen hat, vom Nachbargeraet -- das Geraet am Pufferspeicher zeichnet so
 * auf, obwohl der Brenner in einem anderen Raum steht. Steht auch von dort
 * nichts an, dient die steigende Speichertemperatur als Ersatz.
 */
static void rec_tick(const rec_input_t *in, uint32_t t)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_tin = *in;
    rec_trig_phase_t vorher = s_trig.phase;

    if (rec_trigger_tick(&s_trig, in, &s_tcfg, t)) {
        rec_begin_locked();
        ESP_LOGI(TAG, "%s, Aufzeichnung beginnt",
                 s_trig.source == REC_SRC_BURNER ? "Brenner angelaufen"
                                                 : "Speichertemperatur steigt");
    } else if (vorher == REC_TRIG_RUN && s_trig.phase == REC_TRIG_DONE) {
        ESP_LOGI(TAG, "Ladung durch, Aufzeichnung beendet, %u Zeilen", (unsigned)s_rec_len);
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
    out->state = s_trig.phase == REC_TRIG_ARMED ? REC_ARMED
                 : s_trig.phase == REC_TRIG_RUN ? REC_RUNNING
                 : s_trig.phase == REC_TRIG_DONE ? REC_DONE : REC_IDLE;
    out->samples = s_rec_len;
    out->period_s = REC_PERIOD_S;
    out->started_epoch = s_rec_started_epoch;
    out->cols = s_rec_cols;
    memcpy(out->role, s_rec_role, sizeof(out->role));
    out->bytes = s_rec ? (uint32_t)REC_SLOTS * s_rec_cols * sizeof(int16_t) : 0;
    out->automatic = s_trig.automatic;
    out->wait_off = s_trig.wait_off;
    out->tail = s_trig.tail;
    out->tail_left_s = (uint16_t)rec_trigger_tail_left_s(&s_trig, s_tcfg.tail_s, now_ms());
    out->source = s_trig.phase == REC_TRIG_OFF ? rec_trigger_source(&s_tin) : s_trig.source;
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
    if (s_rec == NULL || s_trig.phase != REC_TRIG_RUN || s_rec_len >= REC_SLOTS) {
        if (s_trig.phase == REC_TRIG_RUN && s_rec_len >= REC_SLOTS) {
            rec_trigger_full(&s_trig);
            ESP_LOGI(TAG, "Aufzeichnung voll, beendet");
        }
        return;
    }
    int16_t *row = &s_rec[(size_t)s_rec_len * s_rec_cols];
    for (uint8_t c = 0; c < s_rec_cols; c++) {
        row[c] = SENS_HIST_NONE;
        /* Erst der eigene Fuehler, dann der des Nachbargeraets: eine
         * Aufzeichnung soll die ganze Anlage zeigen, nicht nur die Haelfte,
         * die an diesem Geraet haengt. */
        float wert = 0.0f;
        if (any_role_value(s_rec_role[c], &wert, NULL)) {
            float v = wert * 10.0f;
            if (v <= 32000.0f && v >= -32000.0f) {
                row[c] = (int16_t)lroundf(v);
            }
        }
    }
    (void)snap;
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
    s_ccfg.spread_full_k = cfg.buffer.spread_full_k;
    s_ccfg.spread_hold_s = cfg.buffer.spread_hold_s;
    s_ccfg.voll_c = cfg.buffer.voll_c;
    s_ccfg.leer_c = cfg.buffer.leer_c;
    s_ccfg.warn_c = cfg.buffer.warn_c;
    s_ccfg.kessel_hot_c = cfg.buffer.kessel_hot_c;
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
    bool brenner_lief = false;
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

        /*
         * Ladezustand. Die Kesselwerte kommen vom eigenen Fuehler, wenn dieses
         * Geraet am Kessel sitzt, sonst vom Nachbargeraet. Der Brennerzustand
         * ebenso.
         */
        charge_input_t cin = {0};
        bool eigen_vl = false, eigen_rl = false;
        bool vl = any_role_value(ROLE_KESSEL_VL, &cin.kessel_vl_c, &eigen_vl);
        bool rl = any_role_value(ROLE_KESSEL_RL, &cin.kessel_rl_c, &eigen_rl);
        cin.kessel_valid = vl && rl;
        cin.puffer_valid = sensors_role_value(ROLE_PUFFER, &cin.puffer_c, NULL);

        if (gueltig) {
            cin.burner_known = s_st.known;
            cin.burner_running = s_st.running;
        } else {
            bool laeuft = false;
            cin.burner_known = remote_burner(&laeuft);
            cin.burner_running = laeuft;
        }
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_eff_known = cin.burner_known;
        s_eff_running = cin.burner_running;
        s_eff_remote = cin.burner_known && !gueltig;
        xSemaphoreGive(s_mtx);

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_kessel_remote = cin.kessel_valid && !(eigen_vl && eigen_rl);
        charge_tick(&s_cst, &s_ccfg, &cin, t);
        xSemaphoreGive(s_mtx);

        /* Scharf geschaltet beginnt die Aufzeichnung von selbst und endet
         * auch von selbst. */
        rec_input_t tin = {
            .burner_known = cin.burner_known,
            .burner_running = cin.burner_running,
            .buffer_valid = cin.puffer_valid,
            .buffer_c = cin.puffer_c,
        };
        rec_tick(&tin, t);

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

        /*
         * Gesichert wird, wenn der Brenner schaltet -- nicht in festem Takt.
         * Ein Fuenfminutentakt waeren 288 Schreibvorgaenge am Tag; der
         * NVS-Bereich fasst nur einige Dutzend Fassungen, bevor er
         * aufgeraeumt werden muss, und laeuft er voll, wird er geleert und
         * die gesamte Einrichtung ist weg. Zwischen zwei Schaltvorgaengen
         * steht der Wert im Arbeitsspeicher; ein Neustart mitten im Brennerlauf
         * kostet hoechstens dessen bisherige Laufzeit.
         */
        if (s_st.running != brenner_lief) {
            brenner_lief = s_st.running;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_stats.runtime_today_s = s_st.runtime_today_s;
            s_stats.starts_today = s_st.starts_today;
            xSemaphoreGive(s_mtx);
            stats_store();
        }

        /* Aufzeichnung im festen Raster. */
        if (s_trig.phase == REC_TRIG_RUN &&
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
    charge_defaults(&s_ccfg);
    rec_trigger_defaults(&s_tcfg);
    rec_trigger_init(&s_trig);
    charge_init(&s_cst);
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
    /*
     * Ohne eigenen Abgasfuehler gilt der Zustand vom Geraet am Kessel. Die
     * Tageswerte bleiben dabei die eigenen und damit null -- gezaehlt wird
     * dort, wo gemessen wird.
     */
    out->known = s_eff_known;
    out->running = s_eff_running;
    out->remote = s_eff_remote;
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

void charge_get(charge_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    out->phase = s_cst.phase;
    out->limited = s_cst.limited;
    out->level_valid = s_cst.level_valid;
    out->level = s_cst.level;
    out->warn_dhw = s_cst.warn_dhw;
    out->spread_valid = s_cst.spread_valid;
    out->spread_k = s_cst.spread_k;
    out->since_s = s_cst.started ? (now_ms() - s_cst.since_ms) / 1000 : 0;
    out->kessel_remote = s_kessel_remote;
    xSemaphoreGive(s_mtx);
}
