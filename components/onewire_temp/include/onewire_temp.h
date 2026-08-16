/*
 * Mehrere DS18B20 an einem oder zwei 1-Wire-Bussen.
 *
 * Unterschiede zu components/sensors_local, das dem Verteiler vorbehalten
 * bleibt:
 *
 *   - Die Pins kommen als Parameter, nicht aus einer Platinenbeschreibung.
 *   - Gewandelt wird per Sammelbefehl. ds18b20_trigger_temperature_conversion()
 *     wartet je Fuehler 750 ms; bei acht Fuehlern waere eine Leserunde damit
 *     sechs Sekunden lang. Hier wandeln alle Fuehler gleichzeitig, gewartet
 *     wird einmal.
 *   - Der Suchlauf laesst sich im Betrieb wiederholen, damit ein
 *     nachgeruesteter Fuehler ohne Neustart erscheint.
 *   - Die beiden bekannten Ausreisser werden verworfen und gezaehlt: 85,0 °C
 *     ist der Einschaltwert nach einem Spannungseinbruch, -127,0 °C zeigt eine
 *     unterbrochene Leitung an. Ein wackelnder Kontakt faellt so auf, statt
 *     die Auswertung zu verfaelschen.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OT_MAX_BUSES 2
#define OT_MAX_PROBES 16

typedef struct {
    uint64_t rom;     /* Kennung des Fuehlers, ab Werk eindeutig */
    uint8_t bus;      /* 0 oder 1 */
    float temp_c;     /* zuletzt gueltiger Messwert */
    bool valid;       /* false, solange nie ein gueltiger Wert kam */
    uint32_t age_ms;  /* Alter des Messwerts */
    uint32_t reads;   /* uebernommene Messungen */
    uint32_t errors;  /* verworfene Messungen */
} ot_probe_t;

typedef struct {
    ot_probe_t probes[OT_MAX_PROBES];
    size_t count;
    uint32_t round_ms;   /* Dauer der letzten Leserunde */
    uint32_t rounds;     /* Anzahl abgeschlossener Leserunden */
} ot_snapshot_t;

/*
 * Startet die Erfassung. pins nennt die GPIO der Busse; ein Eintrag kleiner
 * null wird uebergangen. period_ms ist der Abstand zwischen zwei Leserunden
 * und wird auf mindestens 1000 ms angehoben, weil eine Wandlung allein schon
 * 750 ms braucht.
 */
esp_err_t ot_start(const int *pins, size_t pin_count, uint32_t period_ms);

/* Kopie des letzten Standes. */
void ot_get(ot_snapshot_t *out);

/* Loest vor der naechsten Leserunde einen erneuten Suchlauf aus. */
void ot_rescan(void);

/* Schreibt die Kennung als 16 Hexziffern, etwa "28FF641E8016034A". */
void ot_rom_to_str(uint64_t rom, char *out, size_t len);

/* Liest eine so geschriebene Kennung zurueck. 0, wenn sie nicht passt. */
uint64_t ot_str_to_rom(const char *s);

#ifdef __cplusplus
}
#endif
