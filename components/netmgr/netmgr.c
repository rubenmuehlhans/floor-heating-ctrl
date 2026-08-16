#include "netmgr.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "captive_dns.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "net";

#define STA_FALLBACK_AFTER_MS 30000
#define STA_RETRY_DELAY_MS    5000

static esp_netif_t *s_netif_sta;
static esp_netif_t *s_netif_ap;
static netmgr_status_t s_status;
static bool s_started;
static bool s_sntp_running;
static uint32_t s_connect_attempts;
static netmgr_busy_fn_t s_reboot_guard;

/* Eigene Kopie der Angaben. Der taegliche Neustart und die Aufsicht ueber die
 * Verbindung laufen in eigenen Aufgaben und duerfen nicht auf die
 * Konfiguration der Anwendung zugreifen. */
static netmgr_cfg_t s_cfg_copy;

static void start_ap_fallback(const netmgr_cfg_t *cfg);

/* Kopiert eine Zeichenkette in ein Feld fester Groesse und schneidet dabei
 * sauber ab. snprintf wuerde hier nur Abschneidewarnungen erzeugen. */
static size_t copy_str(void *dst, size_t dst_size, const char *src)
{
    size_t n = strlen(src);
    if (n > dst_size - 1) {
        n = dst_size - 1;
    }
    memcpy(dst, src, n);
    ((char *)dst)[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *ev = data;
        s_status.sta_connected = false;
        s_status.ip[0] = '\0';
        s_connect_attempts++;
        /* Der neue Versuch laeuft ueber die Aufsichtstask - im Ereignisumlauf
         * darf nicht gewartet werden, sonst stehen alle anderen Ereignisse. */
        ESP_LOGW(TAG, "WLAN getrennt (Grund %d), Versuch %lu", ev->reason,
                 (unsigned long)s_connect_attempts);
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "Ein Geraet hat sich mit dem Einrichtungs-Zugangspunkt verbunden");
        break;

    default:
        break;
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;

    ip_event_got_ip_t *ev = data;
    snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ev->ip_info.ip));
    s_status.sta_connected = true;
    s_connect_attempts = 0;
    ESP_LOGI(TAG, "WLAN verbunden, Adresse %s", s_status.ip);

    if (!s_sntp_running) {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
            3, ESP_SNTP_SERVER_LIST("0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"));
        sntp.start = true;
        sntp.sync_cb = NULL;
        if (esp_netif_sntp_init(&sntp) == ESP_OK) {
            s_sntp_running = true;
        }
    }
}

/* ------------------------------------------------------------------ */

void netmgr_set_timezone(const char *tz)
{
    if (tz && tz[0]) {
        setenv("TZ", tz, 1);
        tzset();
    }
}

/*
 * Zugangsdaten der Station uebernehmen.
 *
 * Kein ESP_ERROR_CHECK: laeuft gerade ein Verbindungsversuch, weist der
 * Treiber die neue Beschreibung mit ESP_ERR_WIFI_STATE zurueck. Ein Abbruch
 * waere hier die schlechteste Antwort - das Geraet startet sonst mitten im
 * Speichern von Einstellungen neu.
 */
static esp_err_t apply_sta_config(const netmgr_cfg_t *cfg)
{
    wifi_config_t sta = {0};
    copy_str(sta.sta.ssid, sizeof(sta.sta.ssid), cfg->ssid);
    copy_str(sta.sta.password, sizeof(sta.sta.password), cfg->pass);
    /* Netze koennen versteckt sein - ohne vollstaendige Suche werden sie
     * nicht gefunden. */
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t rc = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (rc == ESP_ERR_WIFI_STATE) {
        /* Verbindungsversuch laeuft: abbrechen und einmal wiederholen. */
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        rc = esp_wifi_set_config(WIFI_IF_STA, &sta);
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "WLAN-Zugangsdaten nicht uebernommen: %s", esp_err_to_name(rc));
    }
    return rc;
}

/* Baut die Beschreibung des Einrichtungs-Zugangspunkts. Der Name traegt die
 * letzten beiden Bytes der MAC, damit mehrere Geraete unterscheidbar sind. */
static void build_ap_config(const netmgr_cfg_t *cfg, wifi_config_t *out)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    char ssid[33];
    snprintf(ssid, sizeof(ssid), "%.24s-%02X%02X",
             cfg->hostname[0] ? cfg->hostname : "floor-heating", mac[4], mac[5]);

    memset(out, 0, sizeof(*out));
    out->ap.ssid_len = (uint8_t)copy_str(out->ap.ssid, sizeof(out->ap.ssid), ssid);
    out->ap.channel = 1;
    out->ap.max_connection = 4;
    out->ap.beacon_interval = 100;
    if (cfg->ap_pass[0] && strlen(cfg->ap_pass) >= 8) {
        copy_str(out->ap.password, sizeof(out->ap.password), cfg->ap_pass);
        out->ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        out->ap.authmode = WIFI_AUTH_OPEN;
    }
}

/*
 * Zugangspunkt einschalten.
 *
 * Die Betriebsart darf nicht bei laufendem WLAN umgeschaltet werden: der
 * Zugangspunkt geht dann mit noch leerem Netznamen auf Sendung, und die
 * anschliessend gesetzte Beschreibung erreicht das Funkteil nicht mehr
 * zuverlaessig - er ist dann fuer Geraete in Reichweite schlicht nicht zu
 * finden. Deshalb anhalten, beides setzen, neu starten.
 */
static void start_ap_fallback(const netmgr_cfg_t *cfg)
{
    if (s_status.ap_active) {
        return;
    }

    wifi_config_t ap;
    build_ap_config(cfg, &ap);

    wifi_config_t sta = {0};
    copy_str(sta.sta.ssid, sizeof(sta.sta.ssid), cfg->ssid);
    copy_str(sta.sta.password, sizeof(sta.sta.password), cfg->pass);
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t rc = esp_wifi_stop();
    if (rc == ESP_OK) {
        rc = esp_wifi_set_mode(WIFI_MODE_APSTA);
    }
    if (rc == ESP_OK) {
        rc = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (rc == ESP_OK) {
        rc = esp_wifi_set_config(WIFI_IF_STA, &sta);
    }
    if (rc == ESP_OK) {
        rc = esp_wifi_start();
    }
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Zugangspunkt nicht startbar: %s", esp_err_to_name(rc));
        return;
    }

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif_ap, &info) == ESP_OK) {
        snprintf(s_status.ap_ip, sizeof(s_status.ap_ip), IPSTR, IP2STR(&info.ip));
        /* Namensdienst des Captive Portals: jede Anfrage bekommt diese
         * Adresse, damit sich die Einrichtungsseite von selbst oeffnet. */
        captive_dns_start(info.ip.addr);
    }
    s_status.ap_active = true;
    ESP_LOGW(TAG, "Einrichtungs-Zugangspunkt \"%s\" ist offen, Adresse %s", ap.ap.ssid,
             s_status.ap_ip);
}

/*
 * Steht die WLAN-Verbindung, wird der Einrichtungs-Zugangspunkt wieder
 * geschlossen - aber erst, wenn niemand mehr daran haengt. Sonst bricht die
 * Verbindung mitten in der Einrichtung ab.
 */
static void stop_ap_when_unused(void)
{
    if (!s_status.ap_active) {
        return;
    }

    wifi_sta_list_t clients;
    if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK && clients.num > 0) {
        return;
    }

    captive_dns_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_status.ap_active = false;
    s_status.ap_ip[0] = '\0';
    ESP_LOGI(TAG, "Einrichtungs-Zugangspunkt geschlossen, WLAN-Verbindung steht");
}

/*
 * Taeglicher Neustart zur eingestellten Uhrzeit. Im ESPHome-Aufbau lag er auf
 * 10:00 Uhr. Ein laufender Stellantrieb wird abgewartet - ein Neustart mitten
 * in einer Fahrt liesse das Ventil in unbekannter Stellung zurueck.
 */
static void check_daily_reboot(void)
{
    static int last_minute = -1;

    if (!s_status.time_valid) {
        return;
    }
    if (s_reboot_guard && s_reboot_guard()) {
        return; /* es faehrt gerade ein Ventil */
    }

    int hour = s_cfg_copy.reboot_hour;
    int minute = s_cfg_copy.reboot_minute;

    if (hour < 0) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (tm_now.tm_hour != hour || tm_now.tm_min != minute) {
        return;
    }
    if (tm_now.tm_min == last_minute) {
        return;
    }
    last_minute = tm_now.tm_min;

    ESP_LOGW(TAG, "Taeglicher Neustart um %02d:%02d", hour, minute);
    esp_restart();
}

/* Beobachtet die Verbindung und oeffnet bei Bedarf den Zugangspunkt. */
static void supervisor_task(void *arg)
{
    netmgr_cfg_t *cfg = arg;
    TickType_t start = xTaskGetTickCount();
    int32_t retry_countdown = STA_RETRY_DELAY_MS;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (s_status.sta_connected) {
            retry_countdown = STA_RETRY_DELAY_MS;
            stop_ap_when_unused();
            wifi_ap_record_t rec;
            if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
                s_status.rssi = rec.rssi;
            }
            time_t now = time(NULL);
            s_status.time_valid = now > 1700000000; /* plausibel ab 2023 */
            check_daily_reboot();
            continue;
        }

        /* Erneut verbinden, mit Abstand zwischen den Versuchen. */
        retry_countdown -= 2000;
        if (retry_countdown <= 0 && cfg->ssid[0] != '\0') {
            retry_countdown = STA_RETRY_DELAY_MS;
            esp_wifi_connect();
        }

        uint32_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed > STA_FALLBACK_AFTER_MS || cfg->ssid[0] == '\0') {
            start_ap_fallback(cfg);
        }
    }
}

esp_err_t netmgr_start(const netmgr_cfg_t *cfg)
{
    if (s_started) {
        return netmgr_apply(cfg);
    }

    netmgr_set_timezone(cfg->timezone);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap = esp_netif_create_default_wifi_ap();

    if (cfg->hostname[0]) {
        esp_netif_set_hostname(s_netif_sta, cfg->hostname);
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip,
                                                        NULL, NULL));

    /*
     * Ohne hinterlegtes Netz wird der Zugangspunkt sofort mitgestartet - und
     * zwar vollstaendig beschrieben, bevor das Funkteil anlaeuft. Ein
     * Moduswechsel im laufenden Betrieb laesst ihn sonst mit leerem Netznamen
     * hochkommen.
     */
    bool need_ap = cfg->ssid[0] == '\0';

    ESP_ERROR_CHECK(esp_wifi_set_mode(need_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    apply_sta_config(cfg);

    wifi_config_t ap;
    if (need_ap) {
        build_ap_config(cfg, &ap);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Erst nach dem Start wirksam: BLE und WLAN teilen sich die Funkstufe. */
    esp_wifi_set_max_tx_power(68);

    if (need_ap) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(s_netif_ap, &info) == ESP_OK) {
            snprintf(s_status.ap_ip, sizeof(s_status.ap_ip), IPSTR, IP2STR(&info.ip));
            captive_dns_start(info.ip.addr);
        }
        s_status.ap_active = true;
        ESP_LOGW(TAG, "Kein WLAN hinterlegt - Einrichtungs-Zugangspunkt \"%s\" ist offen, "
                      "Adresse %s", ap.ap.ssid, s_status.ap_ip);
    }

    s_cfg_copy = *cfg;
    xTaskCreate(supervisor_task, "net_sup", 3072, &s_cfg_copy, 4, NULL);

    s_started = true;
    return ESP_OK;
}

esp_err_t netmgr_apply(const netmgr_cfg_t *cfg)
{
    /* Auch die Aufsicht und der taegliche Neustart arbeiten mit dieser Kopie;
     * ohne die Zuweisung liefen sie mit dem Stand vom Start weiter. */
    s_cfg_copy = *cfg;

    netmgr_set_timezone(cfg->timezone);
    if (cfg->hostname[0]) {
        esp_netif_set_hostname(s_netif_sta, cfg->hostname);
    }

    /* Erst trennen, dann beschreiben, dann verbinden - in dieser Reihenfolge
     * ist der Treiber aufnahmebereit. */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t rc = apply_sta_config(cfg);
    if (rc != ESP_OK) {
        return rc;
    }
    if (cfg->ssid[0] == '\0') {
        return ESP_OK; /* nichts hinterlegt, der Zugangspunkt uebernimmt */
    }
    rc = esp_wifi_connect();
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Verbindungsversuch nicht startbar: %s", esp_err_to_name(rc));
    }
    return rc;
}

void netmgr_status(netmgr_status_t *out)
{
    *out = s_status;
}

void netmgr_set_reboot_guard(netmgr_busy_fn_t fn)
{
    s_reboot_guard = fn;
}

size_t netmgr_scan(netmgr_ap_t *out, size_t max)
{
    wifi_scan_config_t scan = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        return 0;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) {
        return 0;
    }
    if (found > 32) {
        found = 32;
    }

    wifi_ap_record_t *records = calloc(found, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        esp_wifi_clear_ap_list();
        return 0;
    }
    esp_wifi_scan_get_ap_records(&found, records);

    size_t n = 0;
    for (uint16_t i = 0; i < found && n < max; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        /* Mehrfach empfangene Netze nur einmal auffuehren. */
        bool seen = false;
        for (size_t k = 0; k < n; k++) {
            if (strcmp(out[k].ssid, (const char *)records[i].ssid) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        copy_str(out[n].ssid, sizeof(out[n].ssid), (const char *)records[i].ssid);
        out[n].rssi = records[i].rssi;
        out[n].secure = records[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }

    free(records);
    return n;
}
