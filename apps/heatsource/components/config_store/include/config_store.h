/*
 * Konfiguration des Heizungsgeraets im NVS.
 *
 * Eigene Fassung der Komponente config_store: Die gemeinsame unter
 * components/ traegt das Schema der Verteilerplatine und bindet ueber hw_map.h
 * deren elf Ventilkanaele ein. ESP-IDF laesst eine Komponente unter
 * <anwendung>/components die gleichnamige gemeinsame verdraengen, sodass beide
 * Anwendungen ihr eigenes Schema fuehren koennen, ohne dass an der laufenden
 * Verteiler-Firmware etwas geaendert werden muss.
 *
 * Der von components/netmgr benutzte Ausschnitt -- cfg_wifi_t, timezone,
 * reboot_hour, reboot_minute sowie cfg_lock/cfg_unlock/cfg_peek -- ist
 * deckungsgleich mit der gemeinsamen Fassung und muss es bleiben.
 *
 * Abgelegt wird als JSON unter einem einzigen NVS-Schluessel. Geladen wird als
 * Vorgabewerte mit anschliessender Ueberlagerung: ein neu hinzugekommenes Feld
 * behaelt dadurch seinen Vorgabewert, wenn die gespeicherte Fassung es noch
 * nicht kennt.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_VERSION      1
#define CFG_NAME_LEN     32
#define CFG_TZ_LEN       48
#define CFG_MAX_PROBES   12
#define CFG_MAX_BUSES    2

/*
 * Messstelle einer Anlage. Welche Rollen ein Geraet fuehrt, entscheidet ueber
 * seine Aufgabe: ein Geraet mit "abgas" erkennt den Brennerlauf, eines mit
 * "puffer" beurteilt den Ladezustand. Ein Geraet muss nicht alle Rollen
 * belegen.
 */
typedef enum {
    ROLE_NONE = 0,
    ROLE_ABGAS,
    ROLE_KESSEL_VL,
    ROLE_KESSEL_RL,
    ROLE_PUFFER,
    ROLE_PUFFER_UNTEN,
    ROLE_HK1_VL,
    ROLE_HK1_RL,
    ROLE_HK2_VL,
    ROLE_HK2_RL,
    ROLE_COUNT
} probe_role_t;

typedef struct {
    uint64_t rom;          /* Werkskennung des DS18B20, 0 = leerer Eintrag */
    probe_role_t role;
    char name[CFG_NAME_LEN];
    float offset_k;        /* Korrektur, etwa bei ungeschickt sitzendem Fuehler */
} cfg_probe_t;

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
    char prefix[CFG_NAME_LEN];
} cfg_mqtt_t;

typedef struct {
    uint16_t version;

    /* Bezeichnung des Geraets, etwa "Kessel" oder "Pufferspeicher". Sie steht
     * in der Kopfzeile, im mDNS-Eintrag und im Geraetenamen fuer Home
     * Assistant. Ist sie leer, gilt die Einrichtung als offen und die
     * Oberflaeche fuehrt durch den Einrichtungsassistenten. */
    char site[CFG_NAME_LEN];

    /* GPIO der 1-Wire-Busse, -1 = nicht belegt. Beide vorhandenen Platinen
     * sind unterschiedlich verdrahtet, deshalb steht das in der Konfiguration
     * und nicht in einer Platinenbeschreibung. Eine Aenderung wirkt erst nach
     * einem Neustart. */
    int8_t onewire_pin[CFG_MAX_BUSES];
    uint16_t poll_s;       /* Abstand zweier Leserunden */

    uint8_t probe_count;
    cfg_probe_t probes[CFG_MAX_PROBES];

    int8_t reboot_hour;    /* -1 = kein taeglicher Neustart */
    int8_t reboot_minute;
    char timezone[CFG_TZ_LEN];

    cfg_wifi_t wifi;
    cfg_mqtt_t mqtt;
} app_config_t;

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

void cfg_defaults(app_config_t *out);
esp_err_t cfg_reset_defaults(void);

/* Messstelle einer Rolle, oder NULL. */
const cfg_probe_t *cfg_probe_of_role(const app_config_t *cfg, probe_role_t role);

/* Eintrag zu einer Werkskennung, oder NULL. */
const cfg_probe_t *cfg_probe_of_rom(const app_config_t *cfg, uint64_t rom);

/* Kurzname und Klartext einer Rolle, etwa "kessel_vl" und "Kessel Vorlauf". */
const char *cfg_role_key(probe_role_t role);
const char *cfg_role_label(probe_role_t role);
probe_role_t cfg_role_from_key(const char *key);

/* JSON-Umwandlung fuer die Weboberflaeche. Der Aufrufer gibt den Puffer mit
 * free() zurueck. */
char *cfg_to_json(const app_config_t *cfg, bool include_secrets);
esp_err_t cfg_from_json(const char *json, app_config_t *out, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
