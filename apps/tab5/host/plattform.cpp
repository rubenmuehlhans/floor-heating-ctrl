// Vorschau auf dem Mac: erfüllt daten.h, eins.h und ki.h mit der Beispielanlage.
// Befehle wirken nur auf die Beispieldaten, wie im Beispielbetrieb der App.
#include <math.h>
#include <stdio.h>
#include <fstream>
#include <map>
#include <sstream>
#include "daten.h"
#include "eins.h"
#include "json.h"
#include "ki.h"

namespace {

std::vector<anlage::Geraetestand> s_staende;
std::shared_ptr<const anlage::Bild> s_bild = std::make_shared<anlage::Bild>();
befunde::Gedaechtnis s_gedaechtnis;
verlauf::Speicher s_verlauf;
uint32_t s_gen = 1;
int64_t s_jetzt = 1787933045;
std::map<std::string, std::string> s_eins;
uint32_t s_eins_gen;
daten::Rueckmeldung s_rm;
ki::Stand s_ki;
uint32_t s_ki_gen = 1;

std::string datei(const std::string &p)
{
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void neu_bilden()
{
    auto b = anlage::zusammenfuehren(s_staende, s_jetzt);
    b->beispiel = true;
    b->vollstaendig = true;
    auto bef = befunde::pruefen(s_staende, *b);
    // Im Beispiel stehen die Befunde schon einen Tag an.
    static bool erstes = true;
    if (erstes)
        for (int64_t t = s_jetzt - 86400; t < s_jetzt; t += 1800) s_gedaechtnis.fortschreiben(bef, t, true);
    erstes = false;
    b->befunde = s_gedaechtnis.fortschreiben(bef, s_jetzt, true);
    s_bild = b;
    s_gen++;
}

// Verlauf über sieben Tage aus dem jetzigen Stand mit Tagesgang erfinden
void verlauf_erfinden()
{
    auto b = s_bild;
    for (int64_t t = s_jetzt - 7 * 86400; t <= s_jetzt; t += 300) {
        double tag = (t % 86400) / 86400.0;
        double tg = sin((tag - 0.35) * 2 * M_PI);
        auto w = verlauf::abtastung(*b);
        for (auto &kv : w) {
            if (isnan(kv.second)) continue;
            const std::string &k = kv.first;
            if (k.find(".ist") != std::string::npos) kv.second += (float)(0.6 * tg - 0.3 * sin(t / 5400.0));
            else if (k == "aussen") kv.second += (float)(4.5 * tg - 3);
            else if (k == "fuehler.puffer") kv.second += (float)(8 * sin(t / 21600.0));
            else if (k == "brenner") kv.second = fmod(t / 3600.0, 7) < 0.8 ? 1.f : 0.f;
            else if (k.find("stellung") != std::string::npos) kv.second = (float)fmax(0, fmin(1, 0.4 + 0.5 * sin(t / 4000.0)));
            else if (k.rfind("fuehler.", 0) == 0) kv.second += (float)(3 * tg);
        }
        s_verlauf.abtasten(t, w);
    }
}

void gespraech_erfinden()
{
    ki::Beitrag f;
    f.nutzer = true;
    f.text = "Warum zeigt der Speicher nur 8 % Ladung bei 53 °C?";
    s_ki.beitraege.push_back(f);
    ki::Beitrag a;
    a.bausteine.push_back({ki::Baustein::Art::werkzeug, "", {"einstellungen_lesen", "Einstellungen des Pufferspeichers gelesen", "", true}});
    a.bausteine.push_back({ki::Baustein::Art::werkzeug, "", {"protokolle", "Ladungsprotokoll gelesen: 10 Ladungen", "", true}});
    a.bausteine.push_back({ki::Baustein::Art::text,
                           "Der Füllstand ist eine lineare Rechnung zwischen „Leer“ und „Voll“: Bei 52,3 °C gilt der Speicher als "
                           "leer, bei 62 °C als voll. 53 °C liegen knapp über leer, daraus ergeben sich **8 %**.\n\nDie letzten "
                           "Ladungen endeten aber zwischen 66 und 69 °C. Steht „Voll“ höher, passt der Füllstand besser zur "
                           "nutzbaren Wärme.",
                           {}});
    ki::Vorschlag v;
    v.id = "v-1a2b3c4d";
    v.titel = "Speicher voll: 62 → 68 °C";
    v.geraete = "Kessel und Pufferspeicher";
    v.begruendung = "Die letzten zehn Ladungen endeten bei 66 bis 69 °C; mit 62 °C steht der Füllstand lange auf 100 %.";
    v.wirkung = "Der Füllstand zeigt die nutzbare Wärme genauer; an der Regelung ändert sich nichts.";
    v.aenderungen.push_back({"heiz_6e03b5", "Pufferspeicher", "buffer.voll_c", "", "Speicher voll", "62,0 °C", "68,0 °C", "62", "68"});
    v.zeit = s_jetzt - 120;
    s_ki.vorschlaege.push_back(v);
    a.bausteine.push_back({ki::Baustein::Art::vorschlag, v.id, {}});
    s_ki.beitraege.push_back(a);
    s_ki.bericht.zustand = 1;
    s_ki.bericht.kurztext = "Die Anlage heizt ohne Störung, die Relais der Pumpen antworten aber nicht. Im Bad fehlt noch ein halbes Grad.";
    s_ki.bericht.hinweise = {{2, "Die Relais der Pumpen unter 192.168.1.203 und 192.168.1.170 antworten nicht. Prüfen Sie Strom und WLAN der Relais.", "relais"},
                             {1, "Die Batterie des Thermometers im Wohnzimmer ist leer.", ""}};
    s_ki.bericht.modell = "Claude Opus 5.5";
    s_ki.bericht.erstellt = s_jetzt - 1500;
}

}  // namespace

// Wissen des Assistenten aus docs/ (auf dem Gerät eingebettet)
std::string wissen_text()
{
    std::string r;
    for (const char *n : {"handbuch.md", "konzept-waermeerzeuger.md", "konzept-auswertung.md"}) {
        std::string t = datei(std::string("../../docs/") + n);
        if (t.empty()) continue;
        r += (r.empty() ? "" : "\n\n") + std::string("<dokument name=\"") + n + "\">\n" + t + "\n</dokument>";
    }
    return r;
}

// ---- Start der Vorschau ---------------------------------------------------------------
void plattform_start(const char *beispiel)
{
    json::Baum j = json::lesen(datei(beispiel));
    s_jetzt = (int64_t)json::zahl_d(j.get(), "stand", 1787933045);
    for (const cJSON *g : json::liste(j.get(), "geraete")) {
        anlage::Geraetestand s;
        s.id = json::text(g, "device.id");
        s.art = anlage::art_aus(s.id, json::text(g, "device.role"));
        s.ort = json::text(g, "device.site");
        s.ip = json::text(g, "net.ip");
        s.erreichbar = s.gesehen = true;
        s.kontakt = s_jetzt;
        s.state = json::schreiben(g);
        s_staende.push_back(s);
    }
    for (const cJSON *k : json::liste(j.get(), "konfig"))
        for (auto &s : s_staende)
            if (s.id == json::text(k, "id")) s.config = json::schreiben(json::weg(k, "config"));
    neu_bilden();
    verlauf_erfinden();
    gespraech_erfinden();
}

void plattform_hell(bool hell) { s_eins[eins::k::ANSICHT] = hell ? "1" : "2"; }

namespace daten {
std::shared_ptr<const anlage::Bild> bild() { return s_bild; }
uint32_t generation() { return s_gen; }
std::vector<anlage::Geraetestand> staende() { return s_staende; }
int64_t jetzt() { return s_jetzt; }
std::vector<verlauf::Punkt> reihe(const std::string &k, int64_t von, int64_t bis, int r) { return s_verlauf.reihe(k, von, bis, r); }
std::vector<std::string> reihen() { return s_verlauf.schluessel(); }
std::vector<befunde::Erledigt> erledigte_befunde() { return s_gedaechtnis.erledigt(); }
static std::map<std::string, float> s_soll;
void sollwert(const std::string &id, float w) { s_soll[id] = w; }
float soll_ausstehend(const std::string &id) { return s_soll.count(id) ? s_soll[id] : NAN; }
void betriebsart(const std::string &, bool) {}
void raum_pruefen(const std::string &) {}
void heizkreis_modus(int, anlage::Modus) {}
void kkp_modus(anlage::Modus) {}
void kanal(const std::string &, int, const char *) {}
void neustart(const std::string &) {}
void befund_stumm(const std::string &id, bool s) { s_gedaechtnis.stumm(id, s); neu_bilden(); }
void jetzt_abfragen() {}
Rueckmeldung rueckmeldung() { return s_rm; }
void melden(const std::string &t, bool f) { s_rm = {s_rm.nr + 1, t, f}; }
std::string befehl_warten(const std::string &, const char *, const std::string &, const std::string &) { return "In der Vorschau nicht möglich."; }
bool lesen_warten(const std::string &, const std::string &, std::string &, int *) { return false; }
Wlan wlan()
{
    Wlan w;
    w.zustand = Netz::verbunden;
    w.ssid = "Heimnetz";
    w.rssi = -52;
    w.ip = "192.168.1.88";
    return w;
}
void wlan_suchen() {}
bool wlan_sucht() { return false; }
std::vector<Funknetz> wlan_netze() { return {{"Heimnetz", -52, false}, {"Heimnetz-Gast", -61, false}, {"FRITZ!Box 7590 XY", -78, false}}; }
void wlan_verbinden(const std::string &, const std::string &) {}
void wlan_vergessen() {}
uint32_t wlan_generation() { return 1; }
}  // namespace daten

namespace eins {
void init() {}
std::string text(const char *k, const char *v) { return s_eins.count(k) ? s_eins[k] : v; }
void text_setzen(const char *k, const std::string &w) { s_eins[k] = w; s_eins_gen++; }
int64_t zahl(const char *k, int64_t v) { return s_eins.count(k) ? atoll(s_eins[k].c_str()) : v; }
void zahl_setzen(const char *k, int64_t w) { s_eins[k] = std::to_string(w); s_eins_gen++; }
void loeschen(const char *k) { s_eins.erase(k); s_eins_gen++; }
uint32_t generation() { return s_eins_gen; }
}  // namespace eins

namespace ki {
std::string hindernis() { return ""; }
bool braucht_zustimmung() { return false; }
const char *modellname() { return "Claude Opus 5.5"; }
const char *modell_id() { return "claude-opus-5-5"; }
Stand stand() { return s_ki; }
uint32_t generation() { return s_ki_gen; }
bool fragen(const std::string &) { return false; }
void anhalten() {}
void neues_gespraech() { s_ki.beitraege.clear(); s_ki_gen++; }
void bezug_setzen(const std::string &b) { s_ki.bezug = b; s_ki_gen++; }
void entwurf_setzen(const std::string &t) { s_ki.entwurf = t; s_ki_gen++; }
void uebernehmen(const std::string &) {}
void verwerfen(const std::string &) {}
void zuruecknehmen(const std::string &) {}
void lagebericht_erstellen() {}
void zustimmen(bool) {}
void verbindung_pruefen() {}
}  // namespace ki
