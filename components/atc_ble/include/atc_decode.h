/*
 * Dekodierung der Rundrufpakete.
 *
 * Bewusst frei von ESP-IDF und NimBLE: Hier steht nur, wie aus rohen Bytes
 * Messwerte werden. Damit laesst sich das auf dem Rechner gegen die
 * Beispieldatensaetze der Hersteller pruefen, statt es an der Anlage zu
 * probieren -- ein vertauschtes Byte faellt sonst erst auf, wenn ein Ventil
 * am falschen Wert regelt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ATC_FMT_ATC1441 = 0,
    ATC_FMT_PVVX,
    ATC_FMT_RUUVI,
} atc_format_t;

typedef struct {
    uint8_t mac[6];
    /* Name aus dem Rundruf. Die pvvx-Firmware laesst ihn frei setzen, die
     * atc1441-Fassung sendet ATC_<letzte drei MAC-Bytes>. Leer, wenn das
     * Geraet keinen Namen mitsendet. */
    char name[24];
    int8_t rssi;
    float temp_c;
    float humidity;
    uint8_t battery;     /* Prozent; der RuuviTag meldet keine, dort 0 */
    uint16_t battery_mv;
    /* Luftdruck in Hektopascal, 0 wenn das Format keinen liefert. Nur der
     * RuuviTag misst ihn. */
    float pressure_hpa;
    uint32_t last_seen_ms;
    uint32_t packets;
    atc_format_t format;
} atc_device_t;

/* Dienstdaten unter UUID 0x181A, 13 Byte, Big Endian. */
bool atc_decode_atc1441(const uint8_t *d, atc_device_t *out);

/* Dienstdaten unter UUID 0x181A, 15 Byte, Little Endian. */
bool atc_decode_pvvx(const uint8_t *d, atc_device_t *out);

/*
 * Herstellerdaten der Kennung 0x0499, Datensatz 5 ("RAWv2"), 24 Byte hinter
 * der Kennung. Liefert false bei fremdem Datensatz oder fehlender Temperatur.
 */
bool atc_decode_ruuvi(const uint8_t *d, atc_device_t *out);

/* Kurzname des Formats, fuer Oberflaeche und Protokoll. */
const char *atc_format_name(atc_format_t f);

#ifdef __cplusplus
}
#endif
