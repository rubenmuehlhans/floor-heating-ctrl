/*
 * Einstellungen des Leitstands.
 *
 * Wie in den anderen Anwendungen ein einziges JSON unter einem NVS-Schluessel
 * (siehe cfgjson): geladen wird als Vorgabe mit Ueberlagerung, sodass ein neu
 * hinzugekommenes Feld seinen Vorgabewert behaelt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "netmgr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    char pass[64];
    char hostname[32];
    char ap_pass[64];
    char timezone[48];
} st_wifi_t;

typedef struct {
    st_wifi_t wifi;
    char site[32];
    /* Zugeordneter Aussenfuehler als MAC-Adresse, leer = keiner */
    char outdoor_mac[18];
    /* Abfragetakt in Sekunden */
    uint16_t poll_heat_s;
    uint16_t poll_manifold_s;
    /* Anzeige: Helligkeit 10..255; Abdunkeln nach so vielen Minuten ohne
     * Bedienung, 0 = nie; nachts aus von/bis zur vollen Stunde, von < 0 = nie */
    uint8_t brightness;
    uint8_t dim_after_min;
    int8_t night_from_h;
    int8_t night_to_h;
} st_config_t;

esp_err_t st_cfg_init(void);
void st_cfg_copy(st_config_t *out);

/* JSON fuer die Oberflaeche, ohne Kennwoerter. Der Aufrufer gibt es mit
 * free() zurueck. */
char *st_cfg_to_json(const st_config_t *cfg);

/* Ueberlagert `cfg` mit dem JSON-Text und prueft das Ergebnis. Fehlende Felder
 * bleiben; ein leeres Kennwort laesst das hinterlegte stehen. */
esp_err_t st_cfg_from_json(const char *json, st_config_t *cfg, char *err, size_t err_len);

esp_err_t st_cfg_set(const st_config_t *cfg);
esp_err_t st_cfg_reset(void);
void st_cfg_netmgr(const st_config_t *cfg, netmgr_cfg_t *out);

#ifdef __cplusplus
}
#endif
