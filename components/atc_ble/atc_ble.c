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
#include "nvs.h"

static const char *TAG = "ble";

#define MFG_RUUVI 0x0499
#define UUID_ENV_SENSING 0x181A
#define UUID_BTHOME 0xFCD2

/* Eigener Eintrag neben der Konfiguration: Die steht als ein JSON-Text im
 * NVS, dessen Laenge begrenzt ist, und jeder Raum wird beim Speichern aus
 * den Vorgaben neu aufgebaut. Ein Schluessel dort ginge bei jeder
 * Raumaenderung verloren, die ihn nicht mitschickt. */
#define NVS_NAMESPACE "fbh"
#define NVS_KEYS "blekeys"
#define KEYS_VERSION 1

/* Nach einem gueltigen Rahmen gilt ein einzelner mit falscher Pruefsumme
 * eine Minute lang als Stoerung, nicht als falscher Schluessel. Sonst liesse
 * sich die Anzeige mit gefaelschten Paketen unter fremder Adresse umschalten. */
#define KEY_WRONG_GRACE_MS 60000u

static atc_device_t s_devices[ATC_MAX_DEVICES];
static size_t s_device_count;
static atc_key_t s_keys[ATC_MAX_KEYS];
static size_t s_key_count;
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

/* Platz eines Geraets in der Liste, bei Bedarf neu angelegt. Aufruf mit
 * gehaltenem s_mtx. */
static atc_device_t *slot_for(const uint8_t mac[6], const char *name, atc_format_t format)
{
    for (size_t i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) {
            return &s_devices[i];
        }
    }
    atc_device_t *slot = NULL;
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
    memcpy(slot->mac, mac, 6);
    ESP_LOGI(TAG, "Neues Thermometer %s %02X:%02X:%02X:%02X:%02X:%02X (%s)",
             name[0] ? name : "ohne Namen", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             atc_format_name(format));
    return slot;
}

static void store_device(const atc_device_t *in)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    atc_device_t *slot = slot_for(in->mac, in->name, in->format);

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

/* ------------------------------------------------------------------ */
/* Schluessel                                                          */
/* ------------------------------------------------------------------ */

/* Aufruf mit gehaltenem s_mtx. */
static atc_key_t *key_find(const uint8_t mac[6])
{
    for (size_t i = 0; i < s_key_count; i++) {
        if (memcmp(s_keys[i].mac, mac, 6) == 0) {
            return &s_keys[i];
        }
    }
    return NULL;
}

static bool key_lookup(const uint8_t mac[6], uint8_t key[16])
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    const atc_key_t *k = key_find(mac);
    if (k != NULL) {
        memcpy(key, k->key, 16);
    }
    xSemaphoreGive(s_mtx);
    return k != NULL;
}

/* Nach einem neuen oder entfernten Schluessel beginnt die Pruefung des
 * Geraets von vorn: Zaehler vergessen, der naechste Rahmen entscheidet.
 * Aufruf mit gehaltenem s_mtx. */
static void device_key_changed(const uint8_t mac[6])
{
    for (size_t i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) {
            s_devices[i].counter_set = false;
            s_devices[i].counter = 0;
            s_devices[i].counter_ms = 0;
        }
    }
}

/* Aufruf mit gehaltenem s_mtx. */
static esp_err_t keys_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if (s_key_count == 0) {
        err = nvs_erase_key(h, NVS_KEYS);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        uint8_t buf[1 + ATC_MAX_KEYS * sizeof(atc_key_t)];
        buf[0] = KEYS_VERSION;
        memcpy(buf + 1, s_keys, s_key_count * sizeof(atc_key_t));
        err = nvs_set_blob(h, NVS_KEYS, buf, 1 + s_key_count * sizeof(atc_key_t));
        memset(buf, 0, sizeof(buf));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void keys_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t buf[1 + ATC_MAX_KEYS * sizeof(atc_key_t)];
    size_t len = sizeof(buf);
    esp_err_t err = nvs_get_blob(h, NVS_KEYS, buf, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return;
    }
    if (len < 1 || buf[0] != KEYS_VERSION || (len - 1) % sizeof(atc_key_t) != 0) {
        ESP_LOGE(TAG, "Gespeicherte Schluessel unlesbar, sie werden nicht verwendet");
        memset(buf, 0, sizeof(buf));
        return;
    }
    s_key_count = (len - 1) / sizeof(atc_key_t);
    memcpy(s_keys, buf + 1, s_key_count * sizeof(atc_key_t));
    memset(buf, 0, sizeof(buf));
    ESP_LOGI(TAG, "%u Schluessel fuer verschluesselte Thermometer geladen", (unsigned)s_key_count);
}

esp_err_t atc_ble_key_set(const uint8_t mac[6], const uint8_t key[16])
{
    if (s_mtx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    atc_key_t vorher[ATC_MAX_KEYS];
    size_t vorher_n = s_key_count;
    memcpy(vorher, s_keys, sizeof(vorher));

    atc_key_t *k = key_find(mac);
    if (key == NULL) {
        if (k != NULL) {
            *k = s_keys[--s_key_count];
            memset(&s_keys[s_key_count], 0, sizeof(atc_key_t));
        }
    } else if (k != NULL) {
        memcpy(k->key, key, 16);
    } else if (s_key_count < ATC_MAX_KEYS) {
        memcpy(s_keys[s_key_count].mac, mac, 6);
        memcpy(s_keys[s_key_count].key, key, 16);
        s_key_count++;
    } else {
        xSemaphoreGive(s_mtx);
        memset(vorher, 0, sizeof(vorher));
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = keys_save();
    if (err != ESP_OK) {
        /* Nicht gespeichert heisst auch nicht wirksam: Nach dem naechsten
         * Neustart waere der Schluessel sonst stillschweigend weg. */
        memcpy(s_keys, vorher, sizeof(s_keys));
        s_key_count = vorher_n;
    } else {
        device_key_changed(mac);
    }
    xSemaphoreGive(s_mtx);
    memset(vorher, 0, sizeof(vorher));
    return err;
}

size_t atc_ble_keys(atc_key_t *out, size_t max)
{
    if (s_mtx == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t n = s_key_count < max ? s_key_count : max;
    memcpy(out, s_keys, n * sizeof(atc_key_t));
    xSemaphoreGive(s_mtx);
    return n;
}

esp_err_t atc_ble_keys_replace(const atc_key_t *keys, size_t n)
{
    if (s_mtx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (n > ATC_MAX_KEYS) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    atc_key_t vorher[ATC_MAX_KEYS];
    size_t vorher_n = s_key_count;
    memcpy(vorher, s_keys, sizeof(vorher));

    memset(s_keys, 0, sizeof(s_keys));
    if (n > 0) {
        memcpy(s_keys, keys, n * sizeof(atc_key_t));
    }
    s_key_count = n;
    esp_err_t err = keys_save();
    if (err != ESP_OK) {
        memcpy(s_keys, vorher, sizeof(s_keys));
        s_key_count = vorher_n;
    } else {
        for (size_t i = 0; i < s_device_count; i++) {
            device_key_changed(s_devices[i].mac);
        }
    }
    xSemaphoreGive(s_mtx);
    memset(vorher, 0, sizeof(vorher));
    return err;
}

/* ------------------------------------------------------------------ */
/* BTHome                                                              */
/* ------------------------------------------------------------------ */

/*
 * Ein BTHome-Geraet verteilt seine Werte unter Umstaenden auf mehrere
 * Rundrufe; der Climate-Sat mit Lagesensor wechselt zwischen einem Satz mit
 * Temperatur und einem mit Neigung. Uebernommen wird deshalb feldweise: Was
 * ein Paket nicht enthaelt, bleibt stehen. Der Regelung gemeldet wird nur
 * ein Paket, das selbst eine Temperatur trug -- sonst hielte ein Paket ohne
 * sie einen alten Messwert kuenstlich frisch.
 */
static void store_bthome(const uint8_t mac[6], const char *name, int8_t rssi,
                         atc_bthome_result_t res, const atc_bthome_t *b)
{
    uint32_t now = now_ms();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    atc_device_t *slot = slot_for(mac, name, ATC_FMT_BTHOME);

    if (res == ATC_BTHOME_KEY_WRONG && slot->key == ATC_KEY_OK &&
        (uint32_t)(now - slot->counter_ms) < KEY_WRONG_GRACE_MS) {
        xSemaphoreGive(s_mtx);
        return;
    }
    if (res == ATC_BTHOME_OK && b->encrypted) {
        if (!atc_bthome_counter_fresh(slot->counter_set, slot->counter, slot->counter_ms,
                                      b->counter, now)) {
            /* Derselbe Rahmen noch einmal oder ein alter: nichts davon gilt. */
            xSemaphoreGive(s_mtx);
            return;
        }
        slot->counter_set = true;
        slot->counter = b->counter;
        slot->counter_ms = now;
    }

    slot->format = ATC_FMT_BTHOME;
    slot->encrypted = b->encrypted;
    slot->rssi = rssi;
    slot->last_seen_ms = now;
    slot->packets++;
    if (name[0] != '\0') {
        snprintf(slot->name, sizeof(slot->name), "%s", name);
    }

    bool melden = false;
    if (res == ATC_BTHOME_OK) {
        slot->key = b->encrypted ? ATC_KEY_OK : ATC_KEY_NONE;
        if (b->has_temp) {
            slot->temp_c = b->temp_c;
            slot->has_temp = true;
            melden = true;
        }
        if (b->has_humidity) {
            slot->humidity = b->humidity;
            slot->has_humidity = true;
        }
        if (b->has_battery) {
            slot->battery = b->battery;
        }
        if (b->has_voltage) {
            slot->battery_mv = b->battery_mv;
        }
        if (b->has_pressure) {
            slot->pressure_hpa = b->pressure_hpa;
        }
    } else {
        /* Ohne passenden Schluessel gibt es keine Werte, und alte bleiben
         * nicht stehen, als waeren sie frisch. */
        slot->key = res == ATC_BTHOME_KEY_MISSING ? ATC_KEY_MISSING : ATC_KEY_WRONG;
        slot->has_temp = false;
        slot->has_humidity = false;
        slot->temp_c = 0.0f;
        slot->humidity = 0.0f;
        slot->battery = 0;
        slot->battery_mv = 0;
        slot->pressure_hpa = 0.0f;
    }

    atc_device_t copy = *slot;
    xSemaphoreGive(s_mtx);

    if (melden && s_cb) {
        s_cb(&copy, s_ctx);
    }
}

static void handle_bthome(const uint8_t mac[6], const char *name, int8_t rssi, const uint8_t *d,
                          size_t len)
{
    uint8_t key[16];
    bool mit_schluessel = key_lookup(mac, key);
    atc_bthome_t b;
    atc_bthome_result_t res = atc_decode_bthome(d, len, mac, mit_schluessel ? key : NULL, &b);
    memset(key, 0, sizeof(key));
    if (res == ATC_BTHOME_INVALID) {
        return;
    }
    /* Unplausible Werte verwerfen wie bei den anderen Formaten. */
    if (b.has_temp && (b.temp_c < -40.0f || b.temp_c > 80.0f)) {
        b.has_temp = false;
    }
    if (b.has_humidity && (b.humidity < 0.0f || b.humidity > 100.0f)) {
        b.has_humidity = false;
    }
    store_bthome(mac, name, rssi, res, &b);
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
            if (atc_decode_ruuvi(fields.mfg_data + 2, &rd)) {
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
    if (uuid == UUID_BTHOME) {
        /* Die Adresse kommt hier aus dem Sender, nicht aus dem Inhalt: BTHome
         * traegt sie meist nicht mit, und die Nonce braucht genau diese. */
        handle_bthome(addr, name, disc->rssi, sd + 2, (size_t)(fields.svc_data_uuid16_len - 2));
        return;
    }
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
        ok = atc_decode_atc1441(payload, &dev);
    } else if (len >= 15) {
        ok = atc_decode_pvvx(payload, &dev);
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

static bool s_pausiert;

static void start_scan(void)
{
    if (s_pausiert) {
        return;
    }
    /*
     * Der ESP32 hat einen Funkteil, den sich Bluetooth und WLAN teilen. Ein
     * Suchlauf mit window == itvl hoert durchgehend und gibt den Kanal nie
     * frei; der Zugangspunkt sendet dann seine Baken unregelmaessig, und eine
     * Anmeldung daran scheitert immer wieder. Dreissig von hundert
     * Millisekunden genuegen: Die Thermometer melden sich alle paar Sekunden,
     * ein verpasster Rundruf kostet nur Wartezeit, keine Messung.
     */
    struct ble_gap_disc_params params = {
        .itvl = 160,  /* 100 ms Abstand */
        .window = 48, /* davon 30 ms hoeren */
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

void atc_ble_pause(bool pausieren)
{
    if (s_pausiert == pausieren) {
        return;
    }
    s_pausiert = pausieren;
    if (pausieren) {
        ble_gap_disc_cancel();
        ESP_LOGW(TAG, "Suche angehalten, der Funkteil gehoert dem WLAN");
    } else {
        ESP_LOGI(TAG, "Suche laeuft wieder");
        start_scan();
    }
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
    keys_load();

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
