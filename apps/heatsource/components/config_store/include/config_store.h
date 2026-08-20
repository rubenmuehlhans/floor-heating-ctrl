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
#include "netmgr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_VERSION      1
#define CFG_NAME_LEN     32
#define CFG_TZ_LEN       48
#define CFG_MAX_PROBES   12
#define CFG_MAX_BUSES    2
#define CFG_MAX_CIRCUITS 4
#define CFG_MAX_PEERS    4
#define CFG_ID_LEN       24
#define CFG_TOPIC_LEN    48

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
    /* Je Heizkreis ein Vor- und ein Ruecklauffuehler. Vier Kreise aus einem
     * Pufferspeicher decken auch groessere Haeuser ab; die Rollen sind
     * durchnummeriert, damit sich ein Kreis ohne Sonderfall anlegen laesst. */
    ROLE_HK1_VL,
    ROLE_HK1_RL,
    ROLE_HK2_VL,
    ROLE_HK2_RL,
    ROLE_HK3_VL,
    ROLE_HK3_RL,
    ROLE_HK4_VL,
    ROLE_HK4_RL,
    /* Aussentemperatur. Sie stammt nicht von einem DS18B20 an diesem Geraet,
     * sondern vom RuuviTag am Verteiler und kommt ueber die Bedarfsabfrage
     * herein. Sie geht in keine Regelung ein -- sie wird aufgezeichnet, damit
     * sich Verbrauch und Aussenlage spaeter gegenueberstellen lassen. */
    ROLE_AUSSEN,
    ROLE_COUNT
} probe_role_t;

typedef struct {
    uint64_t rom;          /* Werkskennung des DS18B20, 0 = leerer Eintrag */
    probe_role_t role;
    char name[CFG_NAME_LEN];
    float offset_k;        /* Korrektur, etwa bei ungeschickt sitzendem Fuehler */
} cfg_probe_t;

/*
 * Heizkreis mit eigener Pumpe.
 *
 * peers nennt die Verteilerplatinen, die diesen Kreis versorgen -- Kreis 1
 * bekommt ueblicherweise Keller und Erdgeschoss, Kreis 2 das Obergeschoss.
 * Geschaltet wird ueber ein Tasmota-Relais: cmnd/<topic>/POWER<relay>.
 */
typedef struct {
    uint8_t id;                 /* 1..CFG_MAX_CIRCUITS */
    char name[CFG_NAME_LEN];
    bool enabled;

    probe_role_t vl_role;       /* Vorlauffuehler dieses Kreises */
    probe_role_t rl_role;

    uint8_t peer_count;
    char peers[CFG_MAX_PEERS][CFG_ID_LEN];   /* Geraetekennungen, etwa fbh_c2e55c */

    /*
     * Tasmota laesst sich auf zwei Wegen schalten. Ueber MQTT, sobald ein
     * Broker eingerichtet ist -- dann kommt die Rueckmeldung von selbst. Sonst
     * unmittelbar ueber HTTP an /cm; das braucht keinen Broker, liefert die
     * Rueckmeldung aber nur als Antwort auf den eigenen Befehl.
     */
    char pump_topic[CFG_TOPIC_LEN];          /* Tasmota-Thema, etwa pumpe_hk1 */
    char pump_host[CFG_TOPIC_LEN];           /* Adresse fuer HTTP, leer = nur MQTT */
    char pump_user[24];                      /* nur wenn das Relais eines verlangt */
    char pump_pass[48];
    uint8_t pump_relay;                      /* 1..8 */

    uint8_t mode;               /* 0 = auto, 1 = ein, 2 = aus */

    uint16_t overrun_s;
    uint16_t min_run_s;
    uint16_t min_pause_s;
    float min_buffer_c;
    float frost_c;
} cfg_circuit_t;

/*
 * Kesselkreispumpe zwischen Kessel und Pufferspeicher. Sie wird auf dem Geraet
 * eingerichtet, das Kesselvor- und -ruecklauf misst -- ohne diese beiden
 * Werte gibt es nichts zu entscheiden.
 */
typedef struct {
    bool enabled;
    char topic[CFG_TOPIC_LEN];   /* Tasmota-Thema */
    char host[CFG_TOPIC_LEN];    /* Adresse fuer HTTP */
    char user[24];
    char pass[48];
    uint8_t relay;               /* 1..8 */
    uint8_t mode;                /* 0 = auto, 1 = ein, 2 = aus */

    float on_k;                  /* ab dieser Spreizung gibt der Kessel ab */
    float off_k;
    uint16_t hold_s;
    uint16_t min_run_s;
    uint16_t min_pause_s;
    float emergency_c;           /* darueber laeuft sie in jedem Fall */
} cfg_boiler_pump_t;

/* Brennererkennung am Abgasfuehler; nur wirksam, wenn die Rolle vergeben ist. */
typedef struct {
    float delta_on_k;
    float delta_off_k;
    /* Ausschlag gegen den Extremwert: so weit unter dem Hoechstwert gilt der
     * Brenner als aus, so weit ueber dem Tiefstwert wieder als angelaufen. */
    float swing_k;
    uint16_t on_hold_s;
    uint16_t off_hold_s;
    float duese_l_h;      /* Duesendurchsatz, 0 = keine Verbrauchsschaetzung */
    /*
     * Zeitpunkt der letzten Kesselreinigung. Er legt fest, welche Ladungen den
     * sauberen Zustand beschreiben, gegen den der Abgas-Vorlauf-Abstand
     * gehalten wird. 0 heisst: nie eingetragen, dann gibt es keinen Bezug.
     */
    uint32_t wartung_epoch;
} cfg_burner_t;

/* Beurteilung des Ladezustands. */
typedef struct {
    float spread_full_k;   /* darunter nimmt der Speicher keine Waerme mehr auf */
    uint16_t spread_hold_s;
    float voll_c;          /* Pufferwert fuer 100 Prozent */
    float leer_c;          /* Pufferwert fuer 0 Prozent */
    float warn_c;          /* darunter wird das Warmwasser knapp */
    float kessel_hot_c;    /* darueber gilt der Kesselvorlauf als heiss */
    /*
     * Kalibrierung des leeren Speichers. Wann er leer genug ist, entscheidet
     * die Regelung des Kessels, indem sie den Brenner anlaufen laesst -- der
     * Pufferwert in diesem Augenblick ist der brauchbarste Nullpunkt, den die
     * Anlage hergibt. leer_c wird dann nachgezogen; die letzte Kalibrierung
     * steht in leer_epoch.
     */
    bool leer_lernen;
    float lern_drop_k;     /* so weit muss der Speicher vorher gefallen sein */
    uint32_t leer_epoch;   /* Zeitpunkt der letzten Kalibrierung, 0 = nie */
} cfg_buffer_t;

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

    uint8_t circuit_count;
    cfg_circuit_t circuits[CFG_MAX_CIRCUITS];

    /* Abfrage der Verteiler. */
    uint16_t demand_poll_s;
    uint16_t demand_timeout_s;

    cfg_burner_t burner;
    cfg_buffer_t buffer;
    cfg_boiler_pump_t boiler_pump;

    int8_t reboot_hour;    /* -1 = kein taeglicher Neustart */
    int8_t reboot_minute;

    /*
     * Woechentlicher Schutzlauf gegen Festsitzen. Gemessen an der Uhr, nicht
     * an der Laufzeit: ein Geraet mit taeglichem Neustart erreicht nie sieben
     * Tage Laufzeit, ein Schutzlauf daran gemessen kaeme nie.
     */
    int8_t seize_weekday;  /* 0 = Sonntag ... 6 = Samstag, -1 = aus */
    int8_t seize_hour;

    char timezone[CFG_TZ_LEN];

    cfg_wifi_t wifi;
    cfg_mqtt_t mqtt;
} app_config_t;

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

void cfg_defaults(app_config_t *out);
esp_err_t cfg_reset_defaults(void);

/* Messstelle einer Rolle, oder NULL. */
const cfg_probe_t *cfg_probe_of_role(const app_config_t *cfg, probe_role_t role);

/* Eintrag zu einer Werkskennung, oder NULL. */
const cfg_probe_t *cfg_probe_of_rom(const app_config_t *cfg, uint64_t rom);

/* Heizkreis anhand seiner Kennung, oder NULL. */
const cfg_circuit_t *cfg_circuit(const app_config_t *cfg, uint8_t id);

/* Vorbelegter Heizkreis, wie ihn der Einrichtungsassistent anlegt. */
void cfg_circuit_defaults(cfg_circuit_t *c, uint8_t id);

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
