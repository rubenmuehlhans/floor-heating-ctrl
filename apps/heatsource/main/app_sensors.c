#include "app_sensors.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_remote.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "sens";

#define TICK_MS 1000
#define HIST_PERIOD_S SENS_HIST_PERIOD_S
#define DELTA_WINDOW_S 30

static sens_snapshot_t s_snap;
static SemaphoreHandle_t s_mtx;
static volatile bool s_cfg_dirty;

/*
 * Verlauf als Ringpuffer, s_head zeigt auf den juengsten Eintrag.
 *
 * Eine Spalte je Rolle waere bequem, aber teuer: Bei fuenfzehn Rollen sind das
 * 43 kB fuer 24 Stunden -- und auf jedem Geraet bleiben die meisten Spalten
 * leer, weil dort nur drei bis sechs Messstellen haengen. Belegt werden
 * deshalb nur die Rollen, die tatsaechlich einen Wert liefern, dicht gepackt
 * wie bei der Ladungsaufzeichnung. s_hist_col bildet Rolle auf Spalte ab,
 * -1 heisst "nicht im Verlauf".
 */
static int16_t *s_hist;
static int8_t s_hist_col[ROLE_COUNT];
static uint8_t s_hist_cols;
static size_t s_hist_len;
static size_t s_head;
static uint32_t s_hist_epoch; /* Unixzeit des juengsten Eintrags */

/* Fuer die Aenderung der letzten 30 Sekunden: Wert und Zeitpunkt je Fuehler. */
static float s_ref_c[OT_MAX_PROBES];
static uint32_t s_ref_ms[OT_MAX_PROBES];
static uint64_t s_ref_rom[OT_MAX_PROBES];

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */
/* Zuordnung                                                           */
/* ------------------------------------------------------------------ */

static void apply_config(sens_snapshot_t *snap)
{
    static app_config_t cfg;
    cfg_copy(&cfg);

    snap->assigned_count = 0;
    for (size_t i = 0; i < snap->count; i++) {
        sens_probe_t *p = &snap->probes[i];
        const cfg_probe_t *c = cfg_probe_of_rom(&cfg, p->rom);
        if (c != NULL) {
            p->assigned = true;
            p->role = c->role;
            p->offset_k = c->offset_k;
            snprintf(p->name, sizeof(p->name), "%s", c->name);
            snap->assigned_count++;
        } else {
            p->assigned = false;
            p->role = ROLE_NONE;
            p->offset_k = 0.0f;
            /* Ohne Eintrag bleibt nur die Werkskennung als Bezeichnung. */
            char rom[20];
            ot_rom_to_str(p->rom, rom, sizeof(rom));
            snprintf(p->name, sizeof(p->name), "%s", rom);
        }
        p->temp_c = p->raw_c + p->offset_k;
    }
}

/* ------------------------------------------------------------------ */
/* Verlauf                                                             */
/* ------------------------------------------------------------------ */

/*
 * Legt den Verlauf an oder passt seine Spalten an. Kommt eine Rolle hinzu --
 * etwa weil ein Fuehler zugeordnet wurde --, wird neu belegt; der bisherige
 * Verlauf geht dabei verloren. Das ist der Preis dafuer, keine leeren Spalten
 * mitzuschleppen, und faellt nur beim Einrichten an.
 */
static void hist_setup(void)
{
    int8_t neu[ROLE_COUNT];
    uint8_t n = 0;
    neu[ROLE_NONE] = -1;
    for (int r = 1; r < ROLE_COUNT; r++) {
        float wert = 0.0f;
        neu[r] = remote_role_value((probe_role_t)r, &wert, NULL) ||
                         sensors_role_value((probe_role_t)r, &wert, NULL)
                     ? (int8_t)n++
                     : (int8_t)-1;
    }
    if (n == 0 || memcmp(neu, s_hist_col, sizeof(neu)) == 0) {
        return;
    }

    /*
     * Erst freigeben, dann belegen. Andersherum laegen kurzzeitig zwei
     * Verlaeufe im Speicher -- und der Verlauf ist beim Spaltenwechsel ohnehin
     * verloren, es gibt also nichts zu retten.
     */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    free(s_hist);
    s_hist = NULL;
    s_hist_len = 0;
    s_head = 0;
    xSemaphoreGive(s_mtx);

    int16_t *puffer = calloc((size_t)SENS_HIST_SLOTS * n, sizeof(int16_t));
    if (puffer == NULL) {
        ESP_LOGW(TAG, "Kein Speicher fuer den Verlauf, er entfaellt");
        memset(s_hist_col, -1, sizeof(s_hist_col));
        return;
    }
    for (size_t i = 0; i < (size_t)SENS_HIST_SLOTS * n; i++) {
        puffer[i] = SENS_HIST_NONE;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_hist = puffer;
    memcpy(s_hist_col, neu, sizeof(neu));
    s_hist_cols = n;
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "Verlauf angelegt: %u Messstellen, %u Byte", (unsigned)n,
             (unsigned)((size_t)SENS_HIST_SLOTS * n * sizeof(int16_t)));
}

static void hist_append(const sens_snapshot_t *snap)
{
    if (s_hist == NULL) {
        return;
    }
    s_head = (s_head + 1) % SENS_HIST_SLOTS;
    int16_t *row = &s_hist[s_head * s_hist_cols];
    for (int c = 0; c < s_hist_cols; c++) {
        row[c] = SENS_HIST_NONE;
    }
    for (size_t i = 0; i < snap->count; i++) {
        const sens_probe_t *p = &snap->probes[i];
        if (p->role == ROLE_NONE || !p->valid || s_hist_col[p->role] < 0) {
            continue;
        }
        float v = p->temp_c * 10.0f;
        if (v > 32000.0f || v < -32000.0f) {
            continue;
        }
        row[s_hist_col[p->role]] = (int16_t)lroundf(v);
    }
    /*
     * Was dieses Geraet nicht selbst misst, kommt vom Nachbargeraet -- die
     * Kesselwerte am Pufferspeicher, die Aussentemperatur von der
     * Verteilerplatine. Ohne diese Ergaenzung blieben ihre Spalten im Verlauf
     * dauerhaft leer, obwohl der Wert vorliegt.
     */
    for (int r = 1; r < ROLE_COUNT; r++) {
        if (s_hist_col[r] < 0 || row[s_hist_col[r]] != SENS_HIST_NONE) {
            continue;
        }
        float wert = 0.0f;
        if (!remote_role_value((probe_role_t)r, &wert, NULL)) {
            continue;
        }
        float v = wert * 10.0f;
        if (v <= 32000.0f && v >= -32000.0f) {
            row[s_hist_col[r]] = (int16_t)lroundf(v);
        }
    }

    if (s_hist_len < SENS_HIST_SLOTS) {
        s_hist_len++;
    }

    time_t jetzt = time(NULL);
    s_hist_epoch = jetzt > 1700000000 ? (uint32_t)jetzt : 0;
}

size_t sensors_history_len(void)
{
    return s_hist_len;
}

bool sensors_history_row(size_t index, int16_t *out, size_t out_len)
{
    if (s_hist == NULL || index >= s_hist_len || out_len < ROLE_COUNT) {
        return false;
    }
    /* Nach aussen bleibt es eine Zeile je Rolle -- die Aufrufer sollen von der
     * Packung nichts wissen muessen. */
    for (int r = 0; r < ROLE_COUNT; r++) {
        out[r] = SENS_HIST_NONE;
    }
    size_t slot = (s_head + SENS_HIST_SLOTS - index) % SENS_HIST_SLOTS;
    const int16_t *row = &s_hist[slot * s_hist_cols];
    for (int r = 1; r < ROLE_COUNT; r++) {
        if (s_hist_col[r] >= 0) {
            out[r] = row[s_hist_col[r]];
        }
    }
    return true;
}

/* Welche Rollen der Verlauf fuehrt. Ohne das gaebe /api/history Serien aus,
 * die durchgehend leer sind. */
bool sensors_history_has(probe_role_t role)
{
    return role > ROLE_NONE && role < ROLE_COUNT && s_hist_col[role] >= 0;
}

uint32_t sensors_history_newest_epoch(void)
{
    return s_hist_epoch;
}

/* ------------------------------------------------------------------ */
/* Aufgabe                                                             */
/* ------------------------------------------------------------------ */

/*
 * Aenderung der letzten 30 Sekunden. Sie dient allein der Zuordnung: waermt
 * man einen Fuehler mit der Hand an, hebt sich seine Zeile in der Liste
 * sichtbar von den uebrigen ab.
 */
static void update_delta(sens_snapshot_t *snap, uint32_t t)
{
    for (size_t i = 0; i < snap->count; i++) {
        sens_probe_t *p = &snap->probes[i];
        if (!p->valid) {
            p->delta30_c = 0.0f;
            continue;
        }
        if (s_ref_rom[i] != p->rom || s_ref_ms[i] == 0) {
            s_ref_rom[i] = p->rom;
            s_ref_c[i] = p->temp_c;
            s_ref_ms[i] = t;
            p->delta30_c = 0.0f;
            continue;
        }
        p->delta30_c = p->temp_c - s_ref_c[i];
        if (t - s_ref_ms[i] >= DELTA_WINDOW_S * 1000) {
            s_ref_c[i] = p->temp_c;
            s_ref_ms[i] = t;
        }
    }
}

static void sensors_task(void *arg)
{
    (void)arg;
    static ot_snapshot_t raw;
    static sens_snapshot_t snap;
    uint32_t last_hist = 0;

    for (;;) {
        ot_get(&raw);

        memset(&snap, 0, sizeof(snap));
        snap.count = raw.count;
        snap.round_ms = raw.round_ms;
        snap.rounds = raw.rounds;
        for (size_t i = 0; i < raw.count; i++) {
            snap.probes[i].rom = raw.probes[i].rom;
            snap.probes[i].bus = raw.probes[i].bus;
            snap.probes[i].valid = raw.probes[i].valid;
            snap.probes[i].raw_c = raw.probes[i].temp_c;
            snap.probes[i].age_s = raw.probes[i].age_ms / 1000;
            snap.probes[i].reads = raw.probes[i].reads;
            snap.probes[i].errors = raw.probes[i].errors;
        }
        apply_config(&snap);

        uint32_t t = now_ms();
        update_delta(&snap, t);

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_snap = snap;
        xSemaphoreGive(s_mtx);

        if (last_hist == 0 || t - last_hist >= HIST_PERIOD_S * 1000) {
            last_hist = t;
            hist_setup();
            hist_append(&snap);
        }

        if (s_cfg_dirty) {
            s_cfg_dirty = false;

            /* Eine geaenderte Busbelegung wird sofort uebernommen; ohne das
             * muesste man beim Suchen des richtigen Anschlusses jedes Mal neu
             * starten. */
            static app_config_t cfg;
            cfg_copy(&cfg);
            int pins[CFG_MAX_BUSES];
            for (int i = 0; i < CFG_MAX_BUSES; i++) {
                pins[i] = cfg.onewire_pin[i];
            }
            ot_reconfigure(pins, CFG_MAX_BUSES, (uint32_t)cfg.poll_s * 1000);
            ESP_LOGI(TAG, "Zuordnung der Fuehler uebernommen");
        }
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ------------------------------------------------------------------ */

esp_err_t sensors_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    static app_config_t cfg;
    cfg_copy(&cfg);

    int pins[CFG_MAX_BUSES];
    for (int i = 0; i < CFG_MAX_BUSES; i++) {
        pins[i] = cfg.onewire_pin[i];
    }
    esp_err_t rc = ot_start(pins, CFG_MAX_BUSES, (uint32_t)cfg.poll_s * 1000);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "1-Wire liess sich nicht starten: %s", esp_err_to_name(rc));
        return rc;
    }

    /* Der Verlauf wird erst angelegt, wenn feststeht, welche Messstellen es
     * gibt -- also im ersten Durchlauf der Aufgabe. Klappt die Belegung nicht,
     * laeuft alles Uebrige weiter: der Verlauf ist Beiwerk, die Messung nicht. */
    memset(s_hist_col, -1, sizeof(s_hist_col));

    if (xTaskCreate(sensors_task, "sensors", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void sensors_get(sens_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    if (s_mtx == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_mtx);
}

bool sensors_role_value(probe_role_t role, float *out_c, uint32_t *age_s)
{
    if (role == ROLE_NONE || s_mtx == NULL) {
        return false;
    }
    bool gefunden = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (size_t i = 0; i < s_snap.count; i++) {
        if (s_snap.probes[i].role == role && s_snap.probes[i].valid) {
            if (out_c) {
                *out_c = s_snap.probes[i].temp_c;
            }
            if (age_s) {
                *age_s = s_snap.probes[i].age_s;
            }
            gefunden = true;
            break;
        }
    }
    xSemaphoreGive(s_mtx);
    return gefunden;
}

void sensors_config_changed(void)
{
    s_cfg_dirty = true;
}

void sensors_rescan(void)
{
    ot_rescan();
}
