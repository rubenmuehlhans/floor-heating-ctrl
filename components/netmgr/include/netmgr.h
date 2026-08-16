/*
 * WLAN, Zeit und taeglicher Neustart.
 *
 * Das Geraet verbindet sich als Station. Kommt innerhalb der Anlaufzeit keine
 * Verbindung zustande, wird zusaetzlich ein Zugangspunkt geoeffnet, damit die
 * Weboberflaeche zur Einrichtung erreichbar bleibt. Die Station versucht es im
 * Hintergrund weiter.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Achtung: Dieser Baustein nimmt app_config_t aus config_store.h entgegen,
 * benutzt daraus aber nur einen kleinen Ausschnitt:
 *
 *     cfg->wifi.ssid, .pass, .hostname, .ap_pass
 *     cfg->timezone
 *     cfg_peek()->reboot_hour, ->reboot_minute
 *     cfg_lock(), cfg_unlock(), cfg_peek()
 *
 * Eine Anwendung mit eigenem Konfigurationsschema stellt deshalb eine eigene
 * Komponente unter dem Namen config_store bereit, die genau diesen Ausschnitt
 * anbietet; ESP-IDF laesst sie die gemeinsame Fassung verdraengen. Wird der
 * Ausschnitt hier erweitert, muessen alle Fassungen nachziehen.
 */
#include "config_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool sta_connected;
    bool ap_active;
    char ip[16];
    char ap_ip[16];
    int8_t rssi;
    bool time_valid;
} netmgr_status_t;

/* Liefert true, solange ein Neustart verschoben werden soll. */
typedef bool (*netmgr_busy_fn_t)(void);

esp_err_t netmgr_start(const app_config_t *cfg);

/* Sperre fuer den taeglichen Neustart, etwa waehrend eine Ventilfahrt laeuft. */
void netmgr_set_reboot_guard(netmgr_busy_fn_t fn);

/* Uebernimmt geaenderte WLAN-Zugangsdaten ohne Neustart. */
esp_err_t netmgr_apply(const app_config_t *cfg);

void netmgr_status(netmgr_status_t *out);

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} netmgr_ap_t;

/* Sucht erreichbare Netze. Blockiert einige Sekunden und ist deshalb nur fuer
 * die Einrichtung gedacht. */
size_t netmgr_scan(netmgr_ap_t *out, size_t max);

/* Zeitzone setzen und SNTP starten. Wird von netmgr_start miterledigt. */
void netmgr_set_timezone(const char *tz);

#ifdef __cplusplus
}
#endif
