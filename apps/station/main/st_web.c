#include "st_web.h"

#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "atc_ble.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netmgr.h"
#include "peers.h"
#include "st_config.h"
#include "st_poll.h"
#include "esp_core_dump.h"
#include "protokoll.h"
#include "st_hap.h"
#include "st_log.h"
#include "st_ui.h"

static const char *TAG = "web";

#define MAX_BODY 4096

/* Gemeinsame Puffer der Anfragen. Der Webserver bearbeitet sie nacheinander in
 * einem einzigen Auftrag; je eine Kopie spart auf dem Core ohne PSRAM einige
 * Kilobyte gegenueber einer Kopie je Endpunkt. */
static st_plant_t s_anlage;
static atc_device_t s_geraete[ATC_MAX_DEVICES];
static atc_key_t s_schluessel[ATC_MAX_KEYS];

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

void st_device_id(char *out, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "lst_%02x%02x%02x", mac[3], mac[4], mac[5]);
}

/* ------------------------------------------------------------------ */
/* Hilfsmittel, wie im Verteiler                                       */
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
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", msg);
    return send_json_obj(req, root);
}

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

static void mac_text(const uint8_t *m, char *out, size_t len)
{
    snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* ------------------------------------------------------------------ */
/* Oberflaeche und Captive Portal                                      */
/* ------------------------------------------------------------------ */

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

/* Solange der Einrichtungs-Zugangspunkt offen ist, fuehrt jede unbekannte
 * Adresse zur Einrichtungsseite; so oeffnet sich das Anmeldefenster selbst. */
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

static void outdoor_json(cJSON *parent, const st_outdoor_t *o)
{
    cJSON *jo = cJSON_AddObjectToObject(parent, "outdoor");
    cJSON_AddBoolToObject(jo, "assigned", o->assigned);
    if (o->assigned) {
        cJSON_AddStringToObject(jo, "mac", o->mac);
    }
    cJSON_AddBoolToObject(jo, "valid", o->valid);
    if (o->valid) {
        cJSON_AddNumberToObject(jo, "temp_c", o->temp_c);
        if (o->hum_valid) {
            cJSON_AddNumberToObject(jo, "humidity", o->humidity);
        }
        cJSON_AddNumberToObject(jo, "battery", o->battery);
        cJSON_AddNumberToObject(jo, "rssi", o->rssi);
        cJSON_AddNumberToObject(jo, "age_s", o->age_s);
        cJSON_AddStringToObject(jo, "name", o->name);
        /* Woher der Wert kommt: leer heisst eigener Empfang. */
        cJSON_AddStringToObject(jo, "source", o->quelle);
    }
}

static esp_err_t state_get_innen(httpd_req_t *req);

static esp_err_t state_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = state_get_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t state_get_innen(httpd_req_t *req)
{
    st_plant_t *const p = &s_anlage;
    st_poll_snapshot(p);
    st_config_t cfg;
    st_cfg_copy(&cfg);
    netmgr_status_t net;
    netmgr_status(&net);

    char id[24], mac[18];
    st_device_id(id, sizeof(id));
    uint8_t m[6] = {0};
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    mac_text(m, mac, sizeof(mac));

    cJSON *root = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(dev, "id", id);
    cJSON_AddStringToObject(dev, "mac", mac);
    cJSON_AddStringToObject(dev, "site", cfg.site);
    cJSON_AddStringToObject(dev, "model", "Leitstand");
    cJSON_AddStringToObject(dev, "role", PEERS_ROLE_STATION);
    cJSON_AddStringToObject(dev, "board", st_ui_board());

    cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(root, "reset_reason", netmgr_reset_reason());
    cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "heap_min", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "heap_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(root, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddBoolToObject(root, "setup_open", net.ap_active && !net.sta_connected);

    cJSON *jn = cJSON_AddObjectToObject(root, "net");
    cJSON_AddBoolToObject(jn, "sta", net.sta_connected);
    cJSON_AddBoolToObject(jn, "ap", net.ap_active);
    cJSON_AddStringToObject(jn, "ip", net.ip);
    cJSON_AddNumberToObject(jn, "rssi", net.rssi);
    cJSON_AddBoolToObject(jn, "time_valid", net.time_valid);

    outdoor_json(root, &p->outdoor);

    cJSON *jb = cJSON_AddObjectToObject(root, "ble");
    cJSON_AddBoolToObject(jb, "running", atc_ble_running());
    cJSON_AddNumberToObject(jb, "devices", atc_ble_devices(s_geraete, ATC_MAX_DEVICES));
    cJSON_AddNumberToObject(jb, "keys", atc_ble_keys(s_schluessel, ATC_MAX_KEYS));
    memset(s_schluessel, 0, sizeof(s_schluessel));

    cJSON *jp = cJSON_AddObjectToObject(root, "plant");
    int da = 0;
    for (uint8_t i = 0; i < p->heat_count; i++) {
        da += p->heat[i].dev.reachable;
    }
    for (uint8_t i = 0; i < p->manifold_count; i++) {
        da += p->manifolds[i].dev.reachable;
    }
    cJSON_AddNumberToObject(jp, "heat", p->heat_count);
    cJSON_AddNumberToObject(jp, "manifolds", p->manifold_count);
    cJSON_AddNumberToObject(jp, "reachable", da);

    cJSON *jd = cJSON_AddObjectToObject(root, "display");
    cJSON_AddStringToObject(jd, "page", st_ui_page());
    cJSON_AddBoolToObject(jd, "on", st_ui_on());

    st_log_status_t ls;
    st_log_status(&ls);
    cJSON *jl = cJSON_AddObjectToObject(root, "log");
    cJSON_AddBoolToObject(jl, "card", ls.karte);
    cJSON_AddNumberToObject(jl, "size_mb", ls.groesse_mb);
    cJSON_AddNumberToObject(jl, "free_mb", ls.frei_mb);
    cJSON_AddNumberToObject(jl, "today_bytes", ls.heute_byte);
    cJSON_AddNumberToObject(jl, "last_write", ls.zuletzt);
    cJSON_AddNumberToObject(jl, "discarded", ls.verworfen);
    cJSON_AddNumberToObject(jl, "errors", ls.fehler);
    cJSON_AddStringToObject(jl, "message", ls.meldung);
    cJSON_AddBoolToObject(jl, "time_valid", net.time_valid);

    /* HomeKit ohne Code: Den zeigt nur die Anzeige am Geraet. */
    st_hap_stand_t hs;
    st_hap_stand(&hs);
    cJSON *jh = cJSON_AddObjectToObject(root, "homekit");
    cJSON_AddBoolToObject(jh, "active", hs.aktiv);
    if (hs.grund) {
        cJSON_AddStringToObject(jh, "reason", hs.grund);
    }
    cJSON_AddNumberToObject(jh, "controllers", hs.steuerungen);
    cJSON_AddNumberToObject(jh, "accessories", hs.zubehoer);
    return send_json_obj(req, root);
}

/*
 * POST /api/homekit/reset mit {"bestaetigung":"KOPPLUNGEN LOESCHEN"}: loescht
 * alle Kopplungen mit Home. Der Code bleibt; danach laesst sich der Leitstand
 * neu hinzufuegen.
 */
static esp_err_t homekit_reset_post(httpd_req_t *req)
{
    char body[96] = {0};
    int n = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    if (n <= 0 || httpd_req_recv(req, body, n) != n || strstr(body, "\"KOPPLUNGEN LOESCHEN\"") == NULL) {
        return send_error(req, "400 Bad Request", "Nur mit {\"bestaetigung\":\"KOPPLUNGEN LOESCHEN\"}");
    }
    if (st_hap_kopplungen_loeschen() != ESP_OK) {
        return send_error(req, "409 Conflict", "HomeKit laeuft nicht");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return send_ok(req);
}

/* ------------------------------------------------------------------ */
/* Protokoll                                                           */
/* ------------------------------------------------------------------ */

/*
 * Tage mit ihren Dateien. Ohne Angabe alle Tage, nur mit Namen; mit
 * ?tag=JJJJ-MM-TT die Dateien dieses Tags samt Groesse.
 */
static esp_err_t log_days_get_inner(httpd_req_t *req);

/* Wie die anderen grossen Antworten unter der Speichersperre: sonst faellt
 * das Lesen der Karte mit einer Abfrage samt Schreiben zusammen. */
static esp_err_t log_days_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = log_days_get_inner(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t log_days_get_inner(httpd_req_t *req)
{
    char q[48] = "", tag[16] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "tag", tag, sizeof(tag));
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    char buf[160];
    if (tag[0]) {
        char pfad[96];
        char probe[40];
        snprintf(probe, sizeof(probe), "%s/x", tag);
        if (!st_log_pfad(probe, pfad, sizeof(pfad))) {
            return send_error(req, "400 Bad Request", "Tag als JJJJ-MM-TT angeben");
        }
        pfad[strlen(pfad) - 2] = '\0'; /* "/x" weg */
        DIR *d = opendir(pfad);
        if (d == NULL) {
            return send_error(req, "404 Not Found", "Diesen Tag gibt es nicht");
        }
        snprintf(buf, sizeof(buf), "{\"tag\":\"%s\",\"dateien\":[", tag);
        httpd_resp_sendstr_chunk(req, buf);
        struct dirent *e;
        bool erste = true;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') {
                continue;
            }
            char datei[160];
            struct stat st;
            snprintf(datei, sizeof(datei), "%s/%.60s", pfad, e->d_name);
            long groesse = stat(datei, &st) == 0 ? (long)st.st_size : -1;
            snprintf(buf, sizeof(buf), "%s{\"name\":\"%.64s\",\"byte\":%ld}", erste ? "" : ",", e->d_name, groesse);
            erste = false;
            httpd_resp_sendstr_chunk(req, buf);
        }
        closedir(d);
        httpd_resp_sendstr_chunk(req, "]}");
        return httpd_resp_sendstr_chunk(req, NULL);
    }

    /* Alle Tage, in der Reihenfolge des Dateisystems; sortiert wird beim
     * Empfaenger. Eine Liste im Arbeitsspeicher kostete je Jahr 4 KB. */
    DIR *dj = opendir(ST_LOG_WURZEL);
    if (dj == NULL) {
        return httpd_resp_sendstr(req, "{\"tage\":[]}");
    }
    httpd_resp_sendstr_chunk(req, "{\"tage\":[");
    bool erste = true;
    struct dirent *j;
    while ((j = readdir(dj)) != NULL) {
        if (j->d_name[0] < '0' || j->d_name[0] > '9') {
            continue;
        }
        char dir[48];
        snprintf(dir, sizeof(dir), "%s/%.4s", ST_LOG_WURZEL, j->d_name);
        DIR *dt = opendir(dir);
        if (dt == NULL) {
            continue;
        }
        struct dirent *t;
        while ((t = readdir(dt)) != NULL) {
            if (strlen(t->d_name) == 10 && t->d_name[4] == '-') {
                snprintf(buf, sizeof(buf), "%s\"%s\"", erste ? "" : ",", t->d_name);
                erste = false;
                httpd_resp_sendstr_chunk(req, buf);
            }
        }
        closedir(dt);
    }
    closedir(dj);
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

/*
 * Letzter Absturz aus dem Absturzspeicher: Task, Programmzaehler, Ursache und
 * Ruecksprungadressen. Aufgeloest wird am Rechner mit dem ELF derselben
 * Fassung (elf_sha), etwa: xtensa-esp32-elf-addr2line -pfiaC -e station.elf
 * <adressen>. POST /api/coredump/erase loescht ihn.
 */
static esp_err_t coredump_get(httpd_req_t *req)
{
    if (esp_core_dump_image_check() != ESP_OK) {
        return send_json_obj(req, cJSON_CreateObject());
    }
    esp_core_dump_summary_t *sum = malloc(sizeof(*sum));
    if (sum == NULL) {
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }
    cJSON *root = cJSON_CreateObject();
    if (esp_core_dump_get_summary(sum) == ESP_OK) {
        char hex[16];
        cJSON_AddStringToObject(root, "task", sum->exc_task);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->exc_pc);
        cJSON_AddStringToObject(root, "pc", hex);
        cJSON_AddNumberToObject(root, "cause", sum->ex_info.exc_cause);
        snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->ex_info.exc_vaddr);
        cJSON_AddStringToObject(root, "vaddr", hex);
        cJSON *bt = cJSON_AddArrayToObject(root, "backtrace");
        for (uint32_t i = 0; i < sum->exc_bt_info.depth && i < 16; i++) {
            snprintf(hex, sizeof(hex), "0x%08lx", (unsigned long)sum->exc_bt_info.bt[i]);
            cJSON_AddItemToArray(bt, cJSON_CreateString(hex));
        }
        cJSON_AddBoolToObject(root, "backtrace_corrupted", sum->exc_bt_info.corrupted);
        cJSON_AddStringToObject(root, "elf_sha", (const char *)sum->app_elf_sha256);
    }
    free(sum);
    return send_json_obj(req, root);
}

static esp_err_t coredump_erase_post(httpd_req_t *req)
{
    return esp_core_dump_image_erase() == ESP_OK ? send_ok(req)
                                                 : send_error(req, "500 Internal Server Error", "Nicht geloescht");
}

/* ------------------------------------------------------------------ */
/* Auswertung des Protokolls                                           */
/* ------------------------------------------------------------------ */

#define REIHE_SCHLUESSEL_MAX 8
#define REIHE_TAGE_5MIN 31
#define REIHE_TAGE_ROH 2
#define EREIGNISSE_MAX 2000

static uint32_t query_zahl(const char *q, const char *k, uint32_t vorgabe)
{
    char v[16];
    return httpd_query_key_value(q, k, v, sizeof(v)) == ESP_OK ? (uint32_t)strtoul(v, NULL, 10) : vorgabe;
}

/* Kennung eines Geraets: Buchstaben, Ziffern, Unterstrich */
static bool kennung_gueltig(const char *s)
{
    if (!s[0] || strlen(s) > 23) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
            return false;
        }
    }
    return true;
}

typedef struct {
    httpd_req_t *req;
    int n;
    uint32_t raster, platz;
    float summe[REIHE_SCHLUESSEL_MAX];
    uint16_t anzahl[REIHE_SCHLUESSEL_MAX];
    bool zeilen;       /* schon eine Zeile gesendet */
    uint32_t gesendet;
} reihe_t;

static void reihe_ausgeben(reihe_t *r)
{
    bool etwas = false;
    for (int i = 0; i < r->n; i++) {
        etwas = etwas || r->anzahl[i] > 0;
    }
    if (!etwas) {
        return;
    }
    char buf[256], t[24], z[24];
    pk_iso(r->platz * r->raster, t, sizeof(t));
    int pos = snprintf(buf, sizeof(buf), "%s[\"%s\"", r->zeilen ? "," : "", t);
    for (int i = 0; i < r->n; i++) {
        if (r->anzahl[i]) {
            pk_zahl(r->summe[i] / r->anzahl[i], z, sizeof(z));
        } else {
            snprintf(z, sizeof(z), "null");
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, ",%s", z);
        r->summe[i] = 0;
        r->anzahl[i] = 0;
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]");
    httpd_resp_sendstr_chunk(r->req, buf);
    r->zeilen = true;
    r->gesendet++;
}

/*
 * GET /api/log/series?geraet=heiz_2370ec&von=<epoch>&bis=<epoch>
 *     &schluessel=fuehler.puffer,brenner&raster=900
 *
 * Mittel der gewaehlten Messgroessen je Raster. Ab 300 s aus den
 * Fuenfminutenmitteln, hoechstens 31 Tage; darunter aus den Rohwerten,
 * hoechstens zwei Tage. Werte in der Reihenfolge der Schluessel, null ohne.
 */
static esp_err_t log_series_inner(httpd_req_t *req)
{
    char q[320], geraet[24] = "", liste[200] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "geraet", geraet, sizeof(geraet)) != ESP_OK ||
        httpd_query_key_value(q, "schluessel", liste, sizeof(liste)) != ESP_OK || !kennung_gueltig(geraet)) {
        return send_error(req, "400 Bad Request", "geraet, schluessel, von und bis angeben");
    }
    uint32_t von = query_zahl(q, "von", 0), bis = query_zahl(q, "bis", 0);
    uint32_t raster = query_zahl(q, "raster", 300);
    bool mittel = raster >= 300 && raster % 300 == 0;
    uint32_t tage_max = mittel ? REIHE_TAGE_5MIN : REIHE_TAGE_ROH;
    if (von == 0 || bis <= von || raster < 30 || raster > 86400 || (bis - von) / 86400 >= tage_max) {
        return send_error(req, "400 Bad Request",
                          "Zeitraum hoechstens 31 Tage ab 300 s Raster, 2 Tage darunter; Raster 30 bis 86400 s");
    }
    const char *schluessel[REIHE_SCHLUESSEL_MAX];
    int n = 0;
    for (char *p = strtok(liste, ","); p && n < REIHE_SCHLUESSEL_MAX; p = strtok(NULL, ",")) {
        schluessel[n++] = p;
    }
    if (n == 0) {
        return send_error(req, "400 Bad Request", "mindestens ein Schluessel");
    }

    char *zeile = malloc(1536);
    if (zeile == NULL) {
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    snprintf(zeile, 1536, "{\"geraet\":\"%s\",\"raster_s\":%lu,\"spalten\":[", geraet, (unsigned long)raster);
    httpd_resp_sendstr_chunk(req, zeile);
    for (int i = 0; i < n; i++) {
        snprintf(zeile, 1536, "%s\"%.39s\"", i ? "," : "", schluessel[i]);
        httpd_resp_sendstr_chunk(req, zeile);
    }
    httpd_resp_sendstr_chunk(req, "],\"zeilen\":[");

    reihe_t r = {.req = req, .n = n, .raster = raster};
    int16_t spalte[REIHE_SCHLUESSEL_MAX];
    float werte[REIHE_SCHLUESSEL_MAX];
    char name[48], pfad[128];
    for (uint32_t tag = von / 86400; tag <= bis / 86400; tag++) {
        for (uint8_t teil = 1; teil < 50; teil++) {
            pk_dateiname(geraet, teil, mittel, name, sizeof(name));
            if (!st_log_datei(tag * 86400, name, pfad, sizeof(pfad))) {
                break;
            }
            FILE *f = fopen(pfad, "r");
            if (f == NULL) {
                break;
            }
            bool kopf = fgets(zeile, 1536, f) != NULL && pk_spalten_waehlen(zeile, schluessel, n, spalte);
            uint32_t zeit;
            while (kopf && fgets(zeile, 1536, f) != NULL) {
                if (!pk_zeile_lesen(zeile, spalte, n, &zeit, werte) || zeit < von || zeit >= bis) {
                    continue;
                }
                if (zeit / raster != r.platz) {
                    if (r.platz) {
                        reihe_ausgeben(&r);
                    }
                    r.platz = zeit / raster;
                }
                for (int i = 0; i < n; i++) {
                    if (!isnan(werte[i])) {
                        r.summe[i] += werte[i];
                        r.anzahl[i]++;
                    }
                }
            }
            fclose(f);
        }
    }
    if (r.platz) {
        reihe_ausgeben(&r);
    }
    free(zeile);
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t log_series_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = log_series_inner(req);
    st_speicher_freigeben();
    return rc;
}

/*
 * GET /api/log/events?von=<epoch>&bis=<epoch>[&geraet=...][&art=...]
 * Ereignisse als JSON-Liste, hoechstens 31 Tage und 2000 Eintraege.
 */
static esp_err_t log_events_inner(httpd_req_t *req)
{
    char q[160], geraet[24] = "", art[24] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return send_error(req, "400 Bad Request", "von und bis angeben");
    }
    httpd_query_key_value(q, "geraet", geraet, sizeof(geraet));
    httpd_query_key_value(q, "art", art, sizeof(art));
    uint32_t von = query_zahl(q, "von", 0), bis = query_zahl(q, "bis", 0);
    if (von == 0 || bis <= von || (bis - von) / 86400 >= REIHE_TAGE_5MIN ||
        (geraet[0] && !kennung_gueltig(geraet)) || (art[0] && !kennung_gueltig(art))) {
        return send_error(req, "400 Bad Request", "von und bis angeben, hoechstens 31 Tage");
    }
    char mg[48] = "", ma[48] = "";
    if (geraet[0]) {
        snprintf(mg, sizeof(mg), "\"geraet\":\"%s\"", geraet);
    }
    if (art[0]) {
        snprintf(ma, sizeof(ma), "\"art\":\"%s\"", art);
    }
    char *zeile = malloc(512);
    if (zeile == NULL) {
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_sendstr_chunk(req, "[");
    int anzahl = 0;
    char pfad[128];
    for (uint32_t tag = von / 86400; tag <= bis / 86400 && anzahl < EREIGNISSE_MAX; tag++) {
        if (!st_log_datei(tag * 86400, "ereignisse.jsonl", pfad, sizeof(pfad))) {
            continue;
        }
        FILE *f = fopen(pfad, "r");
        if (f == NULL) {
            continue;
        }
        uint32_t zeit;
        while (anzahl < EREIGNISSE_MAX && fgets(zeile, 512, f) != NULL) {
            size_t l = strlen(zeile);
            /* Eine unvollstaendige letzte Zeile nach einem Stromausfall faellt weg */
            if (l < 2 || zeile[l - 1] != '\n' || zeile[l - 2] != '}') {
                continue;
            }
            zeile[l - 1] = '\0';
            if (!pk_ereignis_zeit(zeile, &zeit) || zeit < von || zeit >= bis || (mg[0] && !strstr(zeile, mg)) ||
                (ma[0] && !strstr(zeile, ma))) {
                continue;
            }
            if (anzahl++) {
                httpd_resp_sendstr_chunk(req, ",");
            }
            httpd_resp_sendstr_chunk(req, zeile);
        }
        fclose(f);
    }
    free(zeile);
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t log_events_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = log_events_inner(req);
    st_speicher_freigeben();
    return rc;
}

/*
 * POST /api/log/format mit {"bestaetigung":"KARTE LOESCHEN"}: formatiert die
 * ganze Karte. Ohne die Bestaetigung geschieht nichts -- ein versehentlicher
 * Aufruf soll kein Protokoll kosten.
 */
static esp_err_t log_format_post(httpd_req_t *req)
{
    char body[96] = {0};
    int n = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    if (n <= 0 || httpd_req_recv(req, body, n) != n || strstr(body, "\"KARTE LOESCHEN\"") == NULL) {
        return send_error(req, "400 Bad Request", "Nur mit {\"bestaetigung\":\"KARTE LOESCHEN\"}");
    }
    st_log_status_t ls;
    st_log_status(&ls);
    if (!ls.karte) {
        return send_error(req, "409 Conflict", "Keine Karte eingehaengt");
    }
    st_log_formatieren();
    httpd_resp_set_status(req, "202 Accepted");
    return send_ok(req);
}

/*
 * Dauerlastpruefung: POST /api/log/lasttest {"mb":1024} startet, GET liefert
 * den Stand.
 */
static esp_err_t lasttest_post(httpd_req_t *req)
{
    char body[48] = {0};
    int n = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
    if (n > 0) {
        httpd_req_recv(req, body, n);
    }
    const char *p = strstr(body, "\"mb\"");
    uint32_t mb = p ? (uint32_t)strtoul(strchr(p, ':') ? strchr(p, ':') + 1 : "0", NULL, 10) : 1024;
    if (!st_log_lasttest_starten(mb)) {
        return send_error(req, "409 Conflict", "Keine Karte, schon eine Pruefung im Gang, oder mb nicht 1 bis 4096");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return send_ok(req);
}

static esp_err_t lasttest_get(httpd_req_t *req)
{
    st_log_lasttest_t l;
    st_log_lasttest_stand(&l);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "running", l.laeuft);
    cJSON_AddStringToObject(root, "phase", l.phase);
    cJSON_AddNumberToObject(root, "target_mb", l.ziel_mb);
    cJSON_AddNumberToObject(root, "written_mb", l.geschrieben_mb);
    cJSON_AddNumberToObject(root, "read_mb", l.gelesen_mb);
    cJSON_AddNumberToObject(root, "write_errors", l.schreibfehler);
    cJSON_AddNumberToObject(root, "mismatched_blocks", l.abweichungen);
    cJSON_AddNumberToObject(root, "write_s", l.schreiben_s);
    cJSON_AddNumberToObject(root, "read_s", l.lesen_s);
    cJSON_AddNumberToObject(root, "heap_min", l.heap_min);
    return send_json_obj(req, root);
}

/*
 * Eine Datei des Protokolls: /log/JJJJ-MM-TT/name. Mit "Range: bytes=N-" ab
 * Stelle N -- alle Dateien wachsen nur, so holt eine App bloss das Neue.
 */
static esp_err_t log_file_get_inner(httpd_req_t *req);

static esp_err_t log_file_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = log_file_get_inner(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t log_file_get_inner(httpd_req_t *req)
{
    char pfad[128];
    const char *rest = req->uri + strlen("/log/");
    char ohne_frage[64];
    snprintf(ohne_frage, sizeof(ohne_frage), "%s", rest);
    char *frage = strchr(ohne_frage, '?');
    if (frage) {
        *frage = '\0';
    }
    if (!st_log_pfad(ohne_frage, pfad, sizeof(pfad))) {
        return send_error(req, "400 Bad Request", "Pfad als JJJJ-MM-TT/datei angeben");
    }
    FILE *f = fopen(pfad, "rb");
    if (f == NULL) {
        return send_error(req, "404 Not Found", "Diese Datei gibt es nicht");
    }
    fseek(f, 0, SEEK_END);
    long groesse = ftell(f);
    long ab = 0;
    char range[48];
    if (httpd_req_get_hdr_value_str(req, "Range", range, sizeof(range)) == ESP_OK &&
        sscanf(range, "bytes=%ld-", &ab) == 1 && ab >= 0) {
        if (ab > groesse) {
            fclose(f);
            httpd_resp_set_status(req, "416 Range Not Satisfiable");
            return httpd_resp_send(req, NULL, 0);
        }
        char cr[64];
        snprintf(cr, sizeof(cr), "bytes %ld-%ld/%ld", ab, groesse > 0 ? groesse - 1 : 0, groesse);
        httpd_resp_set_status(req, "206 Partial Content");
        httpd_resp_set_hdr(req, "Content-Range", cr);
    }
    fseek(f, ab, SEEK_SET);
    const char *typ = strstr(pfad, ".csv") ? "text/csv; charset=utf-8"
                      : strstr(pfad, ".jsonl") ? "application/x-ndjson" : "application/json";
    httpd_resp_set_type(req, typ);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    char *buf = malloc(1024);
    if (buf == NULL) {
        fclose(f);
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }
    esp_err_t rc = ESP_OK;
    size_t n;
    while (rc == ESP_OK && (n = fread(buf, 1, 1024, f)) > 0) {
        rc = httpd_resp_send_chunk(req, buf, n);
    }
    free(buf);
    fclose(f);
    return rc == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : rc;
}

static void dev_json(cJSON *o, const st_dev_t *d)
{
    uint32_t jetzt = (uint32_t)(esp_timer_get_time() / 1000);
    cJSON_AddStringToObject(o, "id", d->id);
    cJSON_AddStringToObject(o, "site", d->site);
    cJSON_AddStringToObject(o, "host", d->host);
    cJSON_AddStringToObject(o, "version", d->version);
    cJSON_AddBoolToObject(o, "reachable", d->reachable);
    if (d->seen) {
        cJSON_AddNumberToObject(o, "age_s", (jetzt - d->ok_ms) / 1000);
    }
    cJSON_AddNumberToObject(o, "uptime_s", d->uptime_s);
    cJSON_AddNumberToObject(o, "heap", d->heap);
    cJSON_AddNumberToObject(o, "rssi", d->rssi);
}

/* Was der Leitstand von der Anlage weiss, zur Pruefung und fuer die App */
static esp_err_t plant_get_innen(httpd_req_t *req);

static esp_err_t plant_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = plant_get_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t plant_get_innen(httpd_req_t *req)
{
    st_plant_t *const p = &s_anlage;
    st_poll_snapshot(p);
    cJSON *root = cJSON_CreateObject();
    cJSON *jh = cJSON_AddArrayToObject(root, "heat");
    for (uint8_t i = 0; i < p->heat_count; i++) {
        const st_heat_t *h = &p->heat[i];
        cJSON *o = cJSON_CreateObject();
        dev_json(o, &h->dev);
        cJSON *pr = cJSON_AddObjectToObject(o, "probes");
        for (uint8_t k = 0; k < h->probe_count; k++) {
            cJSON_AddNumberToObject(pr, h->probes[k].role, h->probes[k].temp_c);
        }
        cJSON *b = cJSON_AddObjectToObject(o, "burner");
        cJSON_AddBoolToObject(b, "known", h->burner_known);
        cJSON_AddBoolToObject(b, "own", h->burner_own);
        cJSON_AddBoolToObject(b, "running", h->burner_running);
        cJSON_AddNumberToObject(b, "runtime_today_s", h->runtime_today_s);
        cJSON_AddNumberToObject(b, "starts_today", h->starts_today);
        if (h->charge_valid) {
            cJSON *c = cJSON_AddObjectToObject(o, "charge");
            cJSON_AddBoolToObject(c, "own", h->charge_own);
            cJSON_AddNumberToObject(c, "level", h->charge_level);
            cJSON_AddStringToObject(c, "phase", h->charge_phase);
        }
        cJSON *kr = cJSON_AddArrayToObject(o, "circuits");
        for (uint8_t k = 0; k < h->circuit_count; k++) {
            const st_circuit_t *c = &h->circuits[k];
            cJSON *jc = cJSON_CreateObject();
            cJSON_AddNumberToObject(jc, "id", c->id);
            cJSON_AddStringToObject(jc, "name", c->name);
            cJSON_AddBoolToObject(jc, "on", c->on);
            if (c->vl_valid) {
                cJSON_AddNumberToObject(jc, "vl_c", c->vl_c);
            }
            if (c->rl_valid) {
                cJSON_AddNumberToObject(jc, "rl_c", c->rl_c);
            }
            cJSON_AddItemToArray(kr, jc);
        }
        cJSON_AddNumberToObject(o, "findings", h->findings);
        cJSON_AddItemToArray(jh, o);
    }
    cJSON *jm = cJSON_AddArrayToObject(root, "manifolds");
    for (uint8_t i = 0; i < p->manifold_count; i++) {
        const st_manifold_t *m = &p->manifolds[i];
        cJSON *o = cJSON_CreateObject();
        dev_json(o, &m->dev);
        cJSON *rs = cJSON_AddArrayToObject(o, "rooms");
        for (uint8_t k = 0; k < m->room_count; k++) {
            const st_room_t *r = &m->rooms[k];
            cJSON *jr = cJSON_CreateObject();
            cJSON_AddNumberToObject(jr, "id", r->id);
            cJSON_AddStringToObject(jr, "name", r->name);
            if (r->temp_valid) {
                cJSON_AddNumberToObject(jr, "temp_c", r->temp_c);
            }
            if (r->hum_valid) {
                cJSON_AddNumberToObject(jr, "humidity", r->humidity);
            }
            cJSON_AddNumberToObject(jr, "target_c", r->target_c);
            cJSON_AddBoolToObject(jr, "heat", r->heat);
            cJSON_AddNumberToObject(jr, "position", r->position);
            cJSON_AddItemToArray(rs, jr);
        }
        cJSON_AddItemToArray(jm, o);
    }
    outdoor_json(root, &p->outdoor);
    return send_json_obj(req, root);
}

/* ------------------------------------------------------------------ */
/* Einstellungen                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t config_get_innen(httpd_req_t *req);

static esp_err_t config_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = config_get_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t config_get_innen(httpd_req_t *req)
{
    st_config_t cfg;
    st_cfg_copy(&cfg);
    char *json = st_cfg_to_json(&cfg);
    memset(&cfg, 0, sizeof(cfg));
    if (json == NULL) {
        return send_error(req, "500 Internal Server Error", "Kein Speicher");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t rc = httpd_resp_sendstr(req, json);
    free(json);
    return rc;
}

/* WLAN erst umschalten, wenn die Antwort draussen ist; sonst wartet der
 * Browser vergeblich und die Speicherung sieht wie ein Fehlschlag aus. */
static void wifi_apply_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(600));
    st_config_t cfg;
    netmgr_cfg_t netz;
    st_cfg_copy(&cfg);
    st_cfg_netmgr(&cfg, &netz);
    ESP_LOGI(TAG, "WLAN-Zugangsdaten geaendert, Verbindung wird neu aufgebaut");
    netmgr_apply(&netz);
    memset(&cfg, 0, sizeof(cfg));
    memset(&netz, 0, sizeof(netz));
    vTaskDelete(NULL);
}

static esp_err_t config_put(httpd_req_t *req)
{
    char *body = read_body(req);
    if (body == NULL) {
        return send_error(req, "400 Bad Request", "Die Anfrage ließ sich nicht lesen");
    }
    static st_config_t neu;
    st_config_t alt;
    st_cfg_copy(&neu);
    alt = neu;
    char err[128] = {0};
    esp_err_t rc = st_cfg_from_json(body, &neu, err, sizeof(err));
    memset(body, 0, strlen(body));
    free(body);
    if (rc != ESP_OK) {
        return send_error(req, "400 Bad Request", err[0] ? err : "Ungültige Einstellungen");
    }
    if (st_cfg_set(&neu) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Die Einstellungen ließen sich nicht speichern");
    }
    bool wlan = strcmp(alt.wifi.ssid, neu.wifi.ssid) != 0 || strcmp(alt.wifi.pass, neu.wifi.pass) != 0 ||
                strcmp(alt.wifi.hostname, neu.wifi.hostname) != 0 ||
                strcmp(alt.wifi.ap_pass, neu.wifi.ap_pass) != 0;
    if (strcmp(alt.site, neu.site) != 0 || strcmp(alt.wifi.hostname, neu.wifi.hostname) != 0) {
        peers_update_identity(neu.wifi.hostname, neu.site);
    }
    st_poll_set_outdoor(neu.outdoor_mac);
    st_ui_config_changed();
    ESP_LOGI(TAG, "Einstellungen gespeichert");
    memset(&alt, 0, sizeof(alt));
    memset(&neu, 0, sizeof(neu));

    esp_err_t sent = send_ok(req);
    if (wlan) {
        xTaskCreate(wifi_apply_task, "wifi_apply", 4096, NULL, 4, NULL);
    }
    return sent;
}

/* ------------------------------------------------------------------ */
/* Funkthermometer                                                     */
/* ------------------------------------------------------------------ */

static esp_err_t ble_get_innen(httpd_req_t *req);

static esp_err_t ble_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = ble_get_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t ble_get_innen(httpd_req_t *req)
{
    atc_device_t *const devices = s_geraete;
    size_t n = atc_ble_devices(devices, ATC_MAX_DEVICES);
    st_config_t cfg;
    st_cfg_copy(&cfg);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "running", atc_ble_running());
    cJSON_AddStringToObject(root, "outdoor", cfg.outdoor_mac);
    cJSON *arr = cJSON_AddArrayToObject(root, "devices");
    for (size_t i = 0; i < n; i++) {
        const atc_device_t *d = &devices[i];
        char mac[18];
        mac_text(d->mac, mac, sizeof(mac));
        cJSON *jd = cJSON_CreateObject();
        cJSON_AddStringToObject(jd, "mac", mac);
        cJSON_AddStringToObject(jd, "name", d->name);
        cJSON_AddNumberToObject(jd, "rssi", d->rssi);
        if (d->has_temp) {
            cJSON_AddNumberToObject(jd, "temp_c", d->temp_c);
        }
        if (d->has_humidity) {
            cJSON_AddNumberToObject(jd, "humidity", d->humidity);
        }
        cJSON_AddNumberToObject(jd, "battery", d->battery);
        cJSON_AddNumberToObject(jd, "battery_mv", d->battery_mv);
        cJSON_AddNumberToObject(jd, "packets", d->packets);
        cJSON_AddStringToObject(jd, "format", atc_format_name(d->format));
        if (d->pressure_hpa > 0.0f) {
            cJSON_AddNumberToObject(jd, "pressure_hpa", d->pressure_hpa);
        }
        if (d->format == ATC_FMT_BTHOME) {
            cJSON_AddBoolToObject(jd, "encrypted", d->encrypted);
            const char *k = atc_key_state_name(d->key);
            if (k != NULL) {
                cJSON_AddStringToObject(jd, "key", k);
            }
        }
        cJSON_AddItemToArray(arr, jd);
    }

    /* Adressen mit hinterlegtem Schluessel; die Schluessel selbst nie. */
    atc_key_t *const keys = s_schluessel;
    size_t nk = atc_ble_keys(keys, ATC_MAX_KEYS);
    cJSON *jk = cJSON_AddArrayToObject(root, "keys");
    for (size_t i = 0; i < nk; i++) {
        char mac[18];
        mac_text(keys[i].mac, mac, sizeof(mac));
        cJSON_AddItemToArray(jk, cJSON_CreateString(mac));
    }
    memset(s_schluessel, 0, sizeof(s_schluessel));
    return send_json_obj(req, root);
}

/* {"mac": "...", "bindkey": "<32 Hexadezimalziffern>"}; leer entfernt ihn. */
static esp_err_t ble_key_post(httpd_req_t *req)
{
    char *body = read_body(req);
    if (body == NULL) {
        return send_error(req, "400 Bad Request", "Die Anfrage ließ sich nicht lesen");
    }
    cJSON *doc = cJSON_Parse(body);
    memset(body, 0, strlen(body));
    free(body);
    if (doc == NULL) {
        return send_error(req, "400 Bad Request", "Die Anfrage ist kein JSON");
    }
    const cJSON *jm = cJSON_GetObjectItemCaseSensitive(doc, "mac");
    const cJSON *jk = cJSON_GetObjectItemCaseSensitive(doc, "bindkey");
    uint8_t mac[6], key[16];
    bool entfernen = jk == NULL || cJSON_IsNull(jk) ||
                     (cJSON_IsString(jk) && jk->valuestring && jk->valuestring[0] == '\0');
    const char *fehler = NULL;
    if (!cJSON_IsString(jm) || !atc_parse_mac(jm->valuestring, mac)) {
        fehler = "Die MAC-Adresse ist ungültig";
    } else if (!entfernen && (!cJSON_IsString(jk) || !atc_parse_key(jk->valuestring, key))) {
        fehler = "Der Schlüssel muss aus 32 Hexadezimalziffern (0–9, a–f) bestehen";
    }
    if (cJSON_IsString(jk) && jk->valuestring) {
        memset(jk->valuestring, 0, strlen(jk->valuestring));
    }
    cJSON_Delete(doc);
    if (fehler != NULL) {
        memset(key, 0, sizeof(key));
        return send_error(req, "400 Bad Request", fehler);
    }
    esp_err_t rc = atc_ble_key_set(mac, entfernen ? NULL : key);
    memset(key, 0, sizeof(key));
    if (rc == ESP_ERR_NO_MEM) {
        return send_error(req, "400 Bad Request", "Kein Platz für weitere Schlüssel; höchstens 16 lassen sich hinterlegen");
    }
    if (rc != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Der Schlüssel ließ sich nicht speichern");
    }
    ESP_LOGI(TAG, "Schluessel fuer %02X:%02X:%02X:%02X:%02X:%02X %s", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5], entfernen ? "entfernt" : "hinterlegt");
    return send_ok(req);
}

/* ------------------------------------------------------------------ */
/* Aussentemperatur fuer die Heizungsgeraete                           */
/* ------------------------------------------------------------------ */

/*
 * Dieselbe Form wie /api/demand am Verteiler, aber nur mit dem, was der
 * Leitstand beitragen kann: der Aussentemperatur. Einen Waermebedarf meldet
 * er nicht; er regelt keine Raeume. Die Heizungsgeraete lesen die Antwort in
 * einen Puffer von 512 Byte (app_remote.c) -- sie bleibt deshalb knapp.
 */
static esp_err_t demand_get_innen(httpd_req_t *req);

static esp_err_t demand_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = demand_get_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t demand_get_innen(httpd_req_t *req)
{
    st_plant_t *const p = &s_anlage;
    st_poll_snapshot(p);
    st_config_t cfg;
    st_cfg_copy(&cfg);
    char id[24];
    st_device_id(id, sizeof(id));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "site", cfg.site);
    cJSON_AddStringToObject(root, "role", PEERS_ROLE_STATION);
    cJSON_AddBoolToObject(root, "demand", false);
    if (p->outdoor.valid && p->outdoor.age_s <= ST_OUTDOOR_STALE_S) {
        cJSON_AddNumberToObject(root, "outdoor_c", p->outdoor.temp_c);
        cJSON_AddNumberToObject(root, "outdoor_age_s", p->outdoor.age_s);
        if (p->outdoor.hum_valid) {
            cJSON_AddNumberToObject(root, "outdoor_humidity", p->outdoor.humidity);
        }
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return send_json_obj(req, root);
}

/* ------------------------------------------------------------------ */
/* Netz und Geraete                                                    */
/* ------------------------------------------------------------------ */

static esp_err_t peers_get_handler_innen(httpd_req_t *req);

static esp_err_t peers_get_handler(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = peers_get_handler_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t peers_get_handler_innen(httpd_req_t *req)
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

static esp_err_t wifi_scan_post(httpd_req_t *req)
{
    if (netmgr_scan_start() != ESP_OK) {
        return send_error(req, "503 Service Unavailable", "Der Suchlauf ließ sich nicht starten");
    }
    return send_ok(req);
}

static esp_err_t wifi_scan_get_innen(httpd_req_t *req);

static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = wifi_scan_get_innen(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t wifi_scan_get_innen(httpd_req_t *req)
{
    static netmgr_ap_t aps[24];
    size_t n = netmgr_scan_result(aps, sizeof(aps) / sizeof(aps[0]));
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "running", netmgr_scan_running());
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
/* Bildschirmaufnahme                                                  */
/* ------------------------------------------------------------------ */

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (v >> (8 * i)) & 0xff;
    }
}

/* Der Bildschirm als BMP, Zeile fuer Zeile aus der Anzeige gelesen. */
static esp_err_t screen_get_inner(httpd_req_t *req);

/* Das Bild geht mit rund 230 KB ueber TCP; die Sendepuffer des Netzstapels
 * kommen vom Heap. Unter der Speichersperre faellt das nicht mit dem Auswerten
 * eines Geraetezustands zusammen -- in der Dauerlastpruefung fiel der freie
 * Speicher ohne sie auf 28 Byte. */
static esp_err_t screen_get(httpd_req_t *req)
{
    st_speicher_sperren();
    esp_err_t rc = screen_get_inner(req);
    st_speicher_freigeben();
    return rc;
}

static esp_err_t screen_get_inner(httpd_req_t *req)
{
    int w = st_ui_width(), h = st_ui_height();
    if (w <= 0 || w > 320 || h <= 0 || h > 320) {
        return send_error(req, "503 Service Unavailable", "Keine Anzeige");
    }
    uint32_t zeile = (uint32_t)w * 3;
    uint8_t kopf[54] = {'B', 'M'};
    le32(&kopf[2], 54 + zeile * h);
    le32(&kopf[10], 54);
    le32(&kopf[14], 40);
    le32(&kopf[18], (uint32_t)w);
    le32(&kopf[22], (uint32_t)(-h)); /* von oben nach unten */
    le16(&kopf[26], 1);
    le16(&kopf[28], 24);
    le32(&kopf[34], zeile * h);
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(req, (const char *)kopf, sizeof(kopf)) != ESP_OK) {
        return ESP_FAIL;
    }
    static uint8_t rgb[320 * 3];
    for (int y = 0; y < h; y++) {
        if (!st_ui_read_row(y, rgb)) {
            memset(rgb, 0, zeile);
        }
        /* BMP erwartet Blau, Gruen, Rot */
        for (int x = 0; x < w; x++) {
            uint8_t r = rgb[x * 3];
            rgb[x * 3] = rgb[x * 3 + 2];
            rgb[x * 3 + 2] = r;
        }
        if (httpd_resp_send_chunk(req, (const char *)rgb, zeile) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* Seite der Anzeige waehlen oder sie schalten: {"page": "leitstand", "on": true} */
static esp_err_t display_post(httpd_req_t *req)
{
    char *body = read_body(req);
    cJSON *doc = body ? cJSON_Parse(body) : NULL;
    free(body);
    if (doc == NULL) {
        return send_error(req, "400 Bad Request", "Die Anfrage ist kein JSON");
    }
    const cJSON *jp = cJSON_GetObjectItemCaseSensitive(doc, "page");
    const cJSON *jo = cJSON_GetObjectItemCaseSensitive(doc, "on");
    bool ok = true;
    if (cJSON_IsString(jp)) {
        ok = st_ui_set_page(jp->valuestring);
    }
    if (cJSON_IsBool(jo)) {
        st_ui_set_on(cJSON_IsTrue(jo));
    }
    cJSON_Delete(doc);
    return ok ? send_ok(req) : send_error(req, "400 Bad Request", "Unbekannte Seite");
}

/* ------------------------------------------------------------------ */
/* System                                                              */
/* ------------------------------------------------------------------ */

/*
 * esp_restart() ruft die Abschaltroutinen von WLAN, Bluetooth und Dateisystem
 * im Stapel dieser Aufgabe auf. Mit 2 KB lief er am Leitstand ueber: Nach
 * einem Update meldete der Absturzspeicher "Stack overflow" in "restart",
 * der Neustart geschah als Absturz.
 */
#define RESTART_STACK 4096

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
        xTaskCreate(restart_task, "restart", RESTART_STACK, NULL, 5, NULL);
        return send_ok(req);
    }
    if (strcmp(action, "factory") == 0) {
        if (st_cfg_reset() != ESP_OK) {
            return send_error(req, "500 Internal Server Error", "Das Zurücksetzen ist fehlgeschlagen");
        }
        if (atc_ble_keys_replace(NULL, 0) != ESP_OK) {
            ESP_LOGE(TAG, "Thermometerschluessel liessen sich nicht loeschen");
        }
        xTaskCreate(restart_task, "restart", RESTART_STACK, NULL, 5, NULL);
        return send_ok(req);
    }
    return send_error(req, "404 Not Found", "Unbekannte Aktion");
}

static esp_err_t ota_post_inner(httpd_req_t *req);

/*
 * Waehrend der Uebertragung ruht die Abfrage der Anlage. Beide zusammen
 * brauchten mehr Arbeitsspeicher, als der Core Basic frei hat: Uploads brachen
 * mit "Uebertragung brach ab" ab, der Tiefstwert fiel auf 5 KB.
 */
static esp_err_t ota_post(httpd_req_t *req)
{
    st_poll_pausieren(true);
    esp_err_t rc = ota_post_inner(req);
    st_poll_pausieren(false);
    return rc;
}

static esp_err_t ota_post_inner(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        return send_error(req, "500 Internal Server Error", "Keine freie Firmware-Partition");
    }
    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Die Aktualisierung ließ sich nicht starten");
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
            return send_error(req, "400 Bad Request", "Die Übertragung brach ab");
        }
        if (esp_ota_write(handle, buf, r) != ESP_OK) {
            free(buf);
            esp_ota_abort(handle);
            return send_error(req, "500 Internal Server Error", "Das Schreiben ist fehlgeschlagen");
        }
        remaining -= r;
    }
    free(buf);
    if (esp_ota_end(handle) != ESP_OK) {
        return send_error(req, "400 Bad Request", "Die Firmware ist nicht gültig");
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Die Startpartition ließ sich nicht setzen");
    }
    ESP_LOGW(TAG, "Neue Firmware uebernommen, Neustart folgt");
    xTaskCreate(restart_task, "restart", RESTART_STACK, NULL, 5, NULL);
    return send_ok(req);
}

/* ------------------------------------------------------------------ */

static const httpd_uri_t s_routes[] = {
    {.uri = "/", .method = HTTP_GET, .handler = page_get},
    {.uri = "/index.html", .method = HTTP_GET, .handler = page_get},
    {.uri = "/api/state", .method = HTTP_GET, .handler = state_get},
    {.uri = "/api/plant", .method = HTTP_GET, .handler = plant_get},
    {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
    {.uri = "/api/config", .method = HTTP_PUT, .handler = config_put},
    {.uri = "/api/ble", .method = HTTP_GET, .handler = ble_get},
    {.uri = "/api/ble/key", .method = HTTP_POST, .handler = ble_key_post},
    {.uri = "/api/demand", .method = HTTP_GET, .handler = demand_get},
    {.uri = "/api/peers", .method = HTTP_GET, .handler = peers_get_handler},
    {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get},
    {.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = wifi_scan_post},
    {.uri = "/api/screen", .method = HTTP_GET, .handler = screen_get},
    {.uri = "/api/display", .method = HTTP_POST, .handler = display_post},
    {.uri = "/api/system/*", .method = HTTP_POST, .handler = system_post},
    {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_post},
    {.uri = "/api/log/days", .method = HTTP_GET, .handler = log_days_get},
    {.uri = "/api/log/series", .method = HTTP_GET, .handler = log_series_get},
    {.uri = "/api/log/events", .method = HTTP_GET, .handler = log_events_get},
    {.uri = "/api/log/format", .method = HTTP_POST, .handler = log_format_post},
    {.uri = "/api/homekit/reset", .method = HTTP_POST, .handler = homekit_reset_post},
    {.uri = "/api/log/lasttest", .method = HTTP_POST, .handler = lasttest_post},
    {.uri = "/api/log/lasttest", .method = HTTP_GET, .handler = lasttest_get},
    {.uri = "/api/coredump", .method = HTTP_GET, .handler = coredump_get},
    {.uri = "/api/coredump/erase", .method = HTTP_POST, .handler = coredump_erase_post},
    {.uri = "/log/*", .method = HTTP_GET, .handler = log_file_get},
};

esp_err_t st_web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = sizeof(s_routes) / sizeof(s_routes[0]) + 2;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
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
