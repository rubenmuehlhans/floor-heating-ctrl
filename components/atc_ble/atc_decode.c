#include "atc_decode.h"

#include <string.h>

#include "atc_crypto.h"

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
    out->has_temp = true;
    out->has_humidity = true;
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
    out->has_temp = true;
    out->has_humidity = true;
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
    out->has_temp = true;
    out->has_humidity = roh_h != 0xFFFF;

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

/* ------------------------------------------------------------------ */
/* BTHome v2                                                           */
/* ------------------------------------------------------------------ */

/*
 * Laenge jeder Objektkennung nach bthome.io/format. Die Objekte tragen auf
 * dem Draht keine eigene Laenge; eine Kennung, die hier nicht ausgewertet
 * wird, laesst sich nur ueberspringen, wenn ihre Groesse bekannt ist. 0 heisst
 * unbekannt: Dort endet die Auswertung, was davor stand, gilt.
 * 0x53 (Text) und 0x54 (Rohdaten) tragen ein Laengenbyte und werden eigens
 * behandelt.
 */
static const uint8_t BTHOME_LEN[0x66] = {
    [0x00] = 1, [0x01] = 1, [0x02] = 2, [0x03] = 2, [0x04] = 3, [0x05] = 3, [0x06] = 2,
    [0x07] = 2, [0x08] = 2, [0x09] = 1, [0x0A] = 3, [0x0B] = 3, [0x0C] = 2, [0x0D] = 2,
    [0x0E] = 2, [0x0F] = 1, [0x10] = 1, [0x11] = 1, [0x12] = 2, [0x13] = 2, [0x14] = 2,
    [0x15] = 1, [0x16] = 1, [0x17] = 1, [0x18] = 1, [0x19] = 1, [0x1A] = 1, [0x1B] = 1,
    [0x1C] = 1, [0x1D] = 1, [0x1E] = 1, [0x1F] = 1, [0x20] = 1, [0x21] = 1, [0x22] = 1,
    [0x23] = 1, [0x24] = 1, [0x25] = 1, [0x26] = 1, [0x27] = 1, [0x28] = 1, [0x29] = 1,
    [0x2A] = 1, [0x2B] = 1, [0x2C] = 1, [0x2D] = 1, [0x2E] = 1, [0x2F] = 1, [0x3A] = 1,
    [0x3C] = 2, [0x3D] = 2, [0x3E] = 4, [0x3F] = 2, [0x40] = 2, [0x41] = 2, [0x42] = 3,
    [0x43] = 2, [0x44] = 2, [0x45] = 2, [0x46] = 1, [0x47] = 2, [0x48] = 2, [0x49] = 2,
    [0x4A] = 2, [0x4B] = 3, [0x4C] = 4, [0x4D] = 4, [0x4E] = 4, [0x4F] = 4, [0x50] = 4,
    [0x51] = 2, [0x52] = 2, [0x55] = 4, [0x56] = 2, [0x57] = 1, [0x58] = 1, [0x59] = 1,
    [0x5A] = 2, [0x5B] = 4, [0x5C] = 4, [0x5D] = 2, [0x5E] = 2, [0x5F] = 2, [0x60] = 1,
    [0x61] = 2, [0x62] = 4, [0x63] = 4, [0x64] = 1, [0x65] = 1,
};

static uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void bthome_temp(atc_bthome_t *out, float t)
{
    /* Mehrfuehlergeraete senden die Temperatur mehrfach; ein Raum hat eine,
     * gilt die erste. Werte unter dem absoluten Nullpunkt stehen bei manchen
     * Sendern fuer "kein Fuehler" (BTHome kennt kein NAN). */
    if (out->has_temp || t < -273.15f) {
        return;
    }
    out->temp_c = t;
    out->has_temp = true;
}

static void bthome_objects(const uint8_t *d, size_t len, atc_bthome_t *out)
{
    size_t i = 0;
    while (i < len) {
        uint8_t id = d[i++];
        if (id == 0x53 || id == 0x54) {
            if (i >= len) {
                break;
            }
            i += 1u + d[i];
            continue;
        }
        uint8_t l = id < sizeof(BTHOME_LEN) ? BTHOME_LEN[id] : 0;
        if (l == 0 || i + l > len) {
            break;
        }
        const uint8_t *v = &d[i];
        switch (id) {
        case 0x01:
            out->battery = v[0] > 100 ? 100 : v[0];
            out->has_battery = true;
            break;
        case 0x02:
            bthome_temp(out, (float)(int16_t)u16le(v) * 0.01f);
            break;
        case 0x03:
            out->humidity = (float)u16le(v) * 0.01f;
            out->has_humidity = true;
            break;
        case 0x04:
            out->pressure_hpa =
                (float)((uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16)) * 0.01f;
            out->has_pressure = true;
            break;
        case 0x0C:
            out->battery_mv = u16le(v);
            out->has_voltage = true;
            break;
        case 0x15:
            out->battery_low = v[0] ? 1 : 0;
            break;
        case 0x2E:
            out->humidity = (float)v[0];
            out->has_humidity = true;
            break;
        case 0x45:
            bthome_temp(out, (float)(int16_t)u16le(v) * 0.1f);
            break;
        default:
            break; /* Laenge bekannt, hier ohne Belang */
        }
        i += l;
    }
}

atc_bthome_result_t atc_decode_bthome(const uint8_t *d, size_t len, const uint8_t mac[6],
                                      const uint8_t *key, atc_bthome_t *out)
{
    memset(out, 0, sizeof(*out));
    out->battery_low = -1;
    if (d == NULL || len < 1) {
        return ATC_BTHOME_INVALID;
    }

    /* Geraeteinformation: Bit 0 verschluesselt, Bit 1 MAC im Inhalt,
     * Bit 2 sendet nur bei Anlass, Bits 5..7 Fassung (hier 2). */
    uint8_t info = d[0];
    if (((info >> 5) & 0x07) != 2) {
        return ATC_BTHOME_INVALID;
    }

    if ((info & 0x01) == 0) {
        size_t i = (info & 0x02) ? 7 : 1;
        if (i > len) {
            return ATC_BTHOME_INVALID;
        }
        bthome_objects(d + i, len - i, out);
        return ATC_BTHOME_OK;
    }

    out->encrypted = true;
    if (len < 1 + 1 + 4 + 4 || mac == NULL) {
        return ATC_BTHOME_INVALID;
    }
    size_t n = len - 1 - 4 - 4;
    if (n > ATC_BTHOME_PLAIN_MAX) {
        return ATC_BTHOME_INVALID;
    }
    if (key == NULL) {
        return ATC_BTHOME_KEY_MISSING;
    }

    const uint8_t *ct = d + 1;
    const uint8_t *zaehler = d + 1 + n;
    const uint8_t *mic = zaehler + 4;

    uint8_t nonce[13];
    memcpy(nonce, mac, 6);
    nonce[6] = 0xD2;
    nonce[7] = 0xFC;
    nonce[8] = info;
    memcpy(&nonce[9], zaehler, 4);

    uint8_t klar[ATC_BTHOME_PLAIN_MAX];
    if (!atc_ccm_decrypt(key, nonce, ct, n, mic, klar)) {
        return ATC_BTHOME_KEY_WRONG;
    }
    out->counter = (uint32_t)zaehler[0] | ((uint32_t)zaehler[1] << 8) |
                   ((uint32_t)zaehler[2] << 16) | ((uint32_t)zaehler[3] << 24);
    bthome_objects(klar, n, out);
    memset(klar, 0, sizeof(klar));
    return ATC_BTHOME_OK;
}

bool atc_bthome_counter_fresh(bool known, uint32_t last, uint32_t last_ms, uint32_t counter,
                              uint32_t now_ms)
{
    if (!known || counter > last) {
        return true;
    }
    return (uint32_t)(now_ms - last_ms) >= ATC_BTHOME_RESYNC_MS;
}

static int hexwert(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Liest genau 2 * n Hexadezimalziffern; Trennzeichen dazwischen zaehlen nicht. */
static bool hex_bytes(const char *text, uint8_t *out, size_t n)
{
    if (text == NULL) {
        return false;
    }
    size_t ziffern = 0;
    for (const char *p = text; *p; p++) {
        if (*p == ' ' || *p == ':' || *p == '-') {
            continue;
        }
        int v = hexwert(*p);
        if (v < 0 || ziffern >= 2 * n) {
            return false;
        }
        if (ziffern % 2 == 0) {
            out[ziffern / 2] = (uint8_t)(v << 4);
        } else {
            out[ziffern / 2] |= (uint8_t)v;
        }
        ziffern++;
    }
    return ziffern == 2 * n;
}

bool atc_parse_key(const char *text, uint8_t key[16])
{
    return hex_bytes(text, key, 16);
}

bool atc_parse_mac(const char *text, uint8_t mac[6])
{
    return hex_bytes(text, mac, 6);
}

const char *atc_format_name(atc_format_t f)
{
    switch (f) {
    case ATC_FMT_PVVX:   return "pvvx";
    case ATC_FMT_RUUVI:  return "ruuvi";
    case ATC_FMT_BTHOME: return "bthome";
    default:             return "atc1441";
    }
}

const char *atc_key_state_name(atc_key_state_t k)
{
    switch (k) {
    case ATC_KEY_OK:      return "ok";
    case ATC_KEY_MISSING: return "missing";
    case ATC_KEY_WRONG:   return "wrong";
    default:              return NULL;
    }
}
