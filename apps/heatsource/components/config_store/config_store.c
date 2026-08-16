#include "config_store.h"

#include <inttypes.h>
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
    [ROLE_HK3_VL] = {"hk3_vl", "Heizkreis 3 Vorlauf"},
    [ROLE_HK3_RL] = {"hk3_rl", "Heizkreis 3 Ruecklauf"},
    [ROLE_HK4_VL] = {"hk4_vl", "Heizkreis 4 Vorlauf"},
    [ROLE_HK4_RL] = {"hk4_rl", "Heizkreis 4 Ruecklauf"},
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

    /* GPIO 13: beide Heizungsgeraete sind so verdrahtet, und der Anschluss
     * liegt an der Stiftleiste guenstig. Umstellen laesst er sich im
     * Einrichtungsassistenten und spaeter unter System. */
    out->onewire_pin[0] = 13;
    out->onewire_pin[1] = -1;
    out->poll_s = 10;
    out->probe_count = 0;
    out->circuit_count = 0;
    out->demand_poll_s = 5;
    out->demand_timeout_s = 180;

    /* Dieselben Vorgaben wie in components/heatlogic. */
    out->burner.delta_on_k = 12.0f;
    out->burner.delta_off_k = 6.0f;
    out->burner.on_hold_s = 60;
    out->burner.off_hold_s = 300;
    out->burner.duese_l_h = 2.2f;

    out->buffer.spread_full_k = 8.0f;
    out->buffer.spread_hold_s = 300;
    out->buffer.voll_c = 62.0f;
    out->buffer.leer_c = 35.0f;
    out->buffer.warn_c = 40.0f;
    out->buffer.kessel_hot_c = 60.0f;

    out->reboot_hour = -1; /* Heizungsgeraete laufen durch */
    out->reboot_minute = 0;
    /* Samstag 11 Uhr, wie auf den Verteilerplatinen: die Schutzfahrt der
     * Ventile und der Schutzlauf der Pumpen fallen so zusammen. */
    out->seize_weekday = 6;
    out->seize_hour = 11;
    copy_str(out->timezone, sizeof(out->timezone), "CET-1CEST,M3.5.0,M10.5.0/3");

    copy_str(out->wifi.hostname, sizeof(out->wifi.hostname), "heizung");
    copy_str(out->wifi.ap_pass, sizeof(out->wifi.ap_pass), "fussboden");

    out->mqtt.enabled = false;
    copy_str(out->mqtt.prefix, sizeof(out->mqtt.prefix), "heiz");
}

/*
 * Warum ein Anschluss nicht taugt, oder NULL wenn er taugt.
 *
 * Der 1-Wire-Bus braucht einen Anschlusswiderstand nach 3,3 V und muss in
 * beide Richtungen treiben koennen. Das schliesst mehr Anschluesse aus, als
 * man zunaechst denkt -- und eine blosse Ablehnung ohne Grund hilft beim
 * Verdrahten nicht weiter.
 */
static const char *pin_problem(int8_t pin)
{
    if (pin < 0) {
        return NULL; /* nicht belegt */
    }
    if (pin > 33) {
        return "ist nur als Eingang ausgelegt und kann den Bus nicht treiben";
    }
    if (pin >= 6 && pin <= 11) {
        return "gehoert zum Flash-Speicher";
    }
    if (pin == 1 || pin == 3) {
        return "ist die serielle Schnittstelle";
    }
    if (pin == 12) {
        return "entscheidet beim Start ueber die Flash-Spannung; der noetige "
               "Anschlusswiderstand wuerde den Start verhindern";
    }
    return NULL;
}

static esp_err_t cfg_validate(const app_config_t *cfg, char *err, size_t err_len)
{
#define FEHLER(...)                       \
    do {                                  \
        snprintf(err, err_len, __VA_ARGS__); \
        return ESP_ERR_INVALID_ARG;       \
    } while (0)

    if (cfg->seize_weekday > 6 || cfg->seize_hour < 0 || cfg->seize_hour > 23) {
        FEHLER("Termin der Schutzfahrt liegt ausserhalb des Moeglichen");
    }
    if (cfg->probe_count > CFG_MAX_PROBES) {
        FEHLER("Hoechstens %d Fuehler moeglich", CFG_MAX_PROBES);
    }
    for (int i = 0; i < CFG_MAX_BUSES; i++) {
        const char *grund = pin_problem(cfg->onewire_pin[i]);
        if (grund != NULL) {
            FEHLER("GPIO %d %s", cfg->onewire_pin[i], grund);
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
    if (cfg->burner.delta_on_k <= cfg->burner.delta_off_k) {
        FEHLER("Die Einschaltschwelle des Brenners muss ueber der Ausschaltschwelle liegen");
    }
    if (cfg->burner.delta_on_k < 1.0f || cfg->burner.delta_on_k > 100.0f) {
        FEHLER("Einschaltschwelle des Brenners muss zwischen 1 und 100 K liegen");
    }
    if (cfg->burner.duese_l_h < 0.0f || cfg->burner.duese_l_h > 20.0f) {
        FEHLER("Duesendurchsatz muss zwischen 0 und 20 Litern je Stunde liegen");
    }
    if (cfg->buffer.voll_c <= cfg->buffer.leer_c) {
        FEHLER("Der Wert fuer \"voll\" muss ueber dem fuer \"leer\" liegen");
    }
    if (cfg->buffer.spread_full_k < 1.0f || cfg->buffer.spread_full_k > 40.0f) {
        FEHLER("Die Spreizung fuer \"geladen\" muss zwischen 1 und 40 K liegen");
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
    /* Die Rollen ergeben sich aus der Kennung: Kreis 1 nimmt hk1_vl und
     * hk1_rl, Kreis 2 die naechsten und so fort. */
    if (id >= 1 && id <= CFG_MAX_CIRCUITS) {
        c->vl_role = (probe_role_t)(ROLE_HK1_VL + 2 * (id - 1));
        c->rl_role = (probe_role_t)(ROLE_HK1_RL + 2 * (id - 1));
    }
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
        cJSON_AddStringToObject(pump, "host", c->pump_host);
        cJSON_AddStringToObject(pump, "user", c->pump_user);
        cJSON_AddNumberToObject(pump, "relay", c->pump_relay);
        if (include_secrets) {
            cJSON_AddStringToObject(pump, "pass", c->pump_pass);
        } else {
            cJSON_AddBoolToObject(pump, "pass_set", c->pump_pass[0] != '\0');
        }
        cJSON_AddStringToObject(j, "mode", c->mode == 1 ? "ein" : (c->mode == 2 ? "aus" : "auto"));
        cJSON_AddNumberToObject(j, "overrun_s", c->overrun_s);
        cJSON_AddNumberToObject(j, "min_run_s", c->min_run_s);
        cJSON_AddNumberToObject(j, "min_pause_s", c->min_pause_s);
        cJSON_AddNumberToObject(j, "min_buffer_c", c->min_buffer_c);
        cJSON_AddNumberToObject(j, "frost_c", c->frost_c);
        cJSON_AddItemToArray(kreise, j);
    }
    cJSON_AddNumberToObject(root, "demand_poll_s", cfg->demand_poll_s);
    cJSON_AddNumberToObject(root, "demand_timeout_s", cfg->demand_timeout_s);

    cJSON *b = cJSON_AddObjectToObject(root, "burner");
    cJSON_AddNumberToObject(b, "delta_on_k", cfg->burner.delta_on_k);
    cJSON_AddNumberToObject(b, "delta_off_k", cfg->burner.delta_off_k);
    cJSON_AddNumberToObject(b, "on_hold_s", cfg->burner.on_hold_s);
    cJSON_AddNumberToObject(b, "off_hold_s", cfg->burner.off_hold_s);
    cJSON_AddNumberToObject(b, "duese_l_h", cfg->burner.duese_l_h);

    cJSON *sp = cJSON_AddObjectToObject(root, "buffer");
    cJSON_AddNumberToObject(sp, "spread_full_k", cfg->buffer.spread_full_k);
    cJSON_AddNumberToObject(sp, "spread_hold_s", cfg->buffer.spread_hold_s);
    cJSON_AddNumberToObject(sp, "voll_c", cfg->buffer.voll_c);
    cJSON_AddNumberToObject(sp, "leer_c", cfg->buffer.leer_c);
    cJSON_AddNumberToObject(sp, "warn_c", cfg->buffer.warn_c);
    cJSON_AddNumberToObject(sp, "kessel_hot_c", cfg->buffer.kessel_hot_c);

    cJSON_AddNumberToObject(root, "reboot_hour", cfg->reboot_hour);
    cJSON_AddNumberToObject(root, "seize_weekday", cfg->seize_weekday);
    cJSON_AddNumberToObject(root, "seize_hour", cfg->seize_hour);
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

esp_err_t cfg_from_json(const char *json, app_config_t *out, char *err, size_t err_len)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        snprintf(err, err_len, "Die Konfiguration liess sich nicht lesen");
        return ESP_ERR_INVALID_ARG;
    }

    cfgjson_str(root, "site", out->site, sizeof(out->site));
    out->poll_s = (uint16_t)cfgjson_num(root, "poll_s", out->poll_s);

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
        if (cJSON_GetArraySize(probes) > CFG_MAX_PROBES) {
            snprintf(err, err_len, "Hoechstens %d Fuehler moeglich", CFG_MAX_PROBES);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        /* Ein Eintrag wird mit dem bisherigen Stand derselben Kennung
         * zusammengefuehrt. Sonst verloere eine Teilangabe -- etwa nur die
         * Rolle -- den gespeicherten Korrekturwert. */
        cfg_probe_t vorher[CFG_MAX_PROBES];
        uint8_t vorher_count = out->probe_count;
        memcpy(vorher, out->probes, sizeof(vorher));

        out->probe_count = 0;
        const cJSON *j = NULL;
        cJSON_ArrayForEach(j, probes) {
            cfg_probe_t *p = &out->probes[out->probe_count];
            memset(p, 0, sizeof(*p));

            char rom[24] = {0};
            cfgjson_str(j, "rom", rom, sizeof(rom));
            p->rom = str_to_rom(rom);
            if (p->rom == 0) {
                continue; /* Eintrag ohne brauchbare Kennung wird uebergangen */
            }
            for (uint8_t v = 0; v < vorher_count; v++) {
                if (vorher[v].rom == p->rom) {
                    *p = vorher[v];
                    break;
                }
            }
            char role[24] = {0};
            cfgjson_str(j, "role", role, sizeof(role));
            p->role = cfg_role_from_key(role);
            cfgjson_str(j, "name", p->name, sizeof(p->name));
            p->offset_k = (float)cfgjson_num(j, "offset_k", 0.0);
            if (p->name[0] == '\0') {
                copy_str(p->name, sizeof(p->name),
                         p->role != ROLE_NONE ? cfg_role_label(p->role) : rom);
            }
            out->probe_count++;
        }
    }

    const cJSON *kreise = cJSON_GetObjectItemCaseSensitive(root, "circuits");
    if (cJSON_IsArray(kreise)) {
        if (cJSON_GetArraySize(kreise) > CFG_MAX_CIRCUITS) {
            snprintf(err, err_len, "Hoechstens %d Heizkreise moeglich", CFG_MAX_CIRCUITS);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
        }
        cfg_circuit_t vorher[CFG_MAX_CIRCUITS];
        uint8_t vorher_count = out->circuit_count;
        memcpy(vorher, out->circuits, sizeof(vorher));

        out->circuit_count = 0;
        const cJSON *j = NULL;
        cJSON_ArrayForEach(j, kreise) {
            cfg_circuit_t *c = &out->circuits[out->circuit_count];
            uint8_t id = (uint8_t)cfgjson_num(j, "id", out->circuit_count + 1);
            cfg_circuit_defaults(c, id);
            /* Bisherigen Stand derselben Kennung als Ausgangsbasis nehmen,
             * damit eine Teilangabe die uebrigen Felder stehen laesst. */
            for (uint8_t v = 0; v < vorher_count; v++) {
                if (vorher[v].id == id) {
                    *c = vorher[v];
                    break;
                }
            }

            cfgjson_str(j, "name", c->name, sizeof(c->name));
            c->enabled = cfgjson_bool(j, "enabled", c->enabled);

            char rolle[24] = {0};
            cfgjson_str(j, "vl_role", rolle, sizeof(rolle));
            if (rolle[0]) {
                c->vl_role = cfg_role_from_key(rolle);
            }
            rolle[0] = '\0';
            cfgjson_str(j, "rl_role", rolle, sizeof(rolle));
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
                cfgjson_str(pump, "topic", c->pump_topic, sizeof(c->pump_topic));
                cfgjson_str(pump, "host", c->pump_host, sizeof(c->pump_host));
                cfgjson_str(pump, "user", c->pump_user, sizeof(c->pump_user));
                cfgjson_str(pump, "pass", c->pump_pass, sizeof(c->pump_pass));
                c->pump_relay = (uint8_t)cfgjson_num(pump, "relay", c->pump_relay);
            }

            char modus[12] = {0};
            cfgjson_str(j, "mode", modus, sizeof(modus));
            if (strcmp(modus, "ein") == 0) {
                c->mode = 1;
            } else if (strcmp(modus, "aus") == 0) {
                c->mode = 2;
            } else if (modus[0]) {
                c->mode = 0;
            }

            c->overrun_s = (uint16_t)cfgjson_num(j, "overrun_s", c->overrun_s);
            c->min_run_s = (uint16_t)cfgjson_num(j, "min_run_s", c->min_run_s);
            c->min_pause_s = (uint16_t)cfgjson_num(j, "min_pause_s", c->min_pause_s);
            c->min_buffer_c = (float)cfgjson_num(j, "min_buffer_c", c->min_buffer_c);
            c->frost_c = (float)cfgjson_num(j, "frost_c", c->frost_c);
            out->circuit_count++;
        }
    }
    out->demand_poll_s = (uint16_t)cfgjson_num(root, "demand_poll_s", out->demand_poll_s);
    out->demand_timeout_s = (uint16_t)cfgjson_num(root, "demand_timeout_s", out->demand_timeout_s);

    const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "burner");
    if (cJSON_IsObject(b)) {
        out->burner.delta_on_k = (float)cfgjson_num(b, "delta_on_k", out->burner.delta_on_k);
        out->burner.delta_off_k = (float)cfgjson_num(b, "delta_off_k", out->burner.delta_off_k);
        out->burner.on_hold_s = (uint16_t)cfgjson_num(b, "on_hold_s", out->burner.on_hold_s);
        out->burner.off_hold_s = (uint16_t)cfgjson_num(b, "off_hold_s", out->burner.off_hold_s);
        out->burner.duese_l_h = (float)cfgjson_num(b, "duese_l_h", out->burner.duese_l_h);
    }

    const cJSON *sp = cJSON_GetObjectItemCaseSensitive(root, "buffer");
    if (cJSON_IsObject(sp)) {
        out->buffer.spread_full_k =
            (float)cfgjson_num(sp, "spread_full_k", out->buffer.spread_full_k);
        out->buffer.spread_hold_s =
            (uint16_t)cfgjson_num(sp, "spread_hold_s", out->buffer.spread_hold_s);
        out->buffer.voll_c = (float)cfgjson_num(sp, "voll_c", out->buffer.voll_c);
        out->buffer.leer_c = (float)cfgjson_num(sp, "leer_c", out->buffer.leer_c);
        out->buffer.warn_c = (float)cfgjson_num(sp, "warn_c", out->buffer.warn_c);
        out->buffer.kessel_hot_c =
            (float)cfgjson_num(sp, "kessel_hot_c", out->buffer.kessel_hot_c);
    }

    out->reboot_hour = (int8_t)cfgjson_num(root, "reboot_hour", out->reboot_hour);
    out->seize_weekday = (int8_t)cfgjson_num(root, "seize_weekday", out->seize_weekday);
    out->seize_hour = (int8_t)cfgjson_num(root, "seize_hour", out->seize_hour);
    out->reboot_minute = (int8_t)cfgjson_num(root, "reboot_minute", out->reboot_minute);
    cfgjson_str(root, "timezone", out->timezone, sizeof(out->timezone));

    const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (cJSON_IsObject(w)) {
        cfgjson_str(w, "ssid", out->wifi.ssid, sizeof(out->wifi.ssid));
        cfgjson_str(w, "pass", out->wifi.pass, sizeof(out->wifi.pass));
        cfgjson_str(w, "hostname", out->wifi.hostname, sizeof(out->wifi.hostname));
        cfgjson_str(w, "ap_pass", out->wifi.ap_pass, sizeof(out->wifi.ap_pass));
    }

    const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    if (cJSON_IsObject(m)) {
        out->mqtt.enabled = cfgjson_bool(m, "enabled", out->mqtt.enabled);
        cfgjson_str(m, "uri", out->mqtt.uri, sizeof(out->mqtt.uri));
        cfgjson_str(m, "user", out->mqtt.user, sizeof(out->mqtt.user));
        cfgjson_str(m, "pass", out->mqtt.pass, sizeof(out->mqtt.pass));
        cfgjson_str(m, "prefix", out->mqtt.prefix, sizeof(out->mqtt.prefix));
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
    esp_err_t rc = cfgjson_save(NVS_NAMESPACE, NVS_KEY_CFG, txt);
    free(txt);
    return rc;
}

static esp_err_t cfg_load(app_config_t *out)
{
    cfg_defaults(out);

    char *buf = NULL;
    esp_err_t rc = cfgjson_load(NVS_NAMESPACE, NVS_KEY_CFG, &buf);
    if (rc != ESP_OK) {
        return rc;
    }

    char err[96] = {0};
    if (cfg_from_json(buf, out, err, sizeof(err)) != ESP_OK) {
        ESP_LOGW(TAG, "Gespeicherte Konfiguration unbrauchbar: %s", err);
        cfg_defaults(out);
        rc = ESP_ERR_INVALID_STATE;
    }
    free(buf);
    return rc;
}

void cfg_netmgr(const app_config_t *cfg, netmgr_cfg_t *out)
{
    memset(out, 0, sizeof(*out));
    copy_str(out->ssid, sizeof(out->ssid), cfg->wifi.ssid);
    copy_str(out->pass, sizeof(out->pass), cfg->wifi.pass);
    copy_str(out->hostname, sizeof(out->hostname), cfg->wifi.hostname);
    copy_str(out->ap_pass, sizeof(out->ap_pass), cfg->wifi.ap_pass);
    copy_str(out->timezone, sizeof(out->timezone), cfg->timezone);
    out->reboot_hour = cfg->reboot_hour;
    out->reboot_minute = cfg->reboot_minute;
}

esp_err_t cfg_init(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /*
         * Der Speicher ist voll oder von einer anderen Fassung beschrieben.
         * Es bleibt nur das Loeschen -- damit ist aber die gesamte
         * Konfiguration weg, einschliesslich der WLAN-Zugangsdaten. Das darf
         * nicht stillschweigend geschehen: wer das Geraet danach im
         * Zugangspunkt-Betrieb vorfindet, soll im Protokoll den Grund finden.
         */
        ESP_LOGE(TAG, "Konfigurationsspeicher unbrauchbar (%s) -- er wird geleert. "
                      "Die gesamte Einrichtung geht dabei verloren.",
                 esp_err_to_name(rc));
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
