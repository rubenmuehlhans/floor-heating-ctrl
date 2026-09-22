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
    ATC_FMT_BTHOME,
} atc_format_t;

/* Wie es bei einem verschluesselt sendenden Geraet mit dem Schluessel steht. */
typedef enum {
    ATC_KEY_NONE = 0, /* sendet offen, braucht keinen */
    ATC_KEY_OK,       /* hinterlegt und passend */
    ATC_KEY_MISSING,  /* keiner hinterlegt */
    ATC_KEY_WRONG,    /* hinterlegt, aber die Pruefsumme passt nicht */
} atc_key_state_t;

typedef struct {
    uint8_t mac[6];
    /* Name aus dem Rundruf. Die pvvx-Firmware laesst ihn frei setzen, die
     * atc1441-Fassung sendet ATC_<letzte drei MAC-Bytes>. Leer, wenn das
     * Geraet keinen Namen mitsendet. */
    char name[24];
    int8_t rssi;
    float temp_c;
    float humidity;
    /* Ob temp_c und humidity eine Messung tragen. Die Xiaomi-Formate senden
     * beides in jedem Paket; ein BTHome-Geraet ohne Schluessel liefert gar
     * nichts, und manche senden ihre Werte auf mehrere Pakete verteilt. */
    bool has_temp;
    bool has_humidity;
    uint8_t battery;     /* Prozent; der RuuviTag meldet keine, dort 0 */
    uint16_t battery_mv;
    /* Luftdruck in Hektopascal, 0 wenn das Format keinen liefert. Nur der
     * RuuviTag misst ihn. */
    float pressure_hpa;
    uint32_t last_seen_ms;
    uint32_t packets;
    atc_format_t format;
    /* Nur BTHome: verschluesselt gesendet, der Stand des Schluessels und der
     * Rahmenzaehler des zuletzt angenommenen Pakets. */
    bool encrypted;
    atc_key_state_t key;
    bool counter_set;
    uint32_t counter;
    uint32_t counter_ms;
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

/* ------------------------------------------------------------------ */
/* BTHome v2                                                           */
/* ------------------------------------------------------------------ */

/* Laengster Klartext eines verschluesselten Rundrufs, den der Empfaenger
 * annimmt. Ein Legacy-Rundruf hat ohnehin nur fuer etwa 15 Byte Platz. */
#define ATC_BTHOME_PLAIN_MAX 32

/*
 * Nach einer Pause ohne gueltigen Rahmen wird auch ein kleinerer Zaehler
 * wieder angenommen. Der Satellit zaehlt nach einem Neustart hoeher weiter;
 * zurueck springt er nur, wenn sein Speicher geloescht wurde. Ohne diese
 * Frist bliebe der Raum dann bis zum naechsten Neustart des Verteilers ohne
 * Messwert.
 */
#define ATC_BTHOME_RESYNC_MS (10u * 60u * 1000u)

typedef enum {
    ATC_BTHOME_OK = 0,      /* gelesen; es kann trotzdem ohne Temperatur sein */
    ATC_BTHOME_INVALID,     /* kein BTHome v2 oder verstuemmelt */
    ATC_BTHOME_KEY_MISSING, /* verschluesselt, aber kein Schluessel */
    ATC_BTHOME_KEY_WRONG,   /* verschluesselt, Pruefsumme passt nicht */
} atc_bthome_result_t;

/* Die Werte eines Rundrufs. Was das Paket nicht enthielt, bleibt als
 * "fehlt" markiert -- das Zusammenfuehren mit dem vorigen Stand ist Sache des
 * Empfaengers. */
typedef struct {
    bool encrypted;
    uint32_t counter; /* nur bei verschluesselten Rahmen */
    bool has_temp;
    float temp_c;
    bool has_humidity;
    float humidity;
    bool has_battery;
    uint8_t battery;
    bool has_voltage;
    uint16_t battery_mv;
    bool has_pressure;
    float pressure_hpa;
    int8_t battery_low; /* -1 fehlt, sonst 0 oder 1 */
} atc_bthome_t;

/*
 * Dienstdaten unter UUID 0xFCD2. `d` zeigt auf das erste Byte hinter der
 * UUID (die Geraeteinformation), `len` zaehlt ab dort. `mac` ist die
 * Sendeadresse in ueblicher Schreibweise; sie geht in die Nonce ein. `key`
 * ist der 16-Byte-Schluessel oder NULL.
 *
 * Verschluesselt: Geraeteinformation | Geheimtext | Zaehler (4, LE) | Pruefsumme (4),
 * AES-128-CCM mit der Nonce MAC | D2 FC | Geraeteinformation | Zaehler.
 */
atc_bthome_result_t atc_decode_bthome(const uint8_t *d, size_t len, const uint8_t mac[6],
                                      const uint8_t *key, atc_bthome_t *out);

/*
 * Schutz vor wiederholt gesendeten alten Rahmen: Angenommen wird ein
 * groesserer Zaehler als der zuletzt angenommene, ein kleinerer oder gleicher
 * erst nach ATC_BTHOME_RESYNC_MS ohne gueltigen Rahmen.
 */
bool atc_bthome_counter_fresh(bool known, uint32_t last, uint32_t last_ms, uint32_t counter,
                              uint32_t now_ms);

/*
 * Schluessel aus Text: 32 Hexadezimalziffern, Gross- und Kleinschreibung
 * gleich. Leerzeichen, Doppelpunkte und Bindestriche dazwischen werden
 * uebergangen, so wie Apps den Schluessel gern gruppiert anzeigen.
 */
bool atc_parse_key(const char *text, uint8_t key[16]);

/* MAC-Adresse aus Text, zwoelf Hexadezimalziffern mit oder ohne Trennzeichen. */
bool atc_parse_mac(const char *text, uint8_t mac[6]);

/* Kurzname des Formats, fuer Oberflaeche und Protokoll. */
const char *atc_format_name(atc_format_t f);

/* "ok", "missing", "wrong" -- oder NULL, wenn das Geraet offen sendet. */
const char *atc_key_state_name(atc_key_state_t k);

#ifdef __cplusplus
}
#endif
