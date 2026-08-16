/*
 * Gemeinsamer Unterbau der Konfigurationsspeicher.
 *
 * Beide Anwendungen legen ihre Konfiguration als ein einziges JSON unter einem
 * NVS-Schluessel ab. Das haelt Aenderungen atomar und erspart eine
 * Schluesselwanderung bei jeder Erweiterung: Geladen wird als Vorgabewerte mit
 * anschliessender Ueberlagerung, sodass ein neu hinzugekommenes Feld seinen
 * Vorgabewert behaelt, wenn die gespeicherte Fassung es noch nicht kennt.
 *
 * Hier steht nur das Verfahren -- Ablage und die drei Lesehilfen mit
 * Vorgabewert. Das Schema selbst fuehrt jede Anwendung fuer sich.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Legt den Text unter Namensraum und Schluessel ab. */
esp_err_t cfgjson_save(const char *ns, const char *key, const char *json);

/*
 * Liest den Text. Der Aufrufer gibt den Puffer mit free() zurueck. Ist nichts
 * abgelegt, kommt ESP_ERR_NVS_NOT_FOUND und *out bleibt NULL.
 */
esp_err_t cfgjson_load(const char *ns, const char *key, char **out);

/*
 * Lesehilfen. Fehlt der Schluessel oder hat er den falschen Typ, bleibt der
 * bisherige Wert stehen -- darauf beruht die Abwaertskompatibilitaet des
 * Schemas.
 */
void cfgjson_str(const cJSON *obj, const char *key, char *dst, size_t len);
double cfgjson_num(const cJSON *obj, const char *key, double fallback);
bool cfgjson_bool(const cJSON *obj, const char *key, bool fallback);

#ifdef __cplusplus
}
#endif
