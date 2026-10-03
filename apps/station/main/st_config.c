#include "st_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atc_decode.h"
#include "cJSON.h"
#include "cfgjson.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"

static const char *TAG = "cfg";

#define NVS_NS  "station"
#define NVS_KEY "cfg"

static st_config_t s_cfg;
static SemaphoreHandle_t s_mtx;

static void defaults(st_config_t *c)
{
    memset(c, 0, sizeof(*c));
    snprintf(c->wifi.hostname, sizeof(c->wifi.hostname), "leitstand");
    snprintf(c->wifi.timezone, sizeof(c->wifi.timezone), "CET-1CEST,M3.5.0,M10.5.0/3");
    snprintf(c->site, sizeof(c->site), "Leitstand");
    /* 30 s wie der fruehere Mitschnitt am Rechner; ob die Heizungsgeraete 10 s
     * tragen, klaert Etappe 2 anhand ihres Neustartgrunds. */
    c->poll_heat_s = 30;
    c->poll_manifold_s = 30;
    c->brightness = 160;
    c->dim_after_min = 2;
    c->night_from_h = 23;
    c->night_to_h = 6;
}

/* Kennwort nur uebernehmen, wenn eines kam: Die Oberflaeche bekommt es nie zu
 * sehen und kann es deshalb nicht zuruecksenden. `<name>_clear` loescht es. */
static void kennwort(const cJSON *obj, const char *name, char *dst, size_t len)
{
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (cJSON_IsString(j) && j->valuestring && j->valuestring[0]) {
        snprintf(dst, len, "%s", j->valuestring);
    }
    char clear[24];
    snprintf(clear, sizeof(clear), "%s_clear", name);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, clear))) {
        dst[0] = '\0';
    }
}

static void overlay(const cJSON *root, st_config_t *c)
{
    cfgjson_str(root, "site", c->site, sizeof(c->site));

    const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (cJSON_IsObject(w)) {
        cfgjson_str(w, "ssid", c->wifi.ssid, sizeof(c->wifi.ssid));
        cfgjson_str(w, "hostname", c->wifi.hostname, sizeof(c->wifi.hostname));
        cfgjson_str(w, "timezone", c->wifi.timezone, sizeof(c->wifi.timezone));
        kennwort(w, "pass", c->wifi.pass, sizeof(c->wifi.pass));
        kennwort(w, "ap_pass", c->wifi.ap_pass, sizeof(c->wifi.ap_pass));
    }

    const cJSON *o = cJSON_GetObjectItemCaseSensitive(root, "outdoor");
    if (cJSON_IsObject(o)) {
        cfgjson_str(o, "mac", c->outdoor_mac, sizeof(c->outdoor_mac));
    }

    const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "poll");
    if (cJSON_IsObject(p)) {
        c->poll_heat_s = (uint16_t)cfgjson_num(p, "heat_s", c->poll_heat_s);
        c->poll_manifold_s = (uint16_t)cfgjson_num(p, "manifold_s", c->poll_manifold_s);
    }

    const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "display");
    if (cJSON_IsObject(d)) {
        c->brightness = (uint8_t)cfgjson_num(d, "brightness", c->brightness);
        c->dim_after_min = (uint8_t)cfgjson_num(d, "dim_after_min", c->dim_after_min);
        c->night_from_h = (int8_t)cfgjson_num(d, "night_from_h", c->night_from_h);
        c->night_to_h = (int8_t)cfgjson_num(d, "night_to_h", c->night_to_h);
    }
}

static bool pruefen(st_config_t *c, char *err, size_t len)
{
    size_t n = strlen(c->wifi.hostname);
    if (n == 0) {
        snprintf(err, len, "Der Gerätename darf nicht leer sein");
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char ch = c->wifi.hostname[i];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
        if (!ok) {
            snprintf(err, len, "Der Gerätename erlaubt nur Kleinbuchstaben, Ziffern und Bindestriche");
            return false;
        }
    }
    if (c->wifi.ap_pass[0] && strlen(c->wifi.ap_pass) < 8) {
        snprintf(err, len, "Das Kennwort des Einrichtungszugangs braucht mindestens 8 Zeichen");
        return false;
    }
    if (c->outdoor_mac[0]) {
        uint8_t mac[6];
        if (!atc_parse_mac(c->outdoor_mac, mac)) {
            snprintf(err, len, "Die Adresse des Außenfühlers ist ungültig");
            return false;
        }
        /* Einheitliche Schreibweise, damit der Vergleich beim Empfang greift. */
        snprintf(c->outdoor_mac, sizeof(c->outdoor_mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                 mac[2], mac[3], mac[4], mac[5]);
    }
    if (c->poll_heat_s < 5 || c->poll_heat_s > 300) {
        snprintf(err, len, "Der Abfragetakt der Heizungsgeräte muss zwischen 5 und 300 s liegen");
        return false;
    }
    if (c->poll_manifold_s < 10 || c->poll_manifold_s > 600) {
        snprintf(err, len, "Der Abfragetakt der Verteiler muss zwischen 10 und 600 s liegen");
        return false;
    }
    if (c->brightness < 10) {
        c->brightness = 10;
    }
    if (c->dim_after_min > 120) {
        snprintf(err, len, "Das Abdunkeln ist auf höchstens 120 Minuten einstellbar");
        return false;
    }
    if (c->night_from_h > 23 || c->night_to_h < 0 || c->night_to_h > 23) {
        snprintf(err, len, "Die Nachtzeit braucht volle Stunden zwischen 0 und 23");
        return false;
    }
    return true;
}

/* Vollstaendiges JSON fuer den NVS, mit Kennwoertern. */
static char *speicherform(const st_config_t *c)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "site", c->site);
    cJSON *w = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(w, "ssid", c->wifi.ssid);
    cJSON_AddStringToObject(w, "pass", c->wifi.pass);
    cJSON_AddStringToObject(w, "hostname", c->wifi.hostname);
    cJSON_AddStringToObject(w, "ap_pass", c->wifi.ap_pass);
    cJSON_AddStringToObject(w, "timezone", c->wifi.timezone);
    cJSON *o = cJSON_AddObjectToObject(root, "outdoor");
    cJSON_AddStringToObject(o, "mac", c->outdoor_mac);
    cJSON *p = cJSON_AddObjectToObject(root, "poll");
    cJSON_AddNumberToObject(p, "heat_s", c->poll_heat_s);
    cJSON_AddNumberToObject(p, "manifold_s", c->poll_manifold_s);
    cJSON *d = cJSON_AddObjectToObject(root, "display");
    cJSON_AddNumberToObject(d, "brightness", c->brightness);
    cJSON_AddNumberToObject(d, "dim_after_min", c->dim_after_min);
    cJSON_AddNumberToObject(d, "night_from_h", c->night_from_h);
    cJSON_AddNumberToObject(d, "night_to_h", c->night_to_h);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

esp_err_t st_cfg_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Der Speicher ist voll oder von einer anderen Fassung beschrieben.
         * Loeschen kostet die Einstellungen samt WLAN-Zugang; im Protokoll
         * soll der Grund stehen. */
        ESP_LOGW(TAG, "NVS unbrauchbar (%s), wird geloescht", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    s_mtx = xSemaphoreCreateMutex();
    defaults(&s_cfg);

    char *txt = NULL;
    if (cfgjson_load(NVS_NS, NVS_KEY, &txt) == ESP_OK && txt != NULL) {
        cJSON *root = cJSON_Parse(txt);
        memset(txt, 0, strlen(txt));
        free(txt);
        if (root != NULL) {
            st_config_t c = s_cfg;
            overlay(root, &c);
            cJSON_Delete(root);
            char err_txt[96];
            if (pruefen(&c, err_txt, sizeof(err_txt))) {
                s_cfg = c;
            } else {
                ESP_LOGW(TAG, "Gespeicherte Einstellungen verworfen: %s", err_txt);
            }
        }
    } else {
        ESP_LOGI(TAG, "Keine gespeicherten Einstellungen, Vorgaben gelten");
    }
    return ESP_OK;
}

void st_cfg_copy(st_config_t *out)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mtx);
}

char *st_cfg_to_json(const st_config_t *c)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "site", c->site);
    cJSON *w = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(w, "ssid", c->wifi.ssid);
    cJSON_AddBoolToObject(w, "pass_set", c->wifi.pass[0] != '\0');
    cJSON_AddStringToObject(w, "hostname", c->wifi.hostname);
    cJSON_AddBoolToObject(w, "ap_pass_set", c->wifi.ap_pass[0] != '\0');
    cJSON_AddStringToObject(w, "timezone", c->wifi.timezone);
    cJSON *o = cJSON_AddObjectToObject(root, "outdoor");
    cJSON_AddStringToObject(o, "mac", c->outdoor_mac);
    cJSON *p = cJSON_AddObjectToObject(root, "poll");
    cJSON_AddNumberToObject(p, "heat_s", c->poll_heat_s);
    cJSON_AddNumberToObject(p, "manifold_s", c->poll_manifold_s);
    cJSON *d = cJSON_AddObjectToObject(root, "display");
    cJSON_AddNumberToObject(d, "brightness", c->brightness);
    cJSON_AddNumberToObject(d, "dim_after_min", c->dim_after_min);
    cJSON_AddNumberToObject(d, "night_from_h", c->night_from_h);
    cJSON_AddNumberToObject(d, "night_to_h", c->night_to_h);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

esp_err_t st_cfg_from_json(const char *json, st_config_t *cfg, char *err, size_t err_len)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        snprintf(err, err_len, "Die Anfrage ist kein JSON");
        return ESP_ERR_INVALID_ARG;
    }
    overlay(root, cfg);
    cJSON_Delete(root);
    return pruefen(cfg, err, err_len) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t st_cfg_set(const st_config_t *cfg)
{
    char *txt = speicherform(cfg);
    if (txt == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t rc = cfgjson_save(NVS_NS, NVS_KEY, txt);
    memset(txt, 0, strlen(txt));
    free(txt);
    if (rc != ESP_OK) {
        return rc;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_cfg = *cfg;
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

esp_err_t st_cfg_reset(void)
{
    st_config_t c;
    defaults(&c);
    return st_cfg_set(&c);
}

void st_cfg_netmgr(const st_config_t *cfg, netmgr_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->ssid, sizeof(out->ssid), "%s", cfg->wifi.ssid);
    snprintf(out->pass, sizeof(out->pass), "%s", cfg->wifi.pass);
    snprintf(out->hostname, sizeof(out->hostname), "%s", cfg->wifi.hostname);
    snprintf(out->ap_pass, sizeof(out->ap_pass), "%s", cfg->wifi.ap_pass);
    snprintf(out->timezone, sizeof(out->timezone), "%s", cfg->wifi.timezone);
    /* Kein taeglicher Neustart: Der Leitstand haelt einen Verlauf im
     * Arbeitsspeicher, der sonst jeden Tag verloren ginge. */
    out->reboot_hour = -1;
    out->reboot_minute = 0;
}
