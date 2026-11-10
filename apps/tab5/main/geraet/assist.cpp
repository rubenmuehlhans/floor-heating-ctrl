// Assistent auf dem Tab5 — erfüllt ki.h: eigener Task mit eigener HTTPS-Verbindung zu
// api.anthropic.com, Strom (SSE), Werkzeugschleife in kern/gespraech.cpp.
//
// ⚠️⚠️ Eigener Task, nicht der Abfrage-Task: eine Antwort streamt bis zu einigen Minuten
// (Werkzeugrunden), und so lange stünde die Abfrage der Anlage still (camperSense, Falle 30).
// ⚠️ Ohne Zustimmung und ohne Schlüssel keine Anfrage. Der Schlüssel steht nur im NVS und
// geht in keine Ausgabe der Konsole.
#include <string.h>
#include <algorithm>
#include <deque>
#include <mutex>
#include "anweisungen.h"
#include "daten.h"
#include "eins.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "geraet.h"
#include "gespraech.h"
#include "ki.h"

static const char *TAG = "assist";

extern const char handbuch_md_start[] asm("_binary_handbuch_md_start");
extern const char handbuch_md_end[] asm("_binary_handbuch_md_end");
extern const char konzept_waermeerzeuger_md_start[] asm("_binary_konzept_waermeerzeuger_md_start");
extern const char konzept_waermeerzeuger_md_end[] asm("_binary_konzept_waermeerzeuger_md_end");
extern const char konzept_auswertung_md_start[] asm("_binary_konzept_auswertung_md_start");
extern const char konzept_auswertung_md_end[] asm("_binary_konzept_auswertung_md_end");

std::string wissen_text()
{
    auto dok = [](const char *name, const char *a, const char *e) {
        // EMBED_TXTFILES hängt eine Null an
        size_t n = (size_t)(e - a);
        if (n && a[n - 1] == 0) n--;
        return "<dokument name=\"" + std::string(name) + "\">\n" + std::string(a, n) + "\n</dokument>";
    };
    return dok("handbuch.md", handbuch_md_start, handbuch_md_end) + "\n\n" +
           dok("konzept-waermeerzeuger.md", konzept_waermeerzeuger_md_start, konzept_waermeerzeuger_md_end) + "\n\n" +
           dok("konzept-auswertung.md", konzept_auswertung_md_start, konzept_auswertung_md_end);
}

namespace {

enum class Auftrag : uint8_t { frage, bericht, uebernehmen, zuruecknehmen, pruefen };
struct Job {
    Auftrag art;
    std::string text;
};

gespraech::Assistent s_a;
std::mutex s_mx;
std::deque<Job> s_jobs;
TaskHandle_t s_task;
int64_t s_letzter_bericht;   // Unix-Sekunden
uint32_t s_befund_gen;

const char *MODELLE[] = {"claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-4-5"};

std::string modell()
{
    int i = (int)eins::zahl(eins::k::KI_MODELL, 0);
    return MODELLE[i < 0 || i > 2 ? 0 : i];
}

void einreihen(Job j)
{
    {
        std::lock_guard<std::mutex> l(s_mx);
        s_jobs.push_back(std::move(j));
    }
    if (s_task) xTaskNotifyGive(s_task);
}

// HTTPS mit Strom. Gehaltene Verbindung: ein TLS-Aufbau kostet am P4 rund 2 s.
esp_http_client_handle_t s_h;

int senden(const std::string &rumpf, std::function<bool(const char *, size_t)> empfang, std::string &fehlerkoerper)
{
    std::string schluessel = eins::text(eins::k::KI_SCHLUESSEL);
    if (schluessel.empty()) return 401;
    if (!s_h) {
        esp_http_client_config_t c = {};
        c.url = "https://api.anthropic.com/v1/messages";
        c.method = HTTP_METHOD_POST;
        c.crt_bundle_attach = esp_crt_bundle_attach;
        // ⚠️ Zwischen zwei Ereignissen kann eine lange Überlegung liegen; 120 s wie die App
        c.timeout_ms = 120000;
        c.buffer_size = 4096;
        c.buffer_size_tx = 2048;
        c.keep_alive_enable = true;
        c.user_agent = "heizung-tafel/1";
        s_h = esp_http_client_init(&c);
        if (!s_h) return 0;
    }
    esp_http_client_handle_t h = s_h;
    int status = 0;
    for (int versuch = 0; versuch < 2 && status == 0; versuch++) {
        esp_http_client_set_url(h, "https://api.anthropic.com/v1/messages");
        esp_http_client_set_method(h, HTTP_METHOD_POST);
        esp_http_client_set_header(h, "x-api-key", schluessel.c_str());
        esp_http_client_set_header(h, "anthropic-version", "2023-06-01");
        esp_http_client_set_header(h, "content-type", "application/json");
        esp_http_client_set_header(h, "accept", "text/event-stream");
        int64_t t0 = esp_timer_get_time();
        if (esp_http_client_open(h, (int)rumpf.size()) == ESP_OK) {
            // Rumpf in Stücken: System mit Handbuch rund 170 KB
            size_t gesendet = 0;
            while (gesendet < rumpf.size()) {
                int n = esp_http_client_write(h, rumpf.data() + gesendet, (int)std::min<size_t>(8192, rumpf.size() - gesendet));
                if (n <= 0) break;
                gesendet += n;
            }
            if (gesendet == rumpf.size() && esp_http_client_fetch_headers(h) >= 0) status = esp_http_client_get_status_code(h);
        }
        ESP_LOGI(TAG, "POST /v1/messages (%u B) -> %d nach %d ms", (unsigned)rumpf.size(), status,
                 (int)((esp_timer_get_time() - t0) / 1000));
        if (status == 0) esp_http_client_close(h);   // gehaltene Verbindung war tot: einmal neu
    }
    schluessel.assign(schluessel.size(), '\0');
    char buf[1536];
    int n;
    if (status != 200) {
        while (status && fehlerkoerper.size() < 65536 && (n = esp_http_client_read(h, buf, sizeof buf)) > 0) fehlerkoerper.append(buf, n);
        esp_http_client_close(h);
        return status;
    }
    bool weiter = true;
    while (weiter && (n = esp_http_client_read(h, buf, sizeof buf)) > 0) weiter = empfang(buf, (size_t)n);
    // Nur eine vollständig gelesene Antwort lässt die Verbindung für die nächste frei
    if (!weiter || !esp_http_client_is_complete_data_received(h)) esp_http_client_close(h);
    return status;
}

void speichern()
{
    std::string d = s_a.speichern();
    FILE *f = fopen("/d/assistent.json.neu", "wb");
    if (!f) return;
    fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    rename("/d/assistent.json.neu", "/d/assistent.json");
}

void laden()
{
    FILE *f = fopen("/d/assistent.json", "rb");
    if (!f) return;
    std::string d;
    char buf[2048];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.append(buf, n);
    fclose(f);
    s_a.laden(d);
}

void task(void *)
{
    // Wissen und Systemtext einmal bauen (rund 170 KB im PSRAM)
    ESP_LOGI(TAG, "Systemanweisungen: %u Zeichen", (unsigned)anweisungen::system().size());
    laden();
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
        // Lagebericht: von selbst höchstens alle sechs Stunden, nach neuen Befunden frühestens nach 30 min
        auto b = daten::bild();
        int64_t jetzt = daten::jetzt();
        bool kann = ki::hindernis().empty() && !b->beispiel && b->vollstaendig;
        if (!kann || eins::zahl(eins::k::KI_BERICHT, 1) == 0) s_a.bericht_aus_befunden();
        else {
            static int64_t neue_befunde_seit;
            uint32_t g = (uint32_t)b->befunde.size() * 131 + (b->befunde.empty() ? 0 : b->befunde.front().id.size());
            if (g != s_befund_gen) {
                s_befund_gen = g;
                neue_befunde_seit = jetzt;
            }
            bool faellig = jetzt - s_letzter_bericht >= 6 * 3600 ||
                           (neue_befunde_seit > s_letzter_bericht && jetzt - neue_befunde_seit >= 1800 && jetzt - s_letzter_bericht >= 1800);
            if (faellig) einreihen({Auftrag::bericht, ""});
        }
        while (true) {
            Job j;
            {
                std::lock_guard<std::mutex> l(s_mx);
                if (s_jobs.empty()) break;
                j = std::move(s_jobs.front());
                s_jobs.pop_front();
            }
            s_a.modell_setzen(modell());
            switch (j.art) {
            case Auftrag::frage: s_a.frage(j.text); break;
            case Auftrag::bericht:
                s_letzter_bericht = daten::jetzt();
                s_a.lagebericht();
                break;
            case Auftrag::uebernehmen: s_a.uebernehmen(j.text); break;
            case Auftrag::zuruecknehmen: s_a.zuruecknehmen(j.text); break;
            case Auftrag::pruefen: {
                std::string f = s_a.verbindung_pruefen();
                ESP_LOGI(TAG, "Verbindungstest: %s", f.empty() ? "in Ordnung" : f.c_str());
                daten::melden(f.empty() ? "Die Verbindung zu Anthropic steht." : f, !f.empty());
                break;
            }
            }
            speichern();
        }
    }
}

}  // namespace

namespace ki {

void start()
{
    s_a.senden_setzen(senden);
    s_a.schlafen_setzen([](int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });
    // ⚠️ Großer Stapel: cJSON-Bäume der Werkzeuge und TLS; im PSRAM erlaubt.
    xTaskCreate(task, "assist", 16384, nullptr, 3, &s_task);
}

std::string hindernis()
{
    if (eins::text(eins::k::KI_SCHLUESSEL).empty())
        return "Für Claude braucht der Assistent einen API-Schlüssel von Anthropic. Hinterlegen Sie ihn unter Geräte › Einstellungen der Tafel.";
    if (eins::zahl(eins::k::KI_ZUSTIMMUNG, 0) != 1)
        return "Für Claude braucht die Tafel Ihre Zustimmung, Daten der Anlage an Anthropic zu übertragen.";
    return "";
}

bool braucht_zustimmung() { return !eins::text(eins::k::KI_SCHLUESSEL).empty() && eins::zahl(eins::k::KI_ZUSTIMMUNG, 0) != 1; }

const char *modellname()
{
    static std::string n;
    n = claude::modellname(modell());
    return n.c_str();
}

const char *modell_id()
{
    static std::string n;
    n = modell();
    return n.c_str();
}

Stand stand() { return s_a.stand(); }
uint32_t generation() { return s_a.generation(); }

bool fragen(const std::string &text)
{
    if (!hindernis().empty()) return false;
    Stand st = s_a.stand();
    if (st.lage == Lage::liest || st.lage == Lage::antwortet) return false;
    s_a.status(Lage::liest, st.bericht_laeuft);
    einreihen({Auftrag::frage, text});
    return true;
}

void anhalten() { s_a.anhalten(); }
void neues_gespraech() { s_a.neues_gespraech(); }
void bezug_setzen(const std::string &b) { s_a.bezug_setzen(b); }
void entwurf_setzen(const std::string &t) { s_a.entwurf_setzen(t); }
void uebernehmen(const std::string &v) { einreihen({Auftrag::uebernehmen, v}); }
void verwerfen(const std::string &v) { s_a.verwerfen(v); }
void zuruecknehmen(const std::string &v) { einreihen({Auftrag::zuruecknehmen, v}); }
void lagebericht_erstellen()
{
    if (hindernis().empty()) einreihen({Auftrag::bericht, ""});
}

void zustimmen(bool ja)
{
    if (ja) eins::zahl_setzen(eins::k::KI_ZUSTIMMUNG, 1);
    else {
        eins::loeschen(eins::k::KI_ZUSTIMMUNG);
        s_a.anhalten();   // Widerruf bricht eine laufende Antwort ab
    }
}

void verbindung_pruefen() { einreihen({Auftrag::pruefen, ""}); }

}  // namespace ki
