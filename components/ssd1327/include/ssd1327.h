/*
 * SSD1327-Anzeige, 128x128 Bildpunkte, 16 Graustufen, ueber I2C.
 *
 * Der Treiber haelt einen vollstaendigen Bildspeicher (8 kB) und uebertraegt
 * ihn auf Anforderung. Bei vier Bit je Bildpunkt liegen zwei Punkte in einem
 * Byte: der linke im oberen Halbbyte.
 *
 * In esp_lcd gibt es keinen Treiber fuer diesen Baustein, deshalb die
 * eigene Umsetzung.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1327_WIDTH  128
#define SSD1327_HEIGHT 128

esp_err_t ssd1327_init(void);
bool ssd1327_present(void);

/* Helligkeit in Prozent. Zwei Prozent genuegen im Schaltschrank. */
esp_err_t ssd1327_set_brightness(uint8_t percent);

void ssd1327_clear(uint8_t gray);
void ssd1327_pixel(int x, int y, uint8_t gray);
void ssd1327_fill_rect(int x, int y, int w, int h, uint8_t gray);
void ssd1327_rect(int x, int y, int w, int h, uint8_t gray);

/* Zeichnet Text und liefert die belegte Breite. scale 1 ergibt 6x8, scale 2
 * ergibt 12x16 Bildpunkte je Zeichen. Erwartet UTF-8; Umlaute und das
 * Gradzeichen werden umgesetzt. */
int ssd1327_text(int x, int y, const char *utf8, uint8_t gray, uint8_t scale);
int ssd1327_text_width(const char *utf8, uint8_t scale);

/* Uebertraegt den Bildspeicher an die Anzeige. */
esp_err_t ssd1327_flush(void);

#ifdef __cplusplus
}
#endif
