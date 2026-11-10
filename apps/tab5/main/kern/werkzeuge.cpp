#include "werkzeuge.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include "daten.h"
#include "fmt.h"
#include "json.h"
#include "katalog.h"

namespace werkzeuge {

namespace {

// ---- Definitionen (fest, Byte für Byte gleich) ------------------------------------------

const char *LESEND = R"([
{"name":"anlage_status","description":"Liest den aktuellen Zustand der Anlage: je Verteiler die Räume mit Soll- und Istwert, Thermometer und Ventilstellungen; je Heizungsgerät Fühler, Brenner, Ladung des Pufferspeichers, Heizkreise, Pumpen und die Befunde der Firmware; dazu Erreichbarkeit und Alter der Werte sowie die Zuordnung der Heizkreise zu den Verteilern. Mit rohdaten und geraet die vollständige letzte Antwort eines Geräts. Liefert JSON.","input_schema":{"type":"object","properties":{"bereich":{"type":"string","enum":["alle","verteiler","heizung"],"description":"Welche Geräte"},"geraet":{"type":"string","description":"Kennung wie fbh_… oder heiz_…, oder sein Ort wie Erdgeschoss"},"rohdaten":{"type":"boolean","description":"Vollständige letzte Antwort von /api/state des Geräts"}},"required":["bereich"]}},
{"name":"befunde","description":"Offene Befunde der Anlage mit Schwere, Titel, Text, Ort, Quelle und seit wann sie anstehen; nach Schwere sortiert. Mit erledigte auch die erledigten der letzten 30 Tage. Liefert JSON.","input_schema":{"type":"object","properties":{"erledigte":{"type":"boolean"}},"required":["erledigte"]}},
{"name":"verlauf","description":"Gespeicherte Zeitreihen der Tafel im Raster von 5 Minuten, höchstens sieben Tage zurück. Schlüssel wie fuehler.puffer, fuehler.kessel_vl, fuehler.hk1_vl, brenner (Anteil), fuellstand, pumpe.1, kkp, aussen, raum.3.ist, raum.3.soll, raum.3.stellung (mit geraet), oder Gruppen: waermeerzeugung, heizkreise, aussen, raeume, pumpen. Leere Liste: alle verfügbaren Schlüssel. Liefert Kennwerte je Reihe und eine Tabelle mit höchstens 48 Zeilen.","input_schema":{"type":"object","properties":{"reihen":{"type":"array","items":{"type":"string"}},"stunden":{"type":"integer","minimum":1,"maximum":168},"geraet":{"type":"string","description":"Verteiler für raum.*-Schlüssel, Kennung oder Ort"}},"required":["reihen","stunden"]}},
{"name":"ereignisse","description":"Ereignisse aus dem Protokoll des Leitstands: Neustarts, Ausfälle, Versionswechsel, Brenner- und Pumpenwechsel, Sollwert- und Betriebsartänderungen, Befunde. Nur, wenn ein Leitstand eingebunden ist. Liefert JSON, jüngste zuerst.","input_schema":{"type":"object","properties":{"stunden":{"type":"integer","minimum":1,"maximum":744},"geraet":{"type":"string"},"arten":{"type":"array","items":{"type":"string","enum":["neustart","nicht_erreichbar","erreichbar","version","brenner","pumpe","sollwert","betriebsart","befund","funk","leitstand"]}}},"required":["stunden","arten"]}},
{"name":"feinverlauf","description":"Messwerte im 30-Sekunden-Takt vom Leitstand, bis 24 Stunden, etwa freier Speicher (geraet.heap) und WLAN (geraet.rssi) vor einem Neustart. Nur, wenn ein Leitstand eingebunden ist.","input_schema":{"type":"object","properties":{"geraet":{"type":"string","description":"Kennung, oder leitstand"},"reihen":{"type":"array","items":{"type":"string"},"maxItems":8},"stunden":{"type":"integer","minimum":1,"maximum":24},"beginn":{"type":"string","description":"Ortszeit wie 2026-09-21T14:00; ohne: die letzten Stunden"}},"required":["geraet","reihen","stunden"]}},
{"name":"protokolle","description":"Protokolle des Heizungsgeräts: ladungen (je Ladung Dauer, Brennerzeit, Starts, Speicher vorher und nachher, Höchstwerte, Außen, Öl), tage (Brennerstunden, Starts, Öl, Heizgradtage, Außen min/max) oder aenderungen (Änderungsprotokoll der Tafel). CSV mit Kopfzeile, jüngste zuletzt.","input_schema":{"type":"object","properties":{"art":{"type":"string","enum":["ladungen","tage","aenderungen"]},"anzahl":{"type":"integer","minimum":1,"maximum":120}},"required":["art","anzahl"]}},
{"name":"kennzahlen","description":"Kennzahlen über die letzten Tage (höchstens sieben): Brennerlaufzeit je Tag, Starts und Öl je Tag aus dem Tagesprotokoll, je Raum mittlere Abweichung vom Sollwert, Anteil zu kalt und zu warm, mittlere Ventilstellung. Liefert JSON.","input_schema":{"type":"object","properties":{"tage":{"type":"integer","minimum":1,"maximum":7}},"required":["tage"]}},
{"name":"einstellungen_lesen","description":"Liest die Einstellungen der Geräte ohne Zugangsdaten, dazu für die Einstellungen, die sich vorschlagen lassen, Bezeichnung, Einheit, Bereich und Schritt. Liefert JSON.","input_schema":{"type":"object","properties":{"geraet":{"type":"string","description":"Kennung oder Ort; ohne: alle"},"erlaeuterungen":{"type":"boolean","description":"Hilfetexte zu den Parametern"}}}})";

const char *SCHREIBEND = R"(,
{"name":"aenderung_vorschlagen","description":"Schlägt eine geänderte Einstellung vor. Wirkt nicht sofort: Der Nutzer sieht eine Karte mit Bisher und Neu und übernimmt oder verwirft sie; danach liest die Tafel den Wert zurück. Nur Parameter aus einstellungen_lesen, dazu heizkreis.betriebsart und kesselkreispumpe.betriebsart (auto, ein, aus). Räume als eintrag mit Nummer oder Name, Heizkreise mit Nummer. Mehrere Änderungen, die nur zusammen gelten, gehören in einen Vorschlag.","input_schema":{"type":"object","properties":{"titel":{"type":"string","description":"Kurz, wie „Speicher voll: 62 → 68 °C“"},"geraet":{"type":"string","description":"Kennung oder Ort, wenn nicht eindeutig"},"aenderungen":{"type":"array","minItems":1,"maxItems":4,"items":{"type":"object","properties":{"parameter":{"type":"string"},"eintrag":{"type":"string","description":"Raum oder Heizkreis"},"wert":{"type":"string"}},"required":["parameter","wert"]}},"begruendung":{"type":"string"},"wirkung":{"type":"string"}},"required":["titel","aenderungen","begruendung","wirkung"]}},
{"name":"aktion_vorschlagen","description":"Schlägt eine Aktion am Gerät vor, die keine Einstellung ändert: messfahrt (Kanal eines Verteilers, eintrag = Kanal 1 bis 11), schutzfahrt (alle Ventile bzw. Pumpen einmal bewegen), fuehlersuche (1-Wire-Bus des Heizungsgeräts neu durchsuchen), neustart. Der Nutzer löst sie auf einer Karte aus.","input_schema":{"type":"object","properties":{"aktion":{"type":"string","enum":["messfahrt","schutzfahrt","fuehlersuche","neustart"]},"geraet":{"type":"string"},"eintrag":{"type":"string"},"titel":{"type":"string"},"begruendung":{"type":"string"},"wirkung":{"type":"string"}},"required":["aktion","geraet","titel","begruendung","wirkung"]}}])";

// ---- Helfer ----------------------------------------------------------------------------

std::string klein(std::string s)
{
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

void zahl_dazu(cJSON *o, const char *k, double v, int stellen = 1)
{
    if (isnan(v)) return;
    double f = pow(10, stellen);
    cJSON_AddNumberToObject(o, k, round(v * f) / f);
}

std::string als_text(const cJSON *v)
{
    if (!v) return "";
    if (cJSON_IsString(v)) return v->valuestring;
    if (cJSON_IsNumber(v)) {
        char b[32];
        snprintf(b, sizeof b, "%g", v->valuedouble);
        return b;
    }
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v) ? "true" : "false";
    return json::schreiben(v);
}

// Wert als Zahl: auch „62,5“, „62,5 °C“ (Claude hält sich nicht immer an die Typen)
bool als_zahl(const std::string &s, double &out)
{
    std::string t;
    for (char c : s)
        if (c == ',') t += '.';
        else if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') t += c;
        else if (!t.empty()) break;
    if (t.empty()) return false;
    char *e;
    out = strtod(t.c_str(), &e);
    return e != t.c_str();
}

std::vector<std::string> als_liste(const cJSON *v)
{
    std::vector<std::string> l;
    if (cJSON_IsArray(v))
        for (const cJSON *x = v->child; x; x = x->next) l.push_back(als_text(x));
    else if (cJSON_IsString(v)) {
        std::string s = v->valuestring;
        size_t a = 0;
        while (a <= s.size()) {
            size_t e = s.find(',', a);
            std::string t = s.substr(a, e == std::string::npos ? std::string::npos : e - a);
            while (!t.empty() && t[0] == ' ') t.erase(0, 1);
            if (!t.empty()) l.push_back(t);
            if (e == std::string::npos) break;
            a = e + 1;
        }
    }
    return l;
}

int ganz(const cJSON *in, const char *k, int vorgabe)
{
    const cJSON *v = json::weg(in, k);
    double d;
    if (cJSON_IsNumber(v)) return (int)v->valuedouble;
    if (cJSON_IsString(v) && als_zahl(v->valuestring, d)) return (int)d;
    return vorgabe;
}

bool wahr(const cJSON *in, const char *k)
{
    const cJSON *v = json::weg(in, k);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    if (cJSON_IsString(v)) return !strcmp(v->valuestring, "true") || !strcmp(v->valuestring, "ja");
    return false;
}

// Gerät nach Kennung oder Ort
const anlage::Geraet *geraet_finden(const anlage::Bild &b, const std::string &w)
{
    if (w.empty()) return nullptr;
    std::string k = klein(w);
    for (const auto &g : b.geraete)
        if (g.id == w || klein(g.ort) == k) return &g;
    for (const auto &g : b.geraete)
        if (klein(g.ort).find(k) != std::string::npos) return &g;
    if (k == "leitstand")
        for (const auto &g : b.geraete)
            if (g.art == anlage::Art::leitstand) return &g;
    return nullptr;
}

const anlage::Geraetestand *stand_von(const std::vector<anlage::Geraetestand> &st, const std::string &id)
{
    for (const auto &s : st)
        if (s.id == id) return &s;
    return nullptr;
}

std::string fehler_json(const std::string &t)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "fehler", t.c_str());
    std::string r = json::schreiben(o);
    cJSON_Delete(o);
    return r;
}

std::string json_aus(cJSON *o)
{
    std::string r = json::schreiben(o);
    cJSON_Delete(o);
    return r;
}

const char *zustand_wert(anlage::Raumzustand z)
{
    switch (z) {
    case anlage::Raumzustand::aus: return "aus";
    case anlage::Raumzustand::kein_thermometer: return "kein_thermometer";
    case anlage::Raumzustand::messwert_veraltet: return "messwert_veraltet";
    case anlage::Raumzustand::heizt: return "heizt";
    default: return "soll_erreicht";
    }
}

// ---- anlage_status -----------------------------------------------------------------------

std::string anlage_status(const cJSON *in, std::string &beschr)
{
    auto b = daten::bild();
    auto st = daten::staende();
    std::string bereich = json::text(in, "bereich", "alle"), gw = json::text(in, "geraet");
    if (wahr(in, "rohdaten") && !gw.empty()) {
        const anlage::Geraet *g = geraet_finden(*b, gw);
        const anlage::Geraetestand *s = g ? stand_von(st, g->id) : nullptr;
        if (!s) return fehler_json("Gerät „" + gw + "“ nicht gefunden.");
        json::Baum j = json::lesen(s->state);
        if (!j) return fehler_json("Vom Gerät liegt noch keine Antwort vor.");
        json::ohne_zugangsdaten(j.get());
        cJSON_DeleteItemFromObject(j.get(), "bemf_mv");
        cJSON_DeleteItemFromObject(j.get(), "touch_raw");
        beschr = "Rohdaten von " + g->ort + " gelesen";
        return json::schreiben(j.get());
    }
    beschr = bereich == "verteiler" ? "Zustand der Verteiler gelesen" : bereich == "heizung" ? "Zustand der Heizungsgeräte gelesen" : "Zustand aller Geräte gelesen";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "stand", fmt::iso(b->stand ? b->stand : daten::jetzt()).c_str());
    if (b->beispiel) cJSON_AddStringToObject(o, "hinweis", "Beispielanlage: Die Tafel hat noch keine echten Geräte gefunden.");
    if (!isnan(b->aussen.temperatur)) {
        cJSON *a = cJSON_AddObjectToObject(o, "aussen");
        zahl_dazu(a, "c", b->aussen.temperatur);
        zahl_dazu(a, "feuchte", b->aussen.feuchte, 0);
        cJSON_AddStringToObject(a, "quelle", b->aussen.quelle.c_str());
        if (b->aussen.alter >= 0) cJSON_AddNumberToObject(a, "alter_s", b->aussen.alter);
    }
    int64_t jetzt = daten::jetzt();
    if (bereich != "heizung") {
        cJSON *vl = cJSON_AddArrayToObject(o, "verteiler");
        for (const auto &e : b->etagen) {
            if (!gw.empty() && geraet_finden(*b, gw) && geraet_finden(*b, gw)->id != e.geraet) continue;
            const anlage::Geraet *g = b->geraet(e.geraet);
            cJSON *v = cJSON_CreateObject();
            cJSON_AddStringToObject(v, "id", e.geraet.c_str());
            cJSON_AddStringToObject(v, "ort", e.name.c_str());
            cJSON_AddBoolToObject(v, "erreichbar", g && g->erreichbar);
            if (g && g->kontakt) cJSON_AddNumberToObject(v, "werte_alter_s", (double)std::max<int64_t>(0, jetzt - g->kontakt));
            cJSON_AddBoolToObject(v, "waermebedarf", e.bedarf);
            cJSON *rs = cJSON_AddArrayToObject(v, "raeume");
            for (const auto &r : e.raeume) {
                cJSON *x = cJSON_CreateObject();
                cJSON_AddNumberToObject(x, "nr", r.nr);
                cJSON_AddStringToObject(x, "name", r.name.c_str());
                cJSON_AddStringToObject(x, "zustand", zustand_wert(r.zustand()));
                zahl_dazu(x, "soll_c", r.soll);
                zahl_dazu(x, "ist_c", r.ist);
                zahl_dazu(x, "feuchte", r.feuchte, 0);
                if (r.batterie >= 0) cJSON_AddNumberToObject(x, "batterie", r.batterie);
                if (r.alter >= 0) cJSON_AddNumberToObject(x, "messwert_alter_s", r.alter);
                cJSON_AddBoolToObject(x, "thermometer", r.thermometer);
                zahl_dazu(x, "zielstellung", r.zielstellung, 2);
                if (!isnan(r.p_band)) zahl_dazu(x, "p_band_k", r.p_band);
                cJSON *k = cJSON_AddArrayToObject(x, "kanaele");
                for (int n : r.kanaele) cJSON_AddItemToArray(k, cJSON_CreateNumber(n));
                cJSON_AddItemToArray(rs, x);
            }
            cJSON *ks = cJSON_AddArrayToObject(v, "kanaele");
            for (const auto &k : e.kanaele) {
                cJSON *x = cJSON_CreateObject();
                cJSON_AddNumberToObject(x, "nr", k.nr);
                if (!k.raum.empty()) cJSON_AddStringToObject(x, "raum", k.raum.c_str());
                zahl_dazu(x, "stellung", k.stellung, 2);
                if (k.hand) cJSON_AddBoolToObject(x, "handbetrieb", true);
                if (!k.kalibriert) cJSON_AddBoolToObject(x, "ohne_messfahrt", true);
                if (k.op != "idle") cJSON_AddStringToObject(x, "faehrt", k.op.c_str());
                cJSON_AddItemToArray(ks, x);
            }
            cJSON_AddItemToArray(vl, v);
        }
    }
    if (bereich != "verteiler") {
        cJSON *hl = cJSON_AddArrayToObject(o, "heizungsgeraete");
        for (const auto &g : b->geraete) {
            if (g.art != anlage::Art::heizung) continue;
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "id", g.id.c_str());
            cJSON_AddStringToObject(x, "ort", g.ort.c_str());
            cJSON_AddStringToObject(x, "rolle", g.art_text());
            cJSON_AddBoolToObject(x, "erreichbar", g.erreichbar);
            if (g.kontakt) cJSON_AddNumberToObject(x, "werte_alter_s", (double)std::max<int64_t>(0, jetzt - g.kontakt));
            if (const anlage::Geraetestand *s = stand_von(st, g.id)) {
                json::Baum j = json::lesen(s->state);
                cJSON *f = cJSON_AddObjectToObject(x, "eigene_fuehler");
                for (const cJSON *p : json::liste(j.get(), "probes"))
                    if (json::wahr(p, "assigned")) zahl_dazu(f, json::text(p, "role").c_str(), json::zahl(p, "temp_c"));
                const cJSON *fi = json::weg(j.get(), "findings");
                if (cJSON_GetArraySize(fi)) cJSON_AddItemToObject(x, "befunde_firmware", cJSON_Duplicate(fi, true));
            }
            cJSON_AddItemToArray(hl, x);
        }
        cJSON *f = cJSON_AddObjectToObject(o, "fuehler");
        for (const auto &kv : b->fuehler) zahl_dazu(f, kv.first.c_str(), kv.second);
        if (b->kessel.vorhanden) {
            const auto &k = b->kessel;
            cJSON *x = cJSON_AddObjectToObject(o, "brenner");
            cJSON_AddBoolToObject(x, "laeuft", k.laeuft);
            if (k.seit >= 0) cJSON_AddNumberToObject(x, "seit_s", k.seit);
            zahl_dazu(x, "abgas_c", k.abgas);
            zahl_dazu(x, "bezugslinie_c", k.bezugslinie);
            cJSON_AddNumberToObject(x, "laufzeit_heute_s", k.laufzeit_heute);
            cJSON_AddNumberToObject(x, "starts_heute", k.starts_heute);
            cJSON_AddNumberToObject(x, "laufzeit_gestern_s", k.laufzeit_gestern);
            cJSON_AddNumberToObject(x, "starts_gestern", k.starts_gestern);
            zahl_dazu(x, "liter_heute", k.liter_heute, 2);
            if (k.taktbetrieb) cJSON_AddBoolToObject(x, "taktbetrieb", true);
        }
        if (b->speicher.vorhanden) {
            const auto &s = b->speicher;
            cJSON *x = cJSON_AddObjectToObject(o, "pufferspeicher");
            zahl_dazu(x, "temperatur_c", s.temperatur);
            zahl_dazu(x, "fuellstand", s.ladung, 2);
            cJSON_AddStringToObject(x, "phase", s.phase.c_str());
            if (s.phase_seit >= 0) cJSON_AddNumberToObject(x, "phase_seit_s", s.phase_seit);
            zahl_dazu(x, "leer_c", s.leer);
            zahl_dazu(x, "voll_c", s.voll);
            zahl_dazu(x, "ladungsende_hoechstwert_c", s.hoechstwert);
            if (s.warmwasser_knapp) cJSON_AddBoolToObject(x, "warmwasser_knapp", true);
        }
        cJSON *hk = cJSON_AddArrayToObject(o, "heizkreise");
        for (const auto &h : b->heizkreise) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "nr", h.nr);
            cJSON_AddStringToObject(x, "name", h.name.c_str());
            cJSON_AddStringToObject(x, "betriebsart", anlage::modus_wert(h.modus));
            cJSON_AddBoolToObject(x, "pumpe_laeuft", h.pumpe);
            cJSON_AddBoolToObject(x, "bedarf", h.bedarf);
            if (h.gesperrt) cJSON_AddBoolToObject(x, "von_kesselregelung_gesperrt", true);
            cJSON_AddStringToObject(x, "grund", h.grund.c_str());
            zahl_dazu(x, "vorlauf_c", h.vorlauf);
            zahl_dazu(x, "ruecklauf_c", h.ruecklauf);
            cJSON *v = cJSON_AddArrayToObject(x, "versorgt");
            for (const auto &n : h.versorgt) cJSON_AddItemToArray(v, cJSON_CreateString(n.c_str()));
            cJSON *r = cJSON_AddObjectToObject(x, "relais");
            cJSON_AddBoolToObject(r, "erreichbar", h.relais.online);
            cJSON_AddBoolToObject(r, "ein", h.relais.ein);
            if (h.relais.stromlos) cJSON_AddBoolToObject(r, "ohne_strom", true);
            if (h.relais.abweichung) cJSON_AddBoolToObject(r, "folgt_nicht", true);
            cJSON_AddItemToArray(hk, x);
        }
        if (b->kkp.vorhanden) {
            cJSON *x = cJSON_AddObjectToObject(o, "kesselkreispumpe");
            cJSON_AddStringToObject(x, "betriebsart", anlage::modus_wert(b->kkp.modus));
            cJSON_AddBoolToObject(x, "laeuft", b->kkp.laeuft);
            cJSON_AddStringToObject(x, "grund", b->kkp.grund.c_str());
            cJSON_AddBoolToObject(x, "relais_erreichbar", b->kkp.relais.online);
        }
    }
    return json_aus(o);
}

// ---- befunde --------------------------------------------------------------------------

std::string befunde_lesen(const cJSON *in, std::string &beschr)
{
    auto b = daten::bild();
    cJSON *o = cJSON_CreateObject();
    cJSON *l = cJSON_AddArrayToObject(o, "offen");
    static const char *S[] = {"hinweis", "warnung", "stoerung"};
    for (const auto &f : b->befunde) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "id", f.id.c_str());
        cJSON_AddStringToObject(x, "schwere", S[(int)f.schwere]);
        cJSON_AddStringToObject(x, "titel", f.titel.c_str());
        cJSON_AddStringToObject(x, "text", f.text.c_str());
        cJSON_AddStringToObject(x, "ort", f.ort.c_str());
        cJSON_AddStringToObject(x, "quelle", f.quelle.c_str());
        if (f.seit) cJSON_AddStringToObject(x, "seit", fmt::iso(f.seit).c_str());
        if (f.stumm) cJSON_AddBoolToObject(x, "mitteilung_stumm", true);
        cJSON_AddItemToArray(l, x);
    }
    if (b->befunde.empty()) cJSON_AddStringToObject(o, "hinweis", "Keine offenen Befunde.");
    if (wahr(in, "erledigte")) {
        cJSON *e = cJSON_AddArrayToObject(o, "erledigt");
        for (const auto &x : daten::erledigte_befunde()) {
            cJSON *y = cJSON_CreateObject();
            cJSON_AddStringToObject(y, "id", x.id.c_str());
            cJSON_AddStringToObject(y, "schwere", S[(int)x.schwere]);
            cJSON_AddStringToObject(y, "titel", x.titel.c_str());
            cJSON_AddStringToObject(y, "ort", x.ort.c_str());
            cJSON_AddStringToObject(y, "von", fmt::iso(x.von).c_str());
            cJSON_AddStringToObject(y, "bis", fmt::iso(x.bis).c_str());
            cJSON_AddItemToArray(e, y);
        }
    }
    beschr = b->befunde.empty() ? "Befunde gelesen: keine offenen" : "Befunde gelesen: " + std::to_string(b->befunde.size()) + " offen";
    return json_aus(o);
}

// ---- verlauf ----------------------------------------------------------------------------

std::vector<std::string> schluessel_aufloesen(const anlage::Bild &b, const std::vector<std::string> &wunsch, const std::string &gw,
                                              std::vector<std::string> &unbekannt)
{
    std::vector<std::string> alle = daten::reihen(), r;
    auto dazu = [&](const std::string &k) {
        if (std::find(r.begin(), r.end(), k) == r.end()) r.push_back(k);
    };
    const anlage::Geraet *g = geraet_finden(b, gw);
    for (const auto &w : wunsch) {
        if (w == "waermeerzeugung") {
            for (const char *k : {"fuehler.kessel_vl", "fuehler.kessel_rl", "fuehler.abgas", "fuehler.puffer", "brenner", "fuellstand"})
                if (std::find(alle.begin(), alle.end(), k) != alle.end()) dazu(k);
        } else if (w == "heizkreise" || w == "pumpen") {
            for (const auto &k : alle)
                if ((w == "heizkreise" && k.rfind("fuehler.hk", 0) == 0) || k.rfind("pumpe.", 0) == 0 || (w == "pumpen" && k == "kkp")) dazu(k);
        } else if (w == "raeume") {
            for (const auto &k : alle)
                if (k.find("/raum.") != std::string::npos && k.size() > 4 && k.compare(k.size() - 4, 4, ".ist") == 0 &&
                    (!g || k.rfind(g->id, 0) == 0))
                    dazu(k);
        } else if (std::find(alle.begin(), alle.end(), w) != alle.end()) dazu(w);
        else if (g && std::find(alle.begin(), alle.end(), g->id + "/" + w) != alle.end()) dazu(g->id + "/" + w);
        else {
            // raum.3.ist ohne Gerät: eindeutig über alle Verteiler?
            std::vector<std::string> t;
            for (const auto &k : alle)
                if (k.size() > w.size() && k.compare(k.size() - w.size() - 1, w.size() + 1, "/" + w) == 0) t.push_back(k);
            if (t.size() == 1) dazu(t[0]);
            else unbekannt.push_back(w + (t.size() > 1 ? " (mehrdeutig, geraet angeben)" : ""));
        }
        if (r.size() >= 10) break;
    }
    return r;
}

std::string verlauf_lesen(const cJSON *in, std::string &beschr)
{
    auto b = daten::bild();
    std::vector<std::string> wunsch = als_liste(json::weg(in, "reihen"));
    int stunden = std::min(168, std::max(1, ganz(in, "stunden", 24)));
    if (wunsch.empty()) {
        beschr = "Verfügbare Reihen gelesen";
        std::string t = "schluessel | bezeichnung | einheit\n";
        for (const auto &k : daten::reihen()) {
            std::string e;
            std::string n = verlauf::bezeichnung(*b, k, &e);
            t += k + " | " + n + " | " + e + "\n";
        }
        return t;
    }
    std::vector<std::string> unbekannt;
    auto keys = schluessel_aufloesen(*b, wunsch, json::text(in, "geraet"), unbekannt);
    int64_t bis = daten::jetzt(), von = bis - (int64_t)stunden * 3600;
    int raster = (int)std::max<int64_t>(300, ((int64_t)stunden * 3600 / 48 + 299) / 300 * 300);
    std::string t = "Zeitraum " + fmt::tag_mit_zeit(von) + " bis " + fmt::tag_mit_zeit(bis) + ", Raster " + std::to_string(raster / 60) + " min, Ortszeit\n";
    if (!unbekannt.empty()) {
        t += "unbekannt:";
        for (auto &u : unbekannt) t += " " + u;
        t += "\n";
    }
    std::vector<std::vector<verlauf::Punkt>> reihen;
    t += "reihe | bezeichnung | einheit | tiefst | hoechst | mittel | letzter\n";
    for (const auto &k : keys) {
        auto r = daten::reihe(k, von, bis, raster);
        std::string e;
        std::string n = verlauf::bezeichnung(*b, k, &e);
        bool anteil = e == "%";
        float lo = INFINITY, hi = -INFINITY;
        double s = 0;
        for (auto &p : r) {
            lo = std::min(lo, p.v);
            hi = std::max(hi, p.v);
            s += p.v;
        }
        auto z = [&](double v) { return anteil ? fmt::zahl(v * 100, 0) : fmt::zahl(v); };
        t += k + " | " + n + " | " + e + " | ";
        if (r.empty()) t += "– | – | – | –\n";
        else t += z(lo) + " | " + z(hi) + " | " + z(s / r.size()) + " | " + z(r.back().v) + "\n";
        reihen.push_back(std::move(r));
    }
    t += "\nzeit";
    for (const auto &k : keys) t += ";" + k;
    t += "\n";
    for (int64_t z = von - von % raster; z < bis; z += raster) {
        std::string zeile = fmt::tag_kurz(z) + " " + fmt::uhrzeit(z);
        bool etwas = false;
        for (size_t i = 0; i < keys.size(); i++) {
            zeile += ";";
            for (const auto &p : reihen[i])
                if (p.t == z) {
                    std::string e;
                    verlauf::bezeichnung(*b, keys[i], &e);
                    zeile += e == "%" ? fmt::zahl(p.v * 100, 0) : fmt::zahl(p.v);
                    etwas = true;
                }
        }
        if (etwas) t += zeile + "\n";
    }
    if (daten::reihen().empty()) t += "Hinweis: Die Tafel zeichnet erst seit ihrem Start auf.\n";
    beschr = "Verlauf gelesen: ";
    for (size_t i = 0; i < keys.size() && i < 3; i++) beschr += (i ? ", " : "") + keys[i];
    if (keys.size() > 3) beschr += " …";
    beschr += ", " + std::to_string(stunden) + " Std.";
    return t;
}

// ---- Leitstand: ereignisse, feinverlauf ---------------------------------------------------

std::string leitstand(const std::string &pfad, std::string &beschr, const char *was)
{
    std::string a;
    int status = 0;
    if (!daten::lesen_warten("leitstand", pfad, a, &status)) {
        beschr = std::string(was) + ": Leitstand nicht erreichbar";
        return fehler_json(status ? "Der Leitstand meldet Fehler " + std::to_string(status) + "."
                                  : "Den Verlauf liefert der Leitstand, und er ist nicht erreichbar oder nicht eingebunden.");
    }
    beschr = std::string(was) + " vom Leitstand gelesen";
    if (a.size() > 24000) a = a.substr(0, 24000) + "\n… (gekürzt)";
    return a;
}

std::string ereignisse(const cJSON *in, std::string &beschr)
{
    int stunden = std::min(744, std::max(1, ganz(in, "stunden", 24)));
    int64_t bis = daten::jetzt(), von = bis - (int64_t)stunden * 3600;
    std::string p = "/api/log/events?von=" + std::to_string(von) + "&bis=" + std::to_string(bis);
    std::string g = json::text(in, "geraet");
    if (!g.empty()) {
        const anlage::Geraet *x = geraet_finden(*daten::bild(), g);
        p += "&geraet=" + (x ? x->id : g);
    }
    auto arten = als_liste(json::weg(in, "arten"));
    if (!arten.empty()) {
        p += "&art=";
        for (size_t i = 0; i < arten.size(); i++) p += (i ? "," : "") + arten[i];
    }
    return leitstand(p, beschr, "Ereignisse");
}

std::string feinverlauf(const cJSON *in, std::string &beschr)
{
    auto b = daten::bild();
    std::string g = json::text(in, "geraet");
    const anlage::Geraet *x = geraet_finden(*b, g);
    int stunden = std::min(24, std::max(1, ganz(in, "stunden", 6)));
    int64_t bis = daten::jetzt(), von = bis - (int64_t)stunden * 3600;
    std::string beginn = json::text(in, "beginn");
    if (beginn.size() >= 13) {
        struct tm t = {};
        if (sscanf(beginn.c_str(), "%d-%d-%dT%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min) >= 4) {
            // Ortszeit → Unix, über den Versatz des Endpunkts
            int64_t tage = 0;
            int y = t.tm_year, m = t.tm_mon;
            y -= m <= 2;
            int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
            int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
            tage = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
            int64_t lokal = tage * 86400 + t.tm_hour * 3600 + t.tm_min * 60;
            von = lokal - fmt::ortsversatz(lokal);
            bis = von + (int64_t)stunden * 3600;
        }
    }
    std::string p = "/api/log/series?geraet=" + (x ? x->id : g) + "&von=" + std::to_string(von) + "&bis=" + std::to_string(bis) +
                    "&raster=" + std::to_string(stunden > 6 ? 300 : 30) + "&schluessel=";
    auto r = als_liste(json::weg(in, "reihen"));
    for (size_t i = 0; i < r.size() && i < 8; i++) p += (i ? "," : "") + r[i];
    return leitstand(p, beschr, "Feinverlauf");
}

// ---- protokolle ---------------------------------------------------------------------------

std::string protokolle(const cJSON *in, Kontext &k, std::string &beschr)
{
    std::string art = json::text(in, "art", "ladungen");
    int n = std::min(120, std::max(1, ganz(in, "anzahl", 20)));
    if (art == "aenderungen") {
        auto z = k.aenderungen ? k.aenderungen() : std::vector<std::string>{};
        std::string t = "zeit;geraet;einstellung;bisher;neu;ausloeser\n";
        for (size_t i = z.size() > (size_t)n ? z.size() - n : 0; i < z.size(); i++) t += z[i] + "\n";
        beschr = "Änderungsprotokoll gelesen: " + std::to_string(std::min<size_t>(z.size(), n)) + " Einträge";
        return z.empty() ? t + "(keine Änderungen über die Tafel)\n" : t;
    }
    auto b = daten::bild();
    std::string geraet;
    for (const auto &g : b->geraete)
        if (g.art == anlage::Art::heizung && (geraet.empty() || g.rolle == anlage::Rolle::kessel || g.rolle == anlage::Rolle::beides)) geraet = g.id;
    if (geraet.empty()) return fehler_json("Kein Heizungsgerät eingebunden.");
    std::string csv;
    if (!daten::lesen_warten(geraet, art == "tage" ? "/api/log/days" : "/api/log/charges", csv)) {
        beschr = "Protokoll nicht lesbar";
        return fehler_json("Das Heizungsgerät liefert das Protokoll gerade nicht.");
    }
    // Kopfzeile und die letzten n Zeilen
    std::vector<std::string> zeilen;
    size_t a = 0;
    while (a < csv.size()) {
        size_t e = csv.find('\n', a);
        std::string z = csv.substr(a, e == std::string::npos ? std::string::npos : e - a);
        if (!z.empty() && z.back() == '\r') z.pop_back();
        if (!z.empty()) zeilen.push_back(z);
        if (e == std::string::npos) break;
        a = e + 1;
    }
    std::string t;
    if (!zeilen.empty()) t = zeilen[0] + "\n";
    for (size_t i = zeilen.size() > (size_t)n + 1 ? zeilen.size() - n : 1; i < zeilen.size(); i++) t += zeilen[i] + "\n";
    t += "Hinweis: Zeiten als Unix-Sekunden, Dauern in Sekunden. Werte zwischen −0,9 und −0,1 kommen ohne Vorzeichen (Fehler der Firmware).\n";
    beschr = (art == "tage" ? "Tagesprotokoll gelesen: " : "Ladungsprotokoll gelesen: ") + std::to_string(zeilen.size() > 1 ? std::min<size_t>(zeilen.size() - 1, n) : 0) +
             (art == "tage" ? " Tage" : " Ladungen");
    return t;
}

// ---- kennzahlen --------------------------------------------------------------------------

std::string kennzahlen(const cJSON *in, std::string &beschr)
{
    auto b = daten::bild();
    int tage = std::min(7, std::max(1, ganz(in, "tage", 7)));
    int64_t bis = daten::jetzt(), von = bis - (int64_t)tage * 86400;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "von", fmt::iso(von).c_str());
    cJSON_AddStringToObject(o, "bis", fmt::iso(bis).c_str());
    auto br = daten::reihe("brenner", von, bis, 3600);
    if (!br.empty()) {
        double s = 0;
        for (auto &p : br) s += p.v;
        zahl_dazu(o, "brennerlaufzeit_je_tag_h", s / br.size() * 24, 2);
        cJSON_AddNumberToObject(o, "brenner_stunden_mit_daten", (double)br.size());
    }
    cJSON *rs = cJSON_AddArrayToObject(o, "raeume");
    for (const auto &e : b->etagen)
        for (const auto &r : e.raeume) {
            std::string p = e.geraet + "/raum." + std::to_string(r.nr) + ".";
            auto ist = daten::reihe(p + "ist", von, bis, 900), soll = daten::reihe(p + "soll", von, bis, 900),
                 st = daten::reihe(p + "stellung", von, bis, 900);
            std::map<int64_t, float> sm;
            for (auto &x : soll) sm[x.t] = x.v;
            double abw = 0;
            int n = 0, kalt = 0, warm = 0;
            for (auto &x : ist) {
                auto it = sm.find(x.t);
                if (it == sm.end()) continue;
                double d = x.v - it->second;
                abw += d;
                n++;
                kalt += d < -0.5;
                warm += d > 0.5;
            }
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "raum", (e.geraet + "/" + std::to_string(r.nr)).c_str());
            cJSON_AddStringToObject(x, "name", r.name.c_str());
            cJSON_AddStringToObject(x, "etage", e.name.c_str());
            cJSON_AddNumberToObject(x, "messpunkte", n);
            if (n) {
                zahl_dazu(x, "mittlere_abweichung_k", abw / n, 2);
                zahl_dazu(x, "anteil_zu_kalt", (double)kalt / n, 2);
                zahl_dazu(x, "anteil_zu_warm", (double)warm / n, 2);
            }
            if (!st.empty()) {
                double s = 0;
                for (auto &y : st) s += y.v;
                zahl_dazu(x, "mittlere_stellung", s / st.size(), 2);
            }
            cJSON_AddItemToArray(rs, x);
        }
    cJSON_AddStringToObject(o, "hinweis",
                            "Aus dem Verlauf der Tafel in 15-Minuten-Mitteln; Starts und Öl je Tag liefert protokolle mit art tage.");
    beschr = "Kennzahlen über " + std::to_string(tage) + (tage == 1 ? " Tag berechnet" : " Tage berechnet");
    return json_aus(o);
}

// ---- einstellungen_lesen ----------------------------------------------------------------

std::string einstellungen(const cJSON *in, std::string &beschr)
{
    auto b = daten::bild();
    auto st = daten::staende();
    std::string gw = json::text(in, "geraet");
    const anlage::Geraet *nur = geraet_finden(*b, gw);
    bool hilfe = wahr(in, "erlaeuterungen");
    cJSON *o = cJSON_CreateObject();
    cJSON *gl = cJSON_AddArrayToObject(o, "geraete");
    for (const auto &g : b->geraete) {
        if (nur && nur->id != g.id) continue;
        if (g.art == anlage::Art::leitstand) continue;
        const anlage::Geraetestand *s = stand_von(st, g.id);
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "geraet", g.id.c_str());
        cJSON_AddStringToObject(x, "art", g.art_text());
        cJSON_AddStringToObject(x, "ort", g.ort.c_str());
        json::Baum k = json::lesen(s ? s->config : "");
        if (k) {
            json::ohne_zugangsdaten(k.get());
            for (const char *weg : {"wifi", "mqtt", "net"}) cJSON_DeleteItemFromObject(k.get(), weg);
            cJSON_AddItemToObject(x, "werte", cJSON_Duplicate(k.get(), true));
        } else cJSON_AddStringToObject(x, "werte", "noch nicht gelesen");
        cJSON_AddItemToArray(gl, x);
    }
    cJSON *p = cJSON_AddObjectToObject(o, "parameter");
    for (const auto &x : katalog::alle()) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", x.name);
        cJSON_AddStringToObject(e, "geraeteart", x.art == anlage::Art::verteiler ? "verteiler" : "heizung");
        cJSON_AddStringToObject(e, "ort", x.ort == katalog::Ort::raum ? "je Raum" : x.ort == katalog::Ort::heizkreis ? "je Heizkreis" : "Gerät");
        if (x.auswahl) cJSON_AddStringToObject(e, "werte", x.auswahl);
        else {
            if (*x.einheit) cJSON_AddStringToObject(e, "einheit", x.einheit);
            cJSON *br = cJSON_AddArrayToObject(e, "bereich");
            cJSON_AddItemToArray(br, cJSON_CreateNumber(x.min));
            cJSON_AddItemToArray(br, cJSON_CreateNumber(x.max));
            cJSON_AddNumberToObject(e, "schritt", x.schritt);
        }
        if (x.minuten) cJSON_AddStringToObject(e, "hinweis", "in der Konfiguration in Sekunden, vorschlagen in Minuten");
        if (x.anlagenweit) cJSON_AddBoolToObject(e, "auf_allen_heizungsgeraeten_gleich", true);
        if (hilfe && *x.hilfe) cJSON_AddStringToObject(e, "hilfe", x.hilfe);
        cJSON_AddItemToObject(p, x.id, e);
    }
    cJSON_AddStringToObject(o, "sonderparameter", "heizkreis.betriebsart und kesselkreispumpe.betriebsart: auto, ein, aus");
    beschr = nur ? "Einstellungen von " + nur->ort + " gelesen" : "Einstellungen aller Geräte gelesen";
    return json_aus(o);
}

// ---- Vorschläge --------------------------------------------------------------------------

const anlage::Raum *raum_finden(const anlage::Bild &b, const std::string &eintrag, const anlage::Geraet *g)
{
    if (eintrag.find('/') != std::string::npos) return b.raum(eintrag);
    double d;
    if (g && als_zahl(eintrag, d)) return b.raum(g->id + "/" + std::to_string((int)d));
    const anlage::Raum *treffer = nullptr;
    int n = 0;
    for (const auto &e : b.etagen)
        for (const auto &r : e.raeume)
            if ((!g || g->id == e.geraet) && klein(r.name) == klein(eintrag)) {
                treffer = &r;
                n++;
            }
    return n == 1 ? treffer : nullptr;
}

std::string wert_text(double v, const katalog::Parameter &p)
{
    int stellen = p.schritt >= 1 ? 0 : p.schritt >= 0.1 ? 1 : 2;
    return fmt::mit_einheit(fmt::zahl(v, stellen), p.einheit);
}

std::string vorschlag(const cJSON *in, Kontext &k, std::string &beschr, std::string &vid)
{
    auto b = daten::bild();
    auto st = daten::staende();
    const anlage::Geraet *gw = geraet_finden(*b, json::text(in, "geraet"));
    std::vector<std::string> fehler;
    ki::Vorschlag v;
    v.titel = json::text(in, "titel");
    v.begruendung = json::text(in, "begruendung");
    v.wirkung = json::text(in, "wirkung");
    std::vector<std::string> orte;
    for (const cJSON *a : json::liste(in, "aenderungen")) {
        std::string pid = json::text(a, "parameter"), eintrag = als_text(json::weg(a, "eintrag")), wert = als_text(json::weg(a, "wert"));
        if (pid.rfind("rooms.", 0) == 0) pid = "raum." + pid.substr(6);
        if (pid.rfind("circuits.", 0) == 0) pid = "heizkreis." + pid.substr(9);
        auto dazu = [&](ki::Aenderung x) {
            if (x.wert_bisher == x.wert_neu) {
                fehler.push_back(x.einstellung + " steht bereits auf " + x.neu + ".");
                return;
            }
            if (std::find(orte.begin(), orte.end(), x.ort) == orte.end()) orte.push_back(x.ort);
            v.aenderungen.push_back(std::move(x));
        };
        // Sonderparameter: Betriebsarten der Pumpen
        if (pid == "heizkreis.betriebsart" || pid == "kesselkreispumpe.betriebsart") {
            std::string w = klein(wert);
            if (w == "automatik") w = "auto";
            if (w == "hand ein") w = "ein";
            if (w == "hand aus") w = "aus";
            if (w != "auto" && w != "ein" && w != "aus") {
                fehler.push_back(pid + ": Wert muss auto, ein oder aus sein.");
                continue;
            }
            auto text = [](const std::string &m) { return m == "ein" ? "Hand ein" : m == "aus" ? "Hand aus" : "Automatik"; };
            if (pid == "kesselkreispumpe.betriebsart") {
                if (!b->kkp.vorhanden) {
                    fehler.push_back("Es gibt keine Kesselkreispumpe.");
                    continue;
                }
                std::string id;
                for (const auto &s : st) {
                    json::Baum j = json::lesen(s.state);
                    if (json::wahr(j.get(), "boiler_pump.enabled")) id = s.id;
                }
                const anlage::Geraet *g = b->geraet(id);
                dazu({id, g ? g->ort : id, pid, "", "Kesselkreispumpe, Betriebsart", text(anlage::modus_wert(b->kkp.modus)), text(w),
                      anlage::modus_wert(b->kkp.modus), w});
            } else {
                double d;
                const anlage::Heizkreis *h = nullptr;
                for (const auto &x : b->heizkreise)
                    if ((als_zahl(eintrag, d) && (int)d == x.nr) || klein(x.name) == klein(eintrag)) h = &x;
                if (!h) {
                    fehler.push_back("Heizkreis „" + eintrag + "“ nicht gefunden.");
                    continue;
                }
                std::string id;
                for (const auto &g : b->geraete)
                    if (g.rolle == anlage::Rolle::speicher || g.rolle == anlage::Rolle::beides) id = g.id;
                const anlage::Geraet *g = b->geraet(id);
                dazu({id, g ? g->ort : id, pid, std::to_string(h->nr), h->name + ", Betriebsart", text(anlage::modus_wert(h->modus)), text(w),
                      anlage::modus_wert(h->modus), w});
            }
            continue;
        }
        const katalog::Parameter *p = katalog::finden(pid);
        if (!p) {
            fehler.push_back("„" + pid + "“ ist nicht für Vorschläge freigegeben. Erlaubt sind die Parameter aus einstellungen_lesen.");
            continue;
        }
        // Wert prüfen
        std::string roh, lesbar;
        if (p->auswahl) {
            std::string opts = p->auswahl, w = klein(wert);
            size_t a2 = 0;
            while (a2 < opts.size()) {
                size_t e = opts.find('|', a2);
                std::string o2 = opts.substr(a2, e == std::string::npos ? std::string::npos : e - a2);
                size_t gl = o2.find('=');
                if (klein(o2.substr(0, gl)) == w || klein(o2.substr(gl + 1)) == w) {
                    roh = o2.substr(0, gl);
                    lesbar = o2.substr(gl + 1);
                }
                if (e == std::string::npos) break;
                a2 = e + 1;
            }
            if (roh.empty()) {
                fehler.push_back(std::string(p->name) + ": erlaubt sind " + p->auswahl + ".");
                continue;
            }
        } else {
            double d;
            if (!als_zahl(wert, d)) {
                fehler.push_back(std::string(p->name) + ": „" + wert + "“ ist keine Zahl.");
                continue;
            }
            if (d < p->min - 1e-9 || d > p->max + 1e-9) {
                fehler.push_back(std::string(p->name) + ": " + fmt::zahl(d, 2) + " liegt außerhalb von " + fmt::zahl(p->min, 2) + " bis " +
                                 fmt::zahl(p->max, 2) + ".");
                continue;
            }
            d = round(d / p->schritt) * p->schritt;
            lesbar = wert_text(d, *p);
            char b2[32];
            snprintf(b2, sizeof b2, "%g", p->minuten ? d * 60 : d);
            roh = b2;
        }
        // Ziele: Raum, Heizkreis oder Gerät(e)
        if (p->ort == katalog::Ort::raum) {
            const anlage::Raum *r = raum_finden(*b, eintrag, gw && gw->art == anlage::Art::verteiler ? gw : nullptr);
            if (!r) {
                fehler.push_back("Raum „" + eintrag + "“ nicht eindeutig gefunden; Nummer mit geraet oder Raum-ID wie fbh_…/3 angeben.");
                continue;
            }
            std::string bisher_roh, bisher;
            const anlage::Geraetestand *s = stand_von(st, r->geraet);
            json::Baum kj = json::lesen(s ? s->config : "");
            for (const cJSON *x : json::liste(kj.get(), "rooms"))
                if (json::ganz(x, "id") == r->nr) bisher_roh = als_text(json::weg(x, p->pfad));
            if (bisher_roh.empty() && pid == "raum.target_c") bisher_roh = fmt::zahl(r->soll, 1);
            double bd;
            if (p->auswahl) {
                bisher = bisher_roh == "off" ? "ausgeschaltet" : "Regelung aktiv";
                if (pid == "raum.mode") bisher_roh = r->an ? "heat" : "off";
            } else if (als_zahl(bisher_roh, bd)) {
                bisher = wert_text(bd, *p);
                char b3[32];
                snprintf(b3, sizeof b3, "%g", bd);
                bisher_roh = b3;
            } else bisher = fmt::STRICH;
            dazu({r->geraet, r->etage, pid, std::to_string(r->nr), r->name + ", " + p->name, bisher, lesbar, bisher_roh, roh});
            continue;
        }
        std::vector<const anlage::Geraet *> ziele;
        for (const auto &g : b->geraete) {
            if (g.art != p->art) continue;
            if (p->anlagenweit) ziele.push_back(&g);
            else if (gw && gw->id == g.id) ziele.push_back(&g);
        }
        if (ziele.empty()) {
            // eindeutig ohne Angabe?
            std::vector<const anlage::Geraet *> passend;
            for (const auto &g : b->geraete) {
                if (g.art != p->art) continue;
                const anlage::Geraetestand *s = stand_von(st, g.id);
                json::Baum kj = json::lesen(s ? s->config : "");
                if (p->ort == katalog::Ort::heizkreis ? cJSON_GetArraySize(json::weg(kj.get(), "circuits")) > 0 : json::hat(kj.get(), p->pfad))
                    passend.push_back(&g);
            }
            if (passend.size() == 1) ziele = passend;
        }
        if (ziele.empty()) {
            fehler.push_back(std::string(p->name) + ": Gerät nicht eindeutig; geraet angeben.");
            continue;
        }
        for (const anlage::Geraet *g : ziele) {
            const anlage::Geraetestand *s = stand_von(st, g->id);
            json::Baum kj = json::lesen(s ? s->config : "");
            std::string bisher_roh, einst = p->name;
            if (p->ort == katalog::Ort::heizkreis) {
                double d;
                int nr = als_zahl(eintrag, d) ? (int)d : 0;
                for (const auto &h : b->heizkreise)
                    if (klein(h.name) == klein(eintrag)) nr = h.nr;
                bool da = false;
                for (const cJSON *x : json::liste(kj.get(), "circuits"))
                    if (json::ganz(x, "id") == nr) {
                        bisher_roh = als_text(json::weg(x, p->pfad));
                        da = true;
                    }
                if (!da) {
                    fehler.push_back("Heizkreis „" + eintrag + "“ nicht gefunden.");
                    continue;
                }
                einst = "Heizkreis " + std::to_string(nr) + ", " + p->name;
                eintrag = std::to_string(nr);
            } else bisher_roh = als_text(json::weg(kj.get(), p->pfad));
            std::string bisher = fmt::STRICH;
            double bd;
            if (p->auswahl) bisher = bisher_roh == "true" ? "an" : bisher_roh == "false" ? "aus" : bisher_roh;
            else if (als_zahl(bisher_roh, bd)) {
                bisher = wert_text(p->minuten ? bd / 60 : bd, *p);
                char b3[32];
                snprintf(b3, sizeof b3, "%g", bd);
                bisher_roh = b3;
            }
            dazu({g->id, g->ort, pid, p->ort == katalog::Ort::heizkreis ? eintrag : "", einst, bisher, lesbar, bisher_roh, roh});
        }
    }
    if (v.aenderungen.empty() && fehler.empty()) fehler.push_back("Keine Änderung angegeben.");
    if (!fehler.empty()) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "angelegt", false);
        cJSON *f = cJSON_AddArrayToObject(o, "fehler");
        for (auto &x : fehler) cJSON_AddItemToArray(f, cJSON_CreateString(x.c_str()));
        beschr = "Vorschlag abgelehnt, die KI berichtigt";
        return json_aus(o);
    }
    for (size_t i = 0; i < orte.size(); i++) v.geraete += (i ? (i + 1 == orte.size() ? " und " : ", ") : "") + orte[i];
    vid = k.vorschlag_anlegen ? k.vorschlag_anlegen(v) : "";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "angelegt", true);
    cJSON_AddStringToObject(o, "vorschlag", vid.c_str());
    cJSON_AddStringToObject(o, "titel", v.titel.c_str());
    cJSON *al = cJSON_AddArrayToObject(o, "aenderungen");
    for (const auto &a : v.aenderungen) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "geraet", a.ort.c_str());
        cJSON_AddStringToObject(x, "einstellung", a.einstellung.c_str());
        cJSON_AddStringToObject(x, "bisher", a.bisher.c_str());
        cJSON_AddStringToObject(x, "neu", a.neu.c_str());
        cJSON_AddItemToArray(al, x);
    }
    cJSON_AddStringToObject(o, "hinweis",
                            "Der Nutzer sieht den Vorschlag als Karte mit Übernehmen und Verwerfen. Wiederholen Sie die Werte nicht in "
                            "voller Länge; verweisen Sie auf die Karte.");
    beschr = "Vorschlag angelegt";
    return json_aus(o);
}

std::string aktion(const cJSON *in, Kontext &k, std::string &beschr, std::string &vid)
{
    auto b = daten::bild();
    std::string art = json::text(in, "aktion");
    const anlage::Geraet *g = geraet_finden(*b, json::text(in, "geraet"));
    std::string fehler;
    if (!g) fehler = "Gerät „" + json::text(in, "geraet") + "“ nicht gefunden.";
    else if (art == "fuehlersuche" && g->art != anlage::Art::heizung) fehler = "Die Fühlersuche gibt es nur am Heizungsgerät.";
    else if (art == "messfahrt") {
        double d;
        std::string e = als_text(json::weg(in, "eintrag"));
        const anlage::Etage *et = b->etage(g->id);
        if (g->art != anlage::Art::verteiler || !et) fehler = "Eine Messfahrt gibt es nur am Verteiler.";
        else if (!als_zahl(e, d) || d < 1 || d > 11) fehler = "Für die Messfahrt den Kanal 1 bis 11 als eintrag angeben.";
        else {
            bool belegt = false;
            for (const auto &kn : et->kanaele)
                if (kn.nr == (int)d && !kn.raum.empty()) belegt = true;
            if (!belegt) fehler = "Kanal " + std::to_string((int)d) + " gehört keinem Raum.";
        }
    } else if (art != "schutzfahrt" && art != "neustart" && art != "fuehlersuche") fehler = "Unbekannte Aktion „" + art + "“.";
    if (!fehler.empty()) {
        beschr = "Vorschlag abgelehnt, die KI berichtigt";
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "angelegt", false);
        cJSON *f = cJSON_AddArrayToObject(o, "fehler");
        cJSON_AddItemToArray(f, cJSON_CreateString(fehler.c_str()));
        return json_aus(o);
    }
    ki::Vorschlag v;
    v.aktion = true;
    v.aktion_art = art;
    v.geraet = g->id;
    v.eintrag = als_text(json::weg(in, "eintrag"));
    v.titel = json::text(in, "titel");
    v.begruendung = json::text(in, "begruendung");
    v.wirkung = json::text(in, "wirkung");
    v.geraete = g->ort;
    vid = k.vorschlag_anlegen ? k.vorschlag_anlegen(v) : "";
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "angelegt", true);
    cJSON_AddStringToObject(o, "vorschlag", vid.c_str());
    cJSON_AddStringToObject(o, "hinweis", "Der Nutzer sieht die Aktion als Karte und löst sie selbst aus.");
    beschr = "Aktion vorgeschlagen";
    return json_aus(o);
}

}  // namespace

const std::string &definitionen(bool nur_lesend)
{
    static const std::string LES = std::string(LESEND) + "]";
    static const std::string ALLE = std::string(LESEND) + SCHREIBEND;
    return nur_lesend ? LES : ALLE;
}

std::string laeuft_text(const std::string &n)
{
    if (n == "anlage_status") return "Liest den Zustand der Anlage …";
    if (n == "befunde") return "Liest die Befunde …";
    if (n == "verlauf") return "Liest den Verlauf …";
    if (n == "ereignisse" || n == "feinverlauf") return "Fragt den Leitstand …";
    if (n == "protokolle") return "Liest die Protokolle …";
    if (n == "kennzahlen") return "Berechnet Kennzahlen …";
    if (n == "einstellungen_lesen") return "Liest die Einstellungen …";
    if (n == "aenderung_vorschlagen" || n == "aktion_vorschlagen") return "Bereitet einen Vorschlag vor …";
    return "Arbeitet …";
}

Ergebnis ausfuehren(const std::string &name, const std::string &eingabe, Kontext &k)
{
    Ergebnis e;
    json::Baum in = json::lesen(eingabe.empty() ? "{}" : eingabe);
    const cJSON *j = in ? in.get() : nullptr;
    if (name == "anlage_status") e.text = anlage_status(j, e.beschreibung);
    else if (name == "befunde") e.text = befunde_lesen(j, e.beschreibung);
    else if (name == "verlauf") e.text = verlauf_lesen(j, e.beschreibung);
    else if (name == "ereignisse") e.text = ereignisse(j, e.beschreibung);
    else if (name == "feinverlauf") e.text = feinverlauf(j, e.beschreibung);
    else if (name == "protokolle") e.text = protokolle(j, k, e.beschreibung);
    else if (name == "kennzahlen") e.text = kennzahlen(j, e.beschreibung);
    else if (name == "einstellungen_lesen") e.text = einstellungen(j, e.beschreibung);
    else if (name == "aenderung_vorschlagen") e.text = vorschlag(j, k, e.beschreibung, e.vorschlag);
    else if (name == "aktion_vorschlagen") e.text = aktion(j, k, e.beschreibung, e.vorschlag);
    else {
        e.text = fehler_json("Unbekanntes Werkzeug „" + name + "“.");
        e.beschreibung = "Unbekanntes Werkzeug";
    }
    return e;
}

}  // namespace werkzeuge
