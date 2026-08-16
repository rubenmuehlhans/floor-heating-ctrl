#include "app_pumps.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_sensors.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqttc.h"
#include "peers.h"
#include "schedule.h"

static const char *TAG = "pumps";

#define TICK_MS 1000
#define HTTP_TIMEOUT_MS 2000
#define RESEND_MS 60000       /* Sollzustand zyklisch nachsenden */
#define MISMATCH_MS 30000     /* danach gilt die Rueckmeldung als abweichend */
#define HEARTBEAT_MS 60000    /* Lebenszeichen an das Relais */
/* Nach einem erfolglosen HTTP-Versuch wird gewartet. Ohne das liefe die
 * Aufgabe im Sekundentakt in den Zeitablauf und stuende dabei jedes Mal
 * zwei Sekunden still. */
#define HTTP_RETRY_MS 10000

typedef struct {
    uint8_t id;
    pump_state_t st;
    pump_cfg_t cfg;
    char name[CFG_NAME_LEN];
    bool enabled;
    probe_role_t vl_role, rl_role;

    char topic[CFG_TOPIC_LEN];
    char host[CFG_TOPIC_LEN];
    char user[24];
    char pass[48];
    uint8_t relay;
    int last_status;          /* HTTP-Status des letzten Versuchs */

    /* Rueckmeldung des Relais. */
    bool relay_known, relay_on, relay_online;
    uint32_t relay_seen_ms;
    uint32_t want_since_ms;   /* seit wann der Sollzustand anliegt */
    uint32_t last_sent_ms;
    bool last_sent_on;
    uint32_t last_try_ms;     /* letzter Versuch, auch ein erfolgloser */

    bool demand, stale, any_seen;
} circuit_t;

static circuit_t s_circ[CFG_MAX_CIRCUITS];
static uint8_t s_circ_count;

static peer_demand_t s_peer[CFG_MAX_PEERS * CFG_MAX_CIRCUITS];
static size_t s_peer_count;

static SemaphoreHandle_t s_mtx;
static volatile bool s_cfg_dirty;
static uint32_t s_heartbeat_ms;
static sched_weekly_t s_seize_sched;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */
/* Konfiguration uebernehmen                                           */
/* ------------------------------------------------------------------ */

static void push_relay(circuit_t *z, uint32_t t);

static void apply_config(void)
{
    static app_config_t cfg;
    cfg_copy(&cfg);

    /*
     * Die Liste wird neu aufgebaut, statt sie an Ort und Stelle zu aendern.
     * Beim Loeschen eines Kreises ruecken die uebrigen sonst eine Stelle vor
     * und verlieren dabei ihre Mindestzeiten -- zugeordnet wird deshalb ueber
     * die Kennung, nicht ueber die Stelle in der Liste.
     */
    static circuit_t neu[CFG_MAX_CIRCUITS];
    static circuit_t entfallen[CFG_MAX_CIRCUITS];
    uint8_t entfallen_n = 0;

    memset(neu, 0, sizeof(neu));
    uint8_t n = cfg.circuit_count < CFG_MAX_CIRCUITS ? cfg.circuit_count : CFG_MAX_CIRCUITS;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < n; i++) {
        const cfg_circuit_t *c = &cfg.circuits[i];
        circuit_t *z = &neu[i];

        /* Zustandsmaschine nur beim ersten Mal anlegen, sonst gingen
         * Mindestzeiten und Schutzlaufzaehler bei jedem Speichern verloren. */
        for (uint8_t j = 0; j < s_circ_count; j++) {
            if (s_circ[j].id == c->id) {
                *z = s_circ[j];
                break;
            }
        }
        if (z->id != c->id) {
            z->id = c->id;
            pump_init(&z->st, (pump_mode_t)c->mode);
        } else if (z->st.mode != (pump_mode_t)c->mode) {
            pump_set_mode(&z->st, (pump_mode_t)c->mode, now_ms());
        }

        snprintf(z->name, sizeof(z->name), "%s", c->name);
        z->enabled = c->enabled;
        z->vl_role = c->vl_role;
        z->rl_role = c->rl_role;
        snprintf(z->topic, sizeof(z->topic), "%s", c->pump_topic);
        snprintf(z->host, sizeof(z->host), "%s", c->pump_host);
        snprintf(z->user, sizeof(z->user), "%s", c->pump_user);
        snprintf(z->pass, sizeof(z->pass), "%s", c->pump_pass);
        z->relay = c->pump_relay;

        z->cfg.overrun_s = c->overrun_s;
        z->cfg.min_run_s = c->min_run_s;
        z->cfg.min_pause_s = c->min_pause_s;
        z->cfg.min_buffer_c = c->min_buffer_c;
        z->cfg.frost_c = c->frost_c;
        z->cfg.seize_run_s = 180;
    }

    /* Geloeschte Kreise vormerken: ihre Pumpe wird gleich abgeschaltet. */
    for (uint8_t j = 0; j < s_circ_count; j++) {
        if (s_circ[j].id != 0 && cfg_circuit(&cfg, s_circ[j].id) == NULL) {
            entfallen[entfallen_n++] = s_circ[j];
        }
    }

    memcpy(s_circ, neu, sizeof(s_circ));
    s_circ_count = n;
    xSemaphoreGive(s_mtx);

    /*
     * Eine Pumpe, deren Heizkreis geloescht wurde, wird niemand mehr
     * abschalten -- sie liefe bis zum naechsten Eingriff von Hand weiter.
     * Der Befehl geht ausserhalb der Sperre hinaus, weil er ueber das Netz
     * geht und dabei in eine Zeitueberschreitung laufen kann.
     */
    for (uint8_t i = 0; i < entfallen_n; i++) {
        circuit_t *z = &entfallen[i];
        z->st.on = false;
        z->last_sent_ms = 0;
        z->last_try_ms = 0;
        push_relay(z, now_ms());
        ESP_LOGI(TAG, "Heizkreis %u geloescht, Pumpe abgeschaltet", (unsigned)z->id);
    }
}

/* ------------------------------------------------------------------ */
/* Verteiler abfragen                                                  */
/* ------------------------------------------------------------------ */

/* Sucht den Eintrag zu einer Geraetekennung, legt ihn bei Bedarf an. */
static peer_demand_t *peer_slot(const char *id)
{
    for (size_t i = 0; i < s_peer_count; i++) {
        if (strcmp(s_peer[i].id, id) == 0) {
            return &s_peer[i];
        }
    }
    if (s_peer_count >= sizeof(s_peer) / sizeof(s_peer[0])) {
        return NULL;
    }
    peer_demand_t *p = &s_peer[s_peer_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->id, sizeof(p->id), "%s", id);
    return p;
}

static bool fetch_demand(const char *host, peer_demand_t *out)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/demand", host);

    esp_http_client_config_t hc = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (cl == NULL) {
        return false;
    }

    bool ok = false;
    char body[320];
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int n = esp_http_client_read(cl, body, sizeof(body) - 1);
        if (n > 0) {
            body[n] = '\0';
            cJSON *root = cJSON_Parse(body);
            if (root != NULL) {
                const cJSON *v;
                v = cJSON_GetObjectItemCaseSensitive(root, "demand");
                out->demand = cJSON_IsTrue(v);
                v = cJSON_GetObjectItemCaseSensitive(root, "max_target");
                out->max_target = cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
                v = cJSON_GetObjectItemCaseSensitive(root, "open_channels");
                out->open_channels = cJSON_IsNumber(v) ? (uint8_t)v->valuedouble : 0;
                v = cJSON_GetObjectItemCaseSensitive(root, "rooms_calling");
                out->rooms_calling = cJSON_IsNumber(v) ? (uint8_t)v->valuedouble : 0;
                v = cJSON_GetObjectItemCaseSensitive(root, "min_room_c");
                out->room_valid = cJSON_IsNumber(v);
                out->min_room_c = out->room_valid ? (float)v->valuedouble : 0.0f;
                v = cJSON_GetObjectItemCaseSensitive(root, "site");
                if (cJSON_IsString(v)) {
                    snprintf(out->site, sizeof(out->site), "%s", v->valuestring);
                }
                cJSON_Delete(root);
                ok = true;
            }
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    return ok;
}

/*
 * Fragt alle Verteiler ab, die einem Kreis zugeordnet sind. Die Adresse kommt
 * aus der mDNS-Liste; eine feste Adresse waere ein zweiter Ort, an dem eine
 * Aenderung nachgezogen werden muesste.
 */
static void poll_peers(void)
{
    static app_config_t cfg;
    static peer_t gefunden[PEERS_MAX];
    cfg_copy(&cfg);
    size_t n = peers_get(gefunden, PEERS_MAX);
    uint32_t t = now_ms();

    for (uint8_t i = 0; i < cfg.circuit_count; i++) {
        const cfg_circuit_t *c = &cfg.circuits[i];
        for (uint8_t k = 0; k < c->peer_count; k++) {
            const char *id = c->peers[k];
            if (id[0] == '\0') {
                continue;
            }
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            peer_demand_t *p = peer_slot(id);
            xSemaphoreGive(s_mtx);
            if (p == NULL) {
                continue;
            }

            const char *host = NULL;
            for (size_t j = 0; j < n; j++) {
                if (strcmp(gefunden[j].id, id) == 0) {
                    host = gefunden[j].host;
                    break;
                }
            }
            if (host == NULL) {
                continue; /* noch nicht im Netz gefunden */
            }

            peer_demand_t neu = *p;
            snprintf(neu.host, sizeof(neu.host), "%s", host);
            if (fetch_demand(host, &neu)) {
                neu.seen = true;
                neu.age_s = 0;
                xSemaphoreTake(s_mtx, portMAX_DELAY);
                *p = neu;
                p->age_s = 0;
                xSemaphoreGive(s_mtx);
                p->errors = neu.errors;
            } else {
                xSemaphoreTake(s_mtx, portMAX_DELAY);
                p->errors++;
                snprintf(p->host, sizeof(p->host), "%s", host);
                xSemaphoreGive(s_mtx);
            }
        }
    }
    (void)t;
}

/* ------------------------------------------------------------------ */
/* Relais                                                              */
/* ------------------------------------------------------------------ */

static void relay_state_cb(const char *topic, const char *payload, void *ctx)
{
    (void)ctx;
    /* stat/<topic>/POWER<n> -> ON | OFF */
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < s_circ_count; i++) {
        circuit_t *z = &s_circ[i];
        char erwartet[CFG_TOPIC_LEN + 24];
        snprintf(erwartet, sizeof(erwartet), "stat/%s/POWER%u", z->topic, (unsigned)z->relay);
        if (strcmp(topic, erwartet) == 0) {
            z->relay_known = true;
            z->relay_on = strcmp(payload, "ON") == 0;
            z->relay_seen_ms = now_ms();
        }
    }
    xSemaphoreGive(s_mtx);
}

static void relay_lwt_cb(const char *topic, const char *payload, void *ctx)
{
    (void)ctx;
    bool online = strcmp(payload, "Online") == 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < s_circ_count; i++) {
        circuit_t *z = &s_circ[i];
        char erwartet[CFG_TOPIC_LEN + 24];
        snprintf(erwartet, sizeof(erwartet), "tele/%s/LWT", z->topic);
        if (strcmp(topic, erwartet) == 0) {
            z->relay_online = online;
            /* Ein neu gestartetes Relais bekommt den Sollzustand sofort. */
            if (online) {
                z->last_sent_ms = 0;
            }
        }
    }
    xSemaphoreGive(s_mtx);
}

static void subscribe_relays(void *ctx)
{
    (void)ctx;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    circuit_t kopie[CFG_MAX_CIRCUITS];
    memcpy(kopie, s_circ, sizeof(kopie));
    uint8_t n = s_circ_count;
    xSemaphoreGive(s_mtx);

    for (uint8_t i = 0; i < n; i++) {
        if (kopie[i].topic[0] == '\0') {
            continue;
        }
        char t[CFG_TOPIC_LEN + 24];
        snprintf(t, sizeof(t), "stat/%s/POWER%u", kopie[i].topic, (unsigned)kopie[i].relay);
        mqttc_subscribe(t, relay_state_cb, NULL);
        snprintf(t, sizeof(t), "tele/%s/LWT", kopie[i].topic);
        mqttc_subscribe(t, relay_lwt_cb, NULL);
    }
}

/*
 * Befehl unmittelbar ueber die Tasmota-Schnittstelle /cm.
 *
 * Der Weg ohne Broker. Die Antwort traegt den tatsaechlichen Zustand des
 * Relais -- damit ersetzt ein Aufruf zugleich die Rueckmeldung, die ueber MQTT
 * von selbst kaeme. Zwischen zwei Aufrufen bleibt eine Aenderung am Relais
 * allerdings unbemerkt; deshalb wird der Sollzustand zyklisch nachgesendet.
 */
/* Rueckgabe: HTTP-Status, oder -1 wenn keine Verbindung zustande kam. */
static int http_cmd(const circuit_t *z, const char *cmnd, bool *out_on)
{
    char url[224];
    int n = snprintf(url, sizeof(url), "http://%s/cm?cmnd=%s", z->host, cmnd);
    if (z->user[0] && n > 0 && n < (int)sizeof(url)) {
        snprintf(url + n, sizeof(url) - n, "&user=%s&password=%s", z->user, z->pass);
    }

    esp_http_client_config_t hc = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (cl == NULL) {
        return -1;
    }

    int status = -1;
    char body[192];
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int r = esp_http_client_read(cl, body, sizeof(body) - 1);
        status = esp_http_client_get_status_code(cl);
        if (r > 0 && status == 200) {
            body[r] = '\0';
            /* Antwort ist {"POWER":"ON"} oder {"POWER2":"OFF"}; bei einem
             * einzigen Relais laesst Tasmota die Nummer weg. */
            if (out_on != NULL) {
                cJSON *root = cJSON_Parse(body);
                if (root != NULL) {
                    const cJSON *e = NULL;
                    cJSON_ArrayForEach(e, root) {
                        if (e->string && strncmp(e->string, "POWER", 5) == 0 &&
                            cJSON_IsString(e)) {
                            *out_on = strcmp(e->valuestring, "ON") == 0;
                            break;
                        }
                    }
                    cJSON_Delete(root);
                }
            }
        }
        esp_http_client_close(cl);
    }
    esp_http_client_cleanup(cl);
    return status;
}

/*
 * Sendet den Sollzustand, wenn er sich geaendert hat oder lange her ist.
 *
 * Bevorzugt wird MQTT, sobald die Verbindung steht: dort meldet das Relais
 * jede Aenderung von selbst, auch eine von Hand am Geraet. Ohne Broker geht
 * derselbe Befehl unmittelbar per HTTP hinaus.
 */
static void push_relay(circuit_t *z, uint32_t t)
{
    bool per_mqtt = z->topic[0] != '\0' && mqttc_connected();
    bool per_http = !per_mqtt && z->host[0] != '\0';
    if (!per_mqtt && !per_http) {
        return;
    }

    bool faellig = z->last_sent_ms == 0 || z->last_sent_on != z->st.on ||
                   (t - z->last_sent_ms) >= RESEND_MS;
    if (!faellig) {
        return;
    }
    /* Erfolglose Versuche nicht im Sekundentakt wiederholen. */
    if (z->last_try_ms != 0 && (t - z->last_try_ms) < HTTP_RETRY_MS && per_http) {
        return;
    }
    z->last_try_ms = t;

    if (per_mqtt) {
        char topic[CFG_TOPIC_LEN + 24];
        snprintf(topic, sizeof(topic), "cmnd/%s/POWER%u", z->topic, (unsigned)z->relay);
        if (mqttc_publish(topic, z->st.on ? "ON" : "OFF", false) == ESP_OK) {
            z->last_sent_ms = t;
            z->last_sent_on = z->st.on;
            z->last_status = 0; /* ueber MQTT gibt es keinen Status */
        }
        return;
    }

    char cmnd[32];
    snprintf(cmnd, sizeof(cmnd), "Power%u%%20%s", (unsigned)z->relay, z->st.on ? "ON" : "OFF");
    bool ist = false;
    int status = http_cmd(z, cmnd, &ist);
    z->last_status = status;

    if (status == 200) {
        z->last_sent_ms = t;
        z->last_sent_on = z->st.on;
        z->relay_known = true;
        z->relay_on = ist;
        z->relay_online = true;
        z->relay_seen_ms = t;
    } else {
        z->relay_online = false;
        if (status < 0) {
            ESP_LOGW(TAG, "Relais %s nicht erreichbar", z->host);
        } else {
            /* Verbindung stand, aber die Antwort passt nicht: falsche Adresse,
             * fehlende Anmeldung oder gar kein Tasmota. */
            ESP_LOGW(TAG, "Relais %s antwortet mit %d", z->host, status);
        }
    }
}

/*
 * Lebenszeichen an das Relais. Am Relais ist eine Regel hinterlegt, die die
 * Pumpe einschaltet, wenn dieses Zeichen laenger als eine Viertelstunde
 * ausbleibt -- faellt diese Steuerung aus, laufen die Pumpen weiter.
 */
static void push_heartbeat(uint32_t t)
{
    if (t - s_heartbeat_ms < HEARTBEAT_MS) {
        return;
    }
    s_heartbeat_ms = t;
    for (uint8_t i = 0; i < s_circ_count; i++) {
        circuit_t *z = &s_circ[i];
        if (z->topic[0] != '\0' && mqttc_connected()) {
            char topic[CFG_TOPIC_LEN + 24];
            snprintf(topic, sizeof(topic), "cmnd/%s/Var1", z->topic);
            mqttc_publish(topic, "1", false);
        } else if (z->host[0] != '\0') {
            http_cmd(z, "Var1%201", NULL);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Aufgabe                                                             */
/* ------------------------------------------------------------------ */

/*
 * Faellt der woechentliche Schutzlauf? Gefragt wird die Uhr, nicht die
 * Laufzeit: nach jedem Neustart finge eine Standzeitzaehlung von vorn an, und
 * ein Geraet mit taeglichem Neustart erreichte nie eine Woche.
 */
static bool seize_due_now(const app_config_t *cfg)
{
    if (cfg->seize_weekday < 0) {
        return false;
    }
    time_t jetzt = time(NULL);
    if (jetzt < 1700000000) {
        return false; /* Uhr nicht gestellt */
    }
    struct tm tm;
    localtime_r(&jetzt, &tm);
    return sched_weekly_due(&s_seize_sched, cfg->seize_weekday, cfg->seize_hour, tm.tm_wday,
                            tm.tm_hour, tm.tm_yday, tm.tm_year);
}

static void evaluate(uint32_t t)
{
    static app_config_t cfg;
    cfg_copy(&cfg);

    /* Einmal je Durchlauf gefragt, sonst saehe ihn nur der erste Kreis. */
    bool schutzlauf_faellig = seize_due_now(&cfg);
    if (schutzlauf_faellig) {
        ESP_LOGI(TAG, "Woechentlicher Schutzlauf der Pumpen faellig");
    }

    for (uint8_t i = 0; i < s_circ_count; i++) {
        circuit_t *z = &s_circ[i];
        const cfg_circuit_t *c = cfg_circuit(&cfg, z->id);
        if (c == NULL) {
            continue;
        }

        /* Bedarf der zugeordneten Verteiler zusammenfassen. */
        demand_source_t quellen[CFG_MAX_PEERS];
        uint32_t qn = 0;
        for (uint8_t k = 0; k < c->peer_count && qn < CFG_MAX_PEERS; k++) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            const peer_demand_t *p = NULL;
            for (size_t j = 0; j < s_peer_count; j++) {
                if (strcmp(s_peer[j].id, c->peers[k]) == 0) {
                    p = &s_peer[j];
                    break;
                }
            }
            if (p != NULL) {
                quellen[qn].seen = p->seen;
                quellen[qn].demand = p->demand;
                quellen[qn].age_s = p->age_s;
                quellen[qn].room_valid = p->room_valid;
                quellen[qn].min_room_c = p->min_room_c;
                qn++;
            }
            xSemaphoreGive(s_mtx);
        }

        demand_result_t bedarf;
        demand_evaluate(quellen, qn, cfg.demand_timeout_s, &bedarf);

        pump_input_t in = {0};
        in.demand = bedarf.demand;
        in.room_valid = bedarf.room_valid;
        in.min_room_c = bedarf.min_room_c;
        in.buffer_valid = sensors_role_value(ROLE_PUFFER, &in.buffer_c, NULL);
        in.flow_valid = sensors_role_value(z->vl_role, &in.flow_c, NULL);
        in.seize_due = schutzlauf_faellig;

        /* Ein abgeschalteter Kreis wird nicht geregelt und nicht geschaltet. */
        if (!z->enabled) {
            continue;
        }

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        z->demand = bedarf.demand;
        z->stale = bedarf.stale;
        z->any_seen = bedarf.any_seen;
        pump_tick(&z->st, &z->cfg, &in, t);
        xSemaphoreGive(s_mtx);

        push_relay(z, t);
    }
    push_heartbeat(t);
}

static void pumps_task(void *arg)
{
    (void)arg;
    apply_config();
    subscribe_relays(NULL);

    uint32_t letzte_abfrage = 0;

    for (;;) {
        uint32_t t = now_ms();

        if (s_cfg_dirty) {
            s_cfg_dirty = false;
            apply_config();
            subscribe_relays(NULL);
        }

        static app_config_t cfg;
        cfg_copy(&cfg);
        if (letzte_abfrage == 0 || (t - letzte_abfrage) >= (uint32_t)cfg.demand_poll_s * 1000UL) {
            letzte_abfrage = t;
            poll_peers();
        }

        /* Alter der Antworten fortschreiben. */
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (size_t i = 0; i < s_peer_count; i++) {
            if (s_peer[i].seen) {
                s_peer[i].age_s++;
            }
        }
        xSemaphoreGive(s_mtx);

        evaluate(now_ms());
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ------------------------------------------------------------------ */

esp_err_t pumps_start(void)
{
    s_mtx = xSemaphoreCreateRecursiveMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* Die Abfrage haengt an HTTP und cJSON, das braucht Stapel. */
    if (xTaskCreate(pumps_task, "pumps", 6144, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Pumpensteuerung laeuft");
    return ESP_OK;
}

void pumps_config_changed(void)
{
    s_cfg_dirty = true;
}

esp_err_t pumps_set_mode(uint8_t circuit_id, pump_mode_t mode)
{
    static app_config_t cfg;
    cfg_copy(&cfg);

    bool gefunden = false;
    for (uint8_t i = 0; i < cfg.circuit_count; i++) {
        if (cfg.circuits[i].id == circuit_id) {
            cfg.circuits[i].mode = (uint8_t)mode;
            gefunden = true;
            break;
        }
    }
    if (!gefunden) {
        return ESP_ERR_NOT_FOUND;
    }

    char err[96] = {0};
    esp_err_t rc = cfg_set(&cfg, err, sizeof(err));
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Betriebsart nicht gespeichert: %s", err);
        return rc;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < s_circ_count; i++) {
        if (s_circ[i].id == circuit_id) {
            pump_set_mode(&s_circ[i].st, mode, now_ms());
            /* Der neue Zustand soll sofort hinausgehen, nicht erst beim
             * naechsten zyklischen Nachsenden. */
            s_circ[i].last_sent_ms = 0;
        }
    }
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}

size_t pumps_status(circuit_status_t *out, size_t max)
{
    if (s_mtx == NULL) {
        return 0;
    }
    uint32_t t = now_ms();
    size_t n = 0;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < s_circ_count && n < max; i++) {
        const circuit_t *z = &s_circ[i];
        circuit_status_t *o = &out[n++];
        memset(o, 0, sizeof(*o));
        o->id = z->id;
        snprintf(o->name, sizeof(o->name), "%s", z->name);
        o->enabled = z->enabled;
        o->mode = z->st.mode;
        o->on = z->st.on;
        o->reason = z->st.reason;
        o->since_s = (t - z->st.since_ms) / 1000;
        o->demand = z->demand;
        o->stale = z->stale;
        o->any_seen = z->any_seen;
        o->vl_valid = sensors_role_value(z->vl_role, &o->vl_c, NULL);
        o->rl_valid = sensors_role_value(z->rl_role, &o->rl_c, NULL);
        o->relay_known = z->relay_known;
        o->relay_on = z->relay_on;
        o->relay_online = z->relay_online;
        o->relay_age_s = z->relay_seen_ms ? (t - z->relay_seen_ms) / 1000 : 0;
        o->relay_mismatch = z->relay_known && z->relay_on != z->st.on &&
                            z->last_sent_ms != 0 && (t - z->last_sent_ms) > MISMATCH_MS;
        o->path = (z->topic[0] != '\0' && mqttc_connected()) ? PUMP_PATH_MQTT
                  : (z->host[0] != '\0' ? PUMP_PATH_HTTP : PUMP_PATH_NONE);
        o->last_status = z->last_status;
    }
    xSemaphoreGive(s_mtx);
    return n;
}

size_t pumps_peers(peer_demand_t *out, size_t max)
{
    if (s_mtx == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t n = s_peer_count < max ? s_peer_count : max;
    memcpy(out, s_peer, n * sizeof(peer_demand_t));
    xSemaphoreGive(s_mtx);
    return n;
}

bool pumps_busy(void)
{
    if (s_mtx == NULL) {
        return false;
    }
    bool busy = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (uint8_t i = 0; i < s_circ_count; i++) {
        if (s_circ[i].st.reason == PUMP_REASON_MIN_RUN) {
            busy = true;
        }
    }
    xSemaphoreGive(s_mtx);
    return busy;
}
