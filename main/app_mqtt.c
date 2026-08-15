#include "app_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app_control.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "netmgr.h"
#include "sensors_local.h"

static const char *TAG = "mqtt";

#define PUBLISH_PERIOD_MS 5000
#define TOPIC_LEN         160

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static bool s_discovery_pending;
static char s_prefix[CFG_NAME_LEN];
static char s_node[24]; /* eindeutige Kennung des Geraets, aus der MAC */
static uint16_t s_announced_rooms[CFG_MAX_ROOMS];
static uint8_t s_announced_room_count;

/* ------------------------------------------------------------------ */

static void topic_state_room(char *out, size_t len, uint8_t room_id)
{
    snprintf(out, len, "%s/room/%u/state", s_prefix, room_id);
}

static void topic_state_channel(char *out, size_t len, uint8_t channel)
{
    snprintf(out, len, "%s/cover/%u/state", s_prefix, channel);
}

static void publish(const char *topic, const char *payload, bool retain)
{
    if (!s_connected) {
        return;
    }
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, retain);
}

static void publish_json(const char *topic, cJSON *root, bool retain)
{
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt) {
        publish(topic, txt, retain);
        free(txt);
    }
}

/* Geraeteblock, damit Home Assistant alle Entities einem Geraet zuordnet. */
static void add_device(cJSON *cfg)
{
    cJSON *dev = cJSON_AddObjectToObject(cfg, "dev");
    cJSON *ids = cJSON_AddArrayToObject(dev, "ids");
    cJSON_AddItemToArray(ids, cJSON_CreateString(s_node));
    cJSON_AddStringToObject(dev, "name", "Fussbodenheizung Erdgeschoss");
    cJSON_AddStringToObject(dev, "mf", "Eigenbau");
    cJSON_AddStringToObject(dev, "mdl", "ESP32 Ventilsteuerung, 11 Kanaele");
    cJSON_AddStringToObject(dev, "sw", esp_app_get_description()->version);
}

static void add_availability(cJSON *cfg)
{
    char topic[TOPIC_LEN];
    snprintf(topic, sizeof(topic), "%s/status", s_prefix);
    cJSON_AddStringToObject(cfg, "avty_t", topic);
    cJSON_AddStringToObject(cfg, "pl_avail", "online");
    cJSON_AddStringToObject(cfg, "pl_not_avail", "offline");
}

/* ------------------------------------------------------------------ */
/* Discovery                                                           */
/* ------------------------------------------------------------------ */

static void announce_cover(uint8_t channel)
{
    char disc[TOPIC_LEN], state[TOPIC_LEN], cmd[TOPIC_LEN], pos[TOPIC_LEN], uid[48];
    snprintf(disc, sizeof(disc), "homeassistant/cover/%s/ch%u/config", s_node, channel);
    topic_state_channel(state, sizeof(state), channel);
    snprintf(cmd, sizeof(cmd), "%s/cover/%u/set", s_prefix, channel);
    snprintf(pos, sizeof(pos), "%s/cover/%u/set_position", s_prefix, channel);
    snprintf(uid, sizeof(uid), "%s_ch%u", s_node, channel);

    cJSON *c = cJSON_CreateObject();
    char name[32];
    snprintf(name, sizeof(name), "Ventil CH%u", channel);
    cJSON_AddStringToObject(c, "name", name);
    cJSON_AddStringToObject(c, "uniq_id", uid);
    cJSON_AddStringToObject(c, "dev_cla", "damper");
    cJSON_AddStringToObject(c, "cmd_t", cmd);
    cJSON_AddStringToObject(c, "set_pos_t", pos);
    cJSON_AddStringToObject(c, "stat_t", state);
    cJSON_AddStringToObject(c, "pos_t", state);
    cJSON_AddStringToObject(c, "val_tpl", "{{ value_json.state }}");
    cJSON_AddStringToObject(c, "pos_tpl", "{{ value_json.position }}");
    add_availability(c);
    add_device(c);
    publish_json(disc, c, true);
}

static void announce_room(const ctl_room_t *r)
{
    char disc[TOPIC_LEN], state[TOPIC_LEN], mode_cmd[TOPIC_LEN], temp_cmd[TOPIC_LEN], uid[48];
    snprintf(disc, sizeof(disc), "homeassistant/climate/%s/room%u/config", s_node, r->id);
    topic_state_room(state, sizeof(state), r->id);
    snprintf(mode_cmd, sizeof(mode_cmd), "%s/room/%u/mode/set", s_prefix, r->id);
    snprintf(temp_cmd, sizeof(temp_cmd), "%s/room/%u/target/set", s_prefix, r->id);
    snprintf(uid, sizeof(uid), "%s_room%u", s_node, r->id);

    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", r->name);
    cJSON_AddStringToObject(c, "uniq_id", uid);
    cJSON_AddNumberToObject(c, "min_temp", 10);
    cJSON_AddNumberToObject(c, "max_temp", 30);
    cJSON_AddNumberToObject(c, "temp_step", 0.5);
    cJSON_AddStringToObject(c, "temp_unit", "C");

    cJSON *modes = cJSON_AddArrayToObject(c, "modes");
    cJSON_AddItemToArray(modes, cJSON_CreateString("off"));
    cJSON_AddItemToArray(modes, cJSON_CreateString("heat"));

    cJSON_AddStringToObject(c, "mode_cmd_t", mode_cmd);
    cJSON_AddStringToObject(c, "mode_stat_t", state);
    cJSON_AddStringToObject(c, "mode_stat_tpl", "{{ value_json.mode }}");
    cJSON_AddStringToObject(c, "temp_cmd_t", temp_cmd);
    cJSON_AddStringToObject(c, "temp_stat_t", state);
    cJSON_AddStringToObject(c, "temp_stat_tpl", "{{ value_json.target_c }}");
    cJSON_AddStringToObject(c, "curr_temp_t", state);
    cJSON_AddStringToObject(c, "curr_temp_tpl", "{{ value_json.temp_c }}");
    cJSON_AddStringToObject(c, "act_t", state);
    cJSON_AddStringToObject(c, "act_tpl", "{{ value_json.action }}");
    add_availability(c);
    add_device(c);
    publish_json(disc, c, true);
}

static void announce_room_sensor(const ctl_room_t *r, const char *slug, const char *suffix,
                                 const char *dev_cla, const char *unit, const char *tpl)
{
    char disc[TOPIC_LEN], state[TOPIC_LEN], uid[64], name[64];
    snprintf(disc, sizeof(disc), "homeassistant/sensor/%s/room%u_%s/config", s_node, r->id, slug);
    topic_state_room(state, sizeof(state), r->id);
    snprintf(uid, sizeof(uid), "%s_room%u_%s", s_node, r->id, slug);
    snprintf(name, sizeof(name), "%s %s", r->name, suffix);

    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", name);
    cJSON_AddStringToObject(c, "uniq_id", uid);
    cJSON_AddStringToObject(c, "stat_t", state);
    cJSON_AddStringToObject(c, "val_tpl", tpl);
    cJSON_AddStringToObject(c, "dev_cla", dev_cla);
    cJSON_AddStringToObject(c, "unit_of_meas", unit);
    cJSON_AddStringToObject(c, "stat_cla", "measurement");
    add_availability(c);
    add_device(c);
    publish_json(disc, c, true);
}

static void announce_diag_sensor(const char *slug, const char *name, const char *unit,
                                 const char *tpl, const char *dev_cla)
{
    char disc[TOPIC_LEN], state[TOPIC_LEN], uid[64];
    snprintf(disc, sizeof(disc), "homeassistant/sensor/%s/%s/config", s_node, slug);
    snprintf(state, sizeof(state), "%s/diag/state", s_prefix);
    snprintf(uid, sizeof(uid), "%s_%s", s_node, slug);

    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", name);
    cJSON_AddStringToObject(c, "uniq_id", uid);
    cJSON_AddStringToObject(c, "stat_t", state);
    cJSON_AddStringToObject(c, "val_tpl", tpl);
    if (unit) {
        cJSON_AddStringToObject(c, "unit_of_meas", unit);
        cJSON_AddStringToObject(c, "stat_cla", "measurement");
    }
    if (dev_cla) {
        cJSON_AddStringToObject(c, "dev_cla", dev_cla);
    }
    cJSON_AddStringToObject(c, "ent_cat", "diagnostic");
    add_availability(c);
    add_device(c);
    publish_json(disc, c, true);
}

static void announce_button(const char *slug, const char *name, const char *cmd_topic)
{
    char disc[TOPIC_LEN], uid[64];
    snprintf(disc, sizeof(disc), "homeassistant/button/%s/%s/config", s_node, slug);
    snprintf(uid, sizeof(uid), "%s_%s", s_node, slug);

    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", name);
    cJSON_AddStringToObject(c, "uniq_id", uid);
    cJSON_AddStringToObject(c, "cmd_t", cmd_topic);
    cJSON_AddStringToObject(c, "ent_cat", "config");
    add_availability(c);
    add_device(c);
    publish_json(disc, c, true);
}

/* Entfernt eine Entity, indem der Retained-Payload geleert wird. */
static void retract(const char *component, const char *object)
{
    char disc[TOPIC_LEN];
    snprintf(disc, sizeof(disc), "homeassistant/%s/%s/%s/config", component, s_node, object);
    publish(disc, "", true);
}

static void announce_all(void)
{
    ctl_snapshot_t snap;
    control_snapshot(&snap);

    /* Zuerst die Raeume abmelden, die es nicht mehr gibt. */
    for (int i = 0; i < s_announced_room_count; i++) {
        bool still_there = false;
        for (int j = 0; j < snap.room_count; j++) {
            if (snap.rooms[j].id == s_announced_rooms[i]) {
                still_there = true;
                break;
            }
        }
        if (still_there) {
            continue;
        }
        char obj[32];
        snprintf(obj, sizeof(obj), "room%u", s_announced_rooms[i]);
        retract("climate", obj);
        const char *slugs[] = {"temp", "hum", "batt", "pos"};
        for (size_t k = 0; k < sizeof(slugs) / sizeof(slugs[0]); k++) {
            snprintf(obj, sizeof(obj), "room%u_%s", s_announced_rooms[i], slugs[k]);
            retract("sensor", obj);
        }
        ESP_LOGI(TAG, "Raum %u in Home Assistant entfernt", s_announced_rooms[i]);
    }

    for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
        announce_cover((uint8_t)n);
    }

    for (int i = 0; i < snap.room_count; i++) {
        const ctl_room_t *r = &snap.rooms[i];
        announce_room(r);
        announce_room_sensor(r, "temp", "Temperatur", "temperature", "°C",
                             "{{ value_json.temp_c }}");
        announce_room_sensor(r, "hum", "Luftfeuchtigkeit", "humidity", "%",
                             "{{ value_json.humidity }}");
        announce_room_sensor(r, "batt", "Batterie Thermometer", "battery", "%",
                             "{{ value_json.battery }}");
        announce_room_sensor(r, "pos", "Zielstellung", "power_factor", "%",
                             "{{ value_json.target_position }}");

        char cmd[TOPIC_LEN], slug[32], name[64];
        snprintf(cmd, sizeof(cmd), "%s/room/%u/check", s_prefix, r->id);
        snprintf(slug, sizeof(slug), "room%u_check", r->id);
        snprintf(name, sizeof(name), "%s jetzt pruefen", r->name);
        announce_button(slug, name, cmd);

        s_announced_rooms[i] = r->id;
    }
    s_announced_room_count = snap.room_count;

    for (int g = 0; g < HW_BEMF_GROUP_COUNT; g++) {
        char slug[32], name[48], tpl[48];
        snprintf(slug, sizeof(slug), "bemf%d", g + 1);
        snprintf(name, sizeof(name), "BEMF Messgruppe %d", g + 1);
        snprintf(tpl, sizeof(tpl), "{{ value_json.bemf[%d] }}", g);
        announce_diag_sensor(slug, name, "mV", tpl, "voltage");
    }
    /* Fuehler auf der Platine. Die Anzahl steht erst nach der Suche am
     * 1-Wire-Bus fest, deshalb hier und nicht statisch. */
    sensors_snapshot_t sens;
    sensors_get(&sens);
    for (int i = 0; i < sens.ds_count; i++) {
        char slug[32], name[48], tpl[64];
        snprintf(slug, sizeof(slug), "ds%d", i + 1);
        snprintf(name, sizeof(name), "Vorlauf %d", i + 1);
        snprintf(tpl, sizeof(tpl), "{{ value_json.ds[%d] }}", i);
        announce_diag_sensor(slug, name, "°C", tpl, "temperature");
    }
    if (sens.hdc_valid) {
        announce_diag_sensor("box_temp", "Schaltschrank Temperatur", "°C",
                             "{{ value_json.box_temp }}", "temperature");
        announce_diag_sensor("box_hum", "Schaltschrank Luftfeuchtigkeit", "%",
                             "{{ value_json.box_hum }}", "humidity");
    }

    announce_diag_sensor("uptime", "Laufzeit", "s", "{{ value_json.uptime_s }}", "duration");
    announce_diag_sensor("rssi", "WLAN-Empfang", "dBm", "{{ value_json.rssi }}",
                         "signal_strength");
    announce_diag_sensor("heap", "Freier Speicher", "B", "{{ value_json.heap }}", NULL);

    char cmd[TOPIC_LEN];
    snprintf(cmd, sizeof(cmd), "%s/system/restart", s_prefix);
    announce_button("restart", "Neu starten", cmd);

    ESP_LOGI(TAG, "Discovery gesendet: %d Raeume, %d Ventile", snap.room_count, HW_CHANNEL_COUNT);
}

/* ------------------------------------------------------------------ */
/* Zustaende senden                                                    */
/* ------------------------------------------------------------------ */

static const char *cover_state_name(valve_op_t op, float position)
{
    if (op == VALVE_OPENING) {
        return "opening";
    }
    if (op == VALVE_CLOSING) {
        return "closing";
    }
    return position > 0.01f ? "open" : "closed";
}

static void publish_states(void)
{
    ctl_snapshot_t snap;
    control_snapshot(&snap);

    char topic[TOPIC_LEN];

    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        const ctl_channel_t *c = &snap.ch[i];
        topic_state_channel(topic, sizeof(topic), (uint8_t)(i + 1));
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "state", cover_state_name(c->op, c->position));
        cJSON_AddNumberToObject(j, "position", (int)(c->position * 100.0f + 0.5f));
        cJSON_AddBoolToObject(j, "known", c->position_known);
        publish_json(topic, j, true);
    }

    for (int i = 0; i < snap.room_count; i++) {
        const ctl_room_t *r = &snap.rooms[i];
        topic_state_room(topic, sizeof(topic), r->id);
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "mode", r->mode_heat ? "heat" : "off");
        cJSON_AddNumberToObject(j, "target_c", r->target_c);
        if (r->temp_valid) {
            cJSON_AddNumberToObject(j, "temp_c", r->temp_c);
            cJSON_AddNumberToObject(j, "humidity", r->humidity);
            cJSON_AddNumberToObject(j, "battery", r->battery);
        }
        cJSON_AddNumberToObject(j, "target_position", (int)(r->target_position * 100.0f + 0.5f));
        cJSON_AddNumberToObject(j, "temp_age_s", r->temp_age_s);
        /* Fuer die Aktionsanzeige in Home Assistant. */
        const char *action = "off";
        if (r->mode_heat) {
            action = r->target_position > 0.01f ? "heating" : "idle";
        }
        cJSON_AddStringToObject(j, "action", action);
        publish_json(topic, j, true);
    }

    netmgr_status_t net;
    netmgr_status(&net);

    snprintf(topic, sizeof(topic), "%s/diag/state", s_prefix);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "uptime_s", snap.uptime_s);
    cJSON_AddNumberToObject(j, "rssi", net.rssi);
    cJSON_AddNumberToObject(j, "heap", esp_get_free_heap_size());
    cJSON *bemf = cJSON_AddArrayToObject(j, "bemf");
    for (int g = 0; g < HW_BEMF_GROUP_COUNT; g++) {
        cJSON_AddItemToArray(bemf, cJSON_CreateNumber(snap.bemf_mv[g]));
    }

    sensors_snapshot_t sens;
    sensors_get(&sens);
    cJSON *ds = cJSON_AddArrayToObject(j, "ds");
    for (int i = 0; i < sens.ds_count; i++) {
        if (sens.ds[i].valid) {
            cJSON_AddItemToArray(ds, cJSON_CreateNumber(sens.ds[i].temp_c));
        } else {
            cJSON_AddItemToArray(ds, cJSON_CreateNull());
        }
    }
    if (sens.hdc_valid) {
        cJSON_AddNumberToObject(j, "box_temp", sens.hdc_temp_c);
        cJSON_AddNumberToObject(j, "box_hum", sens.hdc_humidity);
    }
    publish_json(topic, j, false);
}

/* ------------------------------------------------------------------ */
/* Befehle empfangen                                                   */
/* ------------------------------------------------------------------ */

/* Erwartet <prefix>/<bereich>/<nummer>/<aktion>. */
static void handle_command(const char *topic, int topic_len, const char *data, int data_len)
{
    char t[TOPIC_LEN];
    int n = topic_len < (int)sizeof(t) - 1 ? topic_len : (int)sizeof(t) - 1;
    memcpy(t, topic, n);
    t[n] = '\0';

    char payload[64];
    n = data_len < (int)sizeof(payload) - 1 ? data_len : (int)sizeof(payload) - 1;
    memcpy(payload, data, n);
    payload[n] = '\0';

    /* Praefix abschneiden. */
    size_t plen = strlen(s_prefix);
    if (strncmp(t, s_prefix, plen) != 0 || t[plen] != '/') {
        return;
    }
    const char *rest = t + plen + 1;

    unsigned number = 0;
    char area[16] = {0}, action[24] = {0};

    if (sscanf(rest, "cover/%u/%23s", &number, action) == 2) {
        if (strcmp(action, "set") == 0) {
            if (strcasecmp(payload, "OPEN") == 0) {
                control_cmd_open((uint8_t)number);
            } else if (strcasecmp(payload, "CLOSE") == 0) {
                control_cmd_close((uint8_t)number);
            } else if (strcasecmp(payload, "STOP") == 0) {
                control_cmd_stop((uint8_t)number);
            }
        } else if (strcmp(action, "set_position") == 0) {
            control_cmd_position((uint8_t)number, (float)atof(payload) / 100.0f);
        }
        return;
    }

    if (sscanf(rest, "room/%u/%23s", &number, action) == 2) {
        if (strcmp(action, "target/set") == 0 || strncmp(action, "target", 6) == 0) {
            control_room_set_target((uint8_t)number, (float)atof(payload));
        } else if (strncmp(action, "mode", 4) == 0) {
            control_room_set_mode((uint8_t)number, strcmp(payload, "off") != 0);
        } else if (strncmp(action, "check", 5) == 0) {
            control_room_check_now((uint8_t)number);
        }
        return;
    }

    if (sscanf(rest, "%15[^/]/%23s", area, action) == 2 && strcmp(area, "system") == 0) {
        if (strcmp(action, "restart") == 0) {
            ESP_LOGW(TAG, "Neustart ueber MQTT ausgeloest");
            esp_restart();
        }
    }
}

static void subscribe_all(void)
{
    char topic[TOPIC_LEN];
    snprintf(topic, sizeof(topic), "%s/cover/+/set", s_prefix);
    esp_mqtt_client_subscribe(s_client, topic, 0);
    snprintf(topic, sizeof(topic), "%s/cover/+/set_position", s_prefix);
    esp_mqtt_client_subscribe(s_client, topic, 0);
    snprintf(topic, sizeof(topic), "%s/room/+/target/set", s_prefix);
    esp_mqtt_client_subscribe(s_client, topic, 0);
    snprintf(topic, sizeof(topic), "%s/room/+/mode/set", s_prefix);
    esp_mqtt_client_subscribe(s_client, topic, 0);
    snprintf(topic, sizeof(topic), "%s/room/+/check", s_prefix);
    esp_mqtt_client_subscribe(s_client, topic, 0);
    snprintf(topic, sizeof(topic), "%s/system/restart", s_prefix);
    esp_mqtt_client_subscribe(s_client, topic, 0);
}

static void mqtt_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t ev = data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        char topic[TOPIC_LEN];
        snprintf(topic, sizeof(topic), "%s/status", s_prefix);
        publish(topic, "online", true);
        subscribe_all();
        s_discovery_pending = true;
        ESP_LOGI(TAG, "Mit dem Broker verbunden");
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Verbindung zum Broker verloren");
        break;
    case MQTT_EVENT_DATA:
        handle_command(ev->topic, ev->topic_len, ev->data, ev->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "Fehler in der Broker-Verbindung");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */

static void mqtt_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_discovery_pending && s_connected) {
            s_discovery_pending = false;
            announce_all();
        }
        if (s_connected) {
            publish_states();
        }
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_PERIOD_MS));
    }
}

esp_err_t mqtt_start(void)
{
    app_config_t cfg;
    cfg_copy(&cfg);

    if (!cfg.mqtt.enabled || cfg.mqtt.uri[0] == '\0') {
        ESP_LOGI(TAG, "MQTT ist nicht eingerichtet");
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_node, sizeof(s_node), "fbh_%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_prefix, sizeof(s_prefix), "%s",
             cfg.mqtt.prefix[0] ? cfg.mqtt.prefix : "fbh_eg");

    char lwt[TOPIC_LEN];
    snprintf(lwt, sizeof(lwt), "%s/status", s_prefix);

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = cfg.mqtt.uri,
        .credentials.username = cfg.mqtt.user[0] ? cfg.mqtt.user : NULL,
        .credentials.authentication.password = cfg.mqtt.pass[0] ? cfg.mqtt.pass : NULL,
        .credentials.client_id = s_node,
        .session.last_will.topic = lwt,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 0,
        .session.last_will.retain = true,
        .session.keepalive = 30,
    };

    s_client = esp_mqtt_client_init(&mc);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_err_t rc = esp_mqtt_client_start(s_client);
    if (rc != ESP_OK) {
        return rc;
    }

    xTaskCreate(mqtt_task, "mqtt_pub", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "MQTT gestartet, Praefix %s, Kennung %s", s_prefix, s_node);
    return ESP_OK;
}

void mqtt_config_changed(void)
{
    s_discovery_pending = true;
}
