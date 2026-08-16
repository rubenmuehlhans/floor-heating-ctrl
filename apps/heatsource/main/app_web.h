/*
 * Weboberflaeche und JSON-Schnittstelle des Heizungsgeraets.
 *
 * Die Oberflaeche liegt komprimiert im Programmabbild und laedt ohne
 * Internetzugang. Im Zugangspunkt-Betrieb leitet der Server jede unbekannte
 * Adresse auf die Einrichtungsseite um, sodass sich das Anmeldefenster von
 * selbst oeffnet.
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
