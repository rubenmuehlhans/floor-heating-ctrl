/*
 * Messwerte des anderen Heizungsgeraets.
 *
 * Kessel und Pufferspeicher stehen in verschiedenen Raeumen, also an zwei
 * Geraeten. Fuer den Ladezustand braucht das Geraet am Speicher aber die
 * Kesselwerte und den Brennerzustand. Es holt sie sich ueber denselben Weg wie
 * den Bedarf der Verteiler: ueber mDNS gefunden, per HTTP abgeholt, ohne
 * Broker.
 *
 * Bleiben sie aus, faellt die Ladeerkennung auf den Pufferfuehler allein
 * zurueck und weist das aus. Die Pumpensteuerung ist davon nicht beruehrt --
 * sie braucht nur die eigenen Fuehler.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_store.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[CFG_ID_LEN];
    char site[CFG_NAME_LEN];
    char host[16];
    bool seen;
    uint32_t age_s;
    uint32_t errors;
    uint8_t roles;      /* Zahl der uebermittelten Messstellen */
    bool burner_known;
    bool burner_running;
} remote_peer_t;

esp_err_t remote_start(void);

/* Messwert einer Rolle vom Nachbargeraet. */
bool remote_role_value(probe_role_t role, float *out_c, uint32_t *age_s);

/*
 * Traegt einen Wert ein, der nicht ueber /api/measurements hereinkommt. Die
 * Aussentemperatur ist so ein Fall: Sie wird an der Verteilerplatine ueber
 * Bluetooth empfangen und reist mit der Bedarfsantwort mit, weil die
 * Heizungsgeraete kein Bluetooth haben.
 */
void remote_set_value(probe_role_t role, float value_c);

/*
 * Von welcher Verteilerplatine die Aussentemperatur zuletzt kam, und wie alt
 * der Messwert dort war. false, solange keine geliefert hat.
 */
bool remote_outdoor_source(char *out, size_t len, uint32_t *age_s);

/* Brennerzustand des Nachbargeraets. */
bool remote_burner(bool *running);

/*
 * Erst der eigene Fuehler, dann der des Nachbargeraets. own meldet, woher der
 * Wert stammt; die Oberflaeche stellt fremde Werte blasser dar.
 */
bool any_role_value(probe_role_t role, float *out_c, bool *own);

size_t remote_peers(remote_peer_t *out, size_t max);

#ifdef __cplusplus
}
#endif
