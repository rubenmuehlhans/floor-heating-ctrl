#include "http.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_http_client.h"
#include "json.h"

namespace http {

namespace {
struct Sammler {
    std::string *body;
    std::string *etag;
};

esp_err_t ereignis(esp_http_client_event_t *e)
{
    auto *s = (Sammler *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && s->body->size() < 2 * 1024 * 1024) s->body->append((const char *)e->data, e->data_len);
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && !strcasecmp(e->header_key, "ETag")) *s->etag = e->header_value;
    return ESP_OK;
}
}  // namespace

Antwort anfrage(const std::string &ip, const char *methode, const std::string &pfad, const std::string &rumpf, const std::string &etag,
                int zeit_ms, int64_t ab)
{
    Antwort a;
    std::string url = "http://" + ip + pfad;
    Sammler s{&a.body, &a.etag};
    esp_http_client_config_t c = {};
    c.url = url.c_str();
    c.timeout_ms = zeit_ms;
    c.disable_auto_redirect = true;
    c.event_handler = ereignis;
    c.user_data = &s;
    c.buffer_size = 2048;
    c.method = !strcmp(methode, "POST") ? HTTP_METHOD_POST : !strcmp(methode, "PUT") ? HTTP_METHOD_PUT : HTTP_METHOD_GET;
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return a;
    if (!etag.empty()) esp_http_client_set_header(h, "If-None-Match", etag.c_str());
    char range[32];
    if (ab >= 0) {
        snprintf(range, sizeof range, "bytes=%lld-", (long long)ab);
        esp_http_client_set_header(h, "Range", range);
    }
    if (c.method != HTTP_METHOD_GET) {
        esp_http_client_set_header(h, "Content-Type", "application/json");
        esp_http_client_set_post_field(h, rumpf.data(), (int)rumpf.size());
    }
    if (esp_http_client_perform(h) == ESP_OK) a.status = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    return a;
}

std::string befehlsfehler(const Antwort &a)
{
    if (a.status == 0) return "Das Gerät antwortet nicht.";
    if (a.status == 404) return "Die Firmware des Geräts kennt diesen Befehl nicht.";
    json::Baum j = json::lesen(a.body);
    if (j && json::hat(j.get(), "ok") && !json::wahr(j.get(), "ok")) {
        std::string f = json::text(j.get(), "error");
        return f.empty() ? "Das Gerät hat den Befehl abgelehnt." : f;
    }
    if (!a.ok()) return "Das Gerät meldet Fehler " + std::to_string(a.status) + ".";
    return "";
}

}  // namespace http
