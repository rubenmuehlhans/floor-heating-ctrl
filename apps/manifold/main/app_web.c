#include "app_web.h"

#include <stdlib.h>
#include <string.h>

#include "app_calib.h"
#include "app_control.h"
#include "app_mqtt.h"
#include "app_ui.h"
#include "atc_ble.h"
#include "bemf.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netmgr.h"
#include "peers.h"
#include "sensors_local.h"

static const char *TAG = "web";

#define MAX_BODY 8192

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

/* ------------------------------------------------------------------ */
/* Hilfsmittel                                                         */
/* ------------------------------------------------------------------ */

static esp_err_t send_json_obj(httpd_req_t *req, cJSON *root)
{
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t rc = httpd_resp_sendstr(req, txt);
    free(txt);
    return rc;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json_obj(req, root);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", msg);
    return send_json_obj(req, root);
}

/* Liest den Anfragekoerper vollstaendig ein. Der Aufrufer gibt ihn frei. */
static char *read_body(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        return NULL;
    }
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
    }
    buf[received] = '\0';
    return buf;
}

/* Letztes Pfadsegment, etwa "stop" aus /api/channel/3/stop. */
static const char *last_segment(const char *uri)
{
    const char *slash = strrchr(uri, '/');
    return slash ? slash + 1 : uri;
}

/* Zahl aus dem vorletzten Pfadsegment, etwa 3 aus /api/channel/3/cmd. */
static int segment_number(const char *uri, int from_end)
{
    const char *p = uri + strlen(uri);
    int seen = 0;
    while (p > uri) {
        p--;
        if (*p == '/') {
            seen++;
            if (seen == from_end) {
                return atoi(p + 1);
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Oberflaeche                                                         */
/* ------------------------------------------------------------------ */

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz_start,
                           index_html_gz_end - index_html_gz_start);
}

/*
 * Captive Portal: Betriebssysteme pruefen ueber feste Adressen, ob ein Netz
 * frei ins Internet fuehrt. Solange der Einrichtungs-Zugangspunkt offen ist,
 * wird jede unbekannte Adresse auf die Einrichtungsseite umgeleitet - dann
 * oeffnet sich das Anmeldefenster von selbst.
 *
 * Im regulaeren Netz bleibt es bei einer gewoehnlichen Fehlermeldung.
 */
static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;

    netmgr_status_t net;
    netmgr_status(&net);

    if (net.ap_active && net.ap_ip[0]) {
        char location[64];
        snprintf(location, sizeof(location), "http://%s/", net.ap_ip);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", location);
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Diese Adresse gibt es hier nicht.");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Zustand                                                             */
/* ------------------------------------------------------------------ */

static const char *op_name(valve_op_t op)
{
    switch (op) {
    case VALVE_OPENING:
        return "opening";
    case VALVE_CLOSING:
        return "closing";
    default:
        return "idle";
    }
}

static const char *stop_reason_name(int reason)
{
    switch (reason) {
    case VALVE_STOP_TARGET:
        return "Zielstellung erreicht";
    case VALVE_STOP_ENDSTOP:
        return "Endlage erkannt";
    case VALVE_STOP_TIMEOUT:
        return "Maximallaufzeit abgelaufen";
    case VALVE_STOP_COMMAND:
        return "von Hand gestoppt";
    default:
        return "";
    }
}

static void add_calib_status(cJSON *parent)
{
    calib_status_t st;
    calib_get_status(&st);

    cJSON *c = cJSON_AddObjectToObject(parent, "calib");
    const char *state = "idle";
    switch (st.state) {
    case CALIB_RUNNING:
        state = "running";
        break;
    case CALIB_DONE:
        state = "done";
        break;
    case CALIB_FAILED:
        state = "failed";
        break;
    default:
        break;
    }
    cJSON_AddStringToObject(c, "state", state);
    cJSON_AddNumberToObject(c, "channel", st.channel);
    cJSON_AddNumberToObject(c, "group", st.group + 1);
    cJSON_AddNumberToObject(c, "phase", st.phase);
    cJSON_AddNumberToObject(c, "sample_count", st.sample_count);
    cJSON_AddNumberToObject(c, "sample_period_ms", st.sample_period_ms);
    cJSON_AddStringToObject(c, "message", st.message);

    cJSON *marks = cJSON_AddObjectToObject(c, "marks");
    cJSON_AddNumberToObject(marks, "close_from", st.close_from);
    cJSON_AddNumberToObject(marks, "close_to", st.close_to);
    cJSON_AddNumberToObject(marks, "open_from", st.open_from);
    cJSON_AddNumberToObject(marks, "open_to", st.open_to);
    cJSON_AddNumberToObject(marks, "baseline_close_mv", st.baseline_close_mv);
    cJSON_AddNumberToObject(marks, "stall_close_mv", st.stall_close_mv);
    cJSON_AddNumberToObject(marks, "baseline_open_mv", st.baseline_open_mv);
    cJSON_AddNumberToObject(marks, "stall_open_mv", st.stall_open_mv);

    if (st.suggestion_valid) {
        cJSON *s = cJSON_AddObjectToObject(c, "suggestion");
        cJSON_AddNumberToObject(s, "close_ms", st.suggest_close_ms);
        cJSON_AddNumberToObject(s, "open_ms", st.suggest_open_ms);
        cJSON_AddNumberToObject(s, "max_ms", st.suggest_max_ms);
        cJSON_AddNumberToObject(s, "bemf_mv", st.suggest_bemf_mv);
        cJSON_AddNumberToObject(s, "hyst_mv", st.suggest_hyst_mv);
    }
}

/*
 * Zustand des Geräts.
 *
 * Solange kein Ventil fährt, ändert sich der Zustand nur bei Ereignissen -
 * einem neuen Messwert, einem Regeleingriff, einer Bedienung. Der
 * Änderungszähler wird deshalb als ETag mitgegeben: fragt die Oberfläche mit
 * demselben Wert erneut an, genügt eine leere Antwort und der JSON-Aufbau
 * entfällt vollständig.
 *
 * Während einer Fahrt läuft die Positionsschätzung dagegen fortlaufend weiter,
 * ohne den Zähler zu erhöhen. Dann wird immer die volle Antwort gesendet.
 */
static esp_err_t state_get(httpd_req_t *req)
{
    char etag[24];
    snprintf(etag, sizeof(etag), "\"%lu\"", (unsigned long)control_revision());

    if (!control_busy()) {
        char seen[24] = {0};
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", seen, sizeof(seen)) == ESP_OK &&
            strcmp(seen, etag) == 0) {
            httpd_resp_set_status(req, "304 Not Modified");
            httpd_resp_set_hdr(req, "ETag", etag);
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            return httpd_resp_send(req, NULL, 0);
        }
    }
    httpd_resp_set_hdr(req, "ETag", etag);

    /* Zusammen rund 2,6 kB - der Stack der HTTP-Task traegt das nicht.
     * Der Server arbeitet Anfragen in einer einzigen Task ab, ein statischer
     * Puffer ist deshalb ausreichend. */
    static ctl_snapshot_t snap;
    static app_config_t cfg;
    control_snapshot(&snap);
    cfg_copy(&cfg);

    netmgr_status_t net;
    netmgr_status(&net);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "revision", control_revision());
    cJSON_AddNumberToObject(root, "uptime_s", snap.uptime_s);
    cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());

    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "version", desc->version);

    /* Stabile Kennung der Anlage. Home Assistant braucht sie, um ein Geraet
     * ueber wechselnde Adressen hinweg wiederzuerkennen. */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[24];
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    snprintf(buf, sizeof(buf), "fbh_%02x%02x%02x", mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(dev, "id", buf);
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    cJSON_AddStringToObject(dev, "mac", buf);
    cJSON_AddStringToObject(dev, "site", cfg.site);
    cJSON_AddStringToObject(dev, "model", "ESP32 Ventilsteuerung, 11 Heizkreise");
    cJSON_AddNumberToObject(dev, "channels", HW_CHANNEL_COUNT);

    cJSON *jnet = cJSON_AddObjectToObject(root, "net");
    cJSON_AddBoolToObject(jnet, "connected", net.sta_connected);
    cJSON_AddStringToObject(jnet, "ip", net.ip);
    cJSON_AddBoolToObject(jnet, "ap_active", net.ap_active);
    cJSON_AddStringToObject(jnet, "ap_ip", net.ap_ip);
    cJSON_AddNumberToObject(jnet, "rssi", net.rssi);
    cJSON_AddBoolToObject(jnet, "time_valid", net.time_valid);

    cJSON *rooms = cJSON_AddArrayToObject(root, "rooms");
    for (int i = 0; i < snap.room_count; i++) {
        const ctl_room_t *r = &snap.rooms[i];
        cJSON *jr = cJSON_CreateObject();
        cJSON_AddNumberToObject(jr, "id", r->id);
        cJSON_AddStringToObject(jr, "name", r->name);
        cJSON *chs = cJSON_AddArrayToObject(jr, "channels");
        for (int n = 1; n <= HW_CHANNEL_COUNT; n++) {
            if (r->channel_mask & (1U << (n - 1))) {
                cJSON_AddItemToArray(chs, cJSON_CreateNumber(n));
            }
        }
        cJSON_AddStringToObject(jr, "mode", r->mode_heat ? "heat" : "off");
        cJSON_AddNumberToObject(jr, "target_c", r->target_c);
        cJSON_AddBoolToObject(jr, "sensor_set", r->sensor_set);
        cJSON_AddBoolToObject(jr, "temp_valid", r->temp_valid);
        cJSON_AddNumberToObject(jr, "temp_c", r->temp_c);
        cJSON_AddNumberToObject(jr, "humidity", r->humidity);
        cJSON_AddNumberToObject(jr, "battery", r->battery);
        cJSON_AddNumberToObject(jr, "temp_age_s", r->temp_age_s);
        cJSON_AddNumberToObject(jr, "target_position", r->target_position);
        cJSON_AddNumberToObject(jr, "next_check_s", r->next_check_s);
        cJSON_AddItemToArray(rooms, jr);
    }

    cJSON *chans = cJSON_AddArrayToObject(root, "channels");
    for (int i = 0; i < HW_CHANNEL_COUNT; i++) {
        const ctl_channel_t *c = &snap.ch[i];
        cJSON *jc = cJSON_CreateObject();
        cJSON_AddNumberToObject(jc, "id", i + 1);
        cJSON_AddNumberToObject(jc, "position", c->position);
        cJSON_AddBoolToObject(jc, "known", c->position_known);
        cJSON_AddStringToObject(jc, "op", op_name(c->op));
        cJSON_AddNumberToObject(jc, "group", c->group + 1);
        cJSON_AddBoolToObject(jc, "reserved", c->reserved);
        cJSON_AddBoolToObject(jc, "manual", c->manual_hold);
        cJSON_AddBoolToObject(jc, "pending", c->request_open);
        cJSON_AddNumberToObject(jc, "bemf_mv", snap.bemf_mv[c->group]);
        cJSON_AddBoolToObject(jc, "calibrated", cfg.channels[i].calibrated);
        cJSON_AddStringToObject(jc, "last_stop", stop_reason_name(c->last_stop_reason));
        cJSON_AddNumberToObject(jc, "last_move_ms", c->last_move_ms);
        cJSON_AddItemToArray(chans, jc);
    }

    cJSON *bemf = cJSON_AddArrayToObject(root, "bemf_mv");
    for (int g = 0; g < HW_BEMF_GROUP_COUNT; g++) {
        cJSON_AddItemToArray(bemf, cJSON_CreateNumber(snap.bemf_mv[g]));
    }

    /* Rohwerte der Tasten, damit sich die Schwellen abgleichen lassen: eine
     * Taste gilt als beruehrt, wenn ihr Wert unter die Schwelle faellt. */
    uint32_t touch[HW_TOUCH_COUNT];
    ui_touch_raw(touch);
    cJSON *jtouch = cJSON_AddArrayToObject(root, "touch_raw");
    for (int i = 0; i < HW_TOUCH_COUNT; i++) {
        cJSON_AddItemToArray(jtouch, cJSON_CreateNumber(touch[i]));
    }

    static sensors_snapshot_t sens;
    sensors_get(&sens);
    cJSON *jsens = cJSON_AddObjectToObject(root, "local_sensors");
    cJSON *jds = cJSON_AddArrayToObject(jsens, "ds18b20");
    for (int i = 0; i < sens.ds_count; i++) {
        char addr[24];
        snprintf(addr, sizeof(addr), "0x%016llX", (unsigned long long)sens.ds[i].address);
        cJSON *jd = cJSON_CreateObject();
        cJSON_AddStringToObject(jd, "address", addr);
        cJSON_AddBoolToObject(jd, "valid", sens.ds[i].valid);
        cJSON_AddNumberToObject(jd, "temp_c", sens.ds[i].temp_c);
        cJSON_AddItemToArray(jds, jd);
    }
    cJSON *jhdc = cJSON_AddObjectToObject(jsens, "hdc1080");
    cJSON_AddBoolToObject(jhdc, "valid", sens.hdc_valid);
    cJSON_AddNumberToObject(jhdc, "temp_c", sens.hdc_temp_c);
    cJSON_AddNumberToObject(jhdc, "humidity", sens.hdc_humidity);

    add_calib_status(root);
    return send_json_obj(req, root);
}

/* ------------------------------------------------------------------ */
/* Konfiguration                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t config_get(httpd_req_t *req)
{
    static app_config_t cfg;
    cfg_copy(&cfg);
    char *json = cfg_to_json(&cfg, false);
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t rc = httpd_resp_sendstr(req, json);
    free(json);
    return rc;
}

/*
 * Neue WLAN-Zugangsdaten uebernehmen - aber erst, nachdem die Antwort auf die
 * Anfrage draussen ist. Das Umschalten trennt die Verbindung, ueber die gerade
 * gespeichert wurde; geschieht es zu frueh, wartet der Browser vergeblich und
 * die Speicherung sieht wie ein Fehlschlag aus, obwohl sie geklappt hat.
 */
static void wifi_apply_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));

    static app_config_t cfg;
    cfg_copy(&cfg);
    ESP_LOGI(TAG, "WLAN-Zugangsdaten geaendert, Verbindung wird neu aufgebaut");
    netmgr_apply(&cfg);
    vTaskDelete(NULL);
}

static esp_err_t config_put(httpd_req_t *req)
{
    char *body = read_body(req);
    if (body == NULL) {
        return send_error(req, "400 Bad Request", "Anfrage konnte nicht gelesen werden");
    }

    /* Bestehende Zugangsdaten als Ausgangsbasis: die Oberflaeche bekommt sie
     * nie zu sehen und kann sie deshalb auch nicht zuruecksenden. */
    static app_config_t next;
    static cfg_wifi_t before;
    cfg_copy(&next);
    before = next.wifi;

    char err[128] = {0};
    esp_err_t rc = cfg_from_json(body, &next, err, sizeof(err));
    free(body);
    if (rc != ESP_OK) {
        return send_error(req, "400 Bad Request", err[0] ? err : "Ungueltige Konfiguration");
    }

    rc = cfg_set(&next, err, sizeof(err));
    if (rc != ESP_OK) {
        return send_error(req, "400 Bad Request", err[0] ? err : "Konfiguration abgelehnt");
    }

    control_config_changed();
    mqtt_config_changed();
    ui_config_changed();
    ESP_LOGI(TAG, "Konfiguration geaendert: %u Raeume", next.room_count);

    /* Nur bei tatsaechlich geaenderten Zugangsdaten ans Funkteil gehen. Wer
     * Raeume oder Fahrzeiten speichert, soll deswegen nicht die Verbindung
     * verlieren. */
    bool wifi_changed = strcmp(before.ssid, next.wifi.ssid) != 0 ||
                        strcmp(before.pass, next.wifi.pass) != 0 ||
                        strcmp(before.hostname, next.wifi.hostname) != 0;

    esp_err_t sent = send_ok(req);
    if (wifi_changed) {
        xTaskCreate(wifi_apply_task, "wifi_apply", 4096, NULL, 4, NULL);
    }
    return sent;
}

/* ------------------------------------------------------------------ */
/* Raum- und Kanalbefehle                                              */
/* ------------------------------------------------------------------ */

static esp_err_t room_post(httpd_req_t *req)
{
    int room_id = segment_number(req->uri, 2);
    const char *action = last_segment(req->uri);

    if (strcmp(action, "check") == 0) {
        control_room_check_now((uint8_t)room_id);
        return send_ok(req);
    }

    char *body = read_body(req);
    if (body == NULL) {
        return send_error(req, "400 Bad Request", "Anfrage konnte nicht gelesen werden");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        return send_error(req, "400 Bad Request", "Ungueltiges JSON");
    }

    esp_err_t rc;
    if (strcmp(action, "target") == 0) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "target_c");
        if (!cJSON_IsNumber(v)) {
            rc = send_error(req, "400 Bad Request", "Feld target_c fehlt");
        } else {
            control_room_set_target((uint8_t)room_id, (float)v->valuedouble);
            rc = send_ok(req);
        }
    } else if (strcmp(action, "mode") == 0) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "mode");
        if (!cJSON_IsString(v)) {
            rc = send_error(req, "400 Bad Request", "Feld mode fehlt");
        } else {
            control_room_set_mode((uint8_t)room_id, strcmp(v->valuestring, "off") != 0);
            rc = send_ok(req);
        }
    } else {
        rc = send_error(req, "404 Not Found", "Unbekannte Aktion");
    }

    cJSON_Delete(root);
    return rc;
}

static esp_err_t channel_post(httpd_req_t *req)
{
    /* /api/channel/all/cmd wirkt auf alle Kreise - fuer den Notfall, in dem
     * die ganze Anlage auf oder zu soll. */
    bool all = strstr(req->uri, "/channel/all/") != NULL;
    int channel = all ? 0 : segment_number(req->uri, 2);
    if (!all && !hw_channel_valid((uint8_t)channel)) {
        return send_error(req, "400 Bad Request", "Unbekannter Kanal");
    }

    char *body = read_body(req);
    if (body == NULL) {
        return send_error(req, "400 Bad Request", "Anfrage konnte nicht gelesen werden");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        return send_error(req, "400 Bad Request", "Ungueltiges JSON");
    }

    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    esp_err_t rc;
    if (!cJSON_IsString(cmd)) {
        rc = send_error(req, "400 Bad Request", "Feld cmd fehlt");
    } else if (strcmp(cmd->valuestring, "open") == 0) {
        if (all) { control_cmd_all(true); } else { control_cmd_open((uint8_t)channel); }
        rc = send_ok(req);
    } else if (strcmp(cmd->valuestring, "close") == 0) {
        if (all) { control_cmd_all(false); } else { control_cmd_close((uint8_t)channel); }
        rc = send_ok(req);
    } else if (strcmp(cmd->valuestring, "auto") == 0) {
        if (all) { control_cmd_all_auto(); } else { control_cmd_auto((uint8_t)channel); }
        rc = send_ok(req);
    } else if (strcmp(cmd->valuestring, "stop") == 0) {
        if (all) {
            for (uint8_t n = 1; n <= HW_CHANNEL_COUNT; n++) {
                control_cmd_stop(n);
            }
        } else {
            control_cmd_stop((uint8_t)channel);
        }
        rc = send_ok(req);
    } else if (strcmp(cmd->valuestring, "position") == 0) {
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "position");
        if (!cJSON_IsNumber(p)) {
            rc = send_error(req, "400 Bad Request", "Feld position fehlt");
        } else {
            control_cmd_position((uint8_t)channel, (float)p->valuedouble);
            rc = send_ok(req);
        }
    } else {
        rc = send_error(req, "400 Bad Request", "Unbekannter Befehl");
    }

    cJSON_Delete(root);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Kalibrierung                                                        */
/* ------------------------------------------------------------------ */

/* Messreihe ab einem Index. Die Oberflaeche holt nur die neuen Werte ab und
 * zeichnet sie fortlaufend. */
static esp_err_t calib_get(httpd_req_t *req)
{
    char query[64] = {0};
    uint16_t from = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[16];
        if (httpd_query_key_value(query, "from", value, sizeof(value)) == ESP_OK) {
            from = (uint16_t)atoi(value);
        }
    }

    static uint16_t buf[512];
    size_t n = calib_get_samples(from, buf, sizeof(buf) / sizeof(buf[0]));

    cJSON *root = cJSON_CreateObject();
    add_calib_status(root);
    cJSON_AddNumberToObject(root, "from", from);

    cJSON *arr = cJSON_AddArrayToObject(root, "samples");
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(buf[i]));
    }
    return send_json_obj(req, root);
}

static esp_err_t calib_post(httpd_req_t *req)
{
    const char *action = last_segment(req->uri);

    if (strcmp(action, "abort") == 0) {
        calib_abort();
        return send_ok(req);
    }
    if (strcmp(action, "discard") == 0) {
        calib_discard();
        return send_ok(req);
    }
    if (strcmp(action, "accept") == 0) {
        char err[128] = {0};
        if (calib_accept(err, sizeof(err)) != ESP_OK) {
            return send_error(req, "400 Bad Request", err);
        }
        return send_ok(req);
    }

    /* /api/calib/{n}/start */
    int channel = segment_number(req->uri, 2);
    esp_err_t rc = calib_start((uint8_t)channel);
    if (rc == ESP_ERR_NOT_FINISHED) {
        return send_error(req, "409 Conflict",
                          "Der Kanal oder ein Nachbar derselben Messgruppe faehrt gerade");
    }
    if (rc == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "Es laeuft bereits eine Kalibrierung");
    }
    if (rc != ESP_OK) {
        return send_error(req, "400 Bad Request", "Kalibrierung liess sich nicht starten");
    }
    return send_ok(req);
}

/* ------------------------------------------------------------------ */
/* Thermometer                                                         */
/* ------------------------------------------------------------------ */

static esp_err_t ble_get(httpd_req_t *req)
{
    static atc_device_t devices[ATC_MAX_DEVICES];
    size_t n = atc_ble_devices(devices, ATC_MAX_DEVICES);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "devices");
    for (size_t i = 0; i < n; i++) {
        const atc_device_t *d = &devices[i];
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", d->mac[0], d->mac[1], d->mac[2],
                 d->mac[3], d->mac[4], d->mac[5]);
        cJSON *jd = cJSON_CreateObject();
        cJSON_AddStringToObject(jd, "mac", mac);
        cJSON_AddStringToObject(jd, "name", d->name);
        cJSON_AddNumberToObject(jd, "rssi", d->rssi);
        cJSON_AddNumberToObject(jd, "temp_c", d->temp_c);
        cJSON_AddNumberToObject(jd, "humidity", d->humidity);
        cJSON_AddNumberToObject(jd, "battery", d->battery);
        cJSON_AddNumberToObject(jd, "battery_mv", d->battery_mv);
        cJSON_AddNumberToObject(jd, "packets", d->packets);
        cJSON_AddStringToObject(jd, "format", d->pvvx ? "pvvx" : "atc1441");
        cJSON_AddItemToArray(arr, jd);
    }
    return send_json_obj(req, root);
}

/* ------------------------------------------------------------------ */
/* Weitere Platinen im Haus                                            */
/* ------------------------------------------------------------------ */

static esp_err_t peers_get_handler(httpd_req_t *req)
{
    static peer_t list[PEERS_MAX];
    size_t n = peers_get(list, PEERS_MAX);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "peers");
    for (size_t i = 0; i < n; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", list[i].id);
        cJSON_AddStringToObject(j, "site", list[i].site);
        cJSON_AddStringToObject(j, "role", list[i].role);
        cJSON_AddStringToObject(j, "host", list[i].host);
        cJSON_AddStringToObject(j, "hostname", list[i].hostname);
        cJSON_AddItemToArray(arr, j);
    }
    return send_json_obj(req, root);
}

/* Erreichbare WLAN-Netze fuer die Einrichtung ueber das Captive Portal. */
static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    static netmgr_ap_t aps[24];
    size_t n = netmgr_scan(aps, sizeof(aps) / sizeof(aps[0]));

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    for (size_t i = 0; i < n; i++) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "ssid", aps[i].ssid);
        cJSON_AddNumberToObject(j, "rssi", aps[i].rssi);
        cJSON_AddBoolToObject(j, "secure", aps[i].secure);
        cJSON_AddItemToArray(arr, j);
    }
    return send_json_obj(req, root);
}

/* ------------------------------------------------------------------ */
/* System                                                              */
/* ------------------------------------------------------------------ */

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t system_post(httpd_req_t *req)
{
    const char *action = last_segment(req->uri);

    if (strcmp(action, "restart") == 0) {
        xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
        return send_ok(req);
    }
    if (strcmp(action, "factory") == 0) {
        if (cfg_reset_defaults() != ESP_OK) {
            return send_error(req, "500 Internal Server Error", "Zuruecksetzen fehlgeschlagen");
        }
        control_config_changed();
        mqtt_config_changed();
        return send_ok(req);
    }
    return send_error(req, "404 Not Found", "Unbekannte Aktion");
}

static esp_err_t ota_post(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        return send_error(req, "500 Internal Server Error", "Keine freie Firmware-Partition");
    }

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Aktualisierung nicht startbar");
    }

    char *buf = malloc(2048);
    if (buf == NULL) {
        esp_ota_abort(handle);
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }

    int remaining = req->content_len;
    ESP_LOGI(TAG, "Firmware-Aktualisierung gestartet, %d Byte", remaining);

    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining > 2048 ? 2048 : remaining);
        if (r <= 0) {
            free(buf);
            esp_ota_abort(handle);
            return send_error(req, "400 Bad Request", "Uebertragung abgebrochen");
        }
        if (esp_ota_write(handle, buf, r) != ESP_OK) {
            free(buf);
            esp_ota_abort(handle);
            return send_error(req, "500 Internal Server Error", "Schreiben fehlgeschlagen");
        }
        remaining -= r;
    }
    free(buf);

    if (esp_ota_end(handle) != ESP_OK) {
        return send_error(req, "400 Bad Request", "Die Firmware ist nicht gueltig");
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Startpartition nicht setzbar");
    }

    ESP_LOGW(TAG, "Neue Firmware uebernommen, Neustart folgt");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return send_ok(req);
}

/* ------------------------------------------------------------------ */

static const httpd_uri_t s_routes[] = {
    {.uri = "/", .method = HTTP_GET, .handler = page_get},
    {.uri = "/index.html", .method = HTTP_GET, .handler = page_get},
    {.uri = "/api/state", .method = HTTP_GET, .handler = state_get},
    {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
    {.uri = "/api/config", .method = HTTP_PUT, .handler = config_put},
    {.uri = "/api/room/*", .method = HTTP_POST, .handler = room_post},
    {.uri = "/api/channel/*", .method = HTTP_POST, .handler = channel_post},
    {.uri = "/api/calib", .method = HTTP_GET, .handler = calib_get},
    {.uri = "/api/calib/*", .method = HTTP_POST, .handler = calib_post},
    {.uri = "/api/ble", .method = HTTP_GET, .handler = ble_get},
    {.uri = "/api/peers", .method = HTTP_GET, .handler = peers_get_handler},
    {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get},
    {.uri = "/api/system/*", .method = HTTP_POST, .handler = system_post},
    {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post},
};

esp_err_t web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = sizeof(s_routes) / sizeof(s_routes[0]) + 2;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    /* Die Firmware-Aktualisierung braucht mehr Geduld als eine normale
     * Abfrage. */
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;

    httpd_handle_t server = NULL;
    esp_err_t rc = httpd_start(&server, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Webserver liess sich nicht starten: %s", esp_err_to_name(rc));
        return rc;
    }

    for (size_t i = 0; i < sizeof(s_routes) / sizeof(s_routes[0]); i++) {
        httpd_register_uri_handler(server, &s_routes[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);

    ESP_LOGI(TAG, "Weboberflaeche laeuft auf Port %d", config.server_port);
    return ESP_OK;
}
