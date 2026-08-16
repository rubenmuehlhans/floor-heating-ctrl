/*
 * Bedienung am Geraet: Anzeige und die drei kapazitiven Tasten.
 *
 * Die Anzeige richtet sich nach der Konfiguration - sie zeigt genau die Raeume,
 * die eingerichtet sind, und teilt die Breite unter ihnen auf.
 *
 * Tastenbelegung: die rechte Taste waehlt den Raum, die beiden anderen
 * verstellen dessen Solltemperatur um ein halbes Grad.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "hw_map.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_start(void);

/* Rohwerte der Tasten fuer den Abgleich der Schwellen in der Oberflaeche. */
void ui_touch_raw(uint32_t out[HW_TOUCH_COUNT]);

/* Nach einer Konfigurationsaenderung: Helligkeit und Schwellen uebernehmen. */
void ui_config_changed(void);

#ifdef __cplusplus
}
#endif
