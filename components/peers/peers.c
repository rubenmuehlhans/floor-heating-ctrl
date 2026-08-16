#include "peers.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"

static const char *TAG = "peers";

#define SERVICE_TYPE  "_fbhctrl"
#define SERVICE_PROTO "_tcp"
#define SERVICE_PORT  80

/* Erst kurz nach dem Start suchen, danach in Ruhe. Die Geschwistergeraete
 * wechseln ihre Adresse selten; haeufiger zu fragen belastet nur das Netz. */
#define FIRST_QUERY_MS  8000
#define QUERY_PERIOD_MS 60000
#define QUERY_TIMEOUT_MS 2500
#define PEER_STALE_MS   (10 * 60 * 1000)

static SemaphoreHandle_t s_mtx;
static peer_t s_peers[PEERS_MAX];
static size_t s_count;
static char s_own_id[24];
static char s_hostname[32];
static char s_site[32];
static bool s_running;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------ */

static void announce(void)
{
    mdns_hostname_set(s_hostname);
    mdns_instance_name_set(s_site[0] ? s_site : s_hostname);

    mdns_service_remove(SERVICE_TYPE, SERVICE_PROTO);

    mdns_txt_item_t txt[] = {
        {"id", s_own_id},
        {"site", s_site},
    };
    esp_err_t rc = mdns_service_add(NULL, SERVICE_TYPE, SERVICE_PROTO, SERVICE_PORT, txt,
                                    sizeof(txt) / sizeof(txt[0]));
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Dienst liess sich nicht anmelden: %s", esp_err_to_name(rc));
        return;
    }
    ESP_LOGI(TAG, "Angemeldet als \"%s\" unter %s.local", s_site, s_hostname);
}

static const char *txt_value(const mdns_result_t *r, const char *key)
{
    for (size_t i = 0; i < r->txt_count; i++) {
        if (r->txt[i].key && strcmp(r->txt[i].key, key) == 0) {
            return r->txt[i].value ? r->txt[i].value : "";
        }
    }
    return NULL;
}

static void store(const char *id, const char *site, const char *hostname, const char *ip)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    peer_t *slot = NULL;
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_peers[i].id, id) == 0) {
            slot = &s_peers[i];
            break;
        }
    }
    if (slot == NULL) {
        if (s_count < PEERS_MAX) {
            slot = &s_peers[s_count++];
        } else {
            xSemaphoreGive(s_mtx);
            return;
        }
        ESP_LOGI(TAG, "Neues Geraet gefunden: %s (%s) auf %s", site, id, ip);
    }

    snprintf(slot->id, sizeof(slot->id), "%s", id);
    snprintf(slot->site, sizeof(slot->site), "%s", site);
    snprintf(slot->hostname, sizeof(slot->hostname), "%s", hostname ? hostname : "");
    snprintf(slot->host, sizeof(slot->host), "%s", ip);
    slot->seen_ms = now_ms();

    xSemaphoreGive(s_mtx);
}

/* Entfernt Geraete, die sich lange nicht mehr gemeldet haben. */
static void forget_stale(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t t = now_ms();
    size_t out = 0;
    for (size_t i = 0; i < s_count; i++) {
        if (t - s_peers[i].seen_ms < PEER_STALE_MS) {
            if (out != i) {
                s_peers[out] = s_peers[i];
            }
            out++;
        } else {
            ESP_LOGI(TAG, "Geraet %s antwortet nicht mehr", s_peers[i].site);
        }
    }
    s_count = out;
    xSemaphoreGive(s_mtx);
}

static void query_once(void)
{
    mdns_result_t *results = NULL;
    esp_err_t rc = mdns_query_ptr(SERVICE_TYPE, SERVICE_PROTO, QUERY_TIMEOUT_MS, PEERS_MAX + 2,
                                  &results);
    if (rc != ESP_OK) {
        return;
    }

    for (mdns_result_t *r = results; r != NULL; r = r->next) {
        const char *id = txt_value(r, "id");
        if (id == NULL || id[0] == '\0') {
            continue;
        }
        if (strcmp(id, s_own_id) == 0) {
            continue; /* das eigene Geraet */
        }
        if (r->addr == NULL || r->addr->addr.type != ESP_IPADDR_TYPE_V4) {
            continue;
        }

        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));

        const char *site = txt_value(r, "site");
        store(id, site && site[0] ? site : (r->instance_name ? r->instance_name : id),
              r->hostname, ip);
    }

    mdns_query_results_free(results);
    forget_stale();
}

static void peers_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(FIRST_QUERY_MS));
    for (;;) {
        query_once();
        vTaskDelay(pdMS_TO_TICKS(QUERY_PERIOD_MS));
    }
}

/* ------------------------------------------------------------------ */

esp_err_t peers_start(const char *hostname, const char *site, const char *device_id)
{
    if (s_running) {
        peers_update_identity(hostname, site);
        return ESP_OK;
    }

    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(s_own_id, sizeof(s_own_id), "%s", device_id ? device_id : "");
    snprintf(s_hostname, sizeof(s_hostname), "%s", hostname && hostname[0] ? hostname
                                                                          : "floor-heating");
    snprintf(s_site, sizeof(s_site), "%s", site ? site : "");

    esp_err_t rc = mdns_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "mDNS liess sich nicht starten: %s", esp_err_to_name(rc));
        return rc;
    }
    announce();

    if (xTaskCreate(peers_task, "peers", 4096, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    return ESP_OK;
}

void peers_update_identity(const char *hostname, const char *site)
{
    if (!s_running) {
        return;
    }
    bool changed = false;
    if (hostname && hostname[0] && strcmp(hostname, s_hostname) != 0) {
        snprintf(s_hostname, sizeof(s_hostname), "%s", hostname);
        changed = true;
    }
    if (site && strcmp(site, s_site) != 0) {
        snprintf(s_site, sizeof(s_site), "%s", site);
        changed = true;
    }
    if (changed) {
        announce();
    }
}

size_t peers_get(peer_t *out, size_t max)
{
    if (s_mtx == NULL) {
        return 0;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    size_t n = s_count < max ? s_count : max;
    memcpy(out, s_peers, n * sizeof(peer_t));
    xSemaphoreGive(s_mtx);
    return n;
}
