/*
 * Wärmeerzeuger: Messstellen an Ölkessel und Pufferspeicher.
 *
 * Dieselbe Firmware laeuft auf beiden Geraeten. Welche Aufgabe eines
 * uebernimmt, ergibt sich allein aus den zugeordneten Fuehlerrollen: ein
 * Geraet mit der Rolle "abgas" erkennt den Brennerlauf, eines mit "puffer"
 * beurteilt den Ladezustand. Einen Rollenschalter gibt es nicht.
 *
 * Umgesetzt sind Fuehlererfassung, Bedarfsabfrage bei den Verteilern und die
 * Pumpensteuerung ueber ein Tasmota-Relais. Brennererkennung und Ladezustand
 * folgen; siehe docs/konzept-waermeerzeuger.md.
 */

#include "app_pumps.h"
#include "app_sensors.h"
#include "app_web.h"
#include "config_store.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqttc.h"
#include "netmgr.h"
#include "peers.h"

static const char *TAG = "app";

void app_main(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Waermeerzeuger, Version %s (%s %s)", desc->version, desc->date, desc->time);

    ESP_ERROR_CHECK(cfg_init());

    /* Ohne Fuehler bleibt die Oberflaeche bedienbar: sonst liesse sich eine
     * falsche Busbelegung nicht mehr richtigstellen. */
    if (sensors_start() != ESP_OK) {
        ESP_LOGW(TAG, "Fuehlererfassung nicht verfuegbar");
    }

    static app_config_t cfg;
    cfg_copy(&cfg);
    ESP_ERROR_CHECK(netmgr_start(&cfg));

    /* Geschwistergeraete im Haus: drei Verteilerplatinen und das zweite
     * Heizungsgeraet. Sie melden sich gegenseitig an, damit die Oberflaeche
     * einen Wechsel anbieten kann und das Speicherboard spaeter weiss, wen es
     * nach dem Bedarf fragen muss. */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char device_id[24];
    snprintf(device_id, sizeof(device_id), "heiz_%02x%02x%02x", mac[3], mac[4], mac[5]);
    if (peers_start(cfg.wifi.hostname, cfg.site, device_id, PEERS_ROLE_HEAT) != ESP_OK) {
        ESP_LOGW(TAG, "Suche nach weiteren Geraeten nicht verfuegbar");
    }

    /* MQTT dient hier zwei Zwecken: dem Schalten des Relais und spaeter der
     * Anmeldung bei Home Assistant. Ohne Broker laeuft alles Uebrige weiter,
     * nur die Pumpen lassen sich dann nicht schalten. */
    if (cfg.mqtt.enabled && cfg.mqtt.uri[0]) {
        char lwt[80];
        snprintf(lwt, sizeof(lwt), "%s/status", cfg.mqtt.prefix);
        mqttc_cfg_t mc = {
            .uri = cfg.mqtt.uri,
            .user = cfg.mqtt.user,
            .pass = cfg.mqtt.pass,
            .client_id = device_id,
            .lwt_topic = lwt,
            .lwt_msg = "offline",
            .online_msg = "online",
        };
        if (mqttc_start(&mc, NULL, NULL) != ESP_OK) {
            ESP_LOGW(TAG, "MQTT liess sich nicht starten");
        }
    }

    if (pumps_start() != ESP_OK) {
        ESP_LOGW(TAG, "Pumpensteuerung nicht verfuegbar");
    }
    netmgr_set_reboot_guard(pumps_busy);

    ESP_ERROR_CHECK(web_start());

    /*
     * Der Bootlader haelt eine frisch eingespielte Firmware zunaechst nur auf
     * Probe. Ohne diese Bestaetigung wuerde er beim naechsten Neustart auf die
     * vorherige zurueckrollen. Sie steht am Ende: bis hierher sind
     * Konfiguration, Fuehlererfassung, Netz und Oberflaeche angelaufen.
     */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "Neue Firmware bestaetigt, kein Ruecksprung");
        }
    }

    ESP_LOGI(TAG, "Start abgeschlossen");
}
