/*
 * Leitstand: Funkempfang, Anzeige, Protokoll und -- auf dem Core2 mit
 * PSRAM -- HomeKit fuer die Fussbodenheizung.
 *
 * Der Leitstand regelt nichts. Er fragt Heizungsgeraete und Verteiler ab,
 * empfaengt den Aussenfuehler ueber Bluetooth und reicht dessen Wert an die
 * Heizungsgeraete weiter. Siehe docs/konzept-leitstand.md.
 */

#include <string.h>

#include "atc_ble.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "netmgr.h"
#include "peers.h"
#include "st_config.h"
#include "st_hap.h"
#include "st_log.h"
#include "st_poll.h"
#include "st_ui.h"
#include "st_web.h"

static const char *TAG = "app";

static void on_ble_measurement(const atc_device_t *dev, void *ctx)
{
    (void)ctx;
    st_poll_ble(dev);
}

void app_main(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Leitstand, Version %s (%s %s)", desc->version, desc->date, desc->time);

    ESP_ERROR_CHECK(st_cfg_init());
    st_poll_init();

    /* Die Anzeige zuerst: Sie zeigt den Start und, solange kein WLAN
     * eingerichtet ist, wie die Einrichtung geht. Ohne Anzeige laeuft der
     * Rest trotzdem. */
    if (st_ui_start() != ESP_OK) {
        ESP_LOGW(TAG, "Anzeige nicht verfuegbar");
    }
    /* Die Karte haengt am Bus der Anzeige und kommt deshalb danach. Fehlt
     * sie, versucht es das Protokoll jede Minute erneut. */
    ESP_ERROR_CHECK(st_log_start());

    st_config_t cfg;
    st_cfg_copy(&cfg);
    netmgr_cfg_t netz;
    st_cfg_netmgr(&cfg, &netz);
    ESP_ERROR_CHECK(netmgr_start(&netz));

    char id[24];
    st_device_id(id, sizeof(id));
    if (peers_start(cfg.wifi.hostname, cfg.site, id, PEERS_ROLE_STATION) != ESP_OK) {
        ESP_LOGW(TAG, "Suche nach den Geraeten der Anlage nicht verfuegbar");
    }
    ESP_ERROR_CHECK(st_poll_start());

    /* Schluessel im eigenen Namensraum; der Verteiler nutzt "fbh". Bluetooth
     * und WLAN teilen sich den Funkteil: Solange nur der
     * Einrichtungs-Zugangspunkt da ist, gehoert er dem WLAN. */
    ESP_ERROR_CHECK(atc_ble_keys_init("station"));
    netmgr_set_setup_hook(atc_ble_pause);
    ESP_ERROR_CHECK(atc_ble_start(on_ble_measurement, NULL));

    ESP_ERROR_CHECK(st_web_start());

    /* HomeKit nach peers, das mDNS schon gestartet hat; ohne PSRAM (Core
     * Basic) bleibt es aus, ein Fehler haelt den Rest nicht auf. */
    if (st_hap_start() != ESP_OK) {
        ESP_LOGW(TAG, "HomeKit nicht verfuegbar");
    }

    memset(&cfg, 0, sizeof(cfg));
    memset(&netz, 0, sizeof(netz));

    /* Eine frisch eingespielte Firmware gilt erst nach diesem Punkt. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "Neue Firmware bestaetigt, kein Ruecksprung");
        }
    }
    ESP_LOGI(TAG, "Start abgeschlossen");
}
