#include "config_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cfgjson.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "cfg";

#define NVS_NAMESPACE "fbh"
#define NVS_KEY_CFG   "cfg"
#define NVS_KEY_POS   "pos"

static app_config_t s_cfg;
static SemaphoreHandle_t s_lock;

/* ------------------------------------------------------------------ */
/* Werksvorgabe                                                        */
/* ------------------------------------------------------------------ */

static void room_defaults(cfg_room_t *r, uint8_t id, const char *name, uint16_t mask)
{
    memset(r, 0, sizeof(*r));
    r->id = id;
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->channel_mask = mask;
    r->mode_heat = true;
    r->target_c = 20.0f;
    r->p_band_k = 1.0f;
    r->interval_s = 30;
    r->min_delta = 0.01f;
    r->step = 0.1f;
}

#define CH(n) (1U << ((n) - 1))

void cfg_defaults(app_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->version = CFG_VERSION;

    /* Ohne Raeume und ohne Anlagenbezeichnung: die Oberflaeche fuehrt dann
     * durch die Einrichtung. Die Aufteilung des Erdgeschosses bietet sie
     * dort als Vorlage an - fest eingetragen waere sie auf einem Geraet fuer
     * eine andere Etage schlicht falsch. */
    out->site[0] = '\0';
    out->room_count = 0;

    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        out->channels[i].open_ms = 39000;
        out->channels[i].close_ms = 40000;
        out->channels[i].max_ms = 45000;
        out->channels[i].blank_ms = 2000;
        out->channels[i].bemf_mv = 190; /* Ausgangswert, die Messfahrt ersetzt ihn */
        out->channels[i].bemf_hyst_mv = 30;
        out->channels[i].calibrated = false;
    }

    /* Ausgangswerte. Die Messdauer haengt an der Treiberfassung, deshalb
     * sind sie nur ein Anhaltspunkt und werden am Geraet abgeglichen. */
    out->touch_enabled = true;
    out->touch_thresh[0] = 1000;
    out->touch_thresh[1] = 870;
    out->touch_thresh[2] = 1000;
    out->display_brightness = 2;

    out->sensor_timeout_s = 900;
    out->reboot_hour = 10;
    /* Samstag 11 Uhr: eine Stunde nach dem taeglichen Neustart, damit sich
     * beides nicht in die Quere kommt. */
    out->seize_weekday = 6;
    out->seize_hour = 11;
    out->reboot_minute = 0;
    snprintf(out->timezone, sizeof(out->timezone), "CET-1CEST,M3.5.0,M10.5.0/3");

    snprintf(out->wifi.hostname, sizeof(out->wifi.hostname), "floor-heating");
    snprintf(out->wifi.ap_pass, sizeof(out->wifi.ap_pass), "fussboden");

    out->mqtt.enabled = false;
    snprintf(out->mqtt.prefix, sizeof(out->mqtt.prefix), "fbh");
}

#undef CH

/* ------------------------------------------------------------------ */
/* Pruefung                                                            */
/* ------------------------------------------------------------------ */

#define FAIL(fmt, ...)                               \
    do {                                             \
        if (err && err_len) {                        \
            snprintf(err, err_len, fmt, ##__VA_ARGS__); \
        }                                            \
        return ESP_ERR_INVALID_ARG;                  \
    } while (0)

static esp_err_t cfg_validate(const app_config_t *c, char *err, size_t err_len)
{
    if (c->room_count > CFG_MAX_ROOMS) {
        FAIL("Zu viele Raeume (%u, hoechstens %d)", c->room_count, CFG_MAX_ROOMS);
    }

    uint16_t used = 0;
    for (int i = 0; i < c->room_count; i++) {
        const cfg_room_t *r = &c->rooms[i];
        if (r->id == 0) {
            FAIL("Raum %d hat keine Kennung", i + 1);
        }
        if (r->name[0] == '\0') {
            FAIL("Raum %u hat keinen Namen", r->id);
        }
        for (int j = 0; j < i; j++) {
            if (c->rooms[j].id == r->id) {
                FAIL("Raumkennung %u ist doppelt vergeben", r->id);
            }
        }
        if (r->channel_mask & ~((1U << HW_CHANNEL_COUNT) - 1)) {
            FAIL("Raum \"%s\" verweist auf einen Kanal ausserhalb 1..%d", r->name,
                 HW_CHANNEL_COUNT);
        }
        if (r->channel_mask & used) {
            FAIL("Raum \"%s\" belegt einen Kanal, der bereits vergeben ist", r->name);
        }
        used |= r->channel_mask;

        if (r->target_c < 5.0f || r->target_c > 35.0f) {
            FAIL("Solltemperatur in \"%s\" ausserhalb 5..35 Grad", r->name);
        }
        if (r->p_band_k < 0.2f || r->p_band_k > 10.0f) {
            FAIL("Proportionalband in \"%s\" ausserhalb 0,2..10 K", r->name);
        }
        if (r->interval_s < 5 || r->interval_s > 3600) {
            FAIL("Pruefintervall in \"%s\" ausserhalb 5..3600 s", r->name);
        }
        if (r->step < 0.01f || r->step > 0.5f) {
            FAIL("Rasterung in \"%s\" ausserhalb 0,01..0,5", r->name);
        }
        if (r->min_delta < 0.0f || r->min_delta > 0.5f) {
            FAIL("Mindestaenderung in \"%s\" ausserhalb 0..0,5", r->name);
        }
    }

    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        const cfg_channel_t *ch = &c->channels[i];
        if (ch->open_ms < 1000 || ch->open_ms > 300000) {
            FAIL("Oeffnungszeit CH%d ausserhalb 1..300 s", i + 1);
        }
        if (ch->close_ms < 1000 || ch->close_ms > 300000) {
            FAIL("Schliesszeit CH%d ausserhalb 1..300 s", i + 1);
        }
        uint32_t longest = ch->open_ms > ch->close_ms ? ch->open_ms : ch->close_ms;
        if (ch->max_ms < longest) {
            FAIL("Maximallaufzeit CH%d ist kuerzer als die Fahrzeit", i + 1);
        }
        if (ch->max_ms > 600000) {
            FAIL("Maximallaufzeit CH%d ueber 600 s", i + 1);
        }
        if (ch->bemf_mv < 10 || ch->bemf_mv > 2200) {
            FAIL("BEMF-Schwelle CH%d ausserhalb 10..2200 mV", i + 1);
        }
        if (ch->bemf_hyst_mv > ch->bemf_mv) {
            FAIL("BEMF-Hysterese CH%d groesser als die Schwelle", i + 1);
        }
        if (ch->blank_ms > 30000) {
            FAIL("Sperrzeit CH%d ueber 30 s", i + 1);
        }
    }

    if (c->sensor_timeout_s < 60 || c->sensor_timeout_s > 43200) {
        FAIL("Sensorueberwachung ausserhalb 60..43200 s");
    }
    if (c->seize_weekday > 6 || c->seize_hour < 0 || c->seize_hour > 23) {
        FAIL("Termin der Schutzfahrt liegt ausserhalb des Moeglichen");
    }
    if (c->reboot_hour > 23 || c->reboot_minute < 0 || c->reboot_minute > 59) {
        FAIL("Zeitpunkt des taeglichen Neustarts ist ungueltig");
    }
    if (c->display_brightness > 100) {
        FAIL("Helligkeit der Anzeige ausserhalb 0..100 Prozent");
    }

    return ESP_OK;
}

#undef FAIL

/* ------------------------------------------------------------------ */
/* JSON                                                                */
/* ------------------------------------------------------------------ */

static void mac_to_str(const uint8_t mac[6], char *out, size_t len)
{
    snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
}

static bool str_to_mac(const char *s, uint8_t mac[6])
{
    unsigned v[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (v[i] > 0xFF) {
            return false;
        }
        mac[i] = (uint8_t)v[i];
    }
    return true;
}

char *cfg_to_json(const app_config_t *cfg, bool include_secrets)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "cfg_version", cfg->version);
    cJSON_AddStringToObject(root, "site", cfg->site);

    cJSON *rooms = cJSON_AddArrayToObject(root, "rooms");
    for (int i = 0; i < cfg->room_count; i++) {
        const cfg_room_t *r = &cfg->rooms[i];
        cJSON *jr = cJSON_CreateObject();
        cJSON_AddNumberToObject(jr, "id", r->id);
        cJSON_AddStringToObject(jr, "name", r->name);

        cJSON *chs = cJSON_AddArrayToObject(jr, "channels");
        for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
            if (r->channel_mask & (1U << (n - 1))) {
                cJSON_AddItemToArray(chs, cJSON_CreateNumber(n));
            }
        }

        if (r->sensor_set) {
            char mac[18];
            mac_to_str(r->sensor_mac, mac, sizeof(mac));
            cJSON_AddStringToObject(jr, "sensor_mac", mac);
        } else {
            cJSON_AddNullToObject(jr, "sensor_mac");
        }

        cJSON_AddStringToObject(jr, "mode", r->mode_heat ? "heat" : "off");
        cJSON_AddNumberToObject(jr, "target_c", r->target_c);
        cJSON_AddNumberToObject(jr, "p_band_k", r->p_band_k);
        cJSON_AddNumberToObject(jr, "interval_s", r->interval_s);
        cJSON_AddNumberToObject(jr, "min_delta", r->min_delta);
        cJSON_AddNumberToObject(jr, "step", r->step);
        cJSON_AddItemToArray(rooms, jr);
    }

    cJSON *chans = cJSON_AddArrayToObject(root, "channels");
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        const cfg_channel_t *c = &cfg->channels[i];
        cJSON *jc = cJSON_CreateObject();
        cJSON_AddNumberToObject(jc, "id", i + 1);
        cJSON_AddNumberToObject(jc, "open_ms", c->open_ms);
        cJSON_AddNumberToObject(jc, "close_ms", c->close_ms);
        cJSON_AddNumberToObject(jc, "max_ms", c->max_ms);
        cJSON_AddNumberToObject(jc, "blank_ms", c->blank_ms);
        cJSON_AddNumberToObject(jc, "bemf_mv", c->bemf_mv);
        cJSON_AddNumberToObject(jc, "bemf_hyst_mv", c->bemf_hyst_mv);
        cJSON_AddBoolToObject(jc, "calibrated", c->calibrated);
        cJSON_AddNumberToObject(jc, "bemf_group", hw_group_of_channel(i + 1) + 1);
        cJSON_AddItemToArray(chans, jc);
    }

    cJSON *touch = cJSON_AddObjectToObject(root, "touch");
    cJSON_AddBoolToObject(touch, "enabled", cfg->touch_enabled);
    cJSON *th = cJSON_AddArrayToObject(touch, "thresholds");
    for (int i = 0; i < HW_TOUCH_COUNT; i++) {
        cJSON_AddItemToArray(th, cJSON_CreateNumber(cfg->touch_thresh[i]));
    }
    cJSON_AddNumberToObject(root, "display_brightness", cfg->display_brightness);

    cJSON_AddNumberToObject(root, "sensor_timeout_s", cfg->sensor_timeout_s);
    cJSON_AddNumberToObject(root, "reboot_hour", cfg->reboot_hour);
    cJSON_AddNumberToObject(root, "seize_weekday", cfg->seize_weekday);
    cJSON_AddNumberToObject(root, "seize_hour", cfg->seize_hour);
    cJSON_AddNumberToObject(root, "reboot_minute", cfg->reboot_minute);
    cJSON_AddStringToObject(root, "timezone", cfg->timezone);

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", cfg->wifi.ssid);
    cJSON_AddStringToObject(wifi, "hostname", cfg->wifi.hostname);
    cJSON_AddBoolToObject(wifi, "pass_set", cfg->wifi.pass[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(wifi, "pass", cfg->wifi.pass);
        cJSON_AddStringToObject(wifi, "ap_pass", cfg->wifi.ap_pass);
    }

    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", cfg->mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "uri", cfg->mqtt.uri);
    cJSON_AddStringToObject(mqtt, "user", cfg->mqtt.user);
    cJSON_AddStringToObject(mqtt, "prefix", cfg->mqtt.prefix);
    cJSON_AddBoolToObject(mqtt, "pass_set", cfg->mqtt.pass[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(mqtt, "pass", cfg->mqtt.pass);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

esp_err_t cfg_from_json(const char *json, app_config_t *out, char *err, size_t err_len)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        if (err && err_len) {
            snprintf(err, err_len, "Die Konfiguration ist kein gueltiges JSON");
        }
        return ESP_ERR_INVALID_ARG;
    }

    /* Vorgaben als Ausgangsbasis, damit unvollstaendige Dokumente sinnvolle
     * Werte erhalten. Bestehende Zugangsdaten werden vom Aufrufer vorher in
     * out gelegt und nur bei ausdruecklicher Angabe ueberschrieben. */
    out->version = CFG_VERSION;
    cfgjson_str(root, "site", out->site, sizeof(out->site));

    esp_err_t ret = ESP_OK;

    const cJSON *rooms = cJSON_GetObjectItemCaseSensitive(root, "rooms");
    if (cJSON_IsArray(rooms)) {
        int n = cJSON_GetArraySize(rooms);
        if (n > CFG_MAX_ROOMS) {
            if (err && err_len) {
                snprintf(err, err_len, "Hoechstens %d Raeume moeglich", CFG_MAX_ROOMS);
            }
            ret = ESP_ERR_INVALID_ARG;
            goto done;
        }
        memset(out->rooms, 0, sizeof(out->rooms));
        out->room_count = (uint8_t)n;
        for (int i = 0; i < n; i++) {
            const cJSON *jr = cJSON_GetArrayItem(rooms, i);
            cfg_room_t *r = &out->rooms[i];
            room_defaults(r, (uint8_t)cfgjson_num(jr, "id", i + 1), "", 0);
            cfgjson_str(jr, "name", r->name, sizeof(r->name));

            const cJSON *chs = cJSON_GetObjectItemCaseSensitive(jr, "channels");
            r->channel_mask = 0;
            if (cJSON_IsArray(chs)) {
                const cJSON *c = NULL;
                cJSON_ArrayForEach(c, chs)
                {
                    int num = cJSON_IsNumber(c) ? c->valueint : 0;
                    if (num >= 1 && num <= HW_CHANNEL_COUNT) {
                        r->channel_mask |= 1U << (num - 1);
                    }
                }
            }

            const cJSON *mac = cJSON_GetObjectItemCaseSensitive(jr, "sensor_mac");
            if (cJSON_IsString(mac) && mac->valuestring && mac->valuestring[0]) {
                if (!str_to_mac(mac->valuestring, r->sensor_mac)) {
                    if (err && err_len) {
                        snprintf(err, err_len, "MAC-Adresse in \"%s\" ist ungueltig", r->name);
                    }
                    ret = ESP_ERR_INVALID_ARG;
                    goto done;
                }
                r->sensor_set = true;
            }

            const cJSON *mode = cJSON_GetObjectItemCaseSensitive(jr, "mode");
            if (cJSON_IsString(mode) && mode->valuestring) {
                r->mode_heat = strcmp(mode->valuestring, "off") != 0;
            }
            r->target_c = (float)cfgjson_num(jr, "target_c", r->target_c);
            r->p_band_k = (float)cfgjson_num(jr, "p_band_k", r->p_band_k);
            r->interval_s = (uint16_t)cfgjson_num(jr, "interval_s", r->interval_s);
            r->min_delta = (float)cfgjson_num(jr, "min_delta", r->min_delta);
            r->step = (float)cfgjson_num(jr, "step", r->step);
        }
    }

    const cJSON *chans = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (cJSON_IsArray(chans)) {
        const cJSON *jc = NULL;
        cJSON_ArrayForEach(jc, chans)
        {
            int id = (int)cfgjson_num(jc, "id", 0);
            if (id < 1 || id > HW_CHANNEL_COUNT) {
                continue;
            }
            cfg_channel_t *c = &out->channels[id - 1];
            c->open_ms = (uint32_t)cfgjson_num(jc, "open_ms", c->open_ms);
            c->close_ms = (uint32_t)cfgjson_num(jc, "close_ms", c->close_ms);
            c->max_ms = (uint32_t)cfgjson_num(jc, "max_ms", c->max_ms);
            c->blank_ms = (uint32_t)cfgjson_num(jc, "blank_ms", c->blank_ms);
            c->bemf_mv = (uint16_t)cfgjson_num(jc, "bemf_mv", c->bemf_mv);
            c->bemf_hyst_mv = (uint16_t)cfgjson_num(jc, "bemf_hyst_mv", c->bemf_hyst_mv);
            c->calibrated = cfgjson_bool(jc, "calibrated", c->calibrated);
        }
    }

    const cJSON *touch = cJSON_GetObjectItemCaseSensitive(root, "touch");
    if (cJSON_IsObject(touch)) {
        out->touch_enabled = cfgjson_bool(touch, "enabled", out->touch_enabled);
        const cJSON *th = cJSON_GetObjectItemCaseSensitive(touch, "thresholds");
        if (cJSON_IsArray(th)) {
            for (int i = 0; i < HW_TOUCH_COUNT && i < cJSON_GetArraySize(th); i++) {
                const cJSON *v = cJSON_GetArrayItem(th, i);
                if (cJSON_IsNumber(v)) {
                    out->touch_thresh[i] = (uint16_t)v->valueint;
                }
            }
        }
    }
    out->display_brightness =
        (uint8_t)cfgjson_num(root, "display_brightness", out->display_brightness);

    out->sensor_timeout_s = (uint16_t)cfgjson_num(root, "sensor_timeout_s", out->sensor_timeout_s);
    out->reboot_hour = (int8_t)cfgjson_num(root, "reboot_hour", out->reboot_hour);
    out->seize_weekday = (int8_t)cfgjson_num(root, "seize_weekday", out->seize_weekday);
    out->seize_hour = (int8_t)cfgjson_num(root, "seize_hour", out->seize_hour);
    out->reboot_minute = (int8_t)cfgjson_num(root, "reboot_minute", out->reboot_minute);
    cfgjson_str(root, "timezone", out->timezone, sizeof(out->timezone));

    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (cJSON_IsObject(wifi)) {
        cfgjson_str(wifi, "ssid", out->wifi.ssid, sizeof(out->wifi.ssid));
        cfgjson_str(wifi, "hostname", out->wifi.hostname, sizeof(out->wifi.hostname));
        cfgjson_str(wifi, "pass", out->wifi.pass, sizeof(out->wifi.pass));
        cfgjson_str(wifi, "ap_pass", out->wifi.ap_pass, sizeof(out->wifi.ap_pass));
    }

    const cJSON *mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        out->mqtt.enabled = cfgjson_bool(mqtt, "enabled", out->mqtt.enabled);
        cfgjson_str(mqtt, "uri", out->mqtt.uri, sizeof(out->mqtt.uri));
        cfgjson_str(mqtt, "user", out->mqtt.user, sizeof(out->mqtt.user));
        cfgjson_str(mqtt, "pass", out->mqtt.pass, sizeof(out->mqtt.pass));
        cfgjson_str(mqtt, "prefix", out->mqtt.prefix, sizeof(out->mqtt.prefix));
    }

done:
    cJSON_Delete(root);
    return ret;
}

/* ------------------------------------------------------------------ */
/* NVS                                                                 */
/* ------------------------------------------------------------------ */

static esp_err_t cfg_store(const app_config_t *cfg)
{
    char *json = cfg_to_json(cfg, true);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = cfgjson_save(NVS_NAMESPACE, NVS_KEY_CFG, json);
    free(json);
    return err;
}

static esp_err_t cfg_load(app_config_t *out)
{
    char *buf = NULL;
    esp_err_t err = cfgjson_load(NVS_NAMESPACE, NVS_KEY_CFG, &buf);
    if (err != ESP_OK) {
        return err;
    }

    cfg_defaults(out);
    char msg[128];
    err = cfg_from_json(buf, out, msg, sizeof(msg));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gespeicherte Konfiguration unbrauchbar: %s", msg);
    }
    free(buf);
    return err;
}

void cfg_netmgr(const app_config_t *cfg, netmgr_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->ssid, sizeof(out->ssid), "%s", cfg->wifi.ssid);
    snprintf(out->pass, sizeof(out->pass), "%s", cfg->wifi.pass);
    snprintf(out->hostname, sizeof(out->hostname), "%s", cfg->wifi.hostname);
    snprintf(out->ap_pass, sizeof(out->ap_pass), "%s", cfg->wifi.ap_pass);
    snprintf(out->timezone, sizeof(out->timezone), "%s", cfg->timezone);
    out->reboot_hour = cfg->reboot_hour;
    out->reboot_minute = cfg->reboot_minute;
}

esp_err_t cfg_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /*
         * Der Speicher ist voll oder von einer anderen Fassung beschrieben.
         * Es bleibt nur das Loeschen -- damit ist aber die gesamte
         * Konfiguration weg, einschliesslich der WLAN-Zugangsdaten. Das darf
         * nicht stillschweigend geschehen: wer das Geraet danach im
         * Zugangspunkt-Betrieb vorfindet, soll im Protokoll den Grund finden.
         */
        ESP_LOGE(TAG, "Konfigurationsspeicher unbrauchbar (%s) -- er wird geleert. "
                      "Die gesamte Einrichtung geht dabei verloren.",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    app_config_t loaded;
    if (cfg_load(&loaded) == ESP_OK) {
        char msg[128] = {0};
        if (cfg_validate(&loaded, msg, sizeof(msg)) == ESP_OK) {
            s_cfg = loaded;
            ESP_LOGI(TAG, "Konfiguration geladen: %u Raeume", s_cfg.room_count);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Gespeicherte Konfiguration abgelehnt: %s", msg);
    }

    ESP_LOGW(TAG, "Werksvorgabe wird geschrieben");
    cfg_defaults(&s_cfg);
    return cfg_store(&s_cfg);
}

void cfg_lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void cfg_unlock(void)
{
    xSemaphoreGive(s_lock);
}

const app_config_t *cfg_peek(void)
{
    return &s_cfg;
}

void cfg_copy(app_config_t *out)
{
    cfg_lock();
    *out = s_cfg;
    cfg_unlock();
}

esp_err_t cfg_set(const app_config_t *in, char *err, size_t err_len)
{
    esp_err_t rc = cfg_validate(in, err, err_len);
    if (rc != ESP_OK) {
        return rc;
    }

    cfg_lock();
    app_config_t previous = s_cfg;
    s_cfg = *in;
    s_cfg.version = CFG_VERSION;
    cfg_unlock();

    rc = cfg_store(&s_cfg);
    if (rc != ESP_OK) {
        cfg_lock();
        s_cfg = previous;
        cfg_unlock();
        if (err && err_len) {
            snprintf(err, err_len, "Speichern fehlgeschlagen: %s", esp_err_to_name(rc));
        }
    }
    return rc;
}

esp_err_t cfg_reset_defaults(void)
{
    app_config_t d;
    cfg_defaults(&d);
    char err[128];
    return cfg_set(&d, err, sizeof(err));
}

const cfg_room_t *cfg_find_room(const app_config_t *cfg, uint8_t room_id)
{
    for (int i = 0; i < cfg->room_count; i++) {
        if (cfg->rooms[i].id == room_id) {
            return &cfg->rooms[i];
        }
    }
    return NULL;
}

const cfg_room_t *cfg_room_of_channel(const app_config_t *cfg, uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return NULL;
    }
    uint16_t bit = 1U << (channel - 1);
    for (int i = 0; i < cfg->room_count; i++) {
        if (cfg->rooms[i].channel_mask & bit) {
            return &cfg->rooms[i];
        }
    }
    return NULL;
}

uint8_t cfg_next_room_id(const app_config_t *cfg)
{
    for (uint8_t id = 1; id < 255; id++) {
        if (cfg_find_room(cfg, id) == NULL) {
            return id;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Ventilstellungen                                                    */
/* ------------------------------------------------------------------ */

esp_err_t cfg_save_positions(const cfg_positions_t *pos)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY_POS, pos, sizeof(*pos));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t cfg_load_positions(cfg_positions_t *pos)
{
    memset(pos, 0, sizeof(*pos));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = sizeof(*pos);
    err = nvs_get_blob(h, NVS_KEY_POS, pos, &len);
    nvs_close(h);

    if (err == ESP_OK && len != sizeof(*pos)) {
        memset(pos, 0, sizeof(*pos));
        err = ESP_ERR_INVALID_SIZE;
    }
    return err;
}
