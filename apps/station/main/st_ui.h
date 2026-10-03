/*
 * Anzeige des Leitstands.
 *
 * Seiten nach docs/entwuerfe/leitstand/, bedient mit drei Feldern unter dem
 * Bildschirm: am Core Basic die Tasten, am Core2 die Beruehrungsfelder.
 * A blaettert zurueck, C vor, B lang gedrueckt schaltet die Anzeige aus.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t st_ui_start(void);

/* Auswahlleitung der SD-Karte am Bus der Anzeige, -1 ohne Steckplatz. Die
 * Sperre haelt die Anzeige an, waehrend die Karte eingerichtet wird. */
int st_ui_sd_cs(void);
void st_ui_bus_sperren(void);
void st_ui_bus_freigeben(void);

/* Bezeichnung des erkannten Geraets, etwa "M5Stack Core" */
const char *st_ui_board(void);
bool st_ui_psram(void);

/* Helligkeit und Zeiten neu lesen */
void st_ui_config_changed(void);

/* Aktuelle Seite und ob die Anzeige an ist, fuer /api/state */
const char *st_ui_page(void);
bool st_ui_on(void);

/* Seite waehlen ("anlage", "leitstand") und Anzeige ein- oder ausschalten,
 * fuer /api/display. false bei unbekannter Seite. */
bool st_ui_set_page(const char *name);
void st_ui_set_on(bool an);

/* Bildschirminhalt zeilenweise lesen, je Bildpunkt R, G, B. Fuer die
 * Bildschirmaufnahme ueber /api/screen. */
int st_ui_width(void);
int st_ui_height(void);
bool st_ui_read_row(int y, uint8_t *rgb);

#ifdef __cplusplus
}
#endif
