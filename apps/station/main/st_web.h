/*
 * Weboberflaeche und HTTP-Schnittstelle des Leitstands.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t st_web_start(void);

/* Kennung des Geraets, etwa lst_a1b2c3 */
void st_device_id(char *out, size_t len);

#ifdef __cplusplus
}
#endif
