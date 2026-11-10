// Zusammenführung der Gerätestände zum Anlagenbild — Regeln wie
// apple/Packages/Anlage/Sources/Anlage/Zusammenfuehrung.swift.
#include <ctype.h>
#include <string.h>
#include <algorithm>
#include "anlage.h"
#include "json.h"

namespace anlage {

// ---- kleine Helfer der Modellstrukturen --------------------------------------

Raumzustand Raum::zustand() const
{
    if (!an) return Raumzustand::aus;
    if (!thermometer) return Raumzustand::kein_thermometer;
    if (isnan(ist)) return Raumzustand::messwert_veraltet;
    if (!isnan(soll) && ist < soll - 0.05f) return Raumzustand::heizt;
    return Raumzustand::soll_erreicht;
}

int Etage::heizen() const
{
    int n = 0;
    for (const auto &r : raeume) n += r.zustand() == Raumzustand::heizt;
    return n;
}

const char *modus_text(Modus m)
{
    return m == Modus::ein ? "Hand ein" : m == Modus::aus ? "Hand aus" : "Automatik";
}

const char *modus_wert(Modus m) { return m == Modus::ein ? "ein" : m == Modus::aus ? "aus" : "auto"; }

static Modus modus_aus(const std::string &s) { return s == "ein" ? Modus::ein : s == "aus" ? Modus::aus : Modus::automatik; }

const char *schwere_text(Schwere s)
{
    return s == Schwere::stoerung ? "Störung" : s == Schwere::warnung ? "Warnung" : "Hinweis";
}

const char *Geraet::art_text() const
{
    switch (art) {
    case Art::verteiler: return "Verteiler";
    case Art::leitstand: return "Leitstand";
    default:
        return rolle == Rolle::kessel ? "Kessel" : rolle == Rolle::speicher ? "Pufferspeicher" : "Heizungsgerät";
    }
}

const char *Geraet::symbol() const
{
    switch (art) {
    case Art::verteiler: return "square.split.2x2";
    case Art::leitstand: return "antenna.radiowaves.left.and.right";
    default: return rolle == Rolle::speicher ? "cylinder.split.1x2" : "flame";
    }
}

const Raum *Bild::raum(const std::string &id) const
{
    for (const auto &e : etagen)
        for (const auto &r : e.raeume)
            if (r.id == id) return &r;
    return nullptr;
}

const Etage *Bild::etage(const std::string &g) const
{
    for (const auto &e : etagen)
        if (e.geraet == g) return &e;
    return nullptr;
}

const Geraet *Bild::geraet(const std::string &id) const
{
    for (const auto &g : geraete)
        if (g.id == id) return &g;
    return nullptr;
}

int Bild::raeume() const
{
    int n = 0;
    for (const auto &e : etagen) n += (int)e.raeume.size();
    return n;
}

int Bild::raeume_heizen() const
{
    int n = 0;
    for (const auto &e : etagen) n += e.heizen();
    return n;
}

int Bild::pumpen_gestoert() const
{
    int n = 0;
    auto gestoert = [](const Relais &r) { return r.bekannt && !r.online && !r.stromlos && r.weg != "keiner"; };
    for (const auto &k : heizkreise) n += gestoert(k.relais);
    if (kkp.vorhanden) n += gestoert(kkp.relais);
    return n;
}

int Bild::pumpen_relais() const { return (int)heizkreise.size() + (kkp.vorhanden ? 1 : 0); }

int Bild::befunde_mit(Schwere s) const
{
    int n = 0;
    for (const auto &b : befunde) n += b.schwere == s;
    return n;
}

int etagenrang(const std::string &name)
{
    std::string n;
    for (char c : name) n += (char)tolower((unsigned char)c);
    if (n.find("keller") != std::string::npos) return 0;
    if (n.find("erdgeschoss") != std::string::npos || n == "eg") return 1;
    if (n.find("2. og") != std::string::npos) return 3;
    if (n.find("obergeschoss") != std::string::npos || n.find("1. og") != std::string::npos || n == "og") return 2;
    if (n.find("dachgeschoss") != std::string::npos) return 4;
    if (n.find("spitzboden") != std::string::npos) return 5;
    return 9;
}

Art art_aus(const std::string &id, const std::string &rolle)
{
    if (rolle == "manifold") return Art::verteiler;
    if (rolle == "heat") return Art::heizung;
    if (rolle == "station") return Art::leitstand;
    if (id.rfind("heiz_", 0) == 0) return Art::heizung;
    if (id.rfind("lst_", 0) == 0) return Art::leitstand;
    return Art::verteiler;
}

// ---- Zusammenführung ------------------------------------------------------------

namespace {

struct Gelesen {
    const Geraetestand *s;
    json::Baum state, config, ble;
};

bool istSpeicherOrt(const std::string &ort)
{
    std::string n;
    for (char c : ort) n += (char)tolower((unsigned char)c);
    return n.find("speicher") != std::string::npos || n.find("puffer") != std::string::npos;
}

bool hat_fuehler(const cJSON *st, const char *rolle)
{
    for (const cJSON *p : json::liste(st, "probes"))
        if (json::wahr(p, "assigned") && json::text(p, "role") == rolle) return true;
    return false;
}

Relais relais_aus(const cJSON *r, const std::string &weg)
{
    Relais x;
    x.bekannt = json::wahr(r, "known");
    x.ein = json::wahr(r, "on");
    x.online = json::wahr(r, "online");
    x.abweichung = json::wahr(r, "mismatch");
    x.stromlos = json::wahr(r, "unpowered");
    x.weg = weg;
    return x;
}

const char *pumpengrund(const std::string &k)
{
    if (k == "transfer") return "der Kessel gibt Wärme ab";
    if (k == "burner") return "der Brenner läuft";
    if (k == "overrun") return "Nachlauf nach Brennerende";
    if (k == "no_transfer") return "Kessel kaum wärmer als der Speicher, Fördern brächte keine Wärme in den Speicher";
    if (k == "emergency") return "Notabfuhr, Kesselvorlauf über der Grenze";
    if (k == "no_reading") return "keine Kesselwerte, sie läuft sicherheitshalber";
    if (k == "hold") return "Schaltverzögerung, die Bedingung hat eben erst gewechselt";
    if (k == "min_run") return "Mindestlaufzeit";
    if (k == "min_pause") return "Mindestpause";
    if (k == "manual") return "Handbetrieb";
    if (k == "disabled") return "keine Kesselkreispumpe eingerichtet";
    return nullptr;
}

}  // namespace

std::shared_ptr<Bild> zusammenfuehren(const std::vector<Geraetestand> &staende, int64_t jetzt)
{
    auto b = std::make_shared<Bild>();
    std::vector<Gelesen> vt, hz, ls;
    bool vollstaendig = !staende.empty();
    for (const auto &s : staende) {
        Gelesen g{&s, json::lesen(s.state), json::lesen(s.config), json::lesen(s.ble)};
        if (!s.gesehen && s.fehler.empty()) vollstaendig = false;
        b->stand = std::max(b->stand, s.kontakt);
        if (s.art == Art::verteiler) vt.push_back(std::move(g));
        else if (s.art == Art::heizung) hz.push_back(std::move(g));
        else ls.push_back(std::move(g));
    }
    b->vollstaendig = vollstaendig;

    // Kessel- und Speichergerät nach Fühlern und Kennzeichen, nicht nach Reihenfolge.
    const Gelesen *kessel = nullptr, *speicher = nullptr;
    for (const auto &h : hz)
        if (h.state && !json::wahr(h.state.get(), "burner.remote") && json::wahr(h.state.get(), "burner.known", true)) {
            kessel = &h;
            break;
        }
    if (!kessel)
        for (const auto &h : hz)
            if (h.state && hat_fuehler(h.state.get(), "abgas")) {
                kessel = &h;
                break;
            }
    for (const auto &h : hz)
        if (h.state && json::hat(h.state.get(), "charge.puffer_remote") && !json::wahr(h.state.get(), "charge.puffer_remote")) {
            speicher = &h;
            break;
        }
    if (!speicher)
        for (const auto &h : hz)
            if (h.state && hat_fuehler(h.state.get(), "puffer")) {
                speicher = &h;
                break;
            }
    if (!speicher)
        for (const auto &h : hz)
            if (h.state && cJSON_GetArraySize(json::weg(h.state.get(), "circuits")) > 0) {
                speicher = &h;
                break;
            }

    // Fühlerwerte: eigene zugeordnete zuerst, fremde (remote_probes) nur für fehlende Rollen.
    for (const auto &h : hz)
        for (const cJSON *p : json::liste(h.state.get(), "probes"))
            if (json::wahr(p, "assigned") && json::hat(p, "temp_c")) {
                std::string r = json::text(p, "role");
                if (!r.empty() && !b->fuehler.count(r)) b->fuehler[r] = json::zahl(p, "temp_c");
            }
    for (const auto &h : hz) {
        const cJSON *rp = json::weg(h.state.get(), "remote_probes");
        if (cJSON_IsObject(rp))
            for (const cJSON *c = rp->child; c; c = c->next)
                if (cJSON_IsNumber(c) && !b->fuehler.count(c->string)) b->fuehler[c->string] = (float)c->valuedouble;
    }
    auto fw = [&](const char *rolle) {
        auto it = b->fuehler.find(rolle);
        return it == b->fuehler.end() ? NAN : it->second;
    };

    // Bedarf je Verteiler: ODER über die Bedarfsquellen aller Heizungsgeräte.
    std::map<std::string, bool> bedarf;
    for (const auto &h : hz)
        for (const cJSON *q : json::liste(h.state.get(), "demand_sources"))
            bedarf[json::text(q, "id")] = bedarf[json::text(q, "id")] || json::wahr(q, "demand");

    // ---- Etagen
    for (const auto &v : vt) {
        const cJSON *st = v.state.get(), *kf = v.config.get();
        if (!st) continue;
        Etage e;
        e.geraet = v.s->id;
        e.name = v.s->ort.empty() ? json::text(st, "device.site", v.s->id.c_str()) : v.s->ort;
        e.rang = etagenrang(e.name);
        e.bedarf = bedarf[e.geraet];
        e.nur_aussen = json::text(st, "device.function") == "outdoor";
        // Thermometernamen aus /api/ble
        std::map<std::string, std::string> thermo;
        for (const cJSON *d : json::liste(v.ble.get(), "devices")) thermo[json::text(d, "mac")] = json::text(d, "name");
        std::map<int, std::string> kanalraum;
        for (const cJSON *r : json::liste(st, "rooms")) {
            Raum x;
            x.nr = json::ganz(r, "id");
            x.id = e.geraet + "/" + std::to_string(x.nr);
            x.name = json::text(r, "name");
            x.etage = e.name;
            x.geraet = e.geraet;
            x.an = json::text(r, "mode", "heat") != "off";
            x.soll = json::zahl(r, "target_c");
            x.thermometer = json::wahr(r, "sensor_set");
            if (json::wahr(r, "temp_valid")) {
                x.ist = json::zahl(r, "temp_c");
                x.feuchte = json::zahl(r, "humidity");
            }
            x.batterie = json::hat(r, "battery") ? json::ganz(r, "battery") : -1;
            x.alter = json::hat(r, "temp_age_s") ? json::ganz(r, "temp_age_s") : -1;
            x.zielstellung = json::zahl(r, "target_position");
            x.naechste_pruefung = json::ganz(r, "next_check_s", -1);
            for (const cJSON *k : json::liste(r, "channels")) {
                x.kanaele.push_back((int)k->valuedouble);
                kanalraum[(int)k->valuedouble] = x.name;
            }
            for (const cJSON *kr : json::liste(kf, "rooms"))
                if (json::ganz(kr, "id") == x.nr) {
                    x.p_band = json::zahl(kr, "p_band_k");
                    std::string mac = json::text(kr, "sensor_mac");
                    x.thermometer_name = thermo.count(mac) ? thermo[mac] : mac;
                }
            e.raeume.push_back(std::move(x));
        }
        for (const cJSON *c : json::liste(st, "channels")) {
            Kanal k;
            k.nr = json::ganz(c, "id");
            k.raum = kanalraum.count(k.nr) ? kanalraum[k.nr] : "";
            k.bekannt = json::wahr(c, "known");
            if (k.bekannt) k.stellung = json::zahl(c, "position");
            k.hand = json::wahr(c, "manual");
            k.kalibriert = json::wahr(c, "calibrated");
            k.reserviert = json::wahr(c, "reserved");
            k.wartet = json::wahr(c, "pending");
            k.op = json::text(c, "op", "idle");
            k.gruppe = json::ganz(c, "group", (k.nr + 1) / 2);
            k.gegenspannung = json::ganz(c, "bemf_mv");
            e.kanaele.push_back(k);
        }
        b->etagen.push_back(std::move(e));
    }
    std::stable_sort(b->etagen.begin(), b->etagen.end(), [](const Etage &a, const Etage &c) {
        return a.rang != c.rang ? a.rang < c.rang : a.name < c.name;
    });

    // ---- Außen: erster Verteiler mit gültigem Außenfühler, sonst ein Heizungsgerät
    for (const auto &v : vt) {
        const cJSON *st = v.state.get();
        if (json::wahr(st, "outdoor.set") && json::wahr(st, "outdoor.valid")) {
            b->aussen.temperatur = json::zahl(st, "outdoor.temp_c");
            b->aussen.feuchte = json::zahl(st, "outdoor.humidity");
            b->aussen.alter = json::ganz(st, "outdoor.age_s", -1);
            b->aussen.quelle = "Verteiler " + v.s->ort;
            break;
        }
    }
    if (isnan(b->aussen.temperatur))
        for (const auto &h : hz) {
            const cJSON *st = h.state.get();
            if (json::hat(st, "outdoor.temp_c")) {
                b->aussen.temperatur = json::zahl(st, "outdoor.temp_c");
                b->aussen.alter = json::ganz(st, "outdoor.age_s", -1);
                std::string q = json::text(st, "outdoor.source");
                b->aussen.quelle = q.empty() ? h.s->ort : "über " + q;
                break;
            }
        }
    if (isnan(b->aussen.temperatur) && !isnan(fw("aussen"))) {
        b->aussen.temperatur = fw("aussen");
        b->aussen.quelle = "Fühler am Heizungsgerät";
    }

    // ---- Kessel
    if (kessel) {
        const cJSON *st = kessel->state.get(), *kf = kessel->config.get();
        Kessel &k = b->kessel;
        k.vorhanden = true;
        k.laeuft = json::wahr(st, "burner.running");
        k.seit = json::ganz(st, "burner.since_s", -1);
        k.abgas = json::zahl(st, "burner.abgas_c", fw("abgas"));
        k.bezugslinie = json::zahl(st, "burner.baseline_c");
        k.laufzeit_heute = json::ganz(st, "burner.runtime_today_s");
        k.starts_heute = json::ganz(st, "burner.starts_today");
        k.laufzeit_gestern = json::ganz(st, "burner.runtime_yesterday_s");
        k.starts_gestern = json::ganz(st, "burner.starts_yesterday");
        k.liter_heute = json::zahl(st, "burner.litres_today");
        k.taktbetrieb = json::wahr(st, "burner.short_cycling");
        k.duese = json::zahl(kf, "burner.duese_l_h");
        k.abgas_ladungen = json::ganz(st, "flue.charges");
        if (json::hat(st, "flue.delta_k")) k.abgas_abstand = json::zahl(st, "flue.delta_k");
    }
    b->kessel.vorlauf = fw("kessel_vl");
    b->kessel.ruecklauf = fw("kessel_rl");
    if (!kessel && (!isnan(b->kessel.vorlauf) || !isnan(fw("abgas")))) {
        b->kessel.vorhanden = true;
        b->kessel.abgas = fw("abgas");
    }

    // ---- Speicher
    if (speicher) {
        const cJSON *st = speicher->state.get(), *kf = speicher->config.get();
        Speicher &s = b->speicher;
        s.vorhanden = true;
        s.ladung = json::zahl(st, "charge.level");
        s.phase = json::text(st, "charge.phase");
        s.phase_seit = json::ganz(st, "charge.since_s", -1);
        s.warmwasser_knapp = json::wahr(st, "charge.warn_dhw");
        s.geschaetzt = json::wahr(st, "charge.limited");
        s.hoechstwert = json::zahl(st, "charge.kalibrierung.peak_c");
        s.voll = json::zahl(kf, "buffer.voll_c");
        s.leer = json::zahl(kf, "buffer.leer_c");
        s.warngrenze = json::zahl(kf, "buffer.warn_c");
        s.leer_gelernt = json::wahr(kf, "buffer.leer_lernen") && json::ganz(kf, "buffer.leer_epoch") > 0;
        for (const cJSON *p : json::liste(st, "probes"))
            if (json::text(p, "role") == "puffer") s.korrektur = json::zahl(p, "offset_k", 0);
    }
    b->speicher.temperatur = fw("puffer");
    if (!isnan(b->speicher.temperatur)) b->speicher.vorhanden = true;

    // ---- Heizkreise: nur vom Speichergerät
    std::map<std::string, std::string> ortVon;
    for (const auto &s : staende) ortVon[s.id] = s.ort;
    if (speicher) {
        const cJSON *st = speicher->state.get(), *kf = speicher->config.get();
        for (const cJSON *c : json::liste(st, "circuits")) {
            Heizkreis k;
            k.nr = json::ganz(c, "id");
            k.name = json::text(c, "name", ("Heizkreis " + std::to_string(k.nr)).c_str());
            k.modus = modus_aus(json::text(c, "mode", "auto"));
            k.grund = json::text(c, "reason");
            k.relais = relais_aus(json::weg(c, "relay"), json::text(c, "path"));
            k.pumpe = json::wahr(c, "on") && !k.relais.stromlos;
            k.bedarf = json::wahr(c, "demand");
            k.gesperrt = json::wahr(c, "blocked") || (json::wahr(c, "on") && k.relais.stromlos);
            k.veraltet = json::wahr(c, "stale");
            k.keiner_erreicht = !json::wahr(c, "any_seen", true);
            std::string vr = "hk" + std::to_string(k.nr) + "_vl", rr = "hk" + std::to_string(k.nr) + "_rl";
            k.vorlauf = json::zahl(c, "vl_c", fw(vr.c_str()));
            k.ruecklauf = json::zahl(c, "rl_c", fw(rr.c_str()));
            for (const cJSON *kc : json::liste(kf, "circuits"))
                if (json::ganz(kc, "id") == k.nr)
                    for (const cJSON *p : json::liste(kc, "peers"))
                        if (cJSON_IsString(p)) k.versorgt.push_back(ortVon.count(p->valuestring) ? ortVon[p->valuestring] : p->valuestring);
            b->heizkreise.push_back(std::move(k));
        }
    }

    // ---- Kesselkreispumpe: erstes Gerät mit boiler_pump.enabled
    for (const auto &h : hz) {
        const cJSON *st = h.state.get();
        if (!json::wahr(st, "boiler_pump.enabled")) continue;
        Kesselkreispumpe &p = b->kkp;
        p.vorhanden = true;
        p.modus = modus_aus(json::text(st, "boiler_pump.mode", "auto"));
        p.relais = relais_aus(json::weg(st, "boiler_pump.relay"), json::text(st, "boiler_pump.path"));
        p.laeuft = json::wahr(st, "boiler_pump.on") && !p.relais.stromlos;
        p.seit = json::ganz(st, "boiler_pump.since_s", -1);
        const char *g = pumpengrund(json::text(st, "boiler_pump.reason_key"));
        p.grund = g ? g : json::text(st, "boiler_pump.reason");
        break;
    }

    // ---- Geräte: Heizungsgeräte (Kessel zuerst), dann Verteiler, dann Leitstand
    auto geraet_aus = [&](const Gelesen &g, Rolle rolle) {
        Geraet x;
        const cJSON *st = g.state.get();
        x.id = g.s->id;
        x.art = g.s->art;
        x.rolle = rolle;
        x.ort = g.s->ort.empty() ? json::text(st, "device.site", g.s->id.c_str()) : g.s->ort;
        x.ip = g.s->ip;
        x.hostname = g.s->hostname;
        x.erreichbar = g.s->erreichbar;
        x.gesehen = g.s->gesehen;
        x.fehler = g.s->fehler;
        x.kontakt = g.s->kontakt;
        x.version = json::text(st, "version");
        x.rssi = json::ganz(st, "net.rssi");
        x.laufzeit = json::ganz(st, "uptime_s", -1);
        x.heap = json::ganz(st, "heap", -1);
        x.zeit_gueltig = json::wahr(st, "net.time_valid", true);
        return x;
    };
    std::vector<const Gelesen *> hzs;
    for (const auto &h : hz) hzs.push_back(&h);
    std::stable_sort(hzs.begin(), hzs.end(), [&](const Gelesen *a, const Gelesen *c) { return (a == kessel ? 0 : 1) < (c == kessel ? 0 : 1); });
    for (const Gelesen *h : hzs) {
        Rolle r = h == kessel && h == speicher ? Rolle::beides
                  : h == kessel                ? Rolle::kessel
                  : h == speicher              ? Rolle::speicher
                  : istSpeicherOrt(h->s->ort)  ? Rolle::speicher
                                               : Rolle::kessel;
        b->geraete.push_back(geraet_aus(*h, r));
    }
    std::vector<const Gelesen *> vts;
    for (const auto &v : vt) vts.push_back(&v);
    std::stable_sort(vts.begin(), vts.end(), [](const Gelesen *a, const Gelesen *c) { return etagenrang(a->s->ort) < etagenrang(c->s->ort); });
    for (const Gelesen *v : vts) b->geraete.push_back(geraet_aus(*v, Rolle::keine));
    for (const auto &l : ls) b->geraete.push_back(geraet_aus(l, Rolle::keine));
    (void)jetzt;
    return b;
}

}  // namespace anlage
