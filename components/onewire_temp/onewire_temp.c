#include "onewire_temp.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ds18b20.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "onewire_cmd.h"

static const char *TAG = "1wire";

/* Wandlungszeit bei 12 Bit Aufloesung, mit Zuschlag. */
#define CONVERT_MS 800
#define MIN_PERIOD_MS 1000

/* Nach so vielen erfolglosen Ruecksetzungen wird der Bus neu aufgesetzt. */
#define BUS_FAIL_LIMIT 3

/* Solange kein Fuehler antwortet, wird in diesem Abstand erneut gesucht --
 * sonst bliebe ein spaeter angeschlossener Fuehler bis zum Neustart
 * unbemerkt. */
#define IDLE_RESCAN_MS 60000

typedef struct {
    ds18b20_device_handle_t dev;
    uint64_t rom;
    uint8_t bus;
} probe_t;

static onewire_bus_handle_t s_bus[OT_MAX_BUSES];
static size_t s_bus_count;
static int s_pin[OT_MAX_BUSES];

static probe_t s_probe[OT_MAX_PROBES];
/* Aufeinanderfolgende erfolglose Bus-Rücksetzungen je Bus. */
static uint8_t s_fail[OT_MAX_BUSES];
static ot_snapshot_t s_snap;
static SemaphoreHandle_t s_mtx;
static volatile bool s_rescan;
static uint32_t s_period_ms = 10000;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */
/* Kennungen                                                           */
/* ------------------------------------------------------------------ */

void ot_rom_to_str(uint64_t rom, char *out, size_t len)
{
    snprintf(out, len, "%016" PRIX64, rom);
}

uint64_t ot_str_to_rom(const char *s)
{
    if (s == NULL) {
        return 0;
    }
    uint64_t v = 0;
    int digits = 0;
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') {
            d = *s - '0';
        } else if (*s >= 'a' && *s <= 'f') {
            d = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'F') {
            d = *s - 'A' + 10;
        } else {
            continue; /* Doppelpunkte und Bindestriche duerfen mitgeschrieben werden */
        }
        v = (v << 4) | (uint64_t)d;
        digits++;
    }
    return digits == 16 ? v : 0;
}

/* ------------------------------------------------------------------ */
/* Suchlauf                                                            */
/* ------------------------------------------------------------------ */

static void forget_all(void)
{
    for (size_t i = 0; i < OT_MAX_PROBES; i++) {
        if (s_probe[i].dev != NULL) {
            ds18b20_del_device(s_probe[i].dev);
            s_probe[i].dev = NULL;
        }
    }
}

/*
 * Sucht alle Fuehler an allen Bussen. Bereits bekannte Kennungen behalten
 * ihren Platz und ihre Zaehler, damit die Zuordnung in der Konfiguration und
 * die Fehlerstatistik einen Suchlauf ueberdauern.
 */
static void discover(void)
{
    ot_probe_t vorher[OT_MAX_PROBES];
    size_t vorher_count;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(vorher, s_snap.probes, sizeof(vorher));
    vorher_count = s_snap.count;
    xSemaphoreGive(s_mtx);

    forget_all();

    probe_t gefunden[OT_MAX_PROBES] = {0};
    size_t n = 0;

    for (size_t b = 0; b < s_bus_count && n < OT_MAX_PROBES; b++) {
        if (s_bus[b] == NULL) {
            continue;
        }
        onewire_device_iter_handle_t iter = NULL;
        if (onewire_new_device_iter(s_bus[b], &iter) != ESP_OK) {
            continue;
        }
        onewire_device_t found;
        while (n < OT_MAX_PROBES && onewire_device_iter_get_next(iter, &found) == ESP_OK) {
            ds18b20_config_t cfg = {};
            ds18b20_device_handle_t dev = NULL;
            if (ds18b20_new_device(&found, &cfg, &dev) != ESP_OK) {
                continue; /* kein DS18B20 */
            }
            ds18b20_set_resolution(dev, DS18B20_RESOLUTION_12B);
            gefunden[n].dev = dev;
            gefunden[n].rom = found.address;
            gefunden[n].bus = (uint8_t)b;
            n++;
        }
        onewire_del_device_iter(iter);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(s_probe, gefunden, sizeof(s_probe));
    memset(&s_snap.probes, 0, sizeof(s_snap.probes));
    s_snap.count = n;
    for (size_t i = 0; i < n; i++) {
        s_snap.probes[i].rom = gefunden[i].rom;
        s_snap.probes[i].bus = gefunden[i].bus;
        for (size_t j = 0; j < vorher_count; j++) {
            if (vorher[j].rom == gefunden[i].rom) {
                s_snap.probes[i] = vorher[j];
                s_snap.probes[i].bus = gefunden[i].bus;
                break;
            }
        }
    }
    xSemaphoreGive(s_mtx);

    if (n != vorher_count || n == 0) {
        if (n == 0 && s_bus_count > 0) {
            /* Der haeufigste Grund ist der falsche Anschluss, der zweite ein
             * fehlender Anschlusswiderstand. Beides steht damit nebeneinander. */
            ESP_LOGW(TAG, "Kein Fuehler an GPIO %d -- Anschluss und "
                          "Anschlusswiderstand nach 3,3 V pruefen", s_pin[0]);
        }
        ESP_LOGI(TAG, "%u Fuehler an %u Bussen gefunden", (unsigned)n, (unsigned)s_bus_count);
    }
    for (size_t i = 0; i < n; i++) {
        char rom[20];
        ot_rom_to_str(gefunden[i].rom, rom, sizeof(rom));
        ESP_LOGI(TAG, "  %s an GPIO %d", rom, s_pin[gefunden[i].bus]);
    }
}

/*
 * Bus neu aufsetzen.
 *
 * Bleibt die Ruecksetzung mehrfach ohne Antwort, schaltet der RMT-Treiber den
 * Kanal ab und meldet fortan nur noch "channel not in enable state" -- ohne
 * Eingriff bliebe der Bus bis zum Neustart tot. Das trifft nicht nur den Fall
 * "noch nichts angeschlossen", sondern auch einen kurzzeitigen Wackelkontakt
 * im Betrieb.
 */
static bool open_bus(size_t b)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = s_pin[b],
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,
    };
    return onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_bus[b]) == ESP_OK;
}

static void recover_bus(size_t b)
{
    ESP_LOGW(TAG, "Bus an GPIO %d antwortet nicht, wird neu aufgesetzt", s_pin[b]);

    /* Die Geraetezeiger verweisen auf den alten Bus und muessen zuerst weg. */
    forget_all();
    if (s_bus[b] != NULL) {
        onewire_bus_del(s_bus[b]);
        s_bus[b] = NULL;
    }
    if (!open_bus(b)) {
        ESP_LOGE(TAG, "Bus an GPIO %d liess sich nicht neu anlegen", s_pin[b]);
    }
    s_fail[b] = 0;
    s_rescan = true;
}

/* ------------------------------------------------------------------ */
/* Leserunde                                                           */
/* ------------------------------------------------------------------ */

/* Sammelwandlung: ein Befehl an alle Fuehler des Busses zugleich. */
static void convert_all(void)
{
    const uint8_t cmd[2] = {ONEWIRE_CMD_SKIP_ROM, 0x44};
    bool gewandelt = false;

    for (size_t b = 0; b < s_bus_count; b++) {
        if (s_bus[b] == NULL) {
            continue;
        }
        if (onewire_bus_reset(s_bus[b]) != ESP_OK) {
            if (s_fail[b] < UINT8_MAX) {
                s_fail[b]++;
            }
            continue; /* kein Fuehler am Bus oder Leitung gestoert */
        }
        s_fail[b] = 0;
        onewire_bus_write_bytes(s_bus[b], cmd, sizeof(cmd));
        gewandelt = true;
    }

    for (size_t b = 0; b < s_bus_count; b++) {
        if (s_fail[b] >= BUS_FAIL_LIMIT) {
            recover_bus(b);
        }
    }

    if (gewandelt) {
        vTaskDelay(pdMS_TO_TICKS(CONVERT_MS));
    }
}

static void read_all(void)
{
    uint32_t t0 = now_ms();
    size_t n;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    n = s_snap.count;
    xSemaphoreGive(s_mtx);

    for (size_t i = 0; i < n; i++) {
        if (s_probe[i].dev == NULL) {
            continue;
        }
        float t = 0.0f;
        bool ok = ds18b20_get_temperature(s_probe[i].dev, &t) == ESP_OK;

        /* 85,0 ist der Einschaltwert des Fuehlers, -127,0 eine unterbrochene
         * Leitung. Beide sind keine Messwerte. */
        if (ok && (t == 85.0f || t <= -55.0f || t > 125.0f)) {
            ok = false;
        }

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (ok) {
            s_snap.probes[i].temp_c = t;
            s_snap.probes[i].valid = true;
            s_snap.probes[i].age_ms = 0;
            s_snap.probes[i].reads++;
        } else {
            s_snap.probes[i].errors++;
        }
        xSemaphoreGive(s_mtx);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_snap.round_ms = now_ms() - t0;
    s_snap.rounds++;
    xSemaphoreGive(s_mtx);
}

static void age_all(uint32_t dt_ms)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (size_t i = 0; i < s_snap.count; i++) {
        if (s_snap.probes[i].valid) {
            s_snap.probes[i].age_ms += dt_ms;
        }
    }
    xSemaphoreGive(s_mtx);
}

static void ot_task(void *arg)
{
    (void)arg;
    discover();
    uint32_t letzte_suche = now_ms();

    for (;;) {
        bool leer;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        leer = s_snap.count == 0;
        xSemaphoreGive(s_mtx);

        if (leer && now_ms() - letzte_suche >= IDLE_RESCAN_MS) {
            s_rescan = true;
        }
        if (s_rescan) {
            s_rescan = false;
            letzte_suche = now_ms();
            discover();
        }
        uint32_t t0 = now_ms();
        convert_all();
        read_all();

        /* Ist nichts angeschlossen, wird seltener nachgefasst: der Treiber
         * meldet jede erfolglose Ruecksetzung, und das fuellt sonst das
         * Protokoll, ohne dass es etwas zu messen gaebe. */
        uint32_t takt = leer ? IDLE_RESCAN_MS : s_period_ms;
        uint32_t verbraucht = now_ms() - t0;
        uint32_t rest = takt > verbraucht ? takt - verbraucht : 0;
        if (rest > 0) {
            vTaskDelay(pdMS_TO_TICKS(rest));
        }
        age_all(now_ms() - t0);
    }
}

/* ------------------------------------------------------------------ */

/* Legt die Busse an. Der Aufrufer haelt s_mtx bereits, sofern noetig. */
static esp_err_t open_all(const int *pins, size_t pin_count)
{
    s_bus_count = 0;
    for (size_t i = 0; i < pin_count && s_bus_count < OT_MAX_BUSES; i++) {
        if (pins[i] < 0) {
            continue;
        }
        s_pin[s_bus_count] = pins[i];
        s_fail[s_bus_count] = 0;
        if (!open_bus(s_bus_count)) {
            ESP_LOGE(TAG, "Bus an GPIO %d liess sich nicht anlegen", pins[i]);
            continue;
        }
        s_bus_count++;
    }
    /*
     * Die belegten Anschluesse gleich beim Anlegen nennen. Ohne das steht die
     * Nummer erst in der Meldung nach dem dritten Fehlversuch -- und beim
     * Suchen eines stummen Busses ist die erste Frage immer, ob ueberhaupt der
     * richtige Anschluss abgesucht wird.
     */
    for (size_t b = 0; b < s_bus_count; b++) {
        ESP_LOGI(TAG, "Bus %u an GPIO %d", (unsigned)(b + 1), s_pin[b]);
    }
    return s_bus_count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t ot_start(const int *pins, size_t pin_count, uint32_t period_ms)
{
    if (pins == NULL || pin_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mtx != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_period_ms = period_ms < MIN_PERIOD_MS ? MIN_PERIOD_MS : period_ms;

    esp_err_t rc = open_all(pins, pin_count);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Kein Bus verfuegbar");
        return rc;
    }

    if (xTaskCreate(ot_task, "1wire", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ot_reconfigure(const int *pins, size_t pin_count, uint32_t period_ms)
{
    if (pins == NULL || pin_count == 0 || s_mtx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool gleich = true;
    size_t belegt = 0;
    for (size_t i = 0; i < pin_count && belegt < OT_MAX_BUSES; i++) {
        if (pins[i] < 0) {
            continue;
        }
        if (belegt >= s_bus_count || s_pin[belegt] != pins[i]) {
            gleich = false;
        }
        belegt++;
    }
    if (belegt != s_bus_count) {
        gleich = false;
    }

    uint32_t takt = period_ms < MIN_PERIOD_MS ? MIN_PERIOD_MS : period_ms;
    s_period_ms = takt;
    if (gleich) {
        return ESP_OK; /* nur der Takt hat sich geaendert */
    }

    ESP_LOGI(TAG, "Busbelegung geaendert, wird neu angelegt");
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    forget_all();
    for (size_t b = 0; b < OT_MAX_BUSES; b++) {
        if (s_bus[b] != NULL) {
            onewire_bus_del(s_bus[b]);
            s_bus[b] = NULL;
        }
    }
    memset(&s_snap, 0, sizeof(s_snap));
    esp_err_t rc = open_all(pins, pin_count);
    xSemaphoreGive(s_mtx);

    s_rescan = true;
    return rc;
}

void ot_get(ot_snapshot_t *out)
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

void ot_rescan(void)
{
    s_rescan = true;
}
