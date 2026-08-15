/*
 * Gemeinsamer I2C-Bus fuer Anzeige (SSD1327) und Klimasensor (HDC1080).
 *
 * Beide haengen am selben Bus; der Handle wird beim ersten Zugriff angelegt.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Liefert den Bus-Handle und richtet ihn beim ersten Aufruf ein.
 * NULL, wenn der Bus nicht verfuegbar ist. */
i2c_master_bus_handle_t i2cbus_get(void);

/* Listet alle antwortenden Adressen im Protokoll auf - hilft beim Suchen
 * einer fehlenden Anzeige oder eines nicht bestueckten Sensors. */
void i2cbus_scan(void);

/* Prueft, ob ein Geraet auf die eigene Adresse antwortet. */
bool i2cbus_probe(uint8_t address);

#ifdef __cplusplus
}
#endif
