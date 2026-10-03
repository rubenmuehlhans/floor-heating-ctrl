/*
 * HomeKit-Bruecke des Leitstands.
 *
 * Je Raum ein Thermostat (Ist, Soll 5 bis 35 °C in 0,5 K, aus oder heizen,
 * Heizzustand, Feuchte), dazu Fuehler fuer Aussen, Speicher und
 * Kesselvorlauf. Die Werte kommen aus der Abfrage; Aenderungen aus Home
 * gehen nach 900 ms Sammelzeit an den zustaendigen Verteiler, ueber
 * dieselben Endpunkte wie die App. Pumpen und Einstellungen der Anlage sind
 * nicht erreichbar.
 *
 * Laeuft nur mit PSRAM (Core2): Kopplung und verschluesselte Sitzungen
 * brauchen mehr Speicher, als der Core Basic neben Protokoll und Anzeige
 * frei hat. Siehe docs/konzept-leitstand.md, Abschnitt HomeKit.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool aktiv;          /* die Bruecke laeuft */
    const char *grund;   /* warum nicht, sonst NULL */
    uint8_t steuerungen; /* gekoppelte Geraete, 0 = noch nicht gekoppelt */
    uint8_t zubehoer;    /* Thermostate und Fuehler */
    char code[11];       /* xxx-xx-xxx, nur fuer die Anzeige */
    char nutzlast[24];   /* X-HM://… fuer den QR-Code */
} st_hap_stand_t;

/* Nach Netz und Abfrage aufrufen. Ohne PSRAM geschieht nichts. */
esp_err_t st_hap_start(void);

void st_hap_stand(st_hap_stand_t *out);

/* Loescht alle Kopplungen; der Code bleibt. */
esp_err_t st_hap_kopplungen_loeschen(void);

/* Rein rechnerisch, auf dem Rechner geprueft: ein achtstelliger Code, den
 * HomeKit annimmt (keine gleichen Ziffern, nicht 12345678 oder 87654321). */
bool st_hap_code_gueltig(const char *acht_ziffern);

#ifdef __cplusplus
}
#endif
