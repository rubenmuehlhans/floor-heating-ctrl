#include "verlauf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace verlauf {

void Speicher::vorruecken(int64_t platz)
{
    if (!kopf_) {
        kopf_ = platz;
        return;
    }
    if (platz <= kopf_) return;
    // Übersprungene Plätze leeren (Ring)
    int64_t schritte = (platz - kopf_) / PLATZ_S;
    if (schritte > PLAETZE) schritte = PLAETZE;
    for (auto &kv : reihen_)
        for (int64_t i = 1; i <= schritte; i++) kv.second.wert[index(kopf_ + i * PLATZ_S)] = NAN;
    kopf_ = platz;
}

void Speicher::abtasten(int64_t jetzt, const std::map<std::string, float> &werte)
{
    if (jetzt < 1600000000) return;   // Uhr steht nicht
    int64_t platz = jetzt - jetzt % PLATZ_S;
    vorruecken(platz);
    for (const auto &kv : werte) {
        if (isnan(kv.second)) continue;
        Reihe &r = reihen_[kv.first];
        if (r.wert.empty()) r.wert.assign(PLAETZE, NAN);
        if (r.platz != platz) {
            r.platz = platz;
            r.summe = 0;
            r.anzahl = 0;
        }
        r.summe += kv.second;
        r.anzahl++;
        r.wert[index(platz)] = (float)(r.summe / r.anzahl);
    }
}

void Speicher::nachtragen(const std::string &k, int64_t t, float v)
{
    if (isnan(v) || t < 1600000000) return;
    int64_t platz = t - t % PLATZ_S;
    if (kopf_ && (platz > kopf_ || platz <= kopf_ - (int64_t)PLAETZE * PLATZ_S)) return;
    if (!kopf_) kopf_ = platz;
    Reihe &r = reihen_[k];
    if (r.wert.empty()) r.wert.assign(PLAETZE, NAN);
    float &w = r.wert[index(platz)];
    if (isnan(w)) w = v;
}

std::vector<Punkt> Speicher::reihe(const std::string &k, int64_t von, int64_t bis, int raster_s) const
{
    std::vector<Punkt> out;
    auto it = reihen_.find(k);
    if (it == reihen_.end() || !kopf_) return out;
    if (raster_s < PLATZ_S) raster_s = PLATZ_S;
    raster_s -= raster_s % PLATZ_S;
    int64_t erster = kopf_ - (int64_t)(PLAETZE - 1) * PLATZ_S;
    if (von < erster) von = erster;
    if (bis > kopf_ + PLATZ_S) bis = kopf_ + PLATZ_S;
    von -= von % raster_s;
    for (int64_t t = von; t < bis; t += raster_s) {
        double s = 0;
        int n = 0;
        for (int64_t p = t; p < t + raster_s && p <= kopf_; p += PLATZ_S) {
            if (p < erster) continue;
            float w = it->second.wert[index(p)];
            if (!isnan(w)) {
                s += w;
                n++;
            }
        }
        if (n) out.push_back({t, (float)(s / n)});
    }
    return out;
}

std::vector<std::string> Speicher::schluessel() const
{
    std::vector<std::string> v;
    for (const auto &kv : reihen_) v.push_back(kv.first);
    return v;
}

int64_t Speicher::aeltester() const
{
    if (!kopf_) return 0;
    int64_t erster = kopf_ - (int64_t)(PLAETZE - 1) * PLATZ_S, best = 0;
    for (const auto &kv : reihen_)
        for (int64_t p = erster; p <= kopf_; p += PLATZ_S)
            if (!isnan(kv.second.wert[index(p)])) {
                if (!best || p < best) best = p;
                break;
            }
    return best;
}

// Form: "VL1" | kopf (8) | anzahl (4) | je Reihe: namenslänge (2), name, PLAETZE × float
std::string Speicher::speichern() const
{
    std::string d = "VL1";
    d.append((const char *)&kopf_, 8);
    uint32_t n = (uint32_t)reihen_.size();
    d.append((const char *)&n, 4);
    for (const auto &kv : reihen_) {
        uint16_t l = (uint16_t)kv.first.size();
        d.append((const char *)&l, 2);
        d += kv.first;
        d.append((const char *)kv.second.wert.data(), PLAETZE * sizeof(float));
    }
    return d;
}

bool Speicher::laden(const std::string &d)
{
    if (d.size() < 15 || d.compare(0, 3, "VL1") != 0) return false;
    size_t p = 3;
    memcpy(&kopf_, d.data() + p, 8);
    p += 8;
    uint32_t n;
    memcpy(&n, d.data() + p, 4);
    p += 4;
    reihen_.clear();
    for (uint32_t i = 0; i < n; i++) {
        if (p + 2 > d.size()) return false;
        uint16_t l;
        memcpy(&l, d.data() + p, 2);
        p += 2;
        if (p + l + PLAETZE * sizeof(float) > d.size()) return false;
        Reihe &r = reihen_[d.substr(p, l)];
        p += l;
        r.wert.resize(PLAETZE);
        memcpy(r.wert.data(), d.data() + p, PLAETZE * sizeof(float));
        p += PLAETZE * sizeof(float);
    }
    return true;
}

std::map<std::string, float> abtastung(const anlage::Bild &b)
{
    std::map<std::string, float> w;
    for (const auto &e : b.etagen) {
        const anlage::Geraet *g = b.geraet(e.geraet);
        if (g && !g->erreichbar) continue;
        for (const auto &r : e.raeume) {
            std::string p = e.geraet + "/raum." + std::to_string(r.nr) + ".";
            w[p + "ist"] = r.ist;
            w[p + "feuchte"] = r.feuchte;
            if (r.an) w[p + "soll"] = r.soll;
            w[p + "stellung"] = r.zielstellung;
        }
        for (const auto &k : e.kanaele)
            if (k.bekannt) w[e.geraet + "/kanal." + std::to_string(k.nr) + ".stellung"] = k.stellung;
    }
    w["aussen"] = b.aussen.temperatur;
    for (const auto &kv : b.fuehler) w["fuehler." + kv.first] = kv.second;
    if (b.kessel.vorhanden) w["brenner"] = b.kessel.laeuft ? 1.f : 0.f;
    if (b.speicher.vorhanden) w["fuellstand"] = b.speicher.ladung;
    for (const auto &k : b.heizkreise) w["pumpe." + std::to_string(k.nr)] = k.pumpe ? 1.f : 0.f;
    if (b.kkp.vorhanden) w["kkp"] = b.kkp.laeuft ? 1.f : 0.f;
    return w;
}

std::string bezeichnung(const anlage::Bild &b, const std::string &k, std::string *einheit)
{
    auto setze = [&](const char *e) {
        if (einheit) *einheit = e;
    };
    setze("°C");
    size_t s = k.find('/');
    std::string geraet = s == std::string::npos ? "" : k.substr(0, s), rest = s == std::string::npos ? k : k.substr(s + 1);
    if (rest.rfind("raum.", 0) == 0) {
        int nr = atoi(rest.c_str() + 5);
        const anlage::Raum *r = b.raum(geraet + "/" + std::to_string(nr));
        std::string name = r ? r->name : "Raum " + std::to_string(nr);
        if (rest.find(".ist") != std::string::npos) return name + ", Ist";
        if (rest.find(".soll") != std::string::npos) return name + ", Soll";
        if (rest.find(".feuchte") != std::string::npos) {
            setze("%");
            return name + ", Feuchte";
        }
        setze("%");
        return name + ", Zielstellung";
    }
    if (rest.rfind("kanal.", 0) == 0) {
        setze("%");
        return "Kanal " + std::to_string(atoi(rest.c_str() + 6));
    }
    if (rest == "aussen") return "Außen";
    if (rest == "brenner") {
        setze("%");
        return "Brenner läuft";
    }
    if (rest == "fuellstand") {
        setze("%");
        return "Füllstand Speicher";
    }
    if (rest == "kkp") {
        setze("%");
        return "Kesselkreispumpe läuft";
    }
    if (rest.rfind("pumpe.", 0) == 0) {
        setze("%");
        return "Pumpe Heizkreis " + rest.substr(6);
    }
    if (rest.rfind("fuehler.", 0) == 0) {
        std::string r = rest.substr(8);
        static const std::map<std::string, const char *> NAMEN = {
            {"abgas", "Abgas"}, {"kessel_vl", "Kessel Vorlauf"}, {"kessel_rl", "Kessel Rücklauf"}, {"puffer", "Pufferspeicher"},
            {"puffer_unten", "Pufferspeicher unten"}, {"aussen", "Außen (Fühler)"}};
        auto it = NAMEN.find(r);
        if (it != NAMEN.end()) return it->second;
        if (r.size() == 6 && r[0] == 'h' && r[1] == 'k')
            return "Heizkreis " + r.substr(2, 1) + (r.substr(4) == "vl" ? " Vorlauf" : " Rücklauf");
        return r;
    }
    return rest;
}

}  // namespace verlauf
