/*
 * Ventilsteuerung Fussbodenheizung Erdgeschoss.
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
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netmgr.h"
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
    ESP_LOGI(TAG, "Ventilsteuerung EG, Version %s (%s %s)", desc->version, desc->date, desc->time);

    ESP_ERROR_CHECK(cfg_init());

    /* Erst die Ausgangsstufe: solange sie nicht steht, darf kein Motor
     * anlaufen. */
    ESP_ERROR_CHECK(sr595_init());
    ESP_ERROR_CHECK(sr595_all_off());

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

    app_config_t cfg;
    cfg_copy(&cfg);
    ESP_ERROR_CHECK(netmgr_start(&cfg));
    netmgr_set_reboot_guard(control_busy);
    ESP_ERROR_CHECK(atc_ble_start(on_ble_measurement, NULL));
    ESP_ERROR_CHECK(web_start());
    ESP_ERROR_CHECK(mqtt_start());

    ESP_LOGI(TAG, "Start abgeschlossen");
}
