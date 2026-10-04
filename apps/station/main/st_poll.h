/*
 * Abfrage der Anlage.
 *
 * Der Leitstand fragt die Heizungsgeraete und Verteiler, die sich per mDNS
 * melden, in festem Takt ab und haelt je Geraet einen knappen Auszug: das,
 * was Anzeige und spaeter das Protokoll brauchen. Die Geraete selbst merken
 * davon nur eine lesende Anfrage mehr.
 *
 * Dazu kommt der eigene Aussenfuehler, der ueber Bluetooth empfangen wird.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "atc_decode.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST_MAX_HEAT     2
#define ST_MAX_MANIFOLD 4
#define ST_MAX_PROBES   8
#define ST_MAX_CIRCUITS 3
#define ST_MAX_ROOMS    12

/* Was fuer jedes abgefragte Geraet gilt */
typedef struct {
    char id[24];
    char site[32];
    char host[16];
    char version[24];
    bool seen;        /* hat seit dem Start geantwortet */
    bool reachable;   /* die letzte Abfrage kam an */
    uint8_t fails;    /* Fehlabfragen in Folge */
    uint32_t ok_ms;   /* Zeitpunkt der letzten Antwort */
    uint32_t uptime_s;
    uint32_t heap;
    int8_t rssi;
} st_dev_t;

typedef struct {
    char role[12];
    float temp_c;
} st_probe_t;

typedef struct {
    uint8_t id;
    char name[24];
    bool on;
    bool vl_valid, rl_valid;
    float vl_c, rl_c;
} st_circuit_t;

typedef struct {
    st_dev_t dev;
    st_probe_t probes[ST_MAX_PROBES];
    uint8_t probe_count;
    /* Brenner: nur das Geraet mit eigenem Abgasfuehler misst ihn selbst */
    bool burner_known, burner_own, burner_running;
    bool abgas_valid;
    float abgas_c;
    uint32_t runtime_today_s;
    uint16_t starts_today;
    float litres_today;
    /* Ladung: nur das Geraet mit eigenem Pufferfuehler */
    bool charge_valid, charge_own, warn_dhw;
    float charge_level;
    char charge_phase[24];
    st_circuit_t circuits[ST_MAX_CIRCUITS];
    uint8_t circuit_count;
    uint8_t findings;
    char finding[64];
    char finding_code[20]; /* Kennung des ersten Befunds, etwa flow_swapped */
} st_heat_t;

typedef struct {
    uint8_t id;
    char name[24];
    bool temp_valid, hum_valid, heat;
    float temp_c, humidity, target_c;
    float position;
} st_room_t;

typedef struct {
    st_dev_t dev;
    st_room_t rooms[ST_MAX_ROOMS];
    uint8_t room_count;
    /* Platine ohne Stellantriebe, die nur den Aussenfuehler empfaengt */
    bool outdoor_only;
    /* Aussenfuehler, wie ihn der Verteiler empfaengt. aussen_ms ist der
     * eigene Zeitpunkt der Abfrage, aussen_age_s das Alter von dort aus. */
    bool aussen_valid, aussen_hum_valid;
    float aussen_c, aussen_hum;
    uint32_t aussen_age_s;
    uint32_t aussen_ms;
} st_manifold_t;

typedef struct {
    bool assigned;  /* ein Aussenfuehler ist eingestellt */
    bool valid;     /* und hat einen Messwert geliefert */
    char mac[18];
    char name[24];
    float temp_c;
    bool hum_valid;
    float humidity;
    uint8_t battery;
    int8_t rssi;
    uint32_t age_s;
    /* Leer: eigener Empfang. Sonst die Bezeichnung des Verteilers, von dem
     * der Wert stammt -- dann, wenn der Leitstand den Fuehler selbst nicht
     * hoert. */
    char quelle[32];
} st_outdoor_t;

typedef struct {
    st_heat_t heat[ST_MAX_HEAT];
    uint8_t heat_count;
    st_manifold_t manifolds[ST_MAX_MANIFOLD];
    uint8_t manifold_count;
    st_outdoor_t outdoor;
    uint32_t revision; /* steigt mit jeder Aenderung */
} st_plant_t;

/*
 * Sperre fuer speicherhungrige Arbeit: das Auswerten eines Geraetezustands
 * und das Zusammenstellen grosser Antworten. Auf dem Core ohne PSRAM belegt
 * jede davon zehn bis zwanzig Kilobyte; nacheinander statt gleichzeitig
 * bleibt genug frei.
 */
void st_speicher_sperren(void);
void st_speicher_freigeben(void);

/* Legt die Sperre an; vor allen anderen Aufrufen, auch vor der Anzeige. */
void st_poll_init(void);
esp_err_t st_poll_start(void);

/* Haelt die Abfrage an und nimmt sie wieder auf, etwa waehrend eines
 * Firmware-Updates. Eine laufende Abfrage wird noch beendet. */
void st_poll_pausieren(bool pause);

/* Kopie des aktuellen Stands */
void st_poll_snapshot(st_plant_t *out);
uint32_t st_poll_revision(void);

/* Aussenfuehler: Adresse aus den Einstellungen, leer = keiner */
void st_poll_set_outdoor(const char *mac);
/* Jeder Bluetooth-Messwert kommt hier vorbei; uebernommen wird nur der des
 * eingestellten Aussenfuehlers. */
void st_poll_ble(const atc_device_t *dev);

/* Aelter als das gilt der Aussenwert als veraltet, wie in den
 * Heizungsgeraeten (AUSSEN_STALE_S in apps/heatsource/main/app_remote.c). */
#define ST_OUTDOOR_STALE_S 900

#ifdef __cplusplus
}
#endif
