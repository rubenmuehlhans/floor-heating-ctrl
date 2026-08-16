/*
 * MQTT-Anbindung, die auch fremde Themen bedienen kann.
 *
 * Die Verteiler-Firmware hat eine eigene Anbindung in app_mqtt.c. Sie ist auf
 * das eigene Praefix zugeschnitten: eingehende Themen, die nicht damit
 * beginnen, werden verworfen, und eine Sendefunktion ist nicht ausgefuehrt.
 * Das Heizungsgeraet muss aber fremde Themen beschreiben (cmnd/... an das
 * Tasmota-Relais) und deren Rueckmeldungen mithoeren (stat/..., tele/.../LWT).
 *
 * Deshalb hier ein schmaler Unterbau: Verbindung, letzter Wille, Senden und
 * eine Abo-Tabelle mit Rueckruf. Was gesendet wird, entscheidet die Anwendung.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTTC_MAX_SUBS 8

typedef struct {
    const char *uri;       /* mqtt://rechner:1883 */
    const char *user;      /* darf leer sein */
    const char *pass;
    const char *client_id;
    const char *lwt_topic; /* darf NULL sein */
    const char *lwt_msg;
    const char *online_msg; /* wird bei Verbindung auf lwt_topic gesendet */
} mqttc_cfg_t;

/* payload ist nullterminiert. */
typedef void (*mqttc_cb_t)(const char *topic, const char *payload, void *ctx);

/* Wird bei jedem Verbindungsaufbau gerufen, damit die Anwendung ihre Abos
 * erneuern und ihren Zustand nachsenden kann. */
typedef void (*mqttc_connected_cb_t)(void *ctx);

esp_err_t mqttc_start(const mqttc_cfg_t *cfg, mqttc_connected_cb_t on_connected, void *ctx);

bool mqttc_connected(void);

/* qos 0, retain nach Wahl. Ohne Verbindung ist das Ergebnis ESP_ERR_INVALID_STATE. */
esp_err_t mqttc_publish(const char *topic, const char *payload, bool retain);

/*
 * Abonniert ein Thema. Die Tabelle bleibt ueber Verbindungsabbrueche hinweg
 * bestehen und wird bei jedem Aufbau erneut angemeldet. Ein zweiter Aufruf mit
 * demselben Thema ersetzt den Rueckruf.
 */
esp_err_t mqttc_subscribe(const char *topic, mqttc_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
