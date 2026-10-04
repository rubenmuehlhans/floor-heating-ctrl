#include "st_poll.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "peers.h"
#include "st_config.h"
#include "st_log.h"

static const char *TAG = "poll";

/* Groesster Zustand, der angenommen wird. Die Verteiler liefern heute knapp
 * 4 KB, die Heizungsgeraete gut 3,5 KB. */
#define BODY_MAX 8192
#define HTTP_TIMEOUT_MS 4000
/* Ab so vielen Fehlabfragen in Folge gilt ein Geraet als nicht erreichbar */
#define FAILS_UNREACHABLE 3

static SemaphoreHandle_t s_mtx;
static SemaphoreHandle_t s_gross;
static st_plant_t s_plant;

/* Nur im Abfrageauftrag benutzt */
static char s_body[BODY_MAX];
static int s_body_len;
static char s_etag[ST_MAX_MANIFOLD][24];
static uint32_t s_next_heat[ST_MAX_HEAT];
static uint32_t s_next_manifold[ST_MAX_MANIFOLD];

/* Aussenfuehler */
static bool s_outdoor_set;
static uint8_t s_outdoor_mac[6];
static uint32_t s_outdoor_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */
/* HTTP                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char *etag;
    size_t len;
} kopf_t;

static esp_err_t http_event(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->user_data != NULL && e->header_key != NULL &&
        strcasecmp(e->header_key, "ETag") == 0) {
        kopf_t *k = (kopf_t *)e->user_data;
        snprintf(k->etag, k->len, "%s", e->header_value);
    }
    return ESP_OK;
}

/*
 * Holt `pfad` von `host`. Liefert den HTTP-Status oder -1 bei einem Fehler
 * der Verbindung, -2 bei einer zu grossen Antwort. Der Inhalt steht danach in
 * s_body. Mit `etag_in` wird nur geliefert, was sich geaendert hat; sonst
 * kommt 304.
 */
static int holen(const char *host, const char *pfad, const char *etag_in, char *etag_out, size_t etag_len)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s%s", host, pfad);
    char etag[24] = {0};
    kopf_t k = {etag, sizeof(etag)};
    esp_http_client_config_t hc = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event,
        .user_data = &k,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (cl == NULL) {
        return -1;
    }
    if (etag_in != NULL && etag_in[0]) {
        esp_http_client_set_header(cl, "If-None-Match", etag_in);
    }

    int status = -1;
    s_body_len = 0;
    s_body[0] = '\0';
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        status = esp_http_client_get_status_code(cl);
        if (status == 200) {
            while (s_body_len < BODY_MAX - 1) {
                int r = esp_http_client_read(cl, s_body + s_body_len, BODY_MAX - 1 - s_body_len);
                if (r <= 0) {
                    break;
                }
                s_body_len += r;
            }
            s_body[s_body_len] = '\0';
            if (s_body_len >= BODY_MAX - 1) {
                status = -2;
            }
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    if (etag_out != NULL && etag[0]) {
        snprintf(etag_out, etag_len, "%s", etag);
    }
    return status;
}

/* Stueckweise Uebernahme fuer das Protokoll: die offene Verbindung liest in
 * den Puffer des Protokolls, ohne den Inhalt ganz im Speicher zu halten. */
static bool strom_lesen(void *ctx, char *buf, int max, int *n)
{
    int r = esp_http_client_read((esp_http_client_handle_t)ctx, buf, max);
    if (r < 0) {
        return false;
    }
    *n = r;
    return true;
}

/* Laedt `pfad` von `host` in die Protokolldatei `art` des Geraets */
static bool protokoll_holen(const char *host, const char *pfad, const char *kennung, const char *art)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s%s", host, pfad);
    esp_http_client_config_t hc = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (cl == NULL) {
        return false;
    }
    bool ok = false;
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        if (esp_http_client_get_status_code(cl) == 200) {
            ok = st_log_protokoll(kennung, art, strom_lesen, cl);
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Auslesen                                                            */
/* ------------------------------------------------------------------ */

static const cJSON *feld(const cJSON *o, const char *k)
{
    return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

static double zahl(const cJSON *o, const char *k, double vorgabe)
{
    const cJSON *j = feld(o, k);
    return cJSON_IsNumber(j) ? j->valuedouble : vorgabe;
}

static bool zahl_da(const cJSON *o, const char *k, float *out)
{
    const cJSON *j = feld(o, k);
    if (!cJSON_IsNumber(j)) {
        return false;
    }
    *out = (float)j->valuedouble;
    return true;
}

static bool wahr(const cJSON *o, const char *k)
{
    return cJSON_IsTrue(feld(o, k));
}

static void text(const cJSON *o, const char *k, char *dst, size_t len)
{
    const cJSON *j = feld(o, k);
    if (cJSON_IsString(j) && j->valuestring) {
        snprintf(dst, len, "%s", j->valuestring);
    }
}

static void geraet_lesen(const cJSON *root, st_dev_t *d)
{
    const cJSON *dev = feld(root, "device");
    text(dev, "id", d->id, sizeof(d->id));
    text(dev, "site", d->site, sizeof(d->site));
    text(root, "version", d->version, sizeof(d->version));
    d->uptime_s = (uint32_t)zahl(root, "uptime_s", 0);
    d->heap = (uint32_t)zahl(root, "heap", 0);
    d->rssi = (int8_t)zahl(feld(root, "net"), "rssi", 0);
}

static void heizgeraet_lesen(const cJSON *root, st_heat_t *h)
{
    geraet_lesen(root, &h->dev);

    h->probe_count = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, feld(root, "probes"))
    {
        char rolle[12] = {0};
        text(p, "role", rolle, sizeof(rolle));
        float t;
        if (!rolle[0] || strcmp(rolle, "none") == 0 || !zahl_da(p, "temp_c", &t) ||
            h->probe_count >= ST_MAX_PROBES) {
            continue;
        }
        if (cJSON_IsFalse(feld(p, "assigned"))) {
            continue;
        }
        st_probe_t *z = &h->probes[h->probe_count++];
        snprintf(z->role, sizeof(z->role), "%s", rolle);
        z->temp_c = t;
    }

    const cJSON *b = feld(root, "burner");
    h->burner_known = wahr(b, "known");
    h->burner_own = h->burner_known && !wahr(b, "remote");
    h->burner_running = wahr(b, "running");
    h->abgas_valid = zahl_da(b, "abgas_c", &h->abgas_c);
    h->runtime_today_s = (uint32_t)zahl(b, "runtime_today_s", 0);
    h->starts_today = (uint16_t)zahl(b, "starts_today", 0);
    h->litres_today = (float)zahl(b, "litres_today", 0);

    const cJSON *c = feld(root, "charge");
    h->charge_valid = zahl_da(c, "level", &h->charge_level);
    h->charge_own = h->charge_valid && !wahr(c, "puffer_remote");
    h->warn_dhw = wahr(c, "warn_dhw");
    h->charge_phase[0] = '\0';
    text(c, "phase", h->charge_phase, sizeof(h->charge_phase));

    h->circuit_count = 0;
    const cJSON *k;
    cJSON_ArrayForEach(k, feld(root, "circuits"))
    {
        if (h->circuit_count >= ST_MAX_CIRCUITS) {
            break;
        }
        st_circuit_t *z = &h->circuits[h->circuit_count++];
        memset(z, 0, sizeof(*z));
        z->id = (uint8_t)zahl(k, "id", 0);
        text(k, "name", z->name, sizeof(z->name));
        z->on = wahr(k, "on");
        z->vl_valid = zahl_da(k, "vl_c", &z->vl_c);
        z->rl_valid = zahl_da(k, "rl_c", &z->rl_c);
    }

    const cJSON *f = feld(root, "findings");
    h->findings = cJSON_IsArray(f) ? (uint8_t)cJSON_GetArraySize(f) : 0;
    h->finding[0] = '\0';
    h->finding_code[0] = '\0';
    if (h->findings > 0) {
        text(cJSON_GetArrayItem(f, 0), "text", h->finding, sizeof(h->finding));
        text(cJSON_GetArrayItem(f, 0), "code", h->finding_code, sizeof(h->finding_code));
    }
}

static void verteiler_lesen(const cJSON *root, st_manifold_t *m)
{
    geraet_lesen(root, &m->dev);
    m->room_count = 0;
    const cJSON *r;
    cJSON_ArrayForEach(r, feld(root, "rooms"))
    {
        if (m->room_count >= ST_MAX_ROOMS) {
            break;
        }
        st_room_t *z = &m->rooms[m->room_count++];
        memset(z, 0, sizeof(*z));
        z->id = (uint8_t)zahl(r, "id", 0);
        text(r, "name", z->name, sizeof(z->name));
        z->temp_valid = wahr(r, "temp_valid") && zahl_da(r, "temp_c", &z->temp_c);
        z->hum_valid = zahl_da(r, "humidity", &z->humidity);
        z->target_c = (float)zahl(r, "target_c", 0);
        char mode[8] = {0};
        text(r, "mode", mode, sizeof(mode));
        z->heat = strcmp(mode, "off") != 0;
        z->position = (float)zahl(r, "target_position", 0);
    }

    char funktion[12] = {0};
    text(feld(root, "device"), "function", funktion, sizeof(funktion));
    m->outdoor_only = strcmp(funktion, "outdoor") == 0;
    const cJSON *o = feld(root, "outdoor");
    m->aussen_valid = wahr(o, "valid") && zahl_da(o, "temp_c", &m->aussen_c);
    m->aussen_hum_valid = m->aussen_valid && zahl_da(o, "humidity", &m->aussen_hum);
    m->aussen_age_s = (uint32_t)zahl(o, "age_s", 0);
    m->aussen_ms = now_ms();
}

/* ------------------------------------------------------------------ */
/* Ablauf                                                              */
/* ------------------------------------------------------------------ */

/* Traegt die per mDNS gefundenen Geraete ein; bekannte behalten ihren Platz. */
static void geraete_eintragen(void)
{
    static peer_t liste[PEERS_MAX];
    size_t n = peers_get(liste, PEERS_MAX);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (size_t i = 0; i < n; i++) {
        const peer_t *p = &liste[i];
        st_dev_t *d = NULL;
        if (strcmp(p->role, PEERS_ROLE_HEAT) == 0) {
            for (uint8_t k = 0; k < s_plant.heat_count && d == NULL; k++) {
                if (strcmp(s_plant.heat[k].dev.id, p->id) == 0) {
                    d = &s_plant.heat[k].dev;
                }
            }
            if (d == NULL && s_plant.heat_count < ST_MAX_HEAT) {
                st_heat_t *h = &s_plant.heat[s_plant.heat_count];
                memset(h, 0, sizeof(*h));
                s_next_heat[s_plant.heat_count] = 0;
                s_plant.heat_count++;
                d = &h->dev;
                ESP_LOGI(TAG, "Heizungsgeraet %s (%s) gefunden", p->id, p->site);
            }
        } else if (strcmp(p->role, PEERS_ROLE_MANIFOLD) == 0) {
            for (uint8_t k = 0; k < s_plant.manifold_count && d == NULL; k++) {
                if (strcmp(s_plant.manifolds[k].dev.id, p->id) == 0) {
                    d = &s_plant.manifolds[k].dev;
                }
            }
            if (d == NULL && s_plant.manifold_count < ST_MAX_MANIFOLD) {
                uint8_t k = s_plant.manifold_count;
                st_manifold_t *m = &s_plant.manifolds[k];
                memset(m, 0, sizeof(*m));
                s_next_manifold[k] = 0;
                s_etag[k][0] = '\0';
                s_plant.manifold_count++;
                d = &m->dev;
                ESP_LOGI(TAG, "Verteiler %s (%s) gefunden", p->id, p->site);
            }
        }
        if (d != NULL) {
            snprintf(d->id, sizeof(d->id), "%s", p->id);
            snprintf(d->host, sizeof(d->host), "%s", p->host);
            if (p->site[0]) {
                snprintf(d->site, sizeof(d->site), "%s", p->site);
            }
        }
    }
    xSemaphoreGive(s_mtx);
}

static void fehlschlag(st_dev_t *d, int status)
{
    if (d->fails < 255) {
        d->fails++;
    }
    if (d->fails == FAILS_UNREACHABLE) {
        ESP_LOGW(TAG, "%s (%s) antwortet nicht mehr (%d)", d->site, d->host, status);
    }
    if (d->fails >= FAILS_UNREACHABLE && d->reachable) {
        d->reachable = false;
        s_plant.revision++;
        st_log_erreichbar(d->id, false, 0);
    }
}

static void erfolg(st_dev_t *d)
{
    if (!d->reachable && d->seen) {
        ESP_LOGI(TAG, "%s (%s) wieder erreichbar", d->site, d->host);
        st_log_erreichbar(d->id, true, (now_ms() - d->ok_ms) / 1000);
    }
    d->fails = 0;
    d->reachable = true;
    d->seen = true;
    d->ok_ms = now_ms();
}

static void heizgeraet_abfragen(uint8_t k)
{
    static st_heat_t neu;
    char host[16];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    snprintf(host, sizeof(host), "%s", s_plant.heat[k].dev.host);
    neu = s_plant.heat[k];
    xSemaphoreGive(s_mtx);

    st_speicher_sperren();
    int status = host[0] ? holen(host, "/api/state", NULL, NULL, 0) : -1;
    cJSON *root = status == 200 ? cJSON_ParseWithLength(s_body, s_body_len) : NULL;
    if (root != NULL) {
        heizgeraet_lesen(root, &neu);
        st_log_zustand(neu.dev.id, neu.dev.site, true, root, NULL, s_body, s_body_len);
        cJSON_Delete(root);
        /* Einmal am Tag die Protokolle des Geraets, gleich nach einer
         * geglueckten Abfrage: dann ist es erreichbar. */
        if (st_log_protokolle_faellig(neu.dev.id) &&
            protokoll_holen(host, "/api/log/charges", neu.dev.id, "ladungen") &&
            protokoll_holen(host, "/api/log/days", neu.dev.id, "tage")) {
            st_log_protokolle_erledigt(neu.dev.id);
        }
    }
    st_speicher_freigeben();

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    st_heat_t *h = &s_plant.heat[k];
    if (root != NULL) {
        st_dev_t alt = h->dev;
        *h = neu;
        h->dev.seen = alt.seen;
        h->dev.reachable = alt.reachable;
        h->dev.fails = alt.fails;
        snprintf(h->dev.host, sizeof(h->dev.host), "%s", alt.host);
        erfolg(&h->dev);
        s_plant.revision++;
    } else {
        fehlschlag(&h->dev, status);
    }
    xSemaphoreGive(s_mtx);
}

static void verteiler_abfragen(uint8_t k)
{
    static st_manifold_t neu;
    char host[16];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    snprintf(host, sizeof(host), "%s", s_plant.manifolds[k].dev.host);
    neu = s_plant.manifolds[k];
    xSemaphoreGive(s_mtx);

    st_speicher_sperren();
    /* Erst die Bedarfsmeldung, sie ist klein und steht dann neben dem
     * Zustand, dessen Text fuer das Protokoll in s_body bleibt. */
    cJSON *bedarf = NULL;
    if (host[0] && holen(host, "/api/demand", NULL, NULL, 0) == 200) {
        bedarf = cJSON_ParseWithLength(s_body, s_body_len);
    }
    int status = host[0] ? holen(host, "/api/state", s_etag[k], s_etag[k], sizeof(s_etag[k])) : -1;
    cJSON *root = status == 200 ? cJSON_ParseWithLength(s_body, s_body_len) : NULL;
    if (root != NULL) {
        verteiler_lesen(root, &neu);
        st_log_zustand(neu.dev.id, neu.dev.site, false, root, bedarf, s_body, s_body_len);
        cJSON_Delete(root);
    } else if (status == 304) {
        st_log_unveraendert(neu.dev.id);
    }
    cJSON_Delete(bedarf);
    st_speicher_freigeben();

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    st_manifold_t *m = &s_plant.manifolds[k];
    if (root != NULL) {
        st_dev_t alt = m->dev;
        *m = neu;
        m->dev.seen = alt.seen;
        m->dev.reachable = alt.reachable;
        m->dev.fails = alt.fails;
        snprintf(m->dev.host, sizeof(m->dev.host), "%s", alt.host);
        erfolg(&m->dev);
        s_plant.revision++;
    } else if (status == 304) {
        /* Unveraendert: nur das Lebenszeichen gilt. */
        erfolg(&m->dev);
    } else {
        s_etag[k][0] = '\0';
        fehlschlag(&m->dev, status);
    }
    xSemaphoreGive(s_mtx);
}

static volatile bool s_pause;

void st_poll_pausieren(bool pause)
{
    s_pause = pause;
    /* Eine gerade laufende Abfrage haelt die Speichersperre; auf ihr Ende
     * wird gewartet, danach ist der Speicher frei. */
    if (pause) {
        st_speicher_sperren();
        st_speicher_freigeben();
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    uint32_t eintragen_ms = 0;
    for (;;) {
        if (s_pause) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        uint32_t t = now_ms();
        if (t - eintragen_ms >= 2000 || eintragen_ms == 0) {
            geraete_eintragen();
            eintragen_ms = t;
        }

        st_config_t cfg;
        st_cfg_copy(&cfg);
        memset(cfg.wifi.pass, 0, sizeof(cfg.wifi.pass));
        memset(cfg.wifi.ap_pass, 0, sizeof(cfg.wifi.ap_pass));

        /* Je Durchgang hoechstens eine Abfrage: So liegt immer nur ein
         * Zustand im Speicher, und die Geraete werden gleichmaessig belastet. */
        bool abgefragt = false;
        for (uint8_t k = 0; k < s_plant.heat_count && !abgefragt; k++) {
            if ((int32_t)(t - s_next_heat[k]) >= 0) {
                s_next_heat[k] = t + cfg.poll_heat_s * 1000u;
                heizgeraet_abfragen(k);
                abgefragt = true;
            }
        }
        for (uint8_t k = 0; k < s_plant.manifold_count && !abgefragt; k++) {
            if ((int32_t)(t - s_next_manifold[k]) >= 0) {
                s_next_manifold[k] = t + cfg.poll_manifold_s * 1000u;
                verteiler_abfragen(k);
                abgefragt = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(abgefragt ? 100 : 250));
    }
}

/* ------------------------------------------------------------------ */
/* Aussenfuehler                                                       */
/* ------------------------------------------------------------------ */

void st_poll_set_outdoor(const char *mac)
{
    uint8_t bin[6];
    bool gesetzt = mac != NULL && mac[0] && atc_parse_mac(mac, bin);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    st_outdoor_t *o = &s_plant.outdoor;
    if (!gesetzt || !s_outdoor_set || memcmp(bin, s_outdoor_mac, 6) != 0) {
        memset(o, 0, sizeof(*o));
        s_outdoor_ms = 0;
    }
    s_outdoor_set = gesetzt;
    o->assigned = gesetzt;
    if (gesetzt) {
        memcpy(s_outdoor_mac, bin, 6);
        snprintf(o->mac, sizeof(o->mac), "%02X:%02X:%02X:%02X:%02X:%02X", bin[0], bin[1], bin[2], bin[3],
                 bin[4], bin[5]);
    }
    s_plant.revision++;
    xSemaphoreGive(s_mtx);
}

void st_poll_ble(const atc_device_t *dev)
{
    if (!s_outdoor_set || memcmp(dev->mac, s_outdoor_mac, 6) != 0 || !dev->has_temp) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    st_outdoor_t *o = &s_plant.outdoor;
    o->valid = true;
    o->temp_c = dev->temp_c;
    o->hum_valid = dev->has_humidity;
    o->humidity = dev->humidity;
    o->battery = dev->battery;
    o->rssi = dev->rssi;
    snprintf(o->name, sizeof(o->name), "%s", dev->name);
    s_outdoor_ms = now_ms();
    s_plant.revision++;
    xSemaphoreGive(s_mtx);
}

/* ------------------------------------------------------------------ */

/*
 * Hoert der Leitstand den Aussenfuehler nicht selbst, gilt der Wert eines
 * Verteilers, der ihn empfaengt -- an dieser Anlage eine eigene Platine, die
 * nur dafuer in Reichweite steht. Der eigene Empfang geht vor, solange er
 * frisch ist; unter den Verteilern der juengste Wert.
 */
static void aussen_vom_verteiler(st_outdoor_t *o, uint32_t t)
{
    if (o->valid && o->age_s <= ST_OUTDOOR_STALE_S) {
        return;
    }
    const st_manifold_t *best = NULL;
    uint32_t best_alter = 0;
    for (uint8_t k = 0; k < s_plant.manifold_count; k++) {
        const st_manifold_t *m = &s_plant.manifolds[k];
        if (!m->aussen_valid) {
            continue;
        }
        uint32_t alter = m->aussen_age_s + (t - m->aussen_ms) / 1000;
        if (alter <= ST_OUTDOOR_STALE_S && (best == NULL || alter < best_alter)) {
            best = m;
            best_alter = alter;
        }
    }
    if (best == NULL) {
        return;
    }
    o->valid = true;
    o->temp_c = best->aussen_c;
    o->hum_valid = best->aussen_hum_valid;
    o->humidity = best->aussen_hum;
    o->battery = 0;
    o->rssi = 0;
    o->age_s = best_alter;
    snprintf(o->quelle, sizeof(o->quelle), "%s", best->dev.site[0] ? best->dev.site : best->dev.id);
}

void st_poll_snapshot(st_plant_t *out)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_plant;
    uint32_t t = now_ms();
    if (s_plant.outdoor.valid) {
        out->outdoor.age_s = (t - s_outdoor_ms) / 1000;
    }
    aussen_vom_verteiler(&out->outdoor, t);
    xSemaphoreGive(s_mtx);
}

uint32_t st_poll_revision(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t r = s_plant.revision;
    xSemaphoreGive(s_mtx);
    return r;
}

void st_poll_init(void)
{
    if (s_mtx == NULL) {
        s_mtx = xSemaphoreCreateMutex();
        s_gross = xSemaphoreCreateMutex();
    }
}

void st_speicher_sperren(void)
{
    xSemaphoreTake(s_gross, portMAX_DELAY);
}

void st_speicher_freigeben(void)
{
    xSemaphoreGive(s_gross);
}

esp_err_t st_poll_start(void)
{
    st_poll_init();
    st_config_t cfg;
    st_cfg_copy(&cfg);
    st_poll_set_outdoor(cfg.outdoor_mac);
    return xTaskCreate(poll_task, "poll", 6144, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}
