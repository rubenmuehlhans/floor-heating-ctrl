// Funkmodul (ESP32-C6 über ESP-Hosted), WLAN und Uhrzeit — erfüllt den Netzteil von daten.h.
//
// Ein eigenes Netz, im NVS gespeichert erst NACH einer erfolgreichen Verbindung: ein falsch
// getipptes Kennwort soll nicht nach jedem Neustart wieder versucht werden (wie camperSense,
// wlan.h). Fällt die Verbindung weg, versucht die Tafel es alle zehn Sekunden erneut.
//
// ⚠️ Der C6 bekommt erst über die IO-Erweiterung Strom (BSP_FEATURE_WIFI); ESP-Hosted startet
// deshalb nicht vor app_main (sdkconfig.defaults).
#include <string.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include "bsp/esp-bsp.h"
#include "daten.h"
#include "eins.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "netz";

namespace daten {

namespace {

std::mutex s_mx;
Wlan s_w;
std::vector<Funknetz> s_netze;
std::atomic<bool> s_sucht{false}, s_bereit{false};
std::atomic<uint32_t> s_gen{1};
std::string s_neu_ssid, s_neu_psk;   // Netz, das gerade erstmals versucht wird
esp_timer_handle_t s_wieder;
bool s_sntp;

void geaendert() { s_gen++; }

void konfigurieren(const std::string &ssid, const std::string &psk)
{
    wifi_config_t wc = {};
    memcpy(wc.sta.ssid, ssid.data(), std::min(ssid.size(), sizeof wc.sta.ssid));
    memcpy(wc.sta.password, psk.data(), std::min(psk.size(), sizeof wc.sta.password));
    wc.sta.threshold.authmode = psk.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}

void wieder(void *) { esp_wifi_connect(); }

void ereignis(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto *d = (wifi_event_sta_disconnected_t *)data;
        std::lock_guard<std::mutex> l(s_mx);
        bool neu = !s_neu_ssid.empty();
        s_w.ip.clear();
        if (neu && (d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT || d->reason == WIFI_REASON_802_1X_AUTH_FAILED)) {
            s_w.zustand = Netz::fehler;
            s_w.grund = "Das Kennwort passt nicht.";
            s_neu_ssid.clear();
            s_neu_psk.assign(s_neu_psk.size(), '\0');
            // zurück zum gespeicherten Netz, falls es eines gibt
            std::string ssid = eins::text(eins::k::WLAN_SSID), psk = eins::text(eins::k::WLAN_PSK);
            if (!ssid.empty()) {
                konfigurieren(ssid, psk);
                esp_timer_start_once(s_wieder, 3000000);
            }
        } else {
            s_w.zustand = Netz::verbindet;
            s_w.grund = d->reason == WIFI_REASON_NO_AP_FOUND ? "Das Netz ist nicht in Reichweite." : "";
            esp_timer_stop(s_wieder);
            esp_timer_start_once(s_wieder, 10000000);
        }
        geaendert();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = (ip_event_got_ip_t *)data;
        char ip[20];
        snprintf(ip, sizeof ip, IPSTR, IP2STR(&e->ip_info.ip));
        wifi_ap_record_t ap = {};
        esp_wifi_sta_get_ap_info(&ap);
        {
            std::lock_guard<std::mutex> l(s_mx);
            s_w.zustand = Netz::verbunden;
            s_w.grund.clear();
            s_w.ip = ip;
            s_w.ssid = (const char *)ap.ssid;
            s_w.rssi = ap.rssi;
            if (!s_neu_ssid.empty()) {
                // ⚠️ Erst jetzt speichern: die Verbindung ist belegt
                eins::text_setzen(eins::k::WLAN_SSID, s_neu_ssid);
                eins::text_setzen(eins::k::WLAN_PSK, s_neu_psk);
                s_neu_ssid.clear();
                s_neu_psk.assign(s_neu_psk.size(), '\0');
            }
        }
        ESP_LOGI(TAG, "verbunden mit %s, %s, %d dBm", (const char *)ap.ssid, ip, ap.rssi);
        if (!s_sntp) {
            s_sntp = true;
            esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&c);
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
            tzset();
        }
        geaendert();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        std::vector<wifi_ap_record_t> recs(n);
        if (n) esp_wifi_scan_get_ap_records(&n, recs.data());
        std::vector<Funknetz> l;
        for (const auto &r : recs) {
            std::string ssid = (const char *)r.ssid;
            if (ssid.empty()) continue;
            bool da = false;
            for (auto &x : l)
                if (x.ssid == ssid) {
                    da = true;
                    if (r.rssi > x.rssi) x.rssi = r.rssi;
                }
            if (!da) l.push_back({ssid, r.rssi, r.authmode == WIFI_AUTH_OPEN});
        }
        std::sort(l.begin(), l.end(), [](const Funknetz &a, const Funknetz &b) { return a.rssi > b.rssi; });
        {
            std::lock_guard<std::mutex> g(s_mx);
            s_netze = std::move(l);
        }
        s_sucht = false;
        geaendert();
    }
}

}  // namespace

// Funkmodul einschalten und verbinden. false = der C6 meldet sich nicht.
bool funk_starten()
{
    bsp_feature_enable(BSP_FEATURE_WIFI, true);
    vTaskDelay(pdMS_TO_TICKS(200));
    int r1 = esp_hosted_init();
    int r2 = r1 == 0 ? esp_hosted_connect_to_slave() : -1;
    if (r1 || r2) {
        ESP_LOGE(TAG, "Funkmodul: init %d, connect %d", r1, r2);
        return false;
    }
    return true;
}

void wlan_start()
{
    esp_timer_create_args_t ta = {};
    ta.callback = wieder;
    ta.name = "wlan_wieder";
    esp_timer_create(&ta, &s_wieder);
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init gescheitert");
        return;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ereignis, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ereignis, nullptr);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);   // Zugang liegt in unserem NVS, nicht doppelt
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start gescheitert");
        return;
    }
    s_bereit = true;
    std::string ssid = eins::text(eins::k::WLAN_SSID), psk = eins::text(eins::k::WLAN_PSK);
    {
        std::lock_guard<std::mutex> l(s_mx);
        s_w.ssid = ssid;
        s_w.zustand = ssid.empty() ? Netz::aus : Netz::verbindet;
    }
    if (!ssid.empty()) {
        konfigurieren(ssid, psk);
        esp_wifi_connect();
    } else wlan_suchen();
    psk.assign(psk.size(), '\0');
    geaendert();
}

Wlan wlan()
{
    std::lock_guard<std::mutex> l(s_mx);
    Wlan w = s_w;
    if (w.zustand == Netz::verbunden) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) w.rssi = ap.rssi;
    }
    return w;
}

void wlan_suchen()
{
    if (!s_bereit || s_sucht) return;
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    if (esp_wifi_scan_start(&sc, false) == ESP_OK) s_sucht = true;
    geaendert();
}

bool wlan_sucht() { return s_sucht; }

std::vector<Funknetz> wlan_netze()
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_netze;
}

void wlan_verbinden(const std::string &ssid, const std::string &kennwort)
{
    if (!s_bereit) return;
    {
        std::lock_guard<std::mutex> l(s_mx);
        s_neu_ssid = ssid;
        s_neu_psk = kennwort;
        s_w.ssid = ssid;
        s_w.zustand = Netz::verbindet;
        s_w.grund.clear();
    }
    esp_timer_stop(s_wieder);
    esp_wifi_disconnect();
    konfigurieren(ssid, kennwort);
    esp_wifi_connect();
    geaendert();
}

void wlan_vergessen()
{
    eins::loeschen(eins::k::WLAN_SSID);
    eins::loeschen(eins::k::WLAN_PSK);
    {
        std::lock_guard<std::mutex> l(s_mx);
        s_w = Wlan{};
    }
    esp_timer_stop(s_wieder);
    esp_wifi_disconnect();
    geaendert();
}

uint32_t wlan_generation() { return s_gen; }

}  // namespace daten
