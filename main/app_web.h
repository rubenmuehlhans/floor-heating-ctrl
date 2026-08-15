/*
 * Weboberflaeche und JSON-Schnittstelle.
 *
 * Die Oberflaeche liegt komprimiert im Programmspeicher und laedt ohne
 * Internetzugang. Ueber sie werden Raeume, Kanalzuordnung, Regelparameter und
 * die Kalibrierung eingerichtet; ausserdem laeuft die Firmware-Aktualisierung
 * darueber.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_start(void);

#ifdef __cplusplus
}
#endif
