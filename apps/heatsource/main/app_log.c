#include "app_log.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "log";

#define NVS_NAMESPACE "heiz"
#define NVS_KEY_CHG   "chglog"
#define NVS_KEY_DAY   "daylog"

/*
 * Beide Ringe liegen als ein Block im NVS, mit Zaehler und Schreibmarke davor.
 * Ein Satz je Schluessel waere zwar feiner, brauchte aber je Eintrag einen
 * eigenen NVS-Eintrag -- und davon gibt es nicht genug.
 */
typedef struct {
    uint16_t count;   /* belegte Saetze, hoechstens LOG_CHARGES */
    uint16_t head;    /* naechste Schreibstelle */
    charge_log_t rec[LOG_CHARGES];
} charge_ring_t;

typedef struct {
    uint16_t count;
    uint16_t head;
    day_log_t rec[LOG_DAYS];
} day_ring_t;

/*
 * Beide Ringe liegen im NVS, nicht im Arbeitsspeicher. Sie dauerhaft
 * vorzuhalten kostete acht Kilobyte fuer Daten, die einmal je Ladung und
 * einmal je Tag angefasst werden -- und der Zustandsbericht braucht diesen
 * Platz dringender: Bei 34 kB freiem Speicher kam er nur noch meistens
 * zustande.
 *
 * Geholt wird deshalb auf Anforderung: belegen, lesen, aendern, schreiben,
 * freigeben. Im Arbeitsspeicher bleiben nur die Zaehler und der laufende Tag.
 */
static uint16_t s_chg_count, s_chg_head;
static uint16_t s_day_count, s_day_head;
static SemaphoreHandle_t s_mtx;

/* Der laufende Tag steht im Arbeitsspeicher; gesichert wird er beim Wechsel. */
static day_log_t s_heute;
static bool s_heute_gueltig;
static uint32_t s_grad_summe_dc; /* Zehntelgrad-Stunden, geteilt wird beim Sichern */
static uint32_t s_grad_stunden;
static uint32_t s_letzte_stunde_ms;

static inline void lock(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
}

static inline void unlock(void)
{
    xSemaphoreGive(s_mtx);
}

/* ------------------------------------------------------------------ */
/* NVS                                                                 */
/* ------------------------------------------------------------------ */

static void ring_load(const char *key, void *ring, size_t groesse, uint16_t max)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = groesse;
    esp_err_t rc = nvs_get_blob(h, key, ring, &len);
    nvs_close(h);

    uint16_t *kopf = ring;
    if (rc != ESP_OK || len != groesse || kopf[0] > max || kopf[1] > max) {
        /* Nichts gespeichert oder unbrauchbar: leer beginnen. */
        memset(ring, 0, groesse);
    }
}

/* Belegt den Ring und liest ihn ein. Der Aufrufer gibt ihn frei. */
static void *ring_get(const char *key, size_t groesse, uint16_t max)
{
    void *ring = malloc(groesse);
    if (ring == NULL) {
        ESP_LOGW(TAG, "Kein Speicher fuer %s", key);
        return NULL;
    }
    ring_load(key, ring, groesse, max);
    return ring;
}

static void ring_store(const char *key, const void *ring, size_t groesse)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    esp_err_t rc = nvs_set_blob(h, key, ring, groesse);
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "%s liess sich nicht sichern: %s", key, esp_err_to_name(rc));
    }
}

/* Liest nur die Zaehler, ohne den Ring dauerhaft zu belegen. */
static void kopf_lesen(const char *key, uint16_t *count, uint16_t *head, uint16_t max)
{
    nvs_handle_t h;
    *count = 0;
    *head = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint16_t kopf[2] = {0, 0};
    size_t len = sizeof(kopf);
    /* nvs_get_blob liefert die Gesamtlaenge, kopiert aber nur len Byte --
     * genau die vier, die hier gebraucht werden. */
    size_t ganz = 0;
    if (nvs_get_blob(h, key, NULL, &ganz) == ESP_OK && ganz > sizeof(kopf)) {
        void *tmp = malloc(ganz);
        if (tmp != NULL) {
            len = ganz;
            if (nvs_get_blob(h, key, tmp, &len) == ESP_OK) {
                memcpy(kopf, tmp, sizeof(kopf));
            }
            free(tmp);
        }
    }
    nvs_close(h);
    if (kopf[0] <= max && kopf[1] <= max) {
        *count = kopf[0];
        *head = kopf[1];
    }
}

esp_err_t applog_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    kopf_lesen(NVS_KEY_CHG, &s_chg_count, &s_chg_head, LOG_CHARGES);
    kopf_lesen(NVS_KEY_DAY, &s_day_count, &s_day_head, LOG_DAYS);
    ESP_LOGI(TAG, "Protokolle: %u Ladungen, %u Tage", (unsigned)s_chg_count,
             (unsigned)s_day_count);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Ladungen                                                            */
/* ------------------------------------------------------------------ */

/*
 * Gesichert wird unter der Sperre, aus dem Ring heraus. Eine Kopie fuer die
 * Zeit des Schreibens waere sauberer getrennt, laege aber mit 1,4 bzw. 6,4 kB
 * auf einem Task-Stack von 4 kB -- genau der Fehler, an dem die Bedien-Task
 * beim ersten Hardwaretest gescheitert ist. Das Schreiben dauert wenige
 * Millisekunden; so lange wartet ein Leser.
 */
void applog_add_charge(const charge_log_t *rec)
{
    lock();
    charge_ring_t *ring = ring_get(NVS_KEY_CHG, sizeof(*ring), LOG_CHARGES);
    if (ring != NULL) {
        ring->rec[ring->head] = *rec;
        ring->head = (uint16_t)((ring->head + 1) % LOG_CHARGES);
        if (ring->count < LOG_CHARGES) {
            ring->count++;
        }
        s_chg_count = ring->count;
        s_chg_head = ring->head;
        ring_store(NVS_KEY_CHG, ring, sizeof(*ring));
        free(ring);
    }
    unlock();
    ESP_LOGI(TAG, "Ladung protokolliert: %u s, davon %u s Brenner, %u Starts, "
                  "Speicher %d -> %d dC, %u ml",
             (unsigned)rec->duration_s, (unsigned)rec->burner_s, (unsigned)rec->starts,
             rec->puffer_start_dc, rec->puffer_end_dc, (unsigned)rec->litres_ml);
}

/* ------------------------------------------------------------------ */
/* Tage                                                                */
/* ------------------------------------------------------------------ */

static void tag_sichern_locked(void)
{
    if (!s_heute_gueltig) {
        return;
    }
    /* Heizgradtage: Mittel der stuendlichen Anteile ueber den Tag. Fehlen
     * Stunden -- etwa weil das Geraet aus war --, wird ueber die vorhandenen
     * gemittelt statt eine Luecke als null zu zaehlen. */
    s_heute.gradtage_dc = s_grad_stunden ? (int16_t)(s_grad_summe_dc / s_grad_stunden) : 0;

    day_ring_t *ring = ring_get(NVS_KEY_DAY, sizeof(*ring), LOG_DAYS);
    if (ring == NULL) {
        return;
    }
    ring->rec[ring->head] = s_heute;
    ring->head = (uint16_t)((ring->head + 1) % LOG_DAYS);
    if (ring->count < LOG_DAYS) {
        ring->count++;
    }
    s_day_count = ring->count;
    s_day_head = ring->head;
    ring_store(NVS_KEY_DAY, ring, sizeof(*ring));
    free(ring);
}

void applog_tick_day(int year, int yday, uint32_t runtime_s, uint32_t starts,
                     uint16_t litres_ml, bool aussen_valid, float aussen_c)
{
    if (year < 0 || yday < 0) {
        return; /* ohne gestellte Uhr wird nichts protokolliert */
    }

    bool sichern = false;

    lock();
    if (s_heute_gueltig && (s_heute.yday != (uint16_t)yday || s_heute.year != (int16_t)year)) {
        tag_sichern_locked();
        sichern = true;
        s_heute_gueltig = false;
    }

    if (!s_heute_gueltig) {
        memset(&s_heute, 0, sizeof(s_heute));
        s_heute.year = (int16_t)year;
        s_heute.yday = (uint16_t)yday;
        s_heute.aussen_min_dc = LOG_NONE;
        s_heute.aussen_max_dc = LOG_NONE;
        s_grad_summe_dc = 0;
        s_grad_stunden = 0;
        s_letzte_stunde_ms = 0;
        s_heute_gueltig = true;
    }

    s_heute.runtime_s = runtime_s;
    s_heute.starts = (uint16_t)starts;
    s_heute.litres_ml = litres_ml;

    if (aussen_valid) {
        int16_t dc = (int16_t)(aussen_c * 10.0f);
        if (s_heute.aussen_min_dc == LOG_NONE || dc < s_heute.aussen_min_dc) {
            s_heute.aussen_min_dc = dc;
        }
        if (s_heute.aussen_max_dc == LOG_NONE || dc > s_heute.aussen_max_dc) {
            s_heute.aussen_max_dc = dc;
        }
    }
    unlock();

    if (sichern) {
        ESP_LOGI(TAG, "Tag abgeschlossen und protokolliert");
    }
}

/*
 * Stuendlicher Beitrag zu den Heizgradtagen. Getrennt von applog_tick_day,
 * weil er an der Uhr haengt und nicht am Aufruftakt.
 */
void applog_tick_hour(bool aussen_valid, float aussen_c)
{
    if (!aussen_valid) {
        return;
    }
    lock();
    if (s_heute_gueltig) {
        float anteil = 20.0f - aussen_c;
        if (anteil > 0.0f) {
            s_grad_summe_dc += (uint32_t)(anteil * 10.0f);
        }
        s_grad_stunden++;
    }
    unlock();
}

/* ------------------------------------------------------------------ */
/* Abfrage                                                             */
/* ------------------------------------------------------------------ */

size_t applog_charge_count(void)
{
    return s_mtx ? s_chg_count : 0;
}

size_t applog_day_count(void)
{
    return s_mtx ? s_day_count : 0;
}

/*
 * Zum Ausgeben wird der Ring einmal geholt und dann durchlaufen. Ihn je Satz
 * neu einzulesen waere bei 365 Saetzen 365 Mal ein NVS-Zugriff.
 */
static charge_ring_t *s_chg_offen;
static day_ring_t *s_day_offen;

bool applog_open(void)
{
    if (s_mtx == NULL) {
        return false;
    }
    lock();
    s_chg_offen = ring_get(NVS_KEY_CHG, sizeof(charge_ring_t), LOG_CHARGES);
    s_day_offen = ring_get(NVS_KEY_DAY, sizeof(day_ring_t), LOG_DAYS);
    return s_chg_offen != NULL && s_day_offen != NULL;
}

void applog_close(void)
{
    free(s_chg_offen);
    free(s_day_offen);
    s_chg_offen = NULL;
    s_day_offen = NULL;
    unlock();
}

bool applog_charge(size_t index, charge_log_t *out)
{
    if (s_chg_offen == NULL || index >= s_chg_offen->count) {
        return false;
    }
    *out = s_chg_offen->rec[(s_chg_offen->head + LOG_CHARGES - 1 - index) % LOG_CHARGES];
    return true;
}

bool applog_day(size_t index, day_log_t *out)
{
    if (s_day_offen == NULL) {
        return false;
    }
    /* Index 0 ist der laufende Tag, sofern es ihn gibt. */
    if (index == 0 && s_heute_gueltig) {
        *out = s_heute;
        out->gradtage_dc = s_grad_stunden ? (int16_t)(s_grad_summe_dc / s_grad_stunden) : 0;
        return true;
    }
    size_t i = s_heute_gueltig ? index - 1 : index;
    if (i >= s_day_offen->count) {
        return false;
    }
    *out = s_day_offen->rec[(s_day_offen->head + LOG_DAYS - 1 - i) % LOG_DAYS];
    return true;
}

void applog_clear(void)
{
    nvs_handle_t h;
    lock();
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_CHG);
        nvs_erase_key(h, NVS_KEY_DAY);
        nvs_commit(h);
        nvs_close(h);
    }
    s_chg_count = s_chg_head = 0;
    s_day_count = s_day_head = 0;
    s_heute_gueltig = false;
    unlock();
    ESP_LOGW(TAG, "Protokolle verworfen");
}
