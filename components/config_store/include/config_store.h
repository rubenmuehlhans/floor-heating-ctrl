/*
 * Konfiguration im NVS.
 *
 * Raeume, Kanalzuordnung, Regelparameter und Zugangsdaten sind zur Laufzeit
 * aenderbar. Abgelegt wird als JSON unter einem einzigen NVS-Schluessel; das
 * haelt Aenderungen atomar und erspart eine Schluesselwanderung bei jeder
 * Erweiterung.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "netmgr.h"
#include "hw_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_VERSION       1
#define CFG_MAX_ROOMS     HW_CHANNEL_COUNT
#define CFG_NAME_LEN      32
#define CFG_TZ_LEN        48

typedef struct {
    uint8_t id;                /* 1..255, stabil ueber Umbenennungen hinweg */
    char name[CFG_NAME_LEN];
    uint16_t channel_mask;     /* Bit 0 = CH1 ... Bit 10 = CH11 */
    uint8_t sensor_mac[6];     /* BLE-Thermometer, 00:00:.. = nicht zugeordnet */
    bool sensor_set;
    bool mode_heat;            /* false = Regelung aus, Ventile bleiben stehen */
    float target_c;
    float p_band_k;            /* Proportionalband, Vorgabe 1.0 K */
    uint16_t interval_s;       /* Pruefintervall, Vorgabe 30 s */
    float min_delta;           /* Mindestaenderung der Position, Vorgabe 0.01 */
    float step;                /* Rasterung der Zielposition, Vorgabe 0.1 */
} cfg_room_t;

typedef struct {
    uint32_t open_ms;
    uint32_t close_ms;
    uint32_t max_ms;
    uint32_t blank_ms;    /* Sperrzeit der Endlagenerkennung nach dem Anlauf */
    uint16_t bemf_mv;     /* Ausloeseschwelle */
    uint16_t bemf_hyst_mv;
    bool calibrated;
} cfg_channel_t;

typedef struct {
    char ssid[33];
    char pass[64];
    char hostname[CFG_NAME_LEN];
    char ap_pass[64];
} cfg_wifi_t;

typedef struct {
    bool enabled;
    char uri[128];     /* z.B. mqtt://192.168.1.10:1883 */
    char user[32];
    char pass[64];
    char prefix[CFG_NAME_LEN]; /* Themenpraefix, Vorgabe fbh_eg */
} cfg_mqtt_t;

typedef struct {
    uint16_t version;

    /* Bezeichnung der Anlage, ueblicherweise die Etage. Sie steht in der
     * Kopfzeile und im Geraetenamen fuer Home Assistant. Ist sie leer, gilt
     * die Einrichtung als offen und die Oberflaeche fuehrt durch den
     * Einrichtungsassistenten. */
    char site[CFG_NAME_LEN];

    uint8_t room_count;
    cfg_room_t rooms[CFG_MAX_ROOMS];
    cfg_channel_t channels[HW_CHANNEL_COUNT];

    /* Bedienung am Geraet. Die Schwellen haengen von Aufbau und Verdrahtung ab
     * und werden anhand der in der Oberflaeche angezeigten Rohwerte
     * eingestellt: eine Taste gilt als beruehrt, wenn ihr Wert darunter
     * faellt. */
    bool touch_enabled;
    uint16_t touch_thresh[HW_TOUCH_COUNT];
    uint8_t display_brightness; /* Prozent, im ESPHome-Aufbau standen 2 */

    uint16_t sensor_timeout_s; /* danach gilt der Messwert als veraltet */
    int8_t reboot_hour;        /* -1 = kein taeglicher Neustart */
    int8_t reboot_minute;
    char timezone[CFG_TZ_LEN];

    cfg_wifi_t wifi;
    cfg_mqtt_t mqtt;
} app_config_t;

/* Gespeicherte Ventilstellungen, damit nach einem Neustart nicht jeder Kanal
 * eine Referenzfahrt braucht. */
typedef struct {
    float position[HW_CHANNEL_COUNT];
    bool known[HW_CHANNEL_COUNT];
} cfg_positions_t;

/* Fuellt die Angaben, die components/netmgr braucht. Der Netzbaustein kennt
 * das Schema dieser Anwendung nicht und soll es auch nicht kennen. */
void cfg_netmgr(const app_config_t *cfg, netmgr_cfg_t *out);

/* NVS oeffnen, Konfiguration laden oder Werksvorgabe schreiben. */
esp_err_t cfg_init(void);

/* Kurzzeitiger Lesezugriff ohne Kopie. Zwischen cfg_lock und cfg_unlock darf
 * nicht blockiert werden. */
void cfg_lock(void);
void cfg_unlock(void);
const app_config_t *cfg_peek(void);

/* Vollstaendige Kopie fuer laenger laufende Auswertungen. */
void cfg_copy(app_config_t *out);

/* Pruefen, speichern und uebernehmen. Bei Verstoessen bleibt die bisherige
 * Konfiguration unveraendert und err beschreibt den Grund. */
esp_err_t cfg_set(const app_config_t *in, char *err, size_t err_len);

/* Werksvorgabe: heutige Raumaufteilung des Erdgeschosses. */
void cfg_defaults(app_config_t *out);
esp_err_t cfg_reset_defaults(void);

/* Raum anhand seiner Kennung suchen. */
const cfg_room_t *cfg_find_room(const app_config_t *cfg, uint8_t room_id);

/* Raum, dem ein Kanal (1..11) zugeordnet ist, oder NULL. */
const cfg_room_t *cfg_room_of_channel(const app_config_t *cfg, uint8_t channel);

/* Naechste freie Raumkennung. 0, wenn kein Platz mehr ist. */
uint8_t cfg_next_room_id(const app_config_t *cfg);

/* JSON-Umwandlung fuer die Weboberflaeche. Der Aufrufer gibt den Puffer mit
 * free() zurueck. */
char *cfg_to_json(const app_config_t *cfg, bool include_secrets);
esp_err_t cfg_from_json(const char *json, app_config_t *out, char *err, size_t err_len);

/* Ventilstellungen sichern und laden. */
esp_err_t cfg_save_positions(const cfg_positions_t *pos);
esp_err_t cfg_load_positions(cfg_positions_t *pos);

#ifdef __cplusplus
}
#endif
