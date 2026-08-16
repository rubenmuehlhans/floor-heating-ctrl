/*
 * Geschwistergeraete im Netz finden.
 *
 * In einem Haus mit mehreren Etagen steht je Verteiler eine eigene Platine.
 * Damit man von der Oberflaeche der einen zu den anderen wechseln kann, meldet
 * jedes Geraet sich per mDNS unter einem eigenen Dienst an und hoert zugleich,
 * wer sonst noch da ist. Eingetragene Adressen sind dafuer nicht noetig.
 *
 * Gefunden wird nur; gesteuert wird weiterhin ausschliesslich das Geraet, mit
 * dessen Oberflaeche man gerade verbunden ist.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PEERS_MAX 8

typedef struct {
    char id[24];       /* Geraetekennung, etwa fbh_c2e55c */
    char site[32];     /* Bezeichnung der Anlage, etwa Obergeschoss */
    char host[16];     /* IPv4-Adresse */
    char hostname[32]; /* Name ohne .local */
    uint32_t seen_ms;  /* Zeitpunkt der letzten Antwort */
} peer_t;

/*
 * Meldet das eigene Geraet an und beginnt zu suchen.
 *
 * hostname, site und device_id stammen aus der Konfiguration beziehungsweise
 * der MAC. Ohne WLAN-Verbindung bleibt die Suche wirkungslos, sie wird
 * spaeter von selbst erfolgreich.
 */
esp_err_t peers_start(const char *hostname, const char *site, const char *device_id);

/* Uebernimmt eine geaenderte Anlagenbezeichnung oder einen neuen Namen. */
void peers_update_identity(const char *hostname, const char *site);

/* Kopiert die bekannten Geschwistergeraete, ohne das eigene. */
size_t peers_get(peer_t *out, size_t max);

#ifdef __cplusplus
}
#endif
