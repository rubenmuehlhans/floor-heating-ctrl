/*
 * 24-Stunden-Verlauf fuer die Anzeige.
 *
 * Fuenfminutenmittel von Speicher, Kesselvorlauf, Aussen, Brennerlauf und
 * bis zu 24 Raumtemperaturen, im Raster der App (Plaetze zu 300 s in UTC).
 * Die Werte kommen aus der laufenden Abfrage; nach einem Neustart fuellt der
 * Verlauf die letzten 24 Stunden aus den Fuenfminutendateien der Karte.
 *
 * Nur mit PSRAM (Core2); rund 16 KB. Alle Aufrufe aus der Aufgabe der
 * Anzeige, deshalb ohne Sperre.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "st_poll.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_PLAETZE 288
#define SV_RAEUME  24

enum { SV_SPEICHER = 0, SV_KESSEL, SV_AUSSEN, SV_BRENNER, SV_FEST };

typedef struct {
    char geraet[24];
    uint8_t raum;
    char name[24];
} sv_raum_t;

/* Legt den Speicher an; false ohne PSRAM. */
bool sv_start(void);
bool sv_aktiv(void);

/* Nimmt den aktuellen Stand der Anlage in den laufenden Platz auf. */
void sv_abtasten(const st_plant_t *p);

/* Fuellt einmal nach dem Start die letzten 24 Stunden von der Karte, sobald
 * Uhr, Karte und die Geraete der Anlage bekannt sind. */
void sv_wiederherstellen(const st_plant_t *p);

/* Der laufende Platz, Sekunden seit 1970 durch 300 */
uint32_t sv_platz(void);

/* Wert einer Reihe (SV_SPEICHER … oder SV_FEST + Raum) in einem Platz. Der
 * Brenner als Anteil 0 bis 1. */
bool sv_wert(int reihe, uint32_t platz, float *out);

int sv_raeume(const sv_raum_t **liste);

/* Steigt, wenn ein Platz abgeschlossen oder die Karte gelesen ist. */
uint32_t sv_revision(void);

#ifdef __cplusplus
}
#endif
