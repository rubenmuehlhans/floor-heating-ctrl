/*
 * Endlagenerkennung ueber die Gegen-EMK der Stellmotoren.
 *
 * Je Messgruppe liegt ein Shunt an einem ADC1-Eingang. Solange der Motor
 * laeuft, steht dort eine kleine Spannung an; erreicht das Ventil den Anschlag,
 * blockiert der Motor und die Spannung steigt deutlich. Ueberschreitet der
 * gleitende Median die Schwelle, gilt die Endlage als erreicht.
 *
 * Gegenueber dem ESPHome-Aufbau wird schneller abgetastet (50 ms statt 250 ms)
 * und der Median bei jedem Wert ausgewertet statt nur jedem fuenften. Der Motor
 * laeuft dadurch nach Erreichen des Anschlags kuerzer weiter.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hw_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BEMF_SAMPLE_PERIOD_MS 50
#define BEMF_MEDIAN_WINDOW    5

/* Wird gemeldet, wenn der Median einer Gruppe die Schwelle ueberschreitet. */
typedef void (*bemf_endstop_cb_t)(uint8_t group, uint16_t mv, void *ctx);

/* Erhaelt jeden einzelnen Messwert - von der Autokalibrierung genutzt. */
typedef void (*bemf_sample_cb_t)(uint8_t group, uint16_t raw_mv, uint16_t median_mv,
                                 uint32_t t_ms, void *ctx);

esp_err_t bemf_start(void);

/* Schwelle und Hysterese einer Gruppe. Die Hysterese verhindert, dass eine
 * Gruppe direkt nach dem Ausloesen erneut meldet. */
void bemf_set_threshold(uint8_t group, uint16_t mv, uint16_t hyst_mv);

void bemf_set_endstop_cb(bemf_endstop_cb_t cb, void *ctx);
void bemf_set_sample_cb(bemf_sample_cb_t cb, void *ctx);

/* Gruppe scharf schalten. Nur waehrend einer Fahrt sinnvoll - im Stillstand
 * wuerde jedes Rauschen als Endlage gelten. */
void bemf_arm(uint8_t group, bool armed);

/* Letzter Medianwert einer Gruppe in mV. */
uint16_t bemf_mv(uint8_t group);

#ifdef __cplusplus
}
#endif
