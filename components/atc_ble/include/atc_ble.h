/*
 * Empfang der Raumthermometer ueber Bluetooth Low Energy.
 *
 * Die Geraete senden ihre Messwerte als Rundruf; das Geraet hoert nur mit
 * (Observer, keine Verbindung, kein eigenes Senden). Unterstuetzt werden vier
 * Formate:
 *
 *   - atc1441: 13 Byte Dienstdaten unter UUID 0x181A, Werte in Big Endian
 *   - pvvx:    15 Byte Dienstdaten unter UUID 0x181A, Werte in Little Endian
 *   - Ruuvi:   24 Byte Herstellerdaten der Kennung 0x0499, Datensatz 5 (RAWv2)
 *   - BTHome:  Dienstdaten unter UUID 0xFCD2, Fassung 2, offen oder mit
 *              AES-128-CCM verschluesselt
 *
 * Die ersten beiden sind die Xiaomi-Raumthermometer mit freier Firmware, das
 * dritte der RuuviTag als Aussenfuehler. Ruuvi sendet nicht unter 0x181A,
 * sondern als Herstellerdaten -- ein anderes Feld desselben Rundrufs.
 * BTHome sendet unter anderem der Climate-Sat von camperSense, verschluesselt
 * mit einem Schluessel je Geraet. Ohne ihn erscheint das Geraet in der Liste,
 * aber ohne Werte; die Schluessel liegen in einem eigenen Eintrag im NVS,
 * nicht in der Konfiguration.
 *
 * Damit ist die Regelung unabhaengig von Home Assistant; frueher kam
 * die Temperatur ueber die Home-Assistant-API herein.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atc_decode.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATC_MAX_DEVICES 24

typedef void (*atc_cb_t)(const atc_device_t *dev, void *ctx);

esp_err_t atc_ble_start(atc_cb_t cb, void *ctx);

/*
 * Legt den Schluesselspeicher an und laedt die Schluessel aus dem
 * NVS-Namensraum `nvs_namespace`, ohne Bluetooth zu starten. So bleiben
 * Schluessel les- und aenderbar, auch wenn der Empfang ausgeschaltet ist --
 * sonst ginge eine Sicherung ohne sie hinaus und loeschte sie beim
 * Zurueckspielen. atc_ble_start ruft es mit "fbh" auf, falls es noch nicht
 * geschehen ist.
 */
esp_err_t atc_ble_keys_init(const char *nvs_namespace);

/* Ob der Empfang laeuft. */
bool atc_ble_running(void);

/* Kopiert die bekannten Geraete in der Reihenfolge, in der sie zuerst
 * empfangen wurden. */
size_t atc_ble_devices(atc_device_t *out, size_t max);

/* Schluessel fuer verschluesselt sendende Thermometer, je Sendeadresse. Elf
 * Raeume und ein Aussenfuehler brauchen zwoelf; der Rest ist Spielraum fuer
 * Geraete, die erst noch zugeordnet werden. */
#define ATC_MAX_KEYS 16

typedef struct {
    uint8_t mac[6];
    uint8_t key[16];
} atc_key_t;

/*
 * Setzt oder ersetzt den Schluessel eines Geraets und speichert ihn; NULL
 * entfernt ihn. ESP_ERR_NO_MEM, wenn schon ATC_MAX_KEYS hinterlegt sind.
 * Der Wiederholungsschutz des Geraets beginnt danach von vorn.
 */
esp_err_t atc_ble_key_set(const uint8_t mac[6], const uint8_t key[16]);

/* Kopiert die hinterlegten Schluessel samt Schluessel -- nur fuer die
 * Sicherung, nie fuer eine Anzeige. */
size_t atc_ble_keys(atc_key_t *out, size_t max);

/* Ersetzt alle Schluessel, etwa beim Zurueckspielen einer Sicherung;
 * n = 0 entfernt alle. */
esp_err_t atc_ble_keys_replace(const atc_key_t *keys, size_t n);

/*
 * Suche anhalten und wieder aufnehmen.
 *
 * Bluetooth und WLAN teilen sich einen Funkteil. Solange der
 * Einrichtungs-Zugangspunkt die einzige Zugangsmoeglichkeit ist, bringt die
 * Suche nichts -- es ist noch kein Raum eingerichtet, dem ein Messwert
 * zugeordnet werden koennte -- und sie kostet genau die Funkzeit, die fuer die
 * Anmeldung am Zugangspunkt gebraucht wird.
 */
void atc_ble_pause(bool pausieren);

#ifdef __cplusplus
}
#endif
