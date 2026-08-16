#include "app_mqtt.h"

#include <stdlib.h>
#include <string.h>

#include "app_burner.h"
#include "app_pumps.h"
#include "app_sensors.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqttc.h"

static const char *TAG = "hamqtt";

#define PUB_PERIOD_MS 10000

static char s_node[24];
static char s_prefix[CFG_NAME_LEN];
static volatile bool s_announce_pending = true;

/* ------------------------------------------------------------------ */
/* Bausteine der Anmeldung                                             */
/* ------------------------------------------------------------------ */

static void add_device(cJSON *parent)
{
    static app_config_t cfg;
    cfg_copy(&cfg);

    cJSON *dev = cJSON_AddObjectToObject(parent, "dev");
    cJSON *ids = cJSON_AddArrayToObject(dev, "ids");
    cJSON_AddItemToArray(ids, cJSON_CreateString(s_node));

    char name[64];
    snprintf(name, sizeof(name), "Waermeerzeuger %s", cfg.site[0] ? cfg.site : s_node);
    cJSON_AddStringToObject(dev, "name", name);
    cJSON_AddStringToObject(dev, "mf", "Eigenbau");
    cJSON_AddStringToObject(dev, "mdl", "ESP32 Heizungserfassung");
    cJSON_AddStringToObject(dev, "sw", esp_app_get_description()->version);
}

static void add_availability(cJSON *parent)
{
    char t[80];
    snprintf(t, sizeof(t), "%s/status", s_prefix);
    cJSON_AddStringToObject(parent, "avty_t", t);
    cJSON_AddStringToObject(parent, "pl_avail", "online");
    cJSON_AddStringToObject(parent, "pl_not_avail", "offline");
}

/*
 * Eine Entitaet anmelden. Alle lesen aus demselben Zustandsthema; das haelt
 * die Zahl der Veroeffentlichungen klein und die Werte zueinander konsistent.
 */
static void announce(const char *component, const char *slug, const char *name, const char *tpl,
                     const char *unit, const char *dev_cla, const char *ent_cat)
{
    cJSON *root = cJSON_CreateObject();
    char buf[128];

    snprintf(buf, sizeof(buf), "%s %s", name, "");
    cJSON_AddStringToObject(root, "name", name);
    snprintf(buf, sizeof(buf), "%s_%s", s_node, slug);
    cJSON_AddStringToObject(root, "uniq_id", buf);
    snprintf(buf, sizeof(buf), "%s/state", s_prefix);
    cJSON_AddStringToObject(root, "stat_t", buf);
    cJSON_AddStringToObject(root, "val_tpl", tpl);
    if (unit) {
        cJSON_AddStringToObject(root, "unit_of_meas", unit);
        cJSON_AddStringToObject(root, "stat_cla", "measurement");
    }
    if (dev_cla) {
        cJSON_AddStringToObject(root, "dev_cla", dev_cla);
    }
    if (ent_cat) {
        cJSON_AddStringToObject(root, "ent_cat", ent_cat);
    }
    add_availability(root);
    add_device(root);

    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", component, s_node, slug);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt != NULL) {
        mqttc_publish(topic, txt, true);
        free(txt);
    }
}

/* Auswahl der Betriebsart eines Heizkreises. */
static void announce_mode_select(uint8_t id, const char *name)
{
    cJSON *root = cJSON_CreateObject();
    char buf[160];

    snprintf(buf, sizeof(buf), "%s Betriebsart", name);
    cJSON_AddStringToObject(root, "name", buf);
    snprintf(buf, sizeof(buf), "%s_circuit%u_mode", s_node, (unsigned)id);
    cJSON_AddStringToObject(root, "uniq_id", buf);
    snprintf(buf, sizeof(buf), "%s/state", s_prefix);
    cJSON_AddStringToObject(root, "stat_t", buf);
    snprintf(buf, sizeof(buf), "{{ value_json.circuits['%u'].mode }}", (unsigned)id);
    cJSON_AddStringToObject(root, "val_tpl", buf);
    snprintf(buf, sizeof(buf), "%s/circuit/%u/mode/set", s_prefix, (unsigned)id);
    cJSON_AddStringToObject(root, "cmd_t", buf);
    cJSON *opts = cJSON_AddArrayToObject(root, "options");
    cJSON_AddItemToArray(opts, cJSON_CreateString("auto"));
    cJSON_AddItemToArray(opts, cJSON_CreateString("ein"));
    cJSON_AddItemToArray(opts, cJSON_CreateString("aus"));
    add_availability(root);
    add_device(root);

    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/select/%s/circuit%u_mode/config", s_node,
             (unsigned)id);
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt != NULL) {
        mqttc_publish(topic, txt, true);
        free(txt);
    }
}

/* Entitaet abmelden: leerer Inhalt auf demselben Thema. */
static void retract(const char *component, const char *slug)
{
    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", component, s_node, slug);
    mqttc_publish(topic, "", true);
}

/* ------------------------------------------------------------------ */

static void announce_all(void)
{
    static app_config_t cfg;
    static sens_snapshot_t snap;
    cfg_copy(&cfg);
    sensors_get(&snap);

    char tpl[96];

    /* Je zugeordneter Messstelle ein Fuehler. */
    for (int r = 1; r < ROLE_COUNT; r++) {
        const char *key = cfg_role_key((probe_role_t)r);
        bool belegt = cfg_probe_of_role(&cfg, (probe_role_t)r) != NULL;
        if (!belegt) {
            retract("sensor", key);
            continue;
        }
        snprintf(tpl, sizeof(tpl), "{{ value_json.probes['%s'] | default('') }}", key);
        announce("sensor", key, cfg_role_label((probe_role_t)r), tpl, "°C", "temperature", NULL);
    }

    /* Brenner -- nur wenn dieses Geraet den Abgasfuehler fuehrt. */
    bool hat_abgas = cfg_probe_of_role(&cfg, ROLE_ABGAS) != NULL;
    if (hat_abgas) {
        announce("binary_sensor", "burner", "Brenner",
                 "{{ 'ON' if value_json.burner.running else 'OFF' }}", NULL, "running", NULL);
        announce("sensor", "burner_runtime", "Brennerlaufzeit heute",
                 "{{ value_json.burner.runtime_today_s }}", "s", "duration", NULL);
        announce("sensor", "burner_starts", "Brennerstarts heute",
                 "{{ value_json.burner.starts_today }}", NULL, NULL, NULL);
        announce("sensor", "oil_today", "Oelverbrauch heute",
                 "{{ value_json.burner.litres_today | round(2) }}", "L", NULL, NULL);
        announce("binary_sensor", "short_cycling", "Taktbetrieb",
                 "{{ 'ON' if value_json.burner.short_cycling else 'OFF' }}", NULL, "problem",
                 "diagnostic");
    } else {
        retract("binary_sensor", "burner");
        retract("sensor", "burner_runtime");
        retract("sensor", "burner_starts");
        retract("sensor", "oil_today");
        retract("binary_sensor", "short_cycling");
    }

    /* Ladezustand -- nur wenn ein Pufferfuehler zugeordnet ist. */
    bool hat_puffer = cfg_probe_of_role(&cfg, ROLE_PUFFER) != NULL;
    if (hat_puffer) {
        announce("sensor", "charge_level", "Fuellstand Pufferspeicher",
                 "{{ (value_json.charge.level * 100) | round(0) }}", "%", NULL, NULL);
        announce("sensor", "charge_phase", "Ladezustand", "{{ value_json.charge.phase }}", NULL,
                 NULL, NULL);
        announce("binary_sensor", "dhw_warn", "Warmwasserreserve knapp",
                 "{{ 'ON' if value_json.charge.warn_dhw else 'OFF' }}", NULL, "problem", NULL);
    } else {
        retract("sensor", "charge_level");
        retract("sensor", "charge_phase");
        retract("binary_sensor", "dhw_warn");
    }

    /* Je Heizkreis Pumpe, Bedarf und Betriebsart. */
    for (uint8_t i = 0; i < CFG_MAX_CIRCUITS; i++) {
        uint8_t id = i + 1;
        const cfg_circuit_t *c = cfg_circuit(&cfg, id);
        char slug[32];

        if (c == NULL) {
            snprintf(slug, sizeof(slug), "circuit%u_pump", (unsigned)id);
            retract("binary_sensor", slug);
            snprintf(slug, sizeof(slug), "circuit%u_demand", (unsigned)id);
            retract("binary_sensor", slug);
            snprintf(slug, sizeof(slug), "circuit%u_mode", (unsigned)id);
            retract("select", slug);
            continue;
        }

        char name[64];
        snprintf(slug, sizeof(slug), "circuit%u_pump", (unsigned)id);
        snprintf(name, sizeof(name), "%s Pumpe", c->name);
        snprintf(tpl, sizeof(tpl), "{{ 'ON' if value_json.circuits['%u'].on else 'OFF' }}",
                 (unsigned)id);
        announce("binary_sensor", slug, name, tpl, NULL, "running", NULL);

        snprintf(slug, sizeof(slug), "circuit%u_demand", (unsigned)id);
        snprintf(name, sizeof(name), "%s Bedarf", c->name);
        snprintf(tpl, sizeof(tpl), "{{ 'ON' if value_json.circuits['%u'].demand else 'OFF' }}",
                 (unsigned)id);
        announce("binary_sensor", slug, name, tpl, NULL, "heat", NULL);

        announce_mode_select(id, c->name);
    }

    ESP_LOGI(TAG, "Entitaeten bei Home Assistant angemeldet");
}

/* ------------------------------------------------------------------ */

static void publish_state(void)
{
    static sens_snapshot_t snap;
    sensors_get(&snap);

    cJSON *root = cJSON_CreateObject();

    cJSON *probes = cJSON_AddObjectToObject(root, "probes");
    for (size_t i = 0; i < snap.count; i++) {
        const sens_probe_t *p = &snap.probes[i];
        if (p->role == ROLE_NONE || !p->valid) {
            continue;
        }
        cJSON_AddNumberToObject(probes, cfg_role_key(p->role), p->temp_c);
    }

    burner_status_t br;
    burner_get(&br);
    cJSON *b = cJSON_AddObjectToObject(root, "burner");
    cJSON_AddBoolToObject(b, "running", br.running);
    cJSON_AddNumberToObject(b, "runtime_today_s", br.runtime_today_s);
    cJSON_AddNumberToObject(b, "starts_today", br.starts_today);
    cJSON_AddNumberToObject(b, "litres_today", br.litres_today);
    cJSON_AddBoolToObject(b, "short_cycling", br.short_cycling);

    charge_status_t ch;
    charge_get(&ch);
    cJSON *c = cJSON_AddObjectToObject(root, "charge");
    cJSON_AddStringToObject(c, "phase", charge_phase_text(ch.phase));
    cJSON_AddNumberToObject(c, "level", ch.level_valid ? ch.level : 0.0);
    cJSON_AddBoolToObject(c, "warn_dhw", ch.warn_dhw);

    static circuit_status_t kreise[CFG_MAX_CIRCUITS];
    size_t kn = pumps_status(kreise, CFG_MAX_CIRCUITS);
    cJSON *ck = cJSON_AddObjectToObject(root, "circuits");
    for (size_t i = 0; i < kn; i++) {
        char id[8];
        snprintf(id, sizeof(id), "%u", (unsigned)kreise[i].id);
        cJSON *j = cJSON_AddObjectToObject(ck, id);
        cJSON_AddBoolToObject(j, "on", kreise[i].on);
        cJSON_AddBoolToObject(j, "demand", kreise[i].demand);
        cJSON_AddStringToObject(j, "mode",
                                kreise[i].mode == PUMP_MODE_ON ? "ein"
                                : (kreise[i].mode == PUMP_MODE_OFF ? "aus" : "auto"));
    }

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt == NULL) {
        return;
    }
    char topic[80];
    snprintf(topic, sizeof(topic), "%s/state", s_prefix);
    mqttc_publish(topic, txt, false);
    free(txt);
}

/* ------------------------------------------------------------------ */

static void on_mode_cmd(const char *topic, const char *payload, void *ctx)
{
    (void)ctx;
    /* <prefix>/circuit/<id>/mode/set */
    unsigned id = 0;
    const char *p = strstr(topic, "/circuit/");
    if (p == NULL || sscanf(p, "/circuit/%u/", &id) != 1) {
        return;
    }
    pump_mode_t m = PUMP_MODE_AUTO;
    if (strcmp(payload, "ein") == 0) {
        m = PUMP_MODE_ON;
    } else if (strcmp(payload, "aus") == 0) {
        m = PUMP_MODE_OFF;
    }
    ESP_LOGI(TAG, "Betriebsart von Heizkreis %u auf %s", id, payload);
    pumps_set_mode((uint8_t)id, m);
}

static void on_connected(void *ctx)
{
    (void)ctx;
    char t[96];
    snprintf(t, sizeof(t), "%s/circuit/+/mode/set", s_prefix);
    mqttc_subscribe(t, on_mode_cmd, NULL);
    s_announce_pending = true;
}

static void pub_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (mqttc_connected()) {
            if (s_announce_pending) {
                s_announce_pending = false;
                announce_all();
            }
            publish_state();
        }
        vTaskDelay(pdMS_TO_TICKS(PUB_PERIOD_MS));
    }
}

esp_err_t hamqtt_start(void)
{
    static app_config_t cfg;
    cfg_copy(&cfg);
    snprintf(s_prefix, sizeof(s_prefix), "%s", cfg.mqtt.prefix[0] ? cfg.mqtt.prefix : "heiz");

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_node, sizeof(s_node), "heiz_%02x%02x%02x", mac[3], mac[4], mac[5]);

    if (xTaskCreate(pub_task, "hamqtt", 5120, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void hamqtt_config_changed(void)
{
    s_announce_pending = true;
}

/* Wird von main.c an mqttc uebergeben. */
mqttc_connected_cb_t hamqtt_on_connected(void)
{
    return on_connected;
}
