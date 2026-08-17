/*
 * Bedarfsabfrage und Pumpensteuerung.
 *
 * Das Geraet am Pufferspeicher fragt die Verteilerplatinen selbst ab -- ueber
 * mDNS gefunden, per HTTP abgeholt. Ein Broker ist dafuer nicht noetig; faellt
 * das Netz aus, laufen die Pumpen weiter, weil ein verstummter Verteiler als
 * bedarfsmeldend gilt.
 *
 * Die Entscheidung selbst faellt in components/heatlogic und ist dort ohne
 * Hardware geprueft. Hier steht nur, was ohne ESP-IDF nicht geht: abfragen,
 * Messwerte einsammeln, schalten.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"
#include "heatlogic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zustand einer abgefragten Verteilerplatine. */
typedef struct {
    char id[CFG_ID_LEN];
    char site[CFG_NAME_LEN];
    char host[16];
    bool seen;        /* hat seit dem Start schon geantwortet */
    bool demand;
    float max_target;
    uint8_t open_channels;
    /* Offene Kanaele, die nicht als Bedarf zaehlen, weil ihr Raum gerade nicht
     * geregelt wird. Sonst bliebe unklar, warum ein sichtbar offenes Ventil
     * keine Pumpe anwirft. */
    uint8_t unregulated;
    uint8_t rooms_calling;
    bool room_valid;
    float min_room_c;
    uint32_t age_s;   /* seit der letzten Antwort */
    uint32_t errors;
} peer_demand_t;

typedef enum {
    PUMP_PATH_NONE = 0, /* weder Thema noch Adresse eingetragen */
    PUMP_PATH_MQTT,
    PUMP_PATH_HTTP,
} pump_path_t;

typedef struct {
    uint8_t id;
    char name[CFG_NAME_LEN];
    bool enabled;
    pump_mode_t mode;
    bool on;
    pump_reason_t reason;
    uint32_t since_s;      /* seit dem letzten Schaltvorgang */

    bool demand;
    bool stale;            /* mindestens ein Verteiler verstummt */
    bool any_seen;

    bool vl_valid, rl_valid;
    float vl_c, rl_c;

    /* Rueckmeldung des Relais. */
    bool relay_known;
    bool relay_on;
    bool relay_online;
    uint32_t relay_age_s;
    bool relay_mismatch;   /* Rueckmeldung weicht laenger vom Sollwert ab */
    /* Welcher Weg zum Relais gerade gilt -- nicht, welcher zuletzt geklappt
     * hat: die Oberflaeche soll zeigen, worueber geschaltet wird, auch wenn
     * das Relais gerade nicht antwortet. */
    pump_path_t path;
    int last_status;       /* HTTP-Status des letzten Versuchs, -1 = keine Verbindung */

    /* Vor- und Ruecklauf vertauscht -- geprueft nur bei laufender Pumpe und
     * warmem Speicher, siehe components/heatlogic/plausi.h. */
    bool flow_swapped;
    uint32_t swapped_held_s;
} circuit_status_t;

/* Zustand der Kesselkreispumpe. */
typedef struct {
    bool enabled;
    bool on;
    uint8_t mode;          /* 0 = auto, 1 = ein, 2 = aus */
    const char *reason;
    const char *reason_key;
    uint32_t since_s;
    bool relay_known, relay_on, relay_online;
    int last_status;
    pump_path_t path;
} boiler_pump_status_t;

void pumps_boiler_status(boiler_pump_status_t *out);
esp_err_t pumps_set_boiler_mode(uint8_t mode);

esp_err_t pumps_start(void);

/* Nach einer Konfigurationsaenderung Kreise und Zeiten neu uebernehmen. */
void pumps_config_changed(void);

/* Betriebsart eines Kreises setzen. Wird auch in der Konfiguration abgelegt. */
esp_err_t pumps_set_mode(uint8_t circuit_id, pump_mode_t mode);

size_t pumps_status(circuit_status_t *out, size_t max);
size_t pumps_peers(peer_demand_t *out, size_t max);

/* true, solange eine Pumpe in einer Mindestlaufzeit steht -- der taegliche
 * Neustart wartet darauf. */
bool pumps_busy(void);

#ifdef __cplusplus
}
#endif
