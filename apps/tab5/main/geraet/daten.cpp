// Abfrage der Anlage und Befehle — erfüllt daten.h auf dem Tab5.
//
// Ein Task („abfrage“) findet die Geräte per mDNS (_fbhctrl._tcp), fragt sie im Takt der App
// ab (Anlagenbetrieb.swift) und baut nach jeder Änderung ein neues Bild:
//   Verteiler /api/state mit ETag alle 6 s, ohne ETag jede Minute, 1 s wenn beschäftigt;
//   Heizungsgerät /api/state alle 5 s; /api/config alle 5 min; Verteiler /api/ble alle 30 s;
//   Leitstand /api/state alle 10 s.
// Erst die zweite Fehlabfrage in Folge gilt als Ausfall.
//
// Befehle reiht die Oberfläche ein; der Task sendet sie, liest den Zustand zurück (bis zu
// drei Versuche im Abstand von 600 ms) und meldet „nicht übernommen“, wenn die Firmware
// quittiert, ohne auszuführen.
//
// Bis ein echtes Gerät antwortet, zeigt die Tafel die Beispielanlage (main/beispiel).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <atomic>
#include <deque>
#include <tuple>
#include <map>
#include <mutex>
#include "daten.h"
#include "eins.h"
#include "fmt.h"
#include "geraet.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http.h"
#include "json.h"
#include "mdns.h"

static const char *TAG = "abfrage";

extern const char beispiel_json_start[] asm("_binary_beispiel_json_start");
extern const char beispiel_json_end[] asm("_binary_beispiel_json_end");

namespace daten {

namespace {

struct Eintrag {
    anlage::Geraetestand s;
    std::string etag;
    int fehler_folge = 0;
    int64_t naechst_state = 0, naechst_config = 0, naechst_ble = 0, letzt_voll = 0;
    bool beschaeftigt = false;
};

struct Befehl {
    std::string geraet, methode, pfad, rumpf, gelungen;
    std::function<bool(const cJSON *)> pruefen;   // Rücklesen; nullptr = keine Prüfung
};

std::mutex s_mx;                       // schützt alles hier
std::vector<Eintrag> s_geraete;
std::shared_ptr<const anlage::Bild> s_bild = std::make_shared<anlage::Bild>();
std::atomic<uint32_t> s_gen{1};
befunde::Gedaechtnis s_gedaechtnis;
verlauf::Speicher s_verlauf;
std::deque<Befehl> s_befehle;
std::map<std::string, std::pair<float, int64_t>> s_soll;   // Raum → (Wert, ms der letzten Änderung)
Rueckmeldung s_rm;
std::atomic<bool> s_aendern{true}, s_wecken{false};
bool s_echt;                           // ein echtes Gerät hat geantwortet
int64_t s_beispiel_stand;
std::vector<anlage::Geraetestand> s_beispiel;
bool s_fs;

int64_t ms() { return esp_timer_get_time() / 1000; }
int64_t uhr() { return (int64_t)time(nullptr); }
bool uhr_gueltig() { return uhr() > 1700000000; }

void melden_intern(const std::string &t, bool fehler)
{
    std::lock_guard<std::mutex> l(s_mx);
    s_rm.nr++;
    s_rm.text = t;
    s_rm.fehler = fehler;
}

// ---- Ablage in LittleFS --------------------------------------------------------------

bool datei_lesen(const char *p, std::string &out)
{
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

void datei_schreiben(const char *p, const std::string &d)
{
    std::string tmp = std::string(p) + ".neu";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    rename(tmp.c_str(), p);   // ⚠️ atomar: ein Stromausfall lässt die alte Fassung stehen
}

void ablage_start()
{
    esp_vfs_littlefs_conf_t c = {};
    c.base_path = "/d";
    c.partition_label = "cache";
    c.format_if_mount_failed = true;
    s_fs = esp_vfs_littlefs_register(&c) == ESP_OK;
    if (!s_fs) {
        ESP_LOGE(TAG, "LittleFS nicht eingebunden");
        return;
    }
    std::string d;
    if (datei_lesen("/d/verlauf.bin", d) && s_verlauf.laden(d)) ESP_LOGI(TAG, "Verlauf geladen, %u Reihen", (unsigned)s_verlauf.schluessel().size());
    if (datei_lesen("/d/befunde.json", d)) s_gedaechtnis.laden(d);
    // Bekannte Geräte: sofort abfragen, ohne auf mDNS zu warten
    if (datei_lesen("/d/geraete.json", d)) {
        json::Baum j = json::lesen(d);
        for (const cJSON *g : json::liste(j.get(), "")) {
            Eintrag e;
            e.s.id = json::text(g, "id");
            e.s.art = (anlage::Art)json::ganz(g, "art");
            e.s.ort = json::text(g, "ort");
            e.s.ip = json::text(g, "ip");
            e.s.hostname = json::text(g, "host");
            if (!e.s.id.empty() && !e.s.ip.empty()) s_geraete.push_back(e);
        }
    }
}

void geraete_speichern()
{
    if (!s_fs) return;
    cJSON *a = cJSON_CreateArray();
    for (const auto &e : s_geraete) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", e.s.id.c_str());
        cJSON_AddNumberToObject(o, "art", (int)e.s.art);
        cJSON_AddStringToObject(o, "ort", e.s.ort.c_str());
        cJSON_AddStringToObject(o, "ip", e.s.ip.c_str());
        cJSON_AddStringToObject(o, "host", e.s.hostname.c_str());
        cJSON_AddItemToArray(a, o);
    }
    datei_schreiben("/d/geraete.json", json::schreiben(a));
    cJSON_Delete(a);
}

// ---- Bild ------------------------------------------------------------------------------

void beispiel_laden()
{
    std::string t(beispiel_json_start, beispiel_json_end - beispiel_json_start);
    json::Baum j = json::lesen(t);
    s_beispiel_stand = (int64_t)json::zahl_d(j.get(), "stand", 0);
    for (const cJSON *g : json::liste(j.get(), "geraete")) {
        anlage::Geraetestand s;
        s.id = json::text(g, "device.id");
        s.art = anlage::art_aus(s.id, json::text(g, "device.role"));
        s.ort = json::text(g, "device.site");
        s.ip = json::text(g, "net.ip");
        s.erreichbar = s.gesehen = true;
        s.kontakt = s_beispiel_stand;
        s.state = json::schreiben(g);
        s_beispiel.push_back(s);
    }
    for (const cJSON *k : json::liste(j.get(), "konfig"))
        for (auto &s : s_beispiel)
            if (s.id == json::text(k, "id")) s.config = json::schreiben(json::weg(k, "config"));
}

void bild_bauen()
{
    std::vector<anlage::Geraetestand> st;
    bool echt;
    {
        std::lock_guard<std::mutex> l(s_mx);
        echt = s_echt;
        if (echt)
            for (const auto &e : s_geraete) st.push_back(e.s);
    }
    int64_t jetzt = echt ? uhr() : s_beispiel_stand;
    auto b = anlage::zusammenfuehren(echt ? st : s_beispiel, jetzt);
    b->beispiel = !echt;
    if (!echt) b->vollstaendig = true;
    auto bef = befunde::pruefen(echt ? st : s_beispiel, *b);
    {
        std::lock_guard<std::mutex> l(s_mx);
        if (echt) {
            uint32_t g0 = s_gedaechtnis.generation();
            b->befunde = s_gedaechtnis.fortschreiben(bef, jetzt, b->vollstaendig);
            (void)g0;
            if (uhr_gueltig()) s_verlauf.abtasten(jetzt, verlauf::abtastung(*b));
        } else {
            // Beispiel: die Befunde stehen schon lange an (eigenes Gedächtnis, nicht gespeichert)
            static befunde::Gedaechtnis g;
            static bool erst = true;
            if (erst)
                for (int64_t t = jetzt - 86400; t < jetzt; t += 1800) g.fortschreiben(bef, t, true);
            erst = false;
            b->befunde = g.fortschreiben(bef, jetzt, true);
        }
        s_bild = b;
    }
    s_gen++;
}

// ---- Abfrage eines Geräts ----------------------------------------------------------------

bool beschaeftigt(const cJSON *st)
{
    if (json::text(st, "calib.state") == "running" || json::wahr(st, "seize.running")) return true;
    for (const cJSON *c : json::liste(st, "channels"))
        if (json::text(c, "op", "idle") != "idle" || json::wahr(c, "pending") || json::wahr(c, "reserved")) return true;
    return false;
}

void abfragen(Eintrag &e, int64_t t)
{
    bool vt = e.s.art == anlage::Art::verteiler;
    if (t >= e.naechst_state) {
        bool voll = !vt || t - e.letzt_voll >= 60000;
        http::Antwort a = http::anfrage(e.s.ip, "GET", "/api/state", "", voll ? "" : e.etag);
        int takt = vt ? (e.beschaeftigt ? 1000 : 6000) : e.s.art == anlage::Art::leitstand ? 10000 : 5000;
        e.naechst_state = t + takt;
        if (a.status == 304) {
            std::lock_guard<std::mutex> l(s_mx);
            e.fehler_folge = 0;
            e.s.kontakt = uhr();
        } else if (a.ok() && !a.body.empty()) {
            json::Baum j = json::lesen(a.body);
            std::lock_guard<std::mutex> l(s_mx);
            e.fehler_folge = 0;
            e.s.erreichbar = e.s.gesehen = true;
            e.s.fehler.clear();
            e.s.kontakt = uhr();
            e.s.state = std::move(a.body);
            e.etag = a.etag;
            if (voll) e.letzt_voll = t;
            if (j) {
                e.beschaeftigt = vt && beschaeftigt(j.get());
                std::string site = json::text(j.get(), "device.site");
                if (!site.empty()) e.s.ort = site;
            }
            s_echt = true;
            s_aendern = true;
        } else {
            std::lock_guard<std::mutex> l(s_mx);
            if (++e.fehler_folge >= 2 && e.s.erreichbar) {
                e.s.erreichbar = false;
                e.s.fehler = a.status ? "HTTP " + std::to_string(a.status) : "keine Antwort";
                s_aendern = true;
            } else if (e.fehler_folge >= 2 && e.s.fehler.empty()) {
                e.s.fehler = "keine Antwort";
                s_aendern = true;
            }
        }
    }
    if (!e.s.erreichbar) return;
    if (e.s.art != anlage::Art::leitstand && t >= e.naechst_config) {
        http::Antwort a = http::anfrage(e.s.ip, "GET", "/api/config");
        e.naechst_config = t + (a.ok() ? 300000 : 30000);
        if (a.ok()) {
            json::Baum j = json::lesen(a.body);
            if (j) json::ohne_zugangsdaten(j.get());   // ⚠️ Kennwörter nie im Speicher halten
            std::lock_guard<std::mutex> l(s_mx);
            e.s.config = j ? json::schreiben(j.get()) : "";
            s_aendern = true;
        }
    }
    if (vt && t >= e.naechst_ble) {
        http::Antwort a = http::anfrage(e.s.ip, "GET", "/api/ble");
        e.naechst_ble = t + 30000;
        if (a.ok()) {
            json::Baum j = json::lesen(a.body);
            if (j) json::ohne_zugangsdaten(j.get());
            std::lock_guard<std::mutex> l(s_mx);
            e.s.ble = j ? json::schreiben(j.get()) : "";
        }
    }
}

// ---- Geräte finden ---------------------------------------------------------------------

void suchen()
{
    mdns_result_t *r = nullptr;
    if (mdns_query_ptr("_fbhctrl", "_tcp", 3000, 20, &r) != ESP_OK) return;
    bool neu = false;
    for (mdns_result_t *x = r; x; x = x->next) {
        std::string id, site, role;
        for (size_t i = 0; i < x->txt_count; i++) {
            if (!x->txt[i].key || !x->txt[i].value) continue;
            if (!strcmp(x->txt[i].key, "id")) id = x->txt[i].value;
            if (!strcmp(x->txt[i].key, "site")) site = x->txt[i].value;
            if (!strcmp(x->txt[i].key, "role")) role = x->txt[i].value;
        }
        std::string ip;
        for (mdns_ip_addr_t *a = x->addr; a; a = a->next)
            if (a->addr.type == ESP_IPADDR_TYPE_V4) {
                char b[16];
                snprintf(b, sizeof b, IPSTR, IP2STR(&a->addr.u_addr.ip4));
                ip = b;
            }
        if (id.empty() || ip.empty()) continue;
        std::lock_guard<std::mutex> l(s_mx);
        Eintrag *e = nullptr;
        for (auto &g : s_geraete)
            if (g.s.id == id) e = &g;
        if (!e) {
            s_geraete.push_back({});
            e = &s_geraete.back();
            e->s.id = id;
            neu = true;
            ESP_LOGI(TAG, "neues Gerät %s (%s) unter %s", id.c_str(), site.c_str(), ip.c_str());
        }
        if (e->s.ip != ip) neu = true;
        e->s.ip = ip;
        e->s.art = anlage::art_aus(id, role);
        if (!site.empty()) e->s.ort = site;
        if (x->hostname) e->s.hostname = x->hostname;
    }
    mdns_query_results_free(r);
    if (neu) geraete_speichern();
}

// ---- Befehle ---------------------------------------------------------------------------

Eintrag *finden(const std::string &id)
{
    for (auto &e : s_geraete)
        if (e.s.id == id) return &e;
    return nullptr;
}

void ausfuehren(const Befehl &b)
{
    std::string ip;
    {
        std::lock_guard<std::mutex> l(s_mx);
        Eintrag *e = finden(b.geraet);
        if (!e) {
            s_rm = {s_rm.nr + 1, "Das Gerät ist nicht bekannt.", true};
            return;
        }
        ip = e->s.ip;
    }
    http::Antwort a = http::anfrage(ip, b.methode.c_str(), b.pfad, b.rumpf);
    std::string f = http::befehlsfehler(a);
    if (!f.empty()) {
        melden_intern(f, true);
        return;
    }
    // Rücklesen: bis zu drei Versuche im Abstand von 600 ms
    bool gut = !b.pruefen;
    for (int i = 0; i < 3 && !gut; i++) {
        vTaskDelay(pdMS_TO_TICKS(600));
        http::Antwort z = http::anfrage(ip, "GET", "/api/state");
        if (!z.ok()) continue;
        json::Baum j = json::lesen(z.body);
        gut = j && b.pruefen(j.get());
        std::lock_guard<std::mutex> l(s_mx);
        if (Eintrag *e = finden(b.geraet)) {
            e->s.state = z.body;
            e->s.kontakt = uhr();
            e->etag.clear();
        }
        s_aendern = true;
    }
    melden_intern(gut ? b.gelungen : "Das Gerät hat den Befehl quittiert, aber nicht übernommen.", !gut);
}

void einreihen(Befehl b)
{
    {
        std::lock_guard<std::mutex> l(s_mx);
        if (!s_echt) {
            s_rm = {s_rm.nr + 1, "In der Beispielanlage wirken Befehle nicht.", false};
            return;
        }
        s_befehle.push_back(std::move(b));
    }
    s_wecken = true;
}

const cJSON *raum_in(const cJSON *st, int nr)
{
    for (const cJSON *r : json::liste(st, "rooms"))
        if (json::ganz(r, "id") == nr) return r;
    return nullptr;
}

bool raum_teilen(const std::string &id, std::string &geraet, int &nr)
{
    size_t p = id.find('/');
    if (p == std::string::npos) return false;
    geraet = id.substr(0, p);
    nr = atoi(id.c_str() + p + 1);
    return nr > 0;
}

std::string raumname(const std::string &id)
{
    auto b = bild();
    const anlage::Raum *r = b->raum(id);
    return r ? r->name : "Raum";
}

// Speichergerät bzw. Gerät mit Kesselkreispumpe
std::string geraet_mit(anlage::Rolle rolle)
{
    auto b = bild();
    for (const auto &g : b->geraete)
        if (g.art == anlage::Art::heizung && (g.rolle == rolle || g.rolle == anlage::Rolle::beides)) return g.id;
    return "";
}

// ---- Verlauf vom Leitstand --------------------------------------------------------------
// Wie Leitstandabgleich.swift der App: Der Leitstand schreibt je Gerät und Tag eine Datei
// `<kennung>[.<teil>].5min.csv` (Kopfzeile `zeit,<schlüssel…>`, Zeit am Beginn des Platzes in
// UTC). Die Tafel holt je Datei nur das Neue (Range) und übernimmt die Werte mit Vorrang vor
// der eigenen Aufzeichnung — der Leitstand zeichnet lückenlos auf, die Tafel nur, solange sie
// läuft. Schlüssel wie docs/katalog-messgroessen.md: Räume und Kanäle der Verteiler bekommen die
// Kennung davor, die Werte der Heizungsgeräte und `aussen` gelten anlagenweit.

struct LsDatei {
    int64_t stelle = 0;
    std::vector<std::string> spalten;
};
std::map<std::string, LsDatei> s_ls;   // „tag/name“
std::atomic<int64_t> s_ls_zuletzt{0};
std::atomic<int> s_ls_plaetze{0};

std::string ls_schluessel(const std::string &kennung, const std::string &k)
{
    bool vt = kennung.rfind("fbh_", 0) == 0, hz = kennung.rfind("heiz_", 0) == 0;
    if (vt && (k.rfind("raum.", 0) == 0 || k.rfind("kanal.", 0) == 0)) return kennung + "/" + k;
    if (vt && k == "aussen") return "aussen";
    if (hz && (k.rfind("fuehler.", 0) == 0 || k == "brenner" || k == "fuellstand" || k.rfind("pumpe.", 0) == 0 || k == "kkp")) return k;
    return "";
}

// Übernimmt die vollständigen Zeilen ab der Stelle; gibt die Zahl der Plätze zurück.
int ls_lesen(LsDatei &d, const std::string &kennung, const std::string &daten)
{
    size_t a = 0;
    int plaetze = 0;
    std::vector<std::tuple<std::string, int64_t, float>> werte;
    while (true) {
        size_t e = daten.find('\n', a);
        if (e == std::string::npos) break;   // ⚠️ nur vollständige Zeilen; der Rest kommt beim nächsten Mal
        std::string z = daten.substr(a, e - a);
        if (!z.empty() && z.back() == '\r') z.pop_back();
        std::vector<std::string> f;
        size_t p = 0;
        while (true) {
            size_t q = z.find(',', p);
            f.push_back(z.substr(p, q == std::string::npos ? std::string::npos : q - p));
            if (q == std::string::npos) break;
            p = q + 1;
        }
        if (d.spalten.empty()) d.spalten = f;   // Kopfzeile
        else if (f.size() == d.spalten.size()) {
            int64_t t = fmt::aus_iso_utc(f[0]);
            if (t) {
                plaetze++;
                for (size_t i = 1; i < f.size(); i++) {
                    if (f[i].empty()) continue;
                    std::string k = ls_schluessel(kennung, d.spalten[i]);
                    if (!k.empty()) werte.push_back({k, t, strtof(f[i].c_str(), nullptr)});
                }
            }
        }
        a = e + 1;
    }
    d.stelle += (int64_t)a;
    if (!werte.empty()) {
        std::lock_guard<std::mutex> l(s_mx);
        for (auto &w : werte) s_verlauf.setzen(std::get<0>(w), std::get<1>(w), std::get<2>(w));
    }
    return plaetze;
}

void ls_abgleich(const std::string &ip)
{
    http::Antwort a = http::anfrage(ip, "GET", "/api/log/days", "", "", 15000);
    if (!a.ok()) return;
    json::Baum j = json::lesen(a.body);
    std::string ab_tag = fmt::tag_utc(uhr() - 8 * 86400);   // sieben Tage reichen dem Verlauf der Tafel
    int plaetze = 0;
    for (const cJSON *t : json::liste(j.get(), "tage")) {
        if (!cJSON_IsString(t) || std::string(t->valuestring) < ab_tag) continue;
        std::string tag = t->valuestring;
        http::Antwort l = http::anfrage(ip, "GET", "/api/log/days?tag=" + tag, "", "", 15000);
        if (!l.ok()) continue;
        json::Baum lj = json::lesen(l.body);
        for (const cJSON *f : json::liste(lj.get(), "dateien")) {
            std::string name = json::text(f, "name");
            int64_t byte = (int64_t)json::zahl_d(f, "byte", 0);
            if (name.size() < 10 || name.compare(name.size() - 9, 9, ".5min.csv") != 0) continue;
            std::string kennung = name.substr(0, name.find('.'));
            LsDatei &d = s_ls[tag + "/" + name];
            if (byte < d.stelle) d = LsDatei{};   // Karte getauscht oder neu angelegt: von vorn
            if (byte <= d.stelle) continue;
            http::Antwort x = http::anfrage(ip, "GET", "/log/" + tag + "/" + name, "", "", 60000, d.stelle);
            if (x.status == 416 || !x.ok()) continue;
            // Antwortet der Leitstand ohne Range mit der ganzen Datei (200 statt 206), von vorn lesen
            if (x.status == 200 && d.stelle > 0) d = LsDatei{};
            plaetze += ls_lesen(d, kennung, x.body);
        }
    }
    s_ls_zuletzt = uhr();
    s_ls_plaetze += plaetze;
    ESP_LOGI(TAG, "Leitstand %s: %d Plätze übernommen, %u Dateien", ip.c_str(), plaetze, (unsigned)s_ls.size());
    if (plaetze) s_gen++;   // Diagramme neu
}

void ls_task(void *)
{
    vTaskDelay(pdMS_TO_TICKS(20000));
    while (true) {
        std::string ip;
        {
            std::lock_guard<std::mutex> l(s_mx);
            for (const auto &e : s_geraete)
                if (e.s.art == anlage::Art::leitstand && e.s.erreichbar) ip = e.s.ip;
        }
        bool versucht = !ip.empty() && uhr_gueltig() && wlan().zustand == Netz::verbunden;
        if (versucht) ls_abgleich(ip);
        // Alle fünf Minuten wie die App; solange der Leitstand noch nicht gefunden ist, alle 30 s.
        vTaskDelay(pdMS_TO_TICKS(versucht ? 300000 : 30000));
    }
}

void task(void *)
{
    int64_t naechst_suche = 0, naechst_speichern = ms() + 1800000, letzt_bild = 0;
    while (true) {
        int64_t t = ms();
        bool netz = wlan().zustand == Netz::verbunden;
        // ⚠️ mDNS erst mit Netz: mdns_init braucht die Netzschnittstelle (esp_netif)
        static bool mdns = false;
        if (netz && !mdns) mdns = mdns_init() == ESP_OK;
        if (netz && mdns && t >= naechst_suche) {
            suchen();
            naechst_suche = t + 60000;
        }
        // Ausstehende Sollwerte: 900 ms nach der letzten Änderung senden
        std::vector<std::pair<std::string, float>> faellig;
        {
            std::lock_guard<std::mutex> l(s_mx);
            for (auto it = s_soll.begin(); it != s_soll.end();)
                if (t - it->second.second >= 900) {
                    faellig.push_back({it->first, it->second.first});
                    it = s_soll.erase(it);
                } else ++it;
        }
        for (auto &f : faellig) {
            std::string g;
            int nr;
            if (!raum_teilen(f.first, g, nr)) continue;
            char rumpf[48];
            snprintf(rumpf, sizeof rumpf, "{\"target_c\":%.1f}", f.second);
            float w = f.second;
            std::string name = raumname(f.first);
            ausfuehren({g, "POST", "/api/room/" + std::to_string(nr) + "/target", rumpf,
                        name + ": Sollwert " + fmt::temperatur(w),
                        [nr, w](const cJSON *st) { return fabsf(json::zahl(raum_in(st, nr), "target_c") - w) < 0.01f; }});
        }
        // Befehle
        while (true) {
            Befehl b;
            {
                std::lock_guard<std::mutex> l(s_mx);
                if (s_befehle.empty()) break;
                b = std::move(s_befehle.front());
                s_befehle.pop_front();
            }
            ausfuehren(b);
        }
        // Geräte abfragen (je Gerät nur eine Anfrage zur Zeit; hier ohnehin nacheinander)
        if (netz) {
            size_t n;
            {
                std::lock_guard<std::mutex> l(s_mx);
                n = s_geraete.size();
            }
            for (size_t i = 0; i < n; i++) {
                Eintrag kopie;
                {
                    std::lock_guard<std::mutex> l(s_mx);
                    if (i >= s_geraete.size()) break;
                    kopie = s_geraete[i];
                }
                abfragen(kopie, ms());
                std::lock_guard<std::mutex> l(s_mx);
                if (i < s_geraete.size() && s_geraete[i].s.id == kopie.s.id) s_geraete[i] = std::move(kopie);
            }
        }
        if (s_aendern && ms() - letzt_bild >= 250) {
            s_aendern = false;
            letzt_bild = ms();
            bild_bauen();
        }
        if (s_fs && ms() >= naechst_speichern && uhr_gueltig()) {
            naechst_speichern = ms() + 1800000;
            std::string v, b;
            {
                std::lock_guard<std::mutex> l(s_mx);
                v = s_verlauf.speichern();
                b = s_gedaechtnis.speichern();
            }
            datei_schreiben("/d/verlauf.bin", v);
            datei_schreiben("/d/befunde.json", b);
        }
        for (int i = 0; i < 10 && !s_wecken; i++) vTaskDelay(pdMS_TO_TICKS(50));
        s_wecken = false;
    }
}

}  // namespace

std::string leitstand_info()
{
    int64_t z = s_ls_zuletzt;
    if (!z) return "noch nicht abgeglichen";
    return "zuletzt " + fmt::uhrzeit(z) + ", " + std::to_string(s_ls_plaetze.load()) + " Plätze übernommen";
}

void start()
{
    beispiel_laden();
    ablage_start();
    bild_bauen();
    // ⚠️ Stapel im PSRAM erlaubt (FREERTOS_TASK_CREATE_ALLOW_EXT_MEM); cJSON und HTTP brauchen Platz.
    xTaskCreate(task, "abfrage", 12288, nullptr, 4, nullptr);
    xTaskCreate(ls_task, "leitstand", 8192, nullptr, 3, nullptr);
}

// ---- Schnittstelle -------------------------------------------------------------------

std::shared_ptr<const anlage::Bild> bild()
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_bild;
}

uint32_t generation() { return s_gen; }

std::vector<anlage::Geraetestand> staende()
{
    std::lock_guard<std::mutex> l(s_mx);
    if (!s_echt) return s_beispiel;
    std::vector<anlage::Geraetestand> v;
    for (const auto &e : s_geraete) v.push_back(e.s);
    return v;
}

int64_t jetzt()
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_echt || uhr_gueltig() ? uhr() : s_beispiel_stand;
}

std::vector<verlauf::Punkt> reihe(const std::string &k, int64_t von, int64_t bis, int r)
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_verlauf.reihe(k, von, bis, r);
}

std::vector<std::string> reihen()
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_verlauf.schluessel();
}

std::vector<befunde::Erledigt> erledigte_befunde()
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_gedaechtnis.erledigt();
}

void sollwert(const std::string &id, float w)
{
    w = roundf(w * 2) / 2;
    w = fminf(35, fmaxf(5, w));
    std::lock_guard<std::mutex> l(s_mx);
    if (!s_echt) return;
    s_soll[id] = {w, ms()};
}

float soll_ausstehend(const std::string &id)
{
    std::lock_guard<std::mutex> l(s_mx);
    auto it = s_soll.find(id);
    return it == s_soll.end() ? NAN : it->second.first;
}

void betriebsart(const std::string &id, bool heizen)
{
    std::string g;
    int nr;
    if (!raum_teilen(id, g, nr)) return;
    einreihen({g, "POST", "/api/room/" + std::to_string(nr) + "/mode", heizen ? "{\"mode\":\"heat\"}" : "{\"mode\":\"off\"}",
               raumname(id) + (heizen ? " eingeschaltet" : " ausgeschaltet"),
               [nr, heizen](const cJSON *st) { return (json::text(raum_in(st, nr), "mode") == "off") == !heizen; }});
}

void raum_pruefen(const std::string &id)
{
    std::string g;
    int nr;
    if (!raum_teilen(id, g, nr)) return;
    einreihen({g, "POST", "/api/room/" + std::to_string(nr) + "/check", "", raumname(id) + ": Regelung ausgelöst", nullptr});
}

void heizkreis_modus(int nr, anlage::Modus m)
{
    std::string g = geraet_mit(anlage::Rolle::speicher), w = anlage::modus_wert(m);
    if (g.empty()) return;
    einreihen({g, "POST", "/api/circuit/" + std::to_string(nr) + "/mode", "{\"mode\":\"" + w + "\"}",
               "Heizkreis " + std::to_string(nr) + ": " + anlage::modus_text(m), [nr, w](const cJSON *st) {
                   for (const cJSON *c : json::liste(st, "circuits"))
                       if (json::ganz(c, "id") == nr) return json::text(c, "mode") == w;
                   return false;
               }});
}

void kkp_modus(anlage::Modus m)
{
    std::string g, w = anlage::modus_wert(m);
    for (const auto &s : staende())
        if (s.art == anlage::Art::heizung) {
            json::Baum j = json::lesen(s.state);
            if (json::wahr(j.get(), "boiler_pump.enabled")) g = s.id;
        }
    if (g.empty()) return;
    einreihen({g, "POST", "/api/boilerpump/" + w, "", std::string("Kesselkreispumpe: ") + anlage::modus_text(m),
               [w](const cJSON *st) { return json::text(st, "boiler_pump.mode") == w; }});
}

void kanal(const std::string &geraet, int nr, const char *cmd)
{
    std::string c = cmd;
    bool hand = c != "auto";
    einreihen({geraet, "POST", nr ? "/api/channel/" + std::to_string(nr) + "/cmd" : std::string("/api/channel/all/cmd"),
               "{\"cmd\":\"" + c + "\"}", nr ? "Kanal " + std::to_string(nr) + ": Befehl ausgeführt" : "Alle Kanäle: Befehl ausgeführt",
               nr ? std::function<bool(const cJSON *)>([nr, hand](const cJSON *st) {
                   for (const cJSON *k : json::liste(st, "channels"))
                       if (json::ganz(k, "id") == nr) return json::wahr(k, "manual") == hand;
                   return false;
               })
                  : nullptr});
}

void neustart(const std::string &g) { einreihen({g, "POST", "/api/system/restart", "", "Das Gerät startet neu.", nullptr}); }

void befund_stumm(const std::string &id, bool stumm)
{
    {
        std::lock_guard<std::mutex> l(s_mx);
        s_gedaechtnis.stumm(id, stumm);
    }
    s_aendern = true;
    s_wecken = true;
}

void jetzt_abfragen()
{
    std::lock_guard<std::mutex> l(s_mx);
    for (auto &e : s_geraete) e.naechst_state = 0;
    s_wecken = true;
}

void melden(const std::string &t, bool fehler) { melden_intern(t, fehler); }

Rueckmeldung rueckmeldung()
{
    std::lock_guard<std::mutex> l(s_mx);
    return s_rm;
}

std::string befehl_warten(const std::string &geraet, const char *methode, const std::string &pfad, const std::string &rumpf)
{
    std::string ip;
    {
        std::lock_guard<std::mutex> l(s_mx);
        if (!s_echt) return "In der Beispielanlage wirken Befehle nicht.";
        Eintrag *e = finden(geraet);
        if (!e) return "Das Gerät ist nicht bekannt.";
        ip = e->s.ip;
    }
    std::string f = http::befehlsfehler(http::anfrage(ip, methode, pfad, rumpf, "", 15000));
    jetzt_abfragen();
    return f;
}

bool lesen_warten(const std::string &geraet, const std::string &pfad, std::string &antwort, int *status)
{
    std::string ip;
    {
        std::lock_guard<std::mutex> l(s_mx);
        Eintrag *e = finden(geraet);
        if (!e && geraet == "leitstand")
            for (auto &x : s_geraete)
                if (x.s.art == anlage::Art::leitstand) e = &x;
        if (!e) return false;
        ip = e->s.ip;
    }
    http::Antwort a = http::anfrage(ip, "GET", pfad, "", "", 30000);
    if (status) *status = a.status;
    antwort = std::move(a.body);
    return a.ok();
}

}  // namespace daten
