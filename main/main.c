/*
 * Ventilsteuerung Fussbodenheizung.
 *
 * Portierung des ESPHome-Aufbaus floor-heating-ctrl-groundfloor.yaml auf
 * natives ESP-IDF. Raeume, Kanalzuordnung und Regelparameter sind zur Laufzeit
 * ueber die Weboberflaeche aenderbar.
 */

#include "app_control.h"
#include "app_mqtt.h"
#include "app_ui.h"
#include "app_web.h"
#include "atc_ble.h"
#include "bemf.h"
#include "config_store.h"
#include "i2cbus.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "netmgr.h"
#include "peers.h"
#include "sensors_local.h"
#include "sr74hc595.h"

static const char *TAG = "app";

/* Jeder empfangene Messwert wird der Regelung angeboten; sie entscheidet
 * anhand der zugeordneten MAC-Adresse, ob er zu einem Raum gehoert. */
static void on_ble_measurement(const atc_device_t *dev, void *ctx)
{
    (void)ctx;
    control_temperature(dev->mac, dev->temp_c, dev->humidity, dev->battery);
}

void app_main(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Ventilsteuerung Fussbodenheizung, Version %s (%s %s)", desc->version,
             desc->date, desc->time);

    ESP_ERROR_CHECK(cfg_init());

    /* Erst die Ausgangsstufe: solange sie nicht steht, darf kein Motor
     * anlaufen. */
    ESP_ERROR_CHECK(sr595_init());
    ESP_ERROR_CHECK(sr595_all_off());

    /* Zeigt beim Start, was am Bus haengt - eine fehlende Anzeige ist sonst
     * nicht von einer falschen Adresse zu unterscheiden. */
    i2cbus_scan();

    ESP_ERROR_CHECK(bemf_start());
    ESP_ERROR_CHECK(control_start());

    /* Fehlende Anzeige oder Fuehler duerfen den Betrieb nicht verhindern -
     * die Regelung ist die Hauptaufgabe. */
    if (sensors_start() != ESP_OK) {
        ESP_LOGW(TAG, "Zusatzsensoren nicht verfuegbar");
    }
    if (ui_start() != ESP_OK) {
        ESP_LOGW(TAG, "Bedienung am Geraet nicht verfuegbar");
    }

    static app_config_t cfg;
    cfg_copy(&cfg);
    ESP_ERROR_CHECK(netmgr_start(&cfg));
    netmgr_set_reboot_guard(control_busy);
    ESP_ERROR_CHECK(atc_ble_start(on_ble_measurement, NULL));

    /* Geschwistergeraete im Haus: je Etage eine Platine. Sie melden sich
     * gegenseitig an, damit die Oberflaeche einen Wechsel anbieten kann. */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char device_id[24];
    snprintf(device_id, sizeof(device_id), "fbh_%02x%02x%02x", mac[3], mac[4], mac[5]);
    if (peers_start(cfg.wifi.hostname, cfg.site, device_id) != ESP_OK) {
        ESP_LOGW(TAG, "Suche nach weiteren Geraeten nicht verfuegbar");
    }
    ESP_ERROR_CHECK(web_start());
    ESP_ERROR_CHECK(mqtt_start());

    /*
     * Der Bootlader haelt eine frisch eingespielte Firmware zunaechst nur auf
     * Probe. Ohne diese Bestaetigung wuerde er beim naechsten Neustart auf die
     * vorherige zurueckrollen. Sie steht bewusst am Ende: bis hierher sind
     * Konfiguration, Ausgangsstufe, Regelung und Weboberflaeche angelaufen.
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
