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

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Was dieser Baustein von der Konfiguration braucht -- nicht mehr.
 *
 * Frueher nahm er die vollstaendige app_config_t der Verteilerplatine
 * entgegen und schleppte damit deren Ventilkanaele in jede Anwendung, die
 * WLAN benutzt. Jede Anwendung fuellt jetzt diese Struktur aus ihrem eigenen
 * Schema; das Schema selbst geht den Netzbaustein nichts an.
 */
typedef struct {
    char ssid[33];
    char pass[64];
    char hostname[32];
    char ap_pass[64];
    char timezone[48];
    int8_t reboot_hour;   /* < 0 = kein taeglicher Neustart */
    int8_t reboot_minute;
} netmgr_cfg_t;

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

esp_err_t netmgr_start(const netmgr_cfg_t *cfg);

/* Sperre fuer den taeglichen Neustart, etwa waehrend eine Ventilfahrt laeuft. */
void netmgr_set_reboot_guard(netmgr_busy_fn_t fn);

/* Uebernimmt geaenderte WLAN-Zugangsdaten ohne Neustart. */
esp_err_t netmgr_apply(const netmgr_cfg_t *cfg);

void netmgr_status(netmgr_status_t *out);

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} netmgr_ap_t;

/* Sucht erreichbare Netze. Blockiert einige Sekunden und ist deshalb nur fuer
 * die Einrichtung gedacht. */
/*
 * Suchlauf nach erreichbaren Netzen.
 *
 * Er laeuft nebenlaeufig, weil das Funkteil dabei die Kanaele durchgeht und
 * der eigene Zugangspunkt so lange nicht erreichbar ist. Ein blockierender
 * Suchlauf laesst genau die Anfrage scheitern, ueber die er angestossen wurde
 * -- und im Zugangspunkt-Betrieb ist das der einzige Weg zum Geraet.
 */
esp_err_t netmgr_scan_start(void);
bool netmgr_scan_running(void);

/* Ergebnis des letzten Suchlaufs. */
size_t netmgr_scan_result(netmgr_ap_t *out, size_t max);

/* Zeitzone setzen und SNTP starten. Wird von netmgr_start miterledigt. */
void netmgr_set_timezone(const char *tz);

#ifdef __cplusplus
}
#endif
