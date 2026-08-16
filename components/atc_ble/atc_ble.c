#include "atc_ble.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ble";

#define MFG_RUUVI 0x0499
#define UUID_ENV_SENSING 0x181A

static atc_device_t s_devices[ATC_MAX_DEVICES];
static size_t s_device_count;
static SemaphoreHandle_t s_mtx;
static atc_cb_t s_cb;
static void *s_ctx;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */
/* Geraeteliste                                                        */
/* ------------------------------------------------------------------ */

static void store_device(const atc_device_t *in)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    atc_device_t *slot = NULL;
    for (size_t i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, in->mac, 6) == 0) {
            slot = &s_devices[i];
            break;
        }
    }
    if (slot == NULL) {
        if (s_device_count < ATC_MAX_DEVICES) {
            slot = &s_devices[s_device_count++];
        } else {
            /* Aeltesten Eintrag verdraengen. */
            uint32_t oldest = UINT32_MAX;
            for (size_t i = 0; i < s_device_count; i++) {
                if (s_devices[i].last_seen_ms < oldest) {
                    oldest = s_devices[i].last_seen_ms;
                    slot = &s_devices[i];
                }
            }
        }
        memset(slot, 0, sizeof(*slot));
        memcpy(slot->mac, in->mac, 6);
        ESP_LOGI(TAG, "Neues Thermometer %s %02X:%02X:%02X:%02X:%02X:%02X (%s)",
                 in->name[0] ? in->name : "ohne Namen", in->mac[0], in->mac[1], in->mac[2],
                 in->mac[3], in->mac[4], in->mac[5], atc_format_name(in->format));
    }

    uint32_t packets = slot->packets + 1;
    char kept_name[sizeof(slot->name)];
    snprintf(kept_name, sizeof(kept_name), "%s", slot->name);

    *slot = *in;
    slot->packets = packets;

    /* Nicht jedes Paket fuehrt den Namen mit - einmal empfangen, bleibt er. */
    if (slot->name[0] == '\0' && kept_name[0] != '\0') {
        snprintf(slot->name, sizeof(slot->name), "%s", kept_name);
    }

    atc_device_t copy = *slot;
    xSemaphoreGive(s_mtx);

    if (s_cb) {
        s_cb(&copy, s_ctx);
    }
}

/*
 * Der Name kommt bei der pvvx-Firmware nicht im Rundruf, sondern erst in der
 * Antwort auf eine Scan-Anfrage - und damit in einem eigenen Paket ohne
 * Messwerte. Er wird deshalb getrennt nachgetragen.
 */
static void update_name(const uint8_t mac[6], const char *name)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (size_t i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) {
            if (strcmp(s_devices[i].name, name) != 0) {
                snprintf(s_devices[i].name, sizeof(s_devices[i].name), "%s", name);
                ESP_LOGI(TAG, "Thermometer %02X:%02X:%02X:%02X:%02X:%02X heisst \"%s\"", mac[0],
                         mac[1], mac[2], mac[3], mac[4], mac[5], name);
            }
            break;
        }
    }
    xSemaphoreGive(s_mtx);
}

size_t atc_ble_devices(atc_device_t *out, size_t max)
{
    if (s_mtx == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t n = s_device_count < max ? s_device_count : max;
    memcpy(out, s_devices, n * sizeof(atc_device_t));
    xSemaphoreGive(s_mtx);
    return n;
}

/* ------------------------------------------------------------------ */
/* Dekodierung                                                         */
/* ------------------------------------------------------------------ */

static bool decode_atc1441(const uint8_t *d, atc_device_t *out)
{
    /* MAC in Sendereihenfolge (Big Endian), Temperatur in Zehntelgrad. */
    for (int i = 0; i < 6; i++) {
        out->mac[i] = d[i];
    }
    int16_t temp = (int16_t)((d[6] << 8) | d[7]);
    out->temp_c = temp / 10.0f;
    out->humidity = d[8];
    out->battery = d[9];
    out->battery_mv = (uint16_t)((d[10] << 8) | d[11]);
    out->format = ATC_FMT_ATC1441;
    return true;
}

static bool decode_pvvx(const uint8_t *d, atc_device_t *out)
{
    /* MAC rueckwaerts, Temperatur in Hundertstelgrad. */
    for (int i = 0; i < 6; i++) {
        out->mac[i] = d[5 - i];
    }
    int16_t temp = (int16_t)(d[6] | (d[7] << 8));
    uint16_t hum = (uint16_t)(d[8] | (d[9] << 8));
    out->temp_c = temp / 100.0f;
    out->humidity = hum / 100.0f;
    out->battery_mv = (uint16_t)(d[10] | (d[11] << 8));
    out->battery = d[12];
    out->format = ATC_FMT_PVVX;
    return true;
}

/*
 * RuuviTag, Datensatz 5 ("RAWv2"). 24 Byte Herstellerdaten hinter der Kennung
 * 0x0499, alles Big Endian:
 *
 *   0      Datensatzkennung, hier 0x05
 *   1..2   Temperatur, 0,005 °C je Schritt
 *   3..4   Feuchte, 0,0025 % je Schritt
 *   5..6   Luftdruck in Pascal, um 50 000 verschoben
 *   7..12  Beschleunigung in drei Achsen -- hier ohne Belang
 *   13..14 obere 11 Bit Batteriespannung ab 1 600 mV, untere 5 Bit Sendeleistung
 *   15     Bewegungszaehler
 *   16..17 laufende Nummer der Messung
 *   18..23 MAC
 *
 * Fuer "kein Messwert" sendet Ruuvi feste Muster: 0x8000 bei der Temperatur,
 * 0xFFFF bei Feuchte und Druck.
 */
static bool decode_ruuvi(const uint8_t *d, atc_device_t *out)
{
    if (d[0] != 0x05) {
        return false; /* aeltere Datensaetze werden nicht ausgewertet */
    }
    for (int i = 0; i < 6; i++) {
        out->mac[i] = d[18 + i];
    }

    uint16_t roh_t = (uint16_t)((d[1] << 8) | d[2]);
    if (roh_t == 0x8000) {
        return false;
    }
    out->temp_c = (float)(int16_t)roh_t * 0.005f;

    uint16_t roh_h = (uint16_t)((d[3] << 8) | d[4]);
    out->humidity = roh_h == 0xFFFF ? 0.0f : (float)roh_h * 0.0025f;

    uint16_t roh_p = (uint16_t)((d[5] << 8) | d[6]);
    out->pressure_hpa = roh_p == 0xFFFF ? 0.0f : ((float)roh_p + 50000.0f) / 100.0f;

    uint16_t leistung = (uint16_t)((d[13] << 8) | d[14]);
    uint16_t mv = (uint16_t)((leistung >> 5) + 1600);
    out->battery_mv = (leistung >> 5) == 0x7FF ? 0 : mv;

    /* Eine Ladungsanzeige in Prozent sendet der RuuviTag nicht. Sie hier aus
     * der Spannung zu schaetzen hiesse, eine Zahl zu erfinden. */
    out->battery = 0;
    out->format = ATC_FMT_RUUVI;
    return true;
}

const char *atc_format_name(atc_format_t f)
{
    switch (f) {
    case ATC_FMT_PVVX:  return "pvvx";
    case ATC_FMT_RUUVI: return "ruuvi";
    default:            return "atc1441";
    }
}

static void handle_adv(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return;
    }

    /* Die Sendeadresse steht in umgekehrter Reihenfolge; hier wird sie gleich
     * in die uebliche Schreibweise gebracht. */
    uint8_t addr[6];
    for (int i = 0; i < 6; i++) {
        addr[i] = disc->addr.val[5 - i];
    }

    char name[sizeof(((atc_device_t *)0)->name)] = {0};
    if (fields.name != NULL && fields.name_len > 0) {
        size_t n = fields.name_len;
        if (n > sizeof(name) - 1) {
            n = sizeof(name) - 1;
        }
        memcpy(name, fields.name, n);
    }

    /*
     * Ruuvi sendet unter Herstellerdaten, nicht unter Dienstdaten -- deshalb
     * wird dieses Feld zuerst geprueft. Beides im selben Rundruf kommt nicht
     * vor.
     */
    if (fields.mfg_data != NULL && fields.mfg_data_len >= 2 + 24) {
        uint16_t hersteller = (uint16_t)(fields.mfg_data[0] | (fields.mfg_data[1] << 8));
        if (hersteller == MFG_RUUVI) {
            atc_device_t rd = {0};
            if (decode_ruuvi(fields.mfg_data + 2, &rd)) {
                snprintf(rd.name, sizeof(rd.name), "%s", name);
                rd.rssi = disc->rssi;
                rd.last_seen_ms = now_ms();
                if (rd.temp_c >= -50.0f && rd.temp_c <= 90.0f) {
                    store_device(&rd);
                }
            }
            return;
        }
    }

    if (fields.svc_data_uuid16 == NULL || fields.svc_data_uuid16_len < 3) {
        if (name[0] != '\0') {
            update_name(addr, name);
        }
        return;
    }

    const uint8_t *sd = fields.svc_data_uuid16;
    uint16_t uuid = (uint16_t)(sd[0] | (sd[1] << 8));
    if (uuid != UUID_ENV_SENSING) {
        if (name[0] != '\0') {
            update_name(addr, name);
        }
        return;
    }

    const uint8_t *payload = sd + 2;
    uint8_t len = (uint8_t)(fields.svc_data_uuid16_len - 2);

    atc_device_t dev = {0};
    bool ok = false;
    if (len >= 13 && len <= 14) {
        ok = decode_atc1441(payload, &dev);
    } else if (len >= 15) {
        ok = decode_pvvx(payload, &dev);
    }
    if (!ok) {
        return;
    }

    /* Unplausible Werte verwerfen - ein falsch dekodiertes Paket darf die
     * Regelung nicht in Bewegung setzen. */
    if (dev.temp_c < -40.0f || dev.temp_c > 80.0f) {
        return;
    }
    if (dev.humidity < 0.0f || dev.humidity > 100.0f) {
        dev.humidity = 0.0f;
    }

    snprintf(dev.name, sizeof(dev.name), "%s", name);

    dev.rssi = disc->rssi;
    dev.last_seen_ms = now_ms();
    store_device(&dev);
}

/* ------------------------------------------------------------------ */
/* NimBLE                                                              */
/* ------------------------------------------------------------------ */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_adv(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(TAG, "Suche beendet, wird neu gestartet");
        break;
    default:
        break;
    }
    return 0;
}

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = 160,   /* 100 ms */
        .window = 160, /* durchgehend hoeren */
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        /* Aktiv: die Thermometer senden ihren Namen erst auf Nachfrage. Er
         * wird einmal je Geraet gebraucht, die Messwerte kommen unabhaengig
         * davon weiter im Rundruf. */
        .passive = 0,
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Suche liess sich nicht starten: %d", rc);
    }
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "Bluetooth bereit, Suche laeuft");
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "Bluetooth zurueckgesetzt, Grund %d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t atc_ble_start(atc_cb_t cb, void *ctx)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_cb = cb;
    s_ctx = ctx;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE liess sich nicht starten: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
