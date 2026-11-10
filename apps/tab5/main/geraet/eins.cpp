// Einstellungen im NVS, Namensraum „tafel“ (eins.h).
#include "eins.h"

#include <atomic>
#include "esp_log.h"
#include "nvs.h"

namespace eins {

static nvs_handle_t s_h;
static std::atomic<uint32_t> s_gen{1};

void init()
{
    if (nvs_open("tafel", NVS_READWRITE, &s_h) != ESP_OK) ESP_LOGE("eins", "NVS nicht geöffnet");
}

std::string text(const char *key, const char *vorgabe)
{
    size_t n = 0;
    if (!s_h || nvs_get_str(s_h, key, nullptr, &n) != ESP_OK || n == 0) return vorgabe;
    std::string s(n, '\0');
    nvs_get_str(s_h, key, s.data(), &n);
    s.resize(n ? n - 1 : 0);
    return s;
}

void text_setzen(const char *key, const std::string &wert)
{
    if (!s_h) return;
    nvs_set_str(s_h, key, wert.c_str());
    nvs_commit(s_h);
    s_gen++;
}

int64_t zahl(const char *key, int64_t vorgabe)
{
    int64_t v;
    return s_h && nvs_get_i64(s_h, key, &v) == ESP_OK ? v : vorgabe;
}

void zahl_setzen(const char *key, int64_t wert)
{
    if (!s_h) return;
    nvs_set_i64(s_h, key, wert);
    nvs_commit(s_h);
    s_gen++;
}

void loeschen(const char *key)
{
    if (!s_h) return;
    nvs_erase_key(s_h, key);
    nvs_commit(s_h);
    s_gen++;
}

uint32_t generation() { return s_gen; }

}  // namespace eins
