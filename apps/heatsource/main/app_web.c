#include "app_web.h"

#include <stdlib.h>
#include <string.h>

#include "app_pumps.h"
#include "app_sensors.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqttc.h"
#include "netmgr.h"
#include "peers.h"

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

static const char *last_segment(const char *uri)
{
    const char *slash = strrchr(uri, '/');
    return slash ? slash + 1 : uri;
}

/* Ganzzahliger Abfrageparameter, etwa step aus /api/history?step=5. */
static int query_int(httpd_req_t *req, const char *key, int fallback)
{
    char query[96];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return fallback;
    }
    char wert[16];
    if (httpd_query_key_value(query, key, wert, sizeof(wert)) != ESP_OK) {
        return fallback;
    }
    return atoi(wert);
}

static void device_id(char *out, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "heiz_%02x%02x%02x", mac[3], mac[4], mac[5]);
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
 * Captive Portal: Solange der Einrichtungs-Zugangspunkt offen ist, wird jede
 * unbekannte Adresse auf die Einrichtungsseite umgeleitet. Im regulaeren Netz
 * bleibt es bei einer gewoehnlichen Fehlermeldung.
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

static void add_probe(cJSON *arr, const sens_probe_t *p)
{
    char rom[20];
    ot_rom_to_str(p->rom, rom, sizeof(rom));

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "rom", rom);
    cJSON_AddStringToObject(j, "name", p->name);
    cJSON_AddStringToObject(j, "role", cfg_role_key(p->role));
    cJSON_AddStringToObject(j, "role_label", cfg_role_label(p->role));
    cJSON_AddNumberToObject(j, "bus", p->bus);
    cJSON_AddBoolToObject(j, "assigned", p->assigned);
    if (p->valid) {
        cJSON_AddNumberToObject(j, "temp_c", p->temp_c);
        cJSON_AddNumberToObject(j, "raw_c", p->raw_c);
        cJSON_AddNumberToObject(j, "delta30_c", p->delta30_c);
        cJSON_AddNumberToObject(j, "age_s", p->age_s);
    } else {
        cJSON_AddNullToObject(j, "temp_c");
        cJSON_AddNullToObject(j, "raw_c");
        cJSON_AddNullToObject(j, "delta30_c");
        cJSON_AddNullToObject(j, "age_s");
    }
    cJSON_AddNumberToObject(j, "offset_k", p->offset_k);
    cJSON_AddNumberToObject(j, "reads", p->reads);
    cJSON_AddNumberToObject(j, "errors", p->errors);
    cJSON_AddItemToArray(arr, j);
}

/*
 * Der Server beantwortet Anfragen in genau einer Aufgabe; die Puffer duerfen
 * deshalb statisch sein und muessen nicht auf den Stapel.
 */
static esp_err_t state_get(httpd_req_t *req)
{
    static sens_snapshot_t snap;
    static app_config_t cfg;
    sensors_get(&snap);
    cfg_copy(&cfg);

    netmgr_status_t net;
    netmgr_status(&net);

    cJSON *root = cJSON_CreateObject();

    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    char id[24];
    device_id(id, sizeof(id));
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macs[18];
    snprintf(macs, sizeof(macs), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    cJSON_AddStringToObject(dev, "id", id);
    cJSON_AddStringToObject(dev, "mac", macs);
    cJSON_AddStringToObject(dev, "site", cfg.site);
    cJSON_AddStringToObject(dev, "model", "Waermeerzeuger");
    cJSON_AddStringToObject(dev, "role", PEERS_ROLE_HEAT);

    const esp_app_desc_t *app = esp_app_get_description();
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());
    cJSON_AddBoolToObject(root, "setup_open", cfg.site[0] == '\0');

    cJSON *n = cJSON_AddObjectToObject(root, "net");
    cJSON_AddBoolToObject(n, "sta", net.sta_connected);
    cJSON_AddBoolToObject(n, "ap", net.ap_active);
    cJSON_AddStringToObject(n, "ip", net.sta_connected ? net.ip : net.ap_ip);
    cJSON_AddNumberToObject(n, "rssi", net.rssi);
    cJSON_AddBoolToObject(n, "time_valid", net.time_valid);

    cJSON *bus = cJSON_AddObjectToObject(root, "onewire");
    cJSON *pins = cJSON_AddArrayToObject(bus, "pins");
    for (int i = 0; i < CFG_MAX_BUSES; i++) {
        cJSON_AddItemToArray(pins, cJSON_CreateNumber(cfg.onewire_pin[i]));
    }
    cJSON_AddNumberToObject(bus, "found", snap.count);
    cJSON_AddNumberToObject(bus, "assigned", snap.assigned_count);
    cJSON_AddNumberToObject(bus, "round_ms", snap.round_ms);
    cJSON_AddNumberToObject(bus, "rounds", snap.rounds);
    cJSON_AddNumberToObject(bus, "poll_s", cfg.poll_s);

    cJSON *arr = cJSON_AddArrayToObject(root, "probes");
    for (size_t i = 0; i < snap.count; i++) {
        add_probe(arr, &snap.probes[i]);
    }

    /* Abgeleitete Werte, soweit die Rollen vergeben sind. */
    cJSON *abl = cJSON_AddObjectToObject(root, "derived");
    float vl = 0.0f, rl = 0.0f;
    if (sensors_role_value(ROLE_KESSEL_VL, &vl, NULL) &&
        sensors_role_value(ROLE_KESSEL_RL, &rl, NULL)) {
        cJSON_AddNumberToObject(abl, "kessel_spreizung_k", vl - rl);
    }
    if (sensors_role_value(ROLE_HK1_VL, &vl, NULL) && sensors_role_value(ROLE_HK1_RL, &rl, NULL)) {
        cJSON_AddNumberToObject(abl, "hk1_spreizung_k", vl - rl);
    }
    if (sensors_role_value(ROLE_HK2_VL, &vl, NULL) && sensors_role_value(ROLE_HK2_RL, &rl, NULL)) {
        cJSON_AddNumberToObject(abl, "hk2_spreizung_k", vl - rl);
    }

    /* Heizkreise mit Pumpenzustand. */
    static circuit_status_t kreise[CFG_MAX_CIRCUITS];
    size_t kn = pumps_status(kreise, CFG_MAX_CIRCUITS);
    cJSON *ck = cJSON_AddArrayToObject(root, "circuits");
    for (size_t i = 0; i < kn; i++) {
        const circuit_status_t *c = &kreise[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "id", c->id);
        cJSON_AddStringToObject(j, "name", c->name);
        cJSON_AddBoolToObject(j, "enabled", c->enabled);
        cJSON_AddStringToObject(j, "mode",
                                c->mode == PUMP_MODE_ON ? "ein"
                                : (c->mode == PUMP_MODE_OFF ? "aus" : "auto"));
        cJSON_AddBoolToObject(j, "on", c->on);
        cJSON_AddStringToObject(j, "reason", pump_reason_text(c->reason));
        cJSON_AddNumberToObject(j, "since_s", c->since_s);
        cJSON_AddBoolToObject(j, "demand", c->demand);
        cJSON_AddBoolToObject(j, "stale", c->stale);
        cJSON_AddBoolToObject(j, "any_seen", c->any_seen);
        cJSON_AddStringToObject(j, "path",
                                c->path == PUMP_PATH_MQTT ? "mqtt"
                                : (c->path == PUMP_PATH_HTTP ? "http" : "keiner"));
        if (c->vl_valid) {
            cJSON_AddNumberToObject(j, "vl_c", c->vl_c);
        } else {
            cJSON_AddNullToObject(j, "vl_c");
        }
        if (c->rl_valid) {
            cJSON_AddNumberToObject(j, "rl_c", c->rl_c);
        } else {
            cJSON_AddNullToObject(j, "rl_c");
        }
        if (c->vl_valid && c->rl_valid) {
            cJSON_AddNumberToObject(j, "spread_k", c->vl_c - c->rl_c);
        } else {
            cJSON_AddNullToObject(j, "spread_k");
        }
        cJSON *rel = cJSON_AddObjectToObject(j, "relay");
        cJSON_AddBoolToObject(rel, "known", c->relay_known);
        cJSON_AddBoolToObject(rel, "on", c->relay_on);
        cJSON_AddBoolToObject(rel, "online", c->relay_online);
        cJSON_AddNumberToObject(rel, "age_s", c->relay_age_s);
        cJSON_AddBoolToObject(rel, "mismatch", c->relay_mismatch);
        if (c->path == PUMP_PATH_HTTP) {
            cJSON_AddNumberToObject(rel, "status", c->last_status);
        }
        cJSON_AddItemToArray(ck, j);
    }

    /* Abgefragte Verteiler. */
    static peer_demand_t quellen[CFG_MAX_PEERS * CFG_MAX_CIRCUITS];
    size_t qn = pumps_peers(quellen, sizeof(quellen) / sizeof(quellen[0]));
    cJSON *cq = cJSON_AddArrayToObject(root, "demand_sources");
    for (size_t i = 0; i < qn; i++) {
        const peer_demand_t *p = &quellen[i];
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", p->id);
        cJSON_AddStringToObject(j, "site", p->site);
        cJSON_AddStringToObject(j, "host", p->host);
        cJSON_AddBoolToObject(j, "seen", p->seen);
        cJSON_AddBoolToObject(j, "demand", p->demand);
        cJSON_AddNumberToObject(j, "max_target", p->max_target);
        cJSON_AddNumberToObject(j, "open_channels", p->open_channels);
        cJSON_AddNumberToObject(j, "rooms_calling", p->rooms_calling);
        cJSON_AddNumberToObject(j, "age_s", p->age_s);
        cJSON_AddNumberToObject(j, "errors", p->errors);
        cJSON_AddItemToArray(cq, j);
    }
    cJSON_AddBoolToObject(root, "mqtt_connected", mqttc_connected());

    cJSON_AddNumberToObject(root, "history_len", sensors_history_len());
    return send_json_obj(req, root);
}

/*
 * Schlanke Auskunft fuer das Nachbargeraet. Sie wird im Sekundentakt
 * abgefragt und traegt deshalb nur Rolle, Wert und Alter.
 */
static esp_err_t measurements_get(httpd_req_t *req)
{
    static sens_snapshot_t snap;
    static app_config_t cfg;
    sensors_get(&snap);
    cfg_copy(&cfg);

    cJSON *root = cJSON_CreateObject();
    char id[24];
    device_id(id, sizeof(id));
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "site", cfg.site);
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));

    /* Das Nachbargeraet wird auch aus dessen Oberflaeche heraus abgefragt, also
     * von einem anderen Ursprung. Die Auskunft ist rein lesend und traegt keine
     * Zugangsdaten; sie wird deshalb freigegeben. */
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    cJSON *arr = cJSON_AddArrayToObject(root, "probes");
    for (size_t i = 0; i < snap.count; i++) {
        const sens_probe_t *p = &snap.probes[i];
        if (p->role == ROLE_NONE || !p->valid) {
            continue;
        }
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "role", cfg_role_key(p->role));
        cJSON_AddNumberToObject(j, "c", p->temp_c);
        cJSON_AddNumberToObject(j, "age_s", p->age_s);
        cJSON_AddItemToArray(arr, j);
    }
    return send_json_obj(req, root);
}

/*
 * Verlauf. Ausgegeben wird stueckweise, damit kein Puffer von zehn Kilobyte
 * und mehr am Stueck belegt werden muss.
 *
 *   /api/history?step=5&max=300
 *
 * step ist der Abstand in Minuten, max die Zahl der Punkte. Der juengste Punkt
 * steht zuletzt, damit die Kurve ohne Umsortieren gezeichnet werden kann.
 */
static esp_err_t history_get(httpd_req_t *req)
{
    int step = query_int(req, "step", 5);
    int limit = query_int(req, "max", 288);
    if (step < 1) {
        step = 1;
    }
    if (limit < 1 || limit > 1440) {
        limit = 288;
    }

    size_t len = sensors_history_len();
    size_t punkte = len / (size_t)step;
    if (punkte > (size_t)limit) {
        punkte = (size_t)limit;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"step_min\":%d,\"points\":%u,\"newest_epoch\":%lu,\"roles\":[", step,
             (unsigned)punkte, (unsigned long)sensors_history_newest_epoch());
    httpd_resp_sendstr_chunk(req, buf);

    for (int r = 1; r < ROLE_COUNT; r++) {
        snprintf(buf, sizeof(buf), "%s\"%s\"", r > 1 ? "," : "", cfg_role_key((probe_role_t)r));
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "],\"series\":{");

    int16_t row[ROLE_COUNT];
    for (int r = 1; r < ROLE_COUNT; r++) {
        snprintf(buf, sizeof(buf), "%s\"%s\":[", r > 1 ? "," : "", cfg_role_key((probe_role_t)r));
        httpd_resp_sendstr_chunk(req, buf);

        /* Von alt nach jung, damit die Kurve in Leserichtung entsteht. */
        for (size_t i = 0; i < punkte; i++) {
            size_t index = (punkte - 1 - i) * (size_t)step;
            if (!sensors_history_row(index, row, ROLE_COUNT) || row[r] == SENS_HIST_NONE) {
                snprintf(buf, sizeof(buf), "%snull", i ? "," : "");
            } else {
                snprintf(buf, sizeof(buf), "%s%d.%d", i ? "," : "", row[r] / 10,
                         abs(row[r] % 10));
            }
            httpd_resp_sendstr_chunk(req, buf);
        }
        httpd_resp_sendstr_chunk(req, "]");
    }
    httpd_resp_sendstr_chunk(req, "}}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
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
 * Neue WLAN-Zugangsdaten uebernehmen, aber erst nachdem die Antwort draussen
 * ist: das Umschalten trennt die Verbindung, ueber die gerade gespeichert
 * wurde.
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
    static int8_t bus_before[CFG_MAX_BUSES];
    cfg_copy(&next);
    before = next.wifi;
    memcpy(bus_before, next.onewire_pin, sizeof(bus_before));

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

    sensors_config_changed();
    pumps_config_changed();
    peers_update_identity(next.wifi.hostname, next.site);
    ESP_LOGI(TAG, "Konfiguration geaendert: %u Fuehler zugeordnet", next.probe_count);

    if (memcmp(bus_before, next.onewire_pin, sizeof(bus_before)) != 0) {
        ESP_LOGW(TAG, "Geaenderte Busbelegung wirkt erst nach einem Neustart");
    }

    bool wifi_changed = strcmp(before.ssid, next.wifi.ssid) != 0 ||
                        strcmp(before.pass, next.wifi.pass) != 0 ||
                        strcmp(before.hostname, next.wifi.hostname) != 0;

    esp_err_t sent = send_ok(req);
    if (wifi_changed) {
        xTaskCreate(wifi_apply_task, "wifi_apply", 4096, NULL, 4, NULL);
    }
    return sent;
}

/* POST /api/circuit/<id>/mode  mit {"mode":"auto|ein|aus"} */
static esp_err_t circuit_post(httpd_req_t *req)
{
    const char *action = last_segment(req->uri);
    if (strcmp(action, "mode") != 0) {
        return send_error(req, "404 Not Found", "Unbekannte Aktion");
    }

    /* Kennung steht im vorletzten Pfadsegment. */
    int id = 0;
    const char *p = req->uri + strlen(req->uri);
    int slashes = 0;
    while (p > req->uri) {
        p--;
        if (*p == '/' && ++slashes == 2) {
            id = atoi(p + 1);
            break;
        }
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
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (!cJSON_IsString(v)) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "Feld mode fehlt");
    }

    pump_mode_t modus = PUMP_MODE_AUTO;
    if (strcmp(v->valuestring, "ein") == 0) {
        modus = PUMP_MODE_ON;
    } else if (strcmp(v->valuestring, "aus") == 0) {
        modus = PUMP_MODE_OFF;
    }
    cJSON_Delete(root);

    if (pumps_set_mode((uint8_t)id, modus) != ESP_OK) {
        return send_error(req, "404 Not Found", "Heizkreis nicht gefunden");
    }
    return send_ok(req);
}

static esp_err_t probes_post(httpd_req_t *req)
{
    const char *action = last_segment(req->uri);
    if (strcmp(action, "rescan") == 0) {
        sensors_rescan();
        return send_ok(req);
    }
    return send_error(req, "404 Not Found", "Unbekannte Aktion");
}

/* ------------------------------------------------------------------ */
/* Netz                                                                */
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
        sensors_config_changed();
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
    {.uri = "/api/measurements", .method = HTTP_GET, .handler = measurements_get},
    {.uri = "/api/history", .method = HTTP_GET, .handler = history_get},
    {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
    {.uri = "/api/config", .method = HTTP_PUT, .handler = config_put},
    {.uri = "/api/probes/*", .method = HTTP_POST, .handler = probes_post},
    {.uri = "/api/circuit/*", .method = HTTP_POST, .handler = circuit_post},
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
