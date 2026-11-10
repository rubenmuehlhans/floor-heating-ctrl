#include "json.h"

#include <string.h>

namespace json {

Baum lesen(const std::string &text) { return Baum(cJSON_ParseWithLength(text.data(), text.size())); }

std::string schreiben(const cJSON *j)
{
    if (!j) return "null";
    char *s = cJSON_PrintUnformatted(j);
    std::string r = s ? s : "null";
    cJSON_free(s);
    return r;
}

const cJSON *weg(const cJSON *j, const char *pfad)
{
    if (!j || !pfad) return nullptr;
    char teil[48];
    const char *p = pfad;
    while (j && *p) {
        const char *punkt = strchr(p, '.');
        size_t n = punkt ? (size_t)(punkt - p) : strlen(p);
        if (n >= sizeof teil) return nullptr;
        memcpy(teil, p, n);
        teil[n] = 0;
        j = cJSON_GetObjectItemCaseSensitive(j, teil);
        p = punkt ? punkt + 1 : p + n;
    }
    return j;
}

float zahl(const cJSON *j, const char *pfad, float vorgabe)
{
    const cJSON *w = weg(j, pfad);
    return cJSON_IsNumber(w) ? (float)w->valuedouble : vorgabe;
}

double zahl_d(const cJSON *j, const char *pfad, double vorgabe)
{
    const cJSON *w = weg(j, pfad);
    return cJSON_IsNumber(w) ? w->valuedouble : vorgabe;
}

int ganz(const cJSON *j, const char *pfad, int vorgabe)
{
    const cJSON *w = weg(j, pfad);
    return cJSON_IsNumber(w) ? (int)w->valuedouble : vorgabe;
}

bool wahr(const cJSON *j, const char *pfad, bool vorgabe)
{
    const cJSON *w = weg(j, pfad);
    return cJSON_IsBool(w) ? cJSON_IsTrue(w) : vorgabe;
}

bool hat(const cJSON *j, const char *pfad)
{
    const cJSON *w = weg(j, pfad);
    return w && !cJSON_IsNull(w);
}

std::string text(const cJSON *j, const char *pfad, const char *vorgabe)
{
    const cJSON *w = weg(j, pfad);
    return cJSON_IsString(w) && w->valuestring ? std::string(w->valuestring) : std::string(vorgabe);
}

Liste liste(const cJSON *j, const char *pfad)
{
    const cJSON *w = pfad && *pfad ? weg(j, pfad) : j;
    return Liste{cJSON_IsArray(w) ? w->child : nullptr};
}

void ohne_zugangsdaten(cJSON *j)
{
    static const char *SCHLUESSEL[] = {"pass", "ap_pass", "password", "user", "ssid", "uri", "host", "topic", "bindkey"};
    if (!j) return;
    if (cJSON_IsObject(j)) {
        for (const char *k : SCHLUESSEL) cJSON_DeleteItemFromObjectCaseSensitive(j, k);
    }
    for (cJSON *c = j->child; c; c = c->next) ohne_zugangsdaten(c);
}

}  // namespace json
