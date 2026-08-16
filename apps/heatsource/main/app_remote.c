#include "app_remote.h"

#include <string.h>

#include "app_sensors.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "peers.h"

#define POLL_MS 5000
#define HTTP_TIMEOUT_MS 2000
/* Danach gilt ein Wert als veraltet und wird nicht mehr herausgegeben. */
#define STALE_S 120
#define MAX_REMOTE 4

typedef struct {
    bool valid;
    float temp_c;
    uint32_t seen_ms;
} rvalue_t;

static rvalue_t s_val[ROLE_COUNT];
static bool s_burner_known, s_burner_running;
static uint32_t s_burner_seen_ms;

static remote_peer_t s_peer[MAX_REMOTE];
static size_t s_peer_count;
static SemaphoreHandle_t s_mtx;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */

static remote_peer_t *peer_slot(const char *id)
{
    for (size_t i = 0; i < s_peer_count; i++) {
        if (strcmp(s_peer[i].id, id) == 0) {
            return &s_peer[i];
        }
    }
    if (s_peer_count >= MAX_REMOTE) {
        return NULL;
    }
    remote_peer_t *p = &s_peer[s_peer_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->id, sizeof(p->id), "%.*s", (int)sizeof(p->id) - 1, id);
    return p;
}

static bool fetch(const char *host, remote_peer_t *p)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s/api/measurements", host);

    esp_http_client_config_t hc = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (cl == NULL) {
        return false;
    }

    bool ok = false;
    /* Die Antwort traegt hoechstens vierzehn Messstellen und den
     * Brennerzustand; ein knapper Puffer genuegt. */
    static char body[768];
    if (esp_http_client_open(cl, 0) == ESP_OK) {
        esp_http_client_fetch_headers(cl);
        int n = esp_http_client_read(cl, body, sizeof(body) - 1);
        if (n > 0 && esp_http_client_get_status_code(cl) == 200) {
            body[n] = '\0';
            cJSON *root = cJSON_Parse(body);
            if (root != NULL) {
                uint32_t t = now_ms();
                const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "site");
                if (cJSON_IsString(v)) {
                    snprintf(p->site, sizeof(p->site), "%.*s", (int)sizeof(p->site) - 1,
                             v->valuestring);
                }

                uint8_t rollen = 0;
                const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "probes");
                const cJSON *e = NULL;
                cJSON_ArrayForEach(e, arr) {
                    const cJSON *rk = cJSON_GetObjectItemCaseSensitive(e, "role");
                    const cJSON *c = cJSON_GetObjectItemCaseSensitive(e, "c");
                    if (!cJSON_IsString(rk) || !cJSON_IsNumber(c)) {
                        continue;
                    }
                    probe_role_t r = cfg_role_from_key(rk->valuestring);
                    if (r == ROLE_NONE) {
                        continue;
                    }
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    s_val[r].valid = true;
                    s_val[r].temp_c = (float)c->valuedouble;
                    s_val[r].seen_ms = t;
                    xSemaphoreGive(s_mtx);
                    rollen++;
                }
                p->roles = rollen;

                const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "burner");
                if (cJSON_IsObject(b)) {
                    const cJSON *k = cJSON_GetObjectItemCaseSensitive(b, "known");
                    const cJSON *r = cJSON_GetObjectItemCaseSensitive(b, "running");
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    s_burner_known = cJSON_IsTrue(k);
                    s_burner_running = cJSON_IsTrue(r);
                    s_burner_seen_ms = t;
                    xSemaphoreGive(s_mtx);
                    p->burner_known = cJSON_IsTrue(k);
                    p->burner_running = cJSON_IsTrue(r);
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

static void remote_task(void *arg)
{
    (void)arg;
    static peer_t gefunden[PEERS_MAX];

    for (;;) {
        size_t n = peers_get(gefunden, PEERS_MAX);

        for (size_t i = 0; i < n; i++) {
            if (strcmp(gefunden[i].role, PEERS_ROLE_HEAT) != 0) {
                continue; /* Verteiler werden von app_pumps abgefragt */
            }
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            remote_peer_t *p = peer_slot(gefunden[i].id);
            xSemaphoreGive(s_mtx);
            if (p == NULL) {
                continue;
            }
            snprintf(p->host, sizeof(p->host), "%.*s", (int)sizeof(p->host) - 1,
                     gefunden[i].host);

            if (fetch(gefunden[i].host, p)) {
                xSemaphoreTake(s_mtx, portMAX_DELAY);
                p->seen = true;
                p->age_s = 0;
                xSemaphoreGive(s_mtx);
            } else {
                xSemaphoreTake(s_mtx, portMAX_DELAY);
                p->errors++;
                xSemaphoreGive(s_mtx);
            }
        }

        xSemaphoreTake(s_mtx, portMAX_DELAY);
        for (size_t i = 0; i < s_peer_count; i++) {
            if (s_peer[i].seen) {
                s_peer[i].age_s += POLL_MS / 1000;
            }
        }
        xSemaphoreGive(s_mtx);

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

/* ------------------------------------------------------------------ */

esp_err_t remote_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(remote_task, "remote", 5120, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void remote_set_value(probe_role_t role, float value_c)
{
    if (role <= ROLE_NONE || role >= ROLE_COUNT || s_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_val[role].valid = true;
    s_val[role].temp_c = value_c;
    s_val[role].seen_ms = now_ms();
    xSemaphoreGive(s_mtx);
}

bool remote_role_value(probe_role_t role, float *out_c, uint32_t *age_s)
{
    if (s_mtx == NULL || role <= ROLE_NONE || role >= ROLE_COUNT) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t alter = (now_ms() - s_val[role].seen_ms) / 1000;
    if (s_val[role].valid && alter <= STALE_S) {
        if (out_c) {
            *out_c = s_val[role].temp_c;
        }
        if (age_s) {
            *age_s = alter;
        }
        ok = true;
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

bool remote_burner(bool *running)
{
    if (s_mtx == NULL) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_burner_known && (now_ms() - s_burner_seen_ms) / 1000 <= STALE_S) {
        if (running) {
            *running = s_burner_running;
        }
        ok = true;
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

bool any_role_value(probe_role_t role, float *out_c, bool *own)
{
    if (sensors_role_value(role, out_c, NULL)) {
        if (own) {
            *own = true;
        }
        return true;
    }
    if (remote_role_value(role, out_c, NULL)) {
        if (own) {
            *own = false;
        }
        return true;
    }
    return false;
}

size_t remote_peers(remote_peer_t *out, size_t max)
{
    if (s_mtx == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t n = s_peer_count < max ? s_peer_count : max;
    memcpy(out, s_peer, n * sizeof(remote_peer_t));
    xSemaphoreGive(s_mtx);
    return n;
}
