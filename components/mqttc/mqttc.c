#include "mqttc.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"

static const char *TAG = "mqttc";

typedef struct {
    char topic[96];
    mqttc_cb_t cb;
    void *ctx;
    bool used;
} sub_t;

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static sub_t s_subs[MQTTC_MAX_SUBS];
static SemaphoreHandle_t s_mtx;

static mqttc_connected_cb_t s_on_connected;
static void *s_on_connected_ctx;
static char s_lwt_topic[96];
static char s_online_msg[32];

/* Vergleicht ein eingegangenes Thema mit einem Abo. Die Platzhalter + und #
 * werden unterstuetzt, weil Tasmota-Themen frei benannt sind und ein Abo auf
 * stat/<geraet>/+ bequemer ist als eines je Relais. */
static bool topic_matches(const char *muster, const char *thema, size_t thema_len)
{
    size_t m = 0, t = 0;
    size_t mlen = strlen(muster);

    while (m < mlen && t < thema_len) {
        if (muster[m] == '#') {
            return true;
        }
        if (muster[m] == '+') {
            while (t < thema_len && thema[t] != '/') {
                t++;
            }
            m++;
            continue;
        }
        if (muster[m] != thema[t]) {
            return false;
        }
        m++;
        t++;
    }
    if (m < mlen && muster[m] == '#') {
        return true;
    }
    return m == mlen && t == thema_len;
}

static void subscribe_all(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (size_t i = 0; i < MQTTC_MAX_SUBS; i++) {
        if (s_subs[i].used) {
            esp_mqtt_client_subscribe(s_client, s_subs[i].topic, 0);
        }
    }
    xSemaphoreGive(s_mtx);
}

static void handle_data(esp_mqtt_event_handle_t e)
{
    /* Der Ereignispuffer ist nicht nullterminiert; eine eigene Kopie ist
     * noetig, bevor der Rueckruf ihn als Zeichenkette sieht. */
    char thema[128];
    size_t tl = (size_t)e->topic_len < sizeof(thema) - 1 ? (size_t)e->topic_len
                                                         : sizeof(thema) - 1;
    memcpy(thema, e->topic, tl);
    thema[tl] = '\0';

    char *inhalt = malloc((size_t)e->data_len + 1);
    if (inhalt == NULL) {
        return;
    }
    memcpy(inhalt, e->data, e->data_len);
    inhalt[e->data_len] = '\0';

    for (size_t i = 0; i < MQTTC_MAX_SUBS; i++) {
        sub_t s;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s = s_subs[i];
        xSemaphoreGive(s_mtx);

        if (s.used && topic_matches(s.topic, thema, tl)) {
            s.cb(thema, inhalt, s.ctx);
        }
    }
    free(inhalt);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t e = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Mit dem Broker verbunden");
        if (s_lwt_topic[0]) {
            esp_mqtt_client_publish(s_client, s_lwt_topic, s_online_msg, 0, 0, 1);
        }
        subscribe_all();
        if (s_on_connected) {
            s_on_connected(s_on_connected_ctx);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Verbindung zum Broker verloren");
        break;
    case MQTT_EVENT_DATA:
        handle_data(e);
        break;
    default:
        break;
    }
}

esp_err_t mqttc_start(const mqttc_cfg_t *cfg, mqttc_connected_cb_t on_connected, void *ctx)
{
    if (cfg == NULL || cfg->uri == NULL || cfg->uri[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mtx == NULL) {
        s_mtx = xSemaphoreCreateMutex();
        if (s_mtx == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_on_connected = on_connected;
    s_on_connected_ctx = ctx;
    snprintf(s_lwt_topic, sizeof(s_lwt_topic), "%s", cfg->lwt_topic ? cfg->lwt_topic : "");
    snprintf(s_online_msg, sizeof(s_online_msg), "%s",
             cfg->online_msg ? cfg->online_msg : "online");

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = cfg->uri,
        .credentials.username = cfg->user && cfg->user[0] ? cfg->user : NULL,
        .credentials.authentication.password = cfg->pass && cfg->pass[0] ? cfg->pass : NULL,
        .credentials.client_id = cfg->client_id,
        .session.keepalive = 30,
    };
    if (s_lwt_topic[0]) {
        mc.session.last_will.topic = s_lwt_topic;
        mc.session.last_will.msg = cfg->lwt_msg ? cfg->lwt_msg : "offline";
        mc.session.last_will.qos = 0;
        mc.session.last_will.retain = 1;
    }

    s_client = esp_mqtt_client_init(&mc);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

bool mqttc_connected(void)
{
    return s_connected;
}

esp_err_t mqttc_publish(const char *topic, const char *payload, bool retain)
{
    if (s_client == NULL || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int rc = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, retain ? 1 : 0);
    return rc >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqttc_subscribe(const char *topic, mqttc_cb_t cb, void *ctx)
{
    if (topic == NULL || cb == NULL || s_mtx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    sub_t *frei = NULL;
    for (size_t i = 0; i < MQTTC_MAX_SUBS; i++) {
        if (s_subs[i].used && strcmp(s_subs[i].topic, topic) == 0) {
            frei = &s_subs[i];
            break;
        }
        if (frei == NULL && !s_subs[i].used) {
            frei = &s_subs[i];
        }
    }
    if (frei == NULL) {
        xSemaphoreGive(s_mtx);
        return ESP_ERR_NO_MEM;
    }
    snprintf(frei->topic, sizeof(frei->topic), "%s", topic);
    frei->cb = cb;
    frei->ctx = ctx;
    frei->used = true;
    xSemaphoreGive(s_mtx);

    if (s_connected) {
        esp_mqtt_client_subscribe(s_client, topic, 0);
    }
    return ESP_OK;
}
