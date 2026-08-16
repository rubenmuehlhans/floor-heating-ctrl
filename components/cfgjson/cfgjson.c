#include "cfgjson.h"

#include <stdio.h>
#include <stdlib.h>

#include "nvs.h"

esp_err_t cfgjson_save(const char *ns, const char *key, const char *json)
{
    if (ns == NULL || key == NULL || json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, key, json);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t cfgjson_load(const char *ns, const char *key, char **out)
{
    if (ns == NULL || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = 0;
    err = nvs_get_str(h, key, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(h);
        return err == ESP_OK ? ESP_ERR_NVS_NOT_FOUND : err;
    }

    char *buf = malloc(len);
    if (buf == NULL) {
        nvs_close(h);
        return ESP_ERR_NO_MEM;
    }
    err = nvs_get_str(h, key, buf, &len);
    nvs_close(h);

    if (err != ESP_OK) {
        free(buf);
        return err;
    }
    *out = buf;
    return ESP_OK;
}

void cfgjson_str(const cJSON *obj, const char *key, char *dst, size_t len)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        snprintf(dst, len, "%s", it->valuestring);
    }
}

double cfgjson_num(const cJSON *obj, const char *key, double fallback)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(it) ? it->valuedouble : fallback;
}

bool cfgjson_bool(const cJSON *obj, const char *key, bool fallback)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(it) ? cJSON_IsTrue(it) : fallback;
}
