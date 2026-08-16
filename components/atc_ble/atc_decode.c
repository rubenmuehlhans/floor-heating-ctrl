#include "atc_decode.h"

bool atc_decode_atc1441(const uint8_t *d, atc_device_t *out)
{
    /* MAC in Sendereihenfolge (Big Endian), Temperatur in Zehntelgrad. */
    for (int i = 0; i < 6; i++) {
        out->mac[i] = d[i];
    }
    int16_t temp = (int16_t)((d[6] << 8) | d[7]);
    out->temp_c = temp / 10.0f;
    out->humidity = d[8];
    out->battery = d[9];
    out->battery_mv = (uint16_t)((d[10] << 8) | d[11]);
    out->format = ATC_FMT_ATC1441;
    return true;
}

bool atc_decode_pvvx(const uint8_t *d, atc_device_t *out)
{
    /* MAC rueckwaerts, Temperatur in Hundertstelgrad. */
    for (int i = 0; i < 6; i++) {
        out->mac[i] = d[5 - i];
    }
    int16_t temp = (int16_t)(d[6] | (d[7] << 8));
    uint16_t hum = (uint16_t)(d[8] | (d[9] << 8));
    out->temp_c = temp / 100.0f;
    out->humidity = hum / 100.0f;
    out->battery_mv = (uint16_t)(d[10] | (d[11] << 8));
    out->battery = d[12];
    out->format = ATC_FMT_PVVX;
    return true;
}

/*
 * RuuviTag, Datensatz 5 ("RAWv2"). 24 Byte Herstellerdaten hinter der Kennung
 * 0x0499, alles Big Endian:
 *
 *   0      Datensatzkennung, hier 0x05
 *   1..2   Temperatur, 0,005 °C je Schritt
 *   3..4   Feuchte, 0,0025 % je Schritt
 *   5..6   Luftdruck in Pascal, um 50 000 verschoben
 *   7..12  Beschleunigung in drei Achsen -- hier ohne Belang
 *   13..14 obere 11 Bit Batteriespannung ab 1 600 mV, untere 5 Bit Sendeleistung
 *   15     Bewegungszaehler
 *   16..17 laufende Nummer der Messung
 *   18..23 MAC
 *
 * Fuer "kein Messwert" sendet Ruuvi feste Muster: 0x8000 bei der Temperatur,
 * 0xFFFF bei Feuchte und Druck.
 */
bool atc_decode_ruuvi(const uint8_t *d, atc_device_t *out)
{
    if (d[0] != 0x05) {
        return false; /* aeltere Datensaetze werden nicht ausgewertet */
    }
    for (int i = 0; i < 6; i++) {
        out->mac[i] = d[18 + i];
    }

    uint16_t roh_t = (uint16_t)((d[1] << 8) | d[2]);
    if (roh_t == 0x8000) {
        return false;
    }
    out->temp_c = (float)(int16_t)roh_t * 0.005f;

    uint16_t roh_h = (uint16_t)((d[3] << 8) | d[4]);
    out->humidity = roh_h == 0xFFFF ? 0.0f : (float)roh_h * 0.0025f;

    uint16_t roh_p = (uint16_t)((d[5] << 8) | d[6]);
    out->pressure_hpa = roh_p == 0xFFFF ? 0.0f : ((float)roh_p + 50000.0f) / 100.0f;

    uint16_t leistung = (uint16_t)((d[13] << 8) | d[14]);
    uint16_t mv = (uint16_t)((leistung >> 5) + 1600);
    out->battery_mv = (leistung >> 5) == 0x7FF ? 0 : mv;

    /* Eine Ladungsanzeige in Prozent sendet der RuuviTag nicht. Sie hier aus
     * der Spannung zu schaetzen hiesse, eine Zahl zu erfinden. */
    out->battery = 0;
    out->format = ATC_FMT_RUUVI;
    return true;
}

const char *atc_format_name(atc_format_t f)
{
    switch (f) {
    case ATC_FMT_PVVX:  return "pvvx";
    case ATC_FMT_RUUVI: return "ruuvi";
    default:            return "atc1441";
    }
}
