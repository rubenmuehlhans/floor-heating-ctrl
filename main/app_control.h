/*
 * Steuerzentrale: Ventilzustaende, Gruppensperre und Raumregelung.
 *
 * Der gesamte Aktorzustand liegt hinter einem Mutex. Befehle aus Weboberflaeche,
 * MQTT, Anzeige und Endlagenerkennung greifen darueber zu; die Steuertask
 * rechnet die Positionen fort und startet faellige Fahrten.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"
#include "valve.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float position;
    bool position_known;
    valve_op_t op;
    uint8_t group;      /* 0..5 */
    bool reserved;      /* von der Autokalibrierung belegt */
    bool manual_hold;   /* Notstellung: die Regelung laesst den Kreis in Ruhe */
    bool request_open;  /* Fahrbefehl liegt an, wartet auf die Gruppe */
    float request_target;
    uint32_t last_move_ms;
    int last_stop_reason;
} ctl_channel_t;

typedef struct {
    uint8_t id;
    char name[CFG_NAME_LEN];
    uint16_t channel_mask;
    bool mode_heat;
    float target_c;

    bool temp_valid;   /* Messwert vorhanden und nicht veraltet */
    bool sensor_set;   /* Thermometer zugeordnet */
    float temp_c;
    float humidity;
    uint8_t battery;
    uint32_t temp_age_s;

    float target_position;
    uint32_t next_check_s;
} ctl_room_t;

typedef struct {
    ctl_channel_t ch[HW_CHANNEL_COUNT];
    ctl_room_t rooms[CFG_MAX_ROOMS];
    uint8_t room_count;
    uint16_t bemf_mv[HW_BEMF_GROUP_COUNT];
    uint32_t uptime_s;
} ctl_snapshot_t;

esp_err_t control_start(void);

void control_snapshot(ctl_snapshot_t *out);

/* Fahrbefehle. Sie werden gemerkt und ausgefuehrt, sobald die Messgruppe des
 * Kanals frei ist. */
void control_cmd_position(uint8_t channel, float position);

/*
 * Notfahrt bis zum Anschlag, unabhaengig davon, welche Stellung die Steuerung
 * dem Ventil zuschreibt. Der Kreis bleibt danach in Handbetrieb: die Regelung
 * fasst ihn nicht mehr an, bis control_cmd_auto ihn wieder freigibt. Ohne
 * diesen Halt waere die Notstellung beim naechsten Regeldurchlauf wieder weg.
 */
void control_cmd_open(uint8_t channel);
void control_cmd_close(uint8_t channel);

/* Haelt den Kreis an. Der Handbetrieb bleibt bestehen. */
void control_cmd_stop(uint8_t channel);

/* Gibt den Kreis wieder an die Regelung zurueck. */
void control_cmd_auto(uint8_t channel);

/* Notfahrt fuer alle Kreise. Sie laufen nacheinander, sobald ihre Messgruppe
 * frei ist. */
void control_cmd_all(bool open);
void control_cmd_all_auto(void);

void control_room_set_target(uint8_t room_id, float target_c);
void control_room_set_mode(uint8_t room_id, bool heat);
void control_room_check_now(uint8_t room_id);

/* Nach einer Konfigurationsaenderung: Raeume und Kanalparameter neu einlesen. */
void control_config_changed(void);

/* Messwert eines BLE-Thermometers einspeisen. */
void control_temperature(const uint8_t mac[6], float temp_c, float humidity, uint8_t battery);

/* Exklusive Nutzung eines Kanals, etwa fuer die Autokalibrierung. Schlaegt
 * fehl, wenn der Kanal oder ein Nachbar derselben Messgruppe faehrt. */
bool control_reserve(uint8_t channel);
void control_release(uint8_t channel);

/* Direkte Ansteuerung eines reservierten Kanals unter Umgehung der
 * Zustandsmaschine. Nur waehrend einer Reservierung erlaubt. */
bool control_raw_drive(uint8_t channel, hw_drive_t drive);

/* true, solange irgendein Kanal faehrt oder belegt ist. */
bool control_busy(void);

/* Zaehlt bei jeder Zustandsaenderung hoch - Grundlage fuer den Livestrom der
 * Weboberflaeche. */
uint32_t control_revision(void);

#ifdef __cplusplus
}
#endif
