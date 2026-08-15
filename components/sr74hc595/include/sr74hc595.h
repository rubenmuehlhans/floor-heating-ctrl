/*
 * Ausgabestufe: drei kaskadierte SN74HC595 an SPI2, dahinter je Kanal ein
 * L9110s-Motortreiber.
 *
 * Die Kette wird immer vollstaendig geschrieben. Der Latch-Pin liegt am
 * Chip-Select der SPI-Einheit: waehrend der Uebertragung low, danach die
 * steigende Flanke, die die Schieberegister in die Ausgaenge uebernimmt.
 */
#pragma once

#include "esp_err.h"
#include "hw_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Richtet SPI2 ein, schaltet den Ausgangstreiber frei und setzt alle
 * Ausgaenge auf 0. */
esp_err_t sr595_init(void);

/* Setzt die Fahrtrichtung eines Kanals (1..11). Verriegelt IA gegen IB und
 * schreibt die gesamte Kette neu. */
esp_err_t sr595_set_drive(uint8_t channel, hw_drive_t drive);

/* Aktuelle Fahrtrichtung eines Kanals. */
hw_drive_t sr595_get_drive(uint8_t channel);

/* Schaltet alle Kanaele ab. Wird beim Start und bei Fehlern verwendet. */
esp_err_t sr595_all_off(void);

/* Rohzustand der 24 Ausgaenge - nur fuer Diagnose und Selbsttest. */
uint32_t sr595_raw_bits(void);

/* Schreibt die 24 Ausgaenge direkt. Umgeht die Verriegelung und ist daher
 * ausschliesslich fuer den Hardware-Selbsttest gedacht. */
esp_err_t sr595_write_raw(uint32_t bits);

#ifdef __cplusplus
}
#endif
