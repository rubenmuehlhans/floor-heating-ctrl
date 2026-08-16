/*
 * Messstellen des Heizungsgeraets.
 *
 * Legt ueber die rohen 1-Wire-Fuehler aus components/onewire_temp die
 * Zuordnung aus der Konfiguration: Rolle, Klartextname und Korrekturwert. Die
 * Firmware rechnet ab hier nur noch mit Rollen, nicht mehr mit Werkskennungen.
 *
 * Zusaetzlich wird ein Verlauf im Minutentakt ueber 24 Stunden gefuehrt. Er
 * liegt im Arbeitsspeicher und geht bei einem Neustart verloren; fuer die
 * Beurteilung einer Ladung reicht das, und eine Ablage im Flash waere bei
 * diesem Schreibaufkommen unangebracht.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"
#include "onewire_temp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 24 Stunden im Minutentakt. */
#define SENS_HIST_SLOTS 1440

typedef struct {
    uint64_t rom;
    uint8_t bus;
    probe_role_t role;
    char name[CFG_NAME_LEN];
    bool assigned;    /* in der Konfiguration eingetragen */
    bool valid;
    float temp_c;     /* mit Korrekturwert */
    float raw_c;      /* wie gemessen */
    float offset_k;
    float delta30_c;  /* Aenderung der letzten 30 Sekunden */
    uint32_t age_s;
    uint32_t reads;
    uint32_t errors;
} sens_probe_t;

typedef struct {
    sens_probe_t probes[OT_MAX_PROBES];
    size_t count;
    size_t assigned_count;
    uint32_t round_ms;
    uint32_t rounds;
} sens_snapshot_t;

esp_err_t sensors_start(void);

/* Kopie des letzten Standes. */
void sensors_get(sens_snapshot_t *out);

/* Messwert einer Rolle. false, wenn die Rolle nicht vergeben ist oder noch
 * kein gueltiger Wert vorliegt. */
bool sensors_role_value(probe_role_t role, float *out_c, uint32_t *age_s);

/* Nach einer Konfigurationsaenderung Rollen und Korrekturwerte uebernehmen. */
void sensors_config_changed(void);

/* Erneuten Suchlauf am Bus anstossen. */
void sensors_rescan(void);

/*
 * Verlauf. index 0 ist der juengste Eintrag. Geschrieben werden bis zu
 * ROLE_COUNT Werte in Zehntelgrad; SENS_HIST_NONE steht fuer "kein Messwert".
 * Die Rueckgabe ist die Zahl der belegten Eintraege.
 */
#define SENS_HIST_NONE INT16_MIN

size_t sensors_history_len(void);
bool sensors_history_row(size_t index, int16_t *out, size_t out_len);

/* Unixzeit des juengsten Eintrags, 0 solange die Uhr nicht gestellt ist. */
uint32_t sensors_history_newest_epoch(void);

#ifdef __cplusplus
}
#endif
