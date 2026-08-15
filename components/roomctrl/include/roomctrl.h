/*
 * Regelgesetz der Raumregelung.
 *
 * Uebernommen aus den *_check_temp-Skripten des ESPHome-Aufbaus: die
 * Ventilstellung folgt der Regelabweichung proportional, ueber ein Band von
 * plus/minus einem Kelvin von ganz zu bis ganz auf, gerastert auf Zehntel.
 *
 *   diff = soll - ist
 *   pos  = runden(((diff / band) + 1) / 2 / raster) * raster,  begrenzt auf 0..1
 *
 * Mit band = 1 K und raster = 0,1 ist das rechnerisch identisch zum
 * urspruenglichen round(((diff+1)/2)/0.1)*0.1.
 *
 * Frei von IDF-Abhaengigkeiten, damit das Verhalten auf dem Rechner pruefbar
 * bleibt.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float roomctrl_target_position(float setpoint_c, float current_c, float p_band_k, float step);

/* Liefert true, wenn die Abweichung eine Fahrt rechtfertigt. */
bool roomctrl_needs_move(float current_position, float target_position, float min_delta);

#ifdef __cplusplus
}
#endif
