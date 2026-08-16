#include "config_store.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "cfg";

#define NVS_NAMESPACE "heiz"
#define NVS_KEY_CFG   "cfg"

static app_config_t s_cfg;
static SemaphoreHandle_t s_mtx;

/* Kuerzt beim Kopieren, statt den Zielpuffer zu ueberschreiten. */
static void copy_str(char *dst, size_t len, const char *src)
{
    if (len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, len, "%s", src);
}

/* ------------------------------------------------------------------ */
/* Rollen                                                              */
/* ------------------------------------------------------------------ */

static const struct {
    const char *key;
    const char *label;
} s_roles[ROLE_COUNT] = {
    [ROLE_NONE] = {"", "nicht zugeordnet"},
    [ROLE_ABGAS] = {"abgas", "Abgas"},
    [ROLE_KESSEL_VL] = {"kessel_vl", "Kessel Vorlauf"},
    [ROLE_KESSEL_RL] = {"kessel_rl", "Kessel Ruecklauf"},
    [ROLE_PUFFER] = {"puffer", "Pufferspeicher"},
    [ROLE_PUFFER_UNTEN] = {"puffer_unten", "Pufferspeicher unten"},
    [ROLE_HK1_VL] = {"hk1_vl", "Heizkreis 1 Vorlauf"},
    [ROLE_HK1_RL] = {"hk1_rl", "Heizkreis 1 Ruecklauf"},
    [ROLE_HK2_VL] = {"hk2_vl", "Heizkreis 2 Vorlauf"},
    [ROLE_HK2_RL] = {"hk2_rl", "Heizkreis 2 Ruecklauf"},
};

const char *cfg_role_key(probe_role_t role)
{
    return role < ROLE_COUNT ? s_roles[role].key : "";
}

const char *cfg_role_label(probe_role_t role)
{
    return role < ROLE_COUNT ? s_roles[role].label : "";
}

probe_role_t cfg_role_from_key(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return ROLE_NONE;
    }
    for (int i = 1; i < ROLE_COUNT; i++) {
        if (strcmp(key, s_roles[i].key) == 0) {
            return (probe_role_t)i;
        }
    }
    return ROLE_NONE;
}

/* ------------------------------------------------------------------ */
/* Werksvorgabe und Pruefung                                           */
/* ------------------------------------------------------------------ */

void cfg_defaults(app_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->version = CFG_VERSION;

    /* Leer: die Einrichtung entscheidet, ob dieses Geraet am Kessel oder am
     * Speicher sitzt. Solange nichts eingetragen ist, fuehrt die Oberflaeche
     * durch den Assistenten. */
    out->site[0] = '\0';

    out->onewire_pin[0] = 23;
    out->onewire_pin[1] = -1;
    out->poll_s = 10;
    out->probe_count = 0;
    out->circuit_count = 0;
    out->demand_poll_s = 5;
    out->demand_timeout_s = 180;

    out->reboot_hour = -1; /* Heizungsgeraete laufen durch */
    out->reboot_minute = 0;
    copy_str(out->timezone, sizeof(out->timezone), "CET-1CEST,M3.5.0,M10.5.0/3");

    copy_str(out->wifi.hostname, sizeof(out->wifi.hostname), "heizung");
    copy_str(out->wifi.ap_pass, sizeof(out->wifi.ap_pass), "fussboden");

    out->mqtt.enabled = false;
    copy_str(out->mqtt.prefix, sizeof(out->mqtt.prefix), "heiz");
}

static bool pin_ok(int8_t pin)
{
    if (pin < 0) {
        return true; /* nicht belegt */
    }
    /* Eingang-nur-Pins (34..39) koennen 1-Wire nicht treiben, GPIO 6..11
     * haengen am Flash. */
    if (pin > 33) {
        return false;
    }
    if (pin >= 6 && pin <= 11) {
        return false;
    }
    return true;
}

static esp_err_t cfg_validate(const app_config_t *cfg, char *err, size_t err_len)
{
#define FEHLER(...)                       \
    do {                                  \
        snprintf(err, err_len, __VA_ARGS__); \
        return ESP_ERR_INVALID_ARG;       \
    } while (0)

    if (cfg->probe_count > CFG_MAX_PROBES) {
        FEHLER("Hoechstens %d Fuehler moeglich", CFG_MAX_PROBES);
    }
    for (int i = 0; i < CFG_MAX_BUSES; i++) {
        if (!pin_ok(cfg->onewire_pin[i])) {
            FEHLER("GPIO %d ist fuer den 1-Wire-Bus nicht geeignet", cfg->onewire_pin[i]);
        }
    }
    if (cfg->onewire_pin[0] < 0 && cfg->onewire_pin[1] < 0) {
        FEHLER("Mindestens ein 1-Wire-Bus muss angegeben sein");
    }
    if (cfg->onewire_pin[0] >= 0 && cfg->onewire_pin[0] == cfg->onewire_pin[1]) {
        FEHLER("Beide Busse koennen nicht am selben GPIO liegen");
    }
    if (cfg->poll_s < 1 || cfg->poll_s > 600) {
        FEHLER("Abtastabstand muss zwischen 1 und 600 Sekunden liegen");
    }

    for (uint8_t i = 0; i < cfg->probe_count; i++) {
        const cfg_probe_t *p = &cfg->probes[i];
        if (p->rom == 0) {
            FEHLER("Fuehler %u ohne Kennung", (unsigned)(i + 1));
        }
        if (p->offset_k < -20.0f || p->offset_k > 20.0f) {
            FEHLER("Korrekturwert von %s liegt ausserhalb von -20 bis 20 K", p->name);
        }
        for (uint8_t j = 0; j < i; j++) {
            if (cfg->probes[j].rom == p->rom) {
                FEHLER("Fuehler %s ist doppelt eingetragen", p->name);
            }
            if (p->role != ROLE_NONE && cfg->probes[j].role == p->role) {
                FEHLER("Rolle \"%s\" ist zweimal vergeben", cfg_role_label(p->role));
            }
        }
    }

    if (cfg->circuit_count > CFG_MAX_CIRCUITS) {
        FEHLER("Hoechstens %d Heizkreise moeglich", CFG_MAX_CIRCUITS);
    }
    for (uint8_t i = 0; i < cfg->circuit_count; i++) {
        const cfg_circuit_t *c = &cfg->circuits[i];
        if (c->id == 0 || c->id > CFG_MAX_CIRCUITS) {
            FEHLER("Heizkreis mit unzulaessiger Kennung %u", (unsigned)c->id);
        }
        if (c->pump_relay < 1 || c->pump_relay > 8) {
            FEHLER("Relaisnummer von %s muss zwischen 1 und 8 liegen", c->name);
        }
        if (c->mode > 2) {
            FEHLER("Unbekannte Betriebsart bei %s", c->name);
        }
        if (c->min_buffer_c < 0.0f || c->min_buffer_c > 90.0f) {
            FEHLER("Mindesttemperatur von %s liegt ausserhalb von 0 bis 90 Grad", c->name);
        }
        if (c->frost_c < -10.0f || c->frost_c > 20.0f) {
            FEHLER("Frostgrenze von %s liegt ausserhalb von -10 bis 20 Grad", c->name);
        }
        if (c->overrun_s > 3600 || c->min_run_s > 3600 || c->min_pause_s > 3600) {
            FEHLER("Zeiten von %s duerfen hoechstens eine Stunde betragen", c->name);
        }
        if (c->peer_count > CFG_MAX_PEERS) {
            FEHLER("Hoechstens %d Verteiler je Heizkreis", CFG_MAX_PEERS);
        }
        for (uint8_t j = 0; j < i; j++) {
            if (cfg->circuits[j].id == c->id) {
                FEHLER("Heizkreis %u ist doppelt angelegt", (unsigned)c->id);
            }
        }
    }
    if (cfg->demand_poll_s < 1 || cfg->demand_poll_s > 300) {
        FEHLER("Abfrage der Verteiler muss zwischen 1 und 300 Sekunden liegen");
    }
    if (cfg->demand_timeout_s < 10 || cfg->demand_timeout_s > 3600) {
        FEHLER("Zeitgrenze der Verteiler muss zwischen 10 und 3600 Sekunden liegen");
    }

    if (cfg->wifi.ap_pass[0] && strlen(cfg->wifi.ap_pass) < 8) {
        FEHLER("Das Kennwort des Zugangspunkts braucht mindestens acht Zeichen");
    }
    if (cfg->mqtt.enabled && cfg->mqtt.uri[0] == '\0') {
        FEHLER("Ohne Adresse des Brokers laesst sich MQTT nicht einschalten");
    }
#undef FEHLER
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Suchen                                                              */
/* ------------------------------------------------------------------ */

const cfg_probe_t *cfg_probe_of_role(const app_config_t *cfg, probe_role_t role)
{
    if (role == ROLE_NONE) {
        return NULL;
    }
    for (uint8_t i = 0; i < cfg->probe_count; i++) {
        if (cfg->probes[i].role == role) {
            return &cfg->probes[i];
        }
    }
    return NULL;
}

void cfg_circuit_defaults(cfg_circuit_t *c, uint8_t id)
{
    memset(c, 0, sizeof(*c));
    c->id = id;
    c->enabled = true;
    snprintf(c->name, sizeof(c->name), "Heizkreis %u", (unsigned)id);
    c->vl_role = id == 1 ? ROLE_HK1_VL : ROLE_HK2_VL;
    c->rl_role = id == 1 ? ROLE_HK1_RL : ROLE_HK2_RL;
    c->pump_relay = 1;
    c->mode = 0;

    /* Dieselben Vorgaben wie in components/heatlogic. Die Mindesttemperatur
     * des Speichers liegt bewusst ueber der noetigen Vorlauftemperatur: der
     * Mischer kann nur herunterregeln. */
    c->overrun_s = 300;
    c->min_run_s = 180;
    c->min_pause_s = 180;
    c->min_buffer_c = 40.0f;
    c->frost_c = 6.0f;
    c->seize_days = 7;
}

const cfg_circuit_t *cfg_circuit(const app_config_t *cfg, uint8_t id)
{
    for (uint8_t i = 0; i < cfg->circuit_count; i++) {
        if (cfg->circuits[i].id == id) {
            return &cfg->circuits[i];
        }
    }
    return NULL;
}

const cfg_probe_t *cfg_probe_of_rom(const app_config_t *cfg, uint64_t rom)
{
    for (uint8_t i = 0; i < cfg->probe_count; i++) {
        if (cfg->probes[i].rom == rom) {
            return &cfg->probes[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* JSON                                                                */
/* ------------------------------------------------------------------ */

static void rom_to_str(uint64_t rom, char *out, size_t len)
{
    snprintf(out, len, "%016" PRIX64, rom);
}

static uint64_t str_to_rom(const char *s)
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
            continue;
        }
        v = (v << 4) | (uint64_t)d;
        digits++;
    }
    return digits == 16 ? v : 0;
}

char *cfg_to_json(const app_config_t *cfg, bool include_secrets)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "cfg_version", cfg->version);
    cJSON_AddStringToObject(root, "site", cfg->site);

    cJSON *bus = cJSON_AddArrayToObject(root, "onewire_pin");
    for (int i = 0; i < CFG_MAX_BUSES; i++) {
        cJSON_AddItemToArray(bus, cJSON_CreateNumber(cfg->onewire_pin[i]));
    }
    cJSON_AddNumberToObject(root, "poll_s", cfg->poll_s);

    cJSON *probes = cJSON_AddArrayToObject(root, "probes");
    for (uint8_t i = 0; i < cfg->probe_count; i++) {
        const cfg_probe_t *p = &cfg->probes[i];
        char rom[20];
        rom_to_str(p->rom, rom, sizeof(rom));
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "rom", rom);
        cJSON_AddStringToObject(j, "role", cfg_role_key(p->role));
        cJSON_AddStringToObject(j, "name", p->name);
        cJSON_AddNumberToObject(j, "offset_k", p->offset_k);
        cJSON_AddItemToArray(probes, j);
    }

    cJSON *kreise = cJSON_AddArrayToObject(root, "circuits");
    for (uint8_t i = 0; i < cfg->circuit_count; i++) {
        const cfg_circuit_t *c = &cfg->circuits[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "id", c->id);
        cJSON_AddStringToObject(j, "name", c->name);
        cJSON_AddBoolToObject(j, "enabled", c->enabled);
        cJSON_AddStringToObject(j, "vl_role", cfg_role_key(c->vl_role));
        cJSON_AddStringToObject(j, "rl_role", cfg_role_key(c->rl_role));
        cJSON *peers = cJSON_AddArrayToObject(j, "peers");
        for (uint8_t k = 0; k < c->peer_count; k++) {
            cJSON_AddItemToArray(peers, cJSON_CreateString(c->peers[k]));
        }
        cJSON *pump = cJSON_AddObjectToObject(j, "pump");
        cJSON_AddStringToObject(pump, "topic", c->pump_topic);
        cJSON_AddNumberToObject(pump, "relay", c->pump_relay);
        cJSON_AddStringToObject(j, "mode", c->mode == 1 ? "ein" : (c->mode == 2 ? "aus" : "auto"));
        cJSON_AddNumberToObject(j, "overrun_s", c->overrun_s);
        cJSON_AddNumberToObject(j, "min_run_s", c->min_run_s);
        cJSON_AddNumberToObject(j, "min_pause_s", c->min_pause_s);
        cJSON_AddNumberToObject(j, "min_buffer_c", c->min_buffer_c);
        cJSON_AddNumberToObject(j, "frost_c", c->frost_c);
        cJSON_AddNumberToObject(j, "seize_days", c->seize_days);
        cJSON_AddItemToArray(kreise, j);
    }
    cJSON_AddNumberToObject(root, "demand_poll_s", cfg->demand_poll_s);
    cJSON_AddNumberToObject(root, "demand_timeout_s", cfg->demand_timeout_s);

    cJSON_AddNumberToObject(root, "reboot_hour", cfg->reboot_hour);
    cJSON_AddNumberToObject(root, "reboot_minute", cfg->reboot_minute);
    cJSON_AddStringToObject(root, "timezone", cfg->timezone);

    cJSON *w = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(w, "ssid", cfg->wifi.ssid);
    cJSON_AddStringToObject(w, "hostname", cfg->wifi.hostname);
    if (include_secrets) {
        cJSON_AddStringToObject(w, "pass", cfg->wifi.pass);
        cJSON_AddStringToObject(w, "ap_pass", cfg->wifi.ap_pass);
    } else {
        cJSON_AddBoolToObject(w, "pass_set", cfg->wifi.pass[0] != '\0');
        cJSON_AddBoolToObject(w, "ap_pass_set", cfg->wifi.ap_pass[0] != '\0');
    }

    cJSON *m = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(m, "enabled", cfg->mqtt.enabled);
    cJSON_AddStringToObject(m, "uri", cfg->mqtt.uri);
    cJSON_AddStringToObject(m, "user", cfg->mqtt.user);
    cJSON_AddStringToObject(m, "prefix", cfg->mqtt.prefix);
    if (include_secrets) {
        cJSON_AddStringToObject(m, "pass", cfg->mqtt.pass);
    } else {
        cJSON_AddBoolToObject(m, "pass_set", cfg->mqtt.pass[0] != '\0');
    }

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return txt;
}

/* Lesehilfen: ein fehlender Schluessel laesst den bisherigen Wert stehen. */
static void json_str(const cJSON *o, const char *key, char *dst, size_t len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring) {
        copy_str(dst, len, v->valuestring);
    }
}

static double json_num(const cJSON *o, const char *key, double fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

static bool json_bool(const cJSON *o, const char *key, bool fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : fallback;
}

esp_err_t cfg_from_json(const char *json, app_config_t *out, char *err, size_t err_len)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        snprintf(err, err_len, "Die Konfiguration liess sich nicht lesen");
        return ESP_ERR_INVALID_ARG;
    }

    json_str(root, "site", out->site, sizeof(out->site));
    out->poll_s = (uint16_t)json_num(root, "poll_s", out->poll_s);

    const cJSON *bus = cJSON_GetObjectItemCaseSensitive(root, "onewire_pin");
    if (cJSON_IsArray(bus)) {
        for (int i = 0; i < CFG_MAX_BUSES; i++) {
            const cJSON *v = cJSON_GetArrayItem(bus, i);
            if (cJSON_IsNumber(v)) {
                out->onewire_pin[i] = (int8_t)v->valuedouble;
            }
        }
    }

    const cJSON *probes = cJSON_GetObjectItemCaseSensitive(root, "probes");
    if (cJSON_IsArray(probes)) {
        out->probe_count = 0;
        const cJSON *j = NULL;
        cJSON_ArrayForEach(j, probes) {
            if (out->probe_count >= CFG_MAX_PROBES) {
                break;
            }
            cfg_probe_t *p = &out->probes[out->probe_count];
            memset(p, 0, sizeof(*p));

            char rom[24] = {0};
            json_str(j, "rom", rom, sizeof(rom));
            p->rom = str_to_rom(rom);
            if (p->rom == 0) {
                continue; /* Eintrag ohne brauchbare Kennung wird uebergangen */
            }
            char role[24] = {0};
            json_str(j, "role", role, sizeof(role));
            p->role = cfg_role_from_key(role);
            json_str(j, "name", p->name, sizeof(p->name));
            p->offset_k = (float)json_num(j, "offset_k", 0.0);
            if (p->name[0] == '\0') {
                copy_str(p->name, sizeof(p->name),
                         p->role != ROLE_NONE ? cfg_role_label(p->role) : rom);
            }
            out->probe_count++;
        }
    }

    const cJSON *kreise = cJSON_GetObjectItemCaseSensitive(root, "circuits");
    if (cJSON_IsArray(kreise)) {
        out->circuit_count = 0;
        const cJSON *j = NULL;
        cJSON_ArrayForEach(j, kreise) {
            if (out->circuit_count >= CFG_MAX_CIRCUITS) {
                break;
            }
            cfg_circuit_t *c = &out->circuits[out->circuit_count];
            uint8_t id = (uint8_t)json_num(j, "id", out->circuit_count + 1);
            cfg_circuit_defaults(c, id);

            json_str(j, "name", c->name, sizeof(c->name));
            c->enabled = json_bool(j, "enabled", c->enabled);

            char rolle[24] = {0};
            json_str(j, "vl_role", rolle, sizeof(rolle));
            if (rolle[0]) {
                c->vl_role = cfg_role_from_key(rolle);
            }
            rolle[0] = '\0';
            json_str(j, "rl_role", rolle, sizeof(rolle));
            if (rolle[0]) {
                c->rl_role = cfg_role_from_key(rolle);
            }

            const cJSON *peers = cJSON_GetObjectItemCaseSensitive(j, "peers");
            if (cJSON_IsArray(peers)) {
                c->peer_count = 0;
                const cJSON *pv = NULL;
                cJSON_ArrayForEach(pv, peers) {
                    if (c->peer_count >= CFG_MAX_PEERS || !cJSON_IsString(pv)) {
                        continue;
                    }
                    copy_str(c->peers[c->peer_count], CFG_ID_LEN, pv->valuestring);
                    c->peer_count++;
                }
            }

            const cJSON *pump = cJSON_GetObjectItemCaseSensitive(j, "pump");
            if (cJSON_IsObject(pump)) {
                json_str(pump, "topic", c->pump_topic, sizeof(c->pump_topic));
                c->pump_relay = (uint8_t)json_num(pump, "relay", c->pump_relay);
            }

            char modus[12] = {0};
            json_str(j, "mode", modus, sizeof(modus));
            if (strcmp(modus, "ein") == 0) {
                c->mode = 1;
            } else if (strcmp(modus, "aus") == 0) {
                c->mode = 2;
            } else if (modus[0]) {
                c->mode = 0;
            }

            c->overrun_s = (uint16_t)json_num(j, "overrun_s", c->overrun_s);
            c->min_run_s = (uint16_t)json_num(j, "min_run_s", c->min_run_s);
            c->min_pause_s = (uint16_t)json_num(j, "min_pause_s", c->min_pause_s);
            c->min_buffer_c = (float)json_num(j, "min_buffer_c", c->min_buffer_c);
            c->frost_c = (float)json_num(j, "frost_c", c->frost_c);
            c->seize_days = (uint8_t)json_num(j, "seize_days", c->seize_days);
            out->circuit_count++;
        }
    }
    out->demand_poll_s = (uint16_t)json_num(root, "demand_poll_s", out->demand_poll_s);
    out->demand_timeout_s = (uint16_t)json_num(root, "demand_timeout_s", out->demand_timeout_s);

    out->reboot_hour = (int8_t)json_num(root, "reboot_hour", out->reboot_hour);
    out->reboot_minute = (int8_t)json_num(root, "reboot_minute", out->reboot_minute);
    json_str(root, "timezone", out->timezone, sizeof(out->timezone));

    const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (cJSON_IsObject(w)) {
        json_str(w, "ssid", out->wifi.ssid, sizeof(out->wifi.ssid));
        json_str(w, "pass", out->wifi.pass, sizeof(out->wifi.pass));
        json_str(w, "hostname", out->wifi.hostname, sizeof(out->wifi.hostname));
        json_str(w, "ap_pass", out->wifi.ap_pass, sizeof(out->wifi.ap_pass));
    }

    const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    if (cJSON_IsObject(m)) {
        out->mqtt.enabled = json_bool(m, "enabled", out->mqtt.enabled);
        json_str(m, "uri", out->mqtt.uri, sizeof(out->mqtt.uri));
        json_str(m, "user", out->mqtt.user, sizeof(out->mqtt.user));
        json_str(m, "pass", out->mqtt.pass, sizeof(out->mqtt.pass));
        json_str(m, "prefix", out->mqtt.prefix, sizeof(out->mqtt.prefix));
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Ablage                                                              */
/* ------------------------------------------------------------------ */

static esp_err_t cfg_store(const app_config_t *cfg)
{
    char *txt = cfg_to_json(cfg, true);
    if (txt == NULL) {
        return ESP_ERR_NO_MEM;
    }
    nvs_handle_t h;
    esp_err_t rc = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (rc == ESP_OK) {
        rc = nvs_set_str(h, NVS_KEY_CFG, txt);
        if (rc == ESP_OK) {
            rc = nvs_commit(h);
        }
        nvs_close(h);
    }
    free(txt);
    return rc;
}

static esp_err_t cfg_load(app_config_t *out)
{
    cfg_defaults(out);

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t len = 0;
    esp_err_t rc = nvs_get_str(h, NVS_KEY_CFG, NULL, &len);
    if (rc != ESP_OK || len == 0) {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }
    char *buf = malloc(len);
    if (buf == NULL) {
        nvs_close(h);
        return ESP_ERR_NO_MEM;
    }
    rc = nvs_get_str(h, NVS_KEY_CFG, buf, &len);
    nvs_close(h);

    if (rc == ESP_OK) {
        char err[96] = {0};
        if (cfg_from_json(buf, out, err, sizeof(err)) != ESP_OK) {
            ESP_LOGW(TAG, "Gespeicherte Konfiguration unbrauchbar: %s", err);
            cfg_defaults(out);
            rc = ESP_ERR_INVALID_STATE;
        }
    }
    free(buf);
    return rc;
}

esp_err_t cfg_init(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        rc = nvs_flash_init();
    }
    if (rc != ESP_OK) {
        return rc;
    }

    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (cfg_load(&s_cfg) != ESP_OK) {
        ESP_LOGI(TAG, "Keine Konfiguration vorhanden, Werksvorgabe wird geschrieben");
        cfg_defaults(&s_cfg);
        cfg_store(&s_cfg);
    }
    ESP_LOGI(TAG, "Anlage \"%s\", %u Fuehler zugeordnet", s_cfg.site, s_cfg.probe_count);
    return ESP_OK;
}

void cfg_lock(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
}

void cfg_unlock(void)
{
    xSemaphoreGive(s_mtx);
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
    app_config_t vorher = s_cfg;
    s_cfg = *in;
    s_cfg.version = CFG_VERSION;
    cfg_unlock();

    rc = cfg_store(&s_cfg);
    if (rc != ESP_OK) {
        cfg_lock();
        s_cfg = vorher;
        cfg_unlock();
        snprintf(err, err_len, "Speichern fehlgeschlagen: %s", esp_err_to_name(rc));
    }
    return rc;
}

esp_err_t cfg_reset_defaults(void)
{
    cfg_lock();
    cfg_defaults(&s_cfg);
    cfg_unlock();
    return cfg_store(&s_cfg);
}
