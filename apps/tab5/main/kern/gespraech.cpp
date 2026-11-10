#include "gespraech.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "anweisungen.h"
#include "daten.h"
#include "fmt.h"
#include "json.h"
#include "katalog.h"
#include "werkzeuge.h"

namespace gespraech {

namespace {

constexpr int RUNDEN = 16;   // Werkzeugrunden je Frage; die App kennt keine Grenze, die Tafel schützt ihr Kontingent

bool denkt(const std::string &modell) { return modell.find("haiku-4") == std::string::npos; }

std::string jetzt_iso() { return fmt::iso(daten::jetzt()); }

}  // namespace

ki::Stand Assistent::stand()
{
    std::lock_guard<std::mutex> l(mx_);
    return st_;
}

void Assistent::status(ki::Lage lage, bool bericht)
{
    std::lock_guard<std::mutex> l(mx_);
    st_.lage = lage;
    st_.bericht_laeuft = bericht;
    geaendert();
}

void Assistent::neues_gespraech()
{
    epoche_++;
    std::lock_guard<std::mutex> l(mx_);
    st_.beitraege.clear();
    st_.lage = ki::Lage::bereit;
    st_.bezug.clear();
    transkript_.clear();
    geaendert();
}

void Assistent::bezug_setzen(const std::string &b)
{
    std::lock_guard<std::mutex> l(mx_);
    st_.bezug = b;
    geaendert();
}

void Assistent::entwurf_setzen(const std::string &t)
{
    std::lock_guard<std::mutex> l(mx_);
    st_.entwurf = t;
    geaendert();
}

std::string Assistent::vorschlag_anlegen(ki::Vorschlag v)
{
    std::lock_guard<std::mutex> l(mx_);
    char id[16];
    snprintf(id, sizeof id, "v-%06x", (unsigned)(naechste_id_++ * 2654435761u >> 8));
    v.id = id;
    v.zeit = daten::jetzt();
    v.status = ki::VStatus::offen;
    st_.vorschlaege.push_back(v);
    if (st_.vorschlaege.size() > 40) st_.vorschlaege.erase(st_.vorschlaege.begin());
    geaendert();
    return v.id;
}

std::vector<std::string> Assistent::aenderungsprotokoll()
{
    std::lock_guard<std::mutex> l(mx_);
    return protokoll_;
}

// Eine Anfrage an die API mit Strom; spiegelt die Blöcke laufend in den Beitrag `beitrag`.
bool Assistent::runde(std::vector<std::string> &nachrichten, const claude::Anfrage &a, size_t beitrag, claude::Antwort &out,
                      uint32_t epoche, std::string &fehler)
{
    std::string rumpf = claude::rumpf(a, nachrichten);
    size_t basis;
    {
        std::lock_guard<std::mutex> l(mx_);
        basis = beitrag < st_.beitraege.size() ? st_.beitraege[beitrag].bausteine.size() : 0;
    }
    for (int versuch = 0; versuch < 3; versuch++) {
        claude::Strom strom;
        std::string koerper;
        int status = senden_ ? senden_(rumpf, [&](const char *d, size_t n) {
            if (epoche != epoche_) return false;
            strom.futter(d, n);
            if (beitrag == (size_t)-1 || !strom.geaendert()) return true;
            std::lock_guard<std::mutex> l(mx_);
            if (beitrag >= st_.beitraege.size()) return true;
            auto &bs = st_.beitraege[beitrag].bausteine;
            bs.resize(basis);
            for (const auto &b : strom.antwort().bloecke) {
                ki::Baustein x;
                if (b.typ == "text" && !b.text.empty()) {
                    x.art = ki::Baustein::Art::text;
                    x.text = b.text;
                } else if (b.typ == "thinking" && !b.text.empty()) {
                    x.art = ki::Baustein::Art::ueberlegung;
                    x.text = b.text;
                } else if (b.typ == "tool_use") {
                    x.art = ki::Baustein::Art::werkzeug;
                    x.werkzeug = {b.name, werkzeuge::laeuft_text(b.name), "", false};
                } else continue;
                bs.push_back(x);
            }
            st_.lage = ki::Lage::antwortet;
            geaendert();
            return true;
        }, koerper) : 0;
        if (epoche != epoche_) {
            fehler = "Angehalten.";
            return false;
        }
        const claude::Antwort &an = strom.antwort();
        if (status == 200 && an.fehler_typ.empty() && an.beendet) {
            out = an;
            return true;
        }
        bool nochmal = (status != 200 && claude::wiederholen(status, "")) || claude::wiederholen(0, an.fehler_typ);
        // ⚠️ Nur wiederholen, solange noch nichts sichtbar geworden ist (ClaudeAusfuehrer.swift)
        if (nochmal && !an.angefangen && versuch < 2) {
            if (schlafen_) schlafen_(versuch ? 6000 : 2000);
            continue;
        }
        if (status != 200) fehler = claude::fehlertext(status, koerper);
        else if (!an.fehler_typ.empty()) fehler = an.fehler_typ == "overloaded_error" ? "Anthropic ist gerade überlastet. Bitte versuchen Sie es gleich noch einmal." : "Anthropic meldet: " + an.fehler_text;
        else if (an.stop_reason == "refusal") fehler = "Claude hat die Antwort abgelehnt.";
        else fehler = "Die Verbindung ist abgerissen, bevor die Antwort vollständig war.";
        return false;
    }
    return false;
}

void Assistent::frage(const std::string &text)
{
    uint32_t epoche = epoche_;
    std::string bezug;
    size_t beitrag;
    {
        std::lock_guard<std::mutex> l(mx_);
        bezug = st_.bezug;
        st_.bezug.clear();
        ki::Beitrag f;
        f.nutzer = true;
        f.text = text;
        st_.beitraege.push_back(f);
        st_.beitraege.push_back(ki::Beitrag{});
        beitrag = st_.beitraege.size() - 1;
        st_.lage = ki::Lage::liest;
        // Ein Bezug aus der Oberfläche geht vor die Frage (AppModell+Assistent.swift)
        transkript_.push_back(claude::nutzer(bezug.empty() ? text : "Zu „" + bezug + "“: " + text));
        geaendert();
    }
    claude::Anfrage a;
    a.modell = modell_;
    a.denken = denkt(modell_);
    a.system = anweisungen::system();
    a.werkzeuge = werkzeuge::definitionen();
    werkzeuge::Kontext k;
    k.vorschlag_anlegen = [this](ki::Vorschlag v) { return vorschlag_anlegen(std::move(v)); };
    k.aenderungen = [this] { return aenderungsprotokoll(); };
    std::string fehler;
    for (int r = 0; r < RUNDEN; r++) {
        std::vector<std::string> nachrichten;
        {
            std::lock_guard<std::mutex> l(mx_);
            nachrichten = transkript_;
        }
        claude::Antwort an;
        if (!runde(nachrichten, a, beitrag, an, epoche, fehler)) break;
        {
            std::lock_guard<std::mutex> l(mx_);
            transkript_.push_back(claude::assistent(an));
        }
        if (an.stop_reason != "tool_use") {
            if (an.stop_reason == "max_tokens") fehler = "Die Antwort wurde an der Längengrenze abgeschnitten.";
            break;
        }
        // Werkzeuge ausführen, Ergebnisse in die Werkzeugzeilen
        std::vector<std::pair<std::string, std::string>> erg;
        for (const auto &b : an.bloecke) {
            if (b.typ != "tool_use" || !b.fertig) continue;
            werkzeuge::Ergebnis e = epoche == epoche_ ? werkzeuge::ausfuehren(b.name, b.eingabe, k)
                                                      : werkzeuge::Ergebnis{"Nicht ausgeführt: Der Nutzer hat die Antwort angehalten.", "angehalten", ""};
            erg.push_back({b.id, e.text});
            std::lock_guard<std::mutex> l(mx_);
            if (beitrag < st_.beitraege.size()) {
                auto &bs = st_.beitraege[beitrag].bausteine;
                for (auto &x : bs)
                    if (x.art == ki::Baustein::Art::werkzeug && x.werkzeug.name == b.name && !x.werkzeug.fertig) {
                        x.werkzeug.fertig = true;
                        x.werkzeug.beschreibung = e.beschreibung;
                        x.werkzeug.ergebnis = e.text.substr(0, 600);
                        break;
                    }
                if (!e.vorschlag.empty()) bs.push_back({ki::Baustein::Art::vorschlag, e.vorschlag, {}});
            }
            geaendert();
        }
        std::lock_guard<std::mutex> l(mx_);
        transkript_.push_back(claude::ergebnisse(erg));
        st_.lage = ki::Lage::liest;
        geaendert();
    }
    std::lock_guard<std::mutex> l(mx_);
    // ⚠️ Zu jedem tool_use gehört ein tool_result: endet die Schleife nach einer Antwort mit
    // Werkzeugaufrufen, fehlt das Ergebnis — dann die Nachricht zurücknehmen.
    if (!transkript_.empty() && transkript_.back().find("\"role\":\"assistant\"") != std::string::npos &&
        transkript_.back().find("\"tool_use\"") != std::string::npos)
        transkript_.pop_back();
    // Und eine Frage ohne Antwort ebenfalls, sonst folgen zwei Nutzernachrichten aufeinander
    if (!transkript_.empty() && transkript_.back().rfind("{\"role\":\"user\",\"content\":[{\"type\":\"text\"", 0) == 0) transkript_.pop_back();
    if (beitrag < st_.beitraege.size()) {
        auto &bs = st_.beitraege[beitrag].bausteine;
        for (auto &x : bs)
            if (x.art == ki::Baustein::Art::werkzeug && !x.werkzeug.fertig) x.werkzeug.fertig = true;
        if (!fehler.empty()) bs.push_back({ki::Baustein::Art::fehler, fehler, {}});
    }
    st_.lage = fehler.empty() || fehler == "Angehalten." ? ki::Lage::bereit : ki::Lage::fehler;
    geaendert();
}

void Assistent::lagebericht()
{
    uint32_t epoche = epoche_;
    status(stand().lage, true);
    claude::Anfrage a;
    a.modell = modell_;
    a.denken = denkt(modell_);
    a.system = anweisungen::system();
    a.werkzeuge = werkzeuge::definitionen(true);
    a.format = anweisungen::lagebericht_schema();
    werkzeuge::Kontext k;
    k.aenderungen = [this] { return aenderungsprotokoll(); };
    std::vector<std::string> n = {claude::nutzer(anweisungen::lagebericht_auftrag())};
    std::string fehler, text;
    for (int r = 0; r < 8; r++) {
        claude::Antwort an;
        if (!runde(n, a, (size_t)-1, an, epoche, fehler)) break;
        n.push_back(claude::assistent(an));
        if (an.stop_reason != "tool_use") {
            for (const auto &b : an.bloecke)
                if (b.typ == "text") text += b.text;
            break;
        }
        std::vector<std::pair<std::string, std::string>> erg;
        for (const auto &b : an.bloecke)
            if (b.typ == "tool_use" && b.fertig) erg.push_back({b.id, werkzeuge::ausfuehren(b.name, b.eingabe, k).text});
        n.push_back(claude::ergebnisse(erg));
    }
    json::Baum j = json::lesen(text);
    std::lock_guard<std::mutex> l(mx_);
    st_.bericht_laeuft = false;
    if (j && json::hat(j.get(), "zustand")) {
        if (st_.bericht.zustand >= 0) {
            st_.fruehere.push_back(st_.bericht);
            if (st_.fruehere.size() > 30) st_.fruehere.erase(st_.fruehere.begin());
        }
        ki::Lagebericht lb;
        std::string z = json::text(j.get(), "zustand");
        lb.zustand = z == "handeln" ? 2 : z == "beobachten" ? 1 : 0;
        lb.kurztext = json::text(j.get(), "kurztext");
        for (const cJSON *h : json::liste(j.get(), "hinweise")) {
            std::string s = json::text(h, "schwere");
            lb.hinweise.push_back({s == "stoerung" ? 2 : s == "warnung" ? 1 : 0, json::text(h, "text"), json::text(h, "bezug")});
            if (lb.hinweise.size() == 3) break;
        }
        lb.erstellt = daten::jetzt();
        lb.modell = claude::modellname(modell_);
        st_.bericht = lb;
    }
    geaendert();
}

void Assistent::bericht_aus_befunden()
{
    auto b = daten::bild();
    std::lock_guard<std::mutex> l(mx_);
    if (st_.bericht.zustand >= 0 && !st_.bericht.aus_befunden) return;   // ein Bericht der KI bleibt stehen
    ki::Lagebericht lb;
    lb.aus_befunden = true;
    lb.erstellt = daten::jetzt();
    int s = b->befunde_mit(anlage::Schwere::stoerung), w = b->befunde_mit(anlage::Schwere::warnung);
    lb.zustand = s ? 2 : w ? 1 : 0;
    // feste Texte wie Lagebericht.ausBefunden der App
    lb.kurztext = s ? "Eine Störung beeinträchtigt den Betrieb." : w ? "Die Anlage arbeitet, einige Punkte sollten geprüft werden."
                    : b->befunde.empty() ? "Die Anlage arbeitet ohne Auffälligkeiten."
                                         : "Die Anlage arbeitet; es liegen nur Hinweise vor.";
    if (s > 1) lb.kurztext = std::to_string(s) + " Störungen beeinträchtigen den Betrieb.";
    for (const auto &f : b->befunde) {
        if (lb.hinweise.size() == 3) break;
        if (f.schwere == anlage::Schwere::hinweis && (s || w)) continue;
        lb.hinweise.push_back({(int)f.schwere, f.titel, f.id});
    }
    st_.bericht = lb;
    geaendert();
}

// ---- Vorschläge übernehmen ---------------------------------------------------------------

namespace {

// Pfad „burner.duese_l_h“ als verschachteltes Objekt mit Wert
cJSON *verschachtelt(const std::string &pfad, cJSON *wert)
{
    size_t p = pfad.find('.');
    cJSON *o = cJSON_CreateObject();
    if (p == std::string::npos) cJSON_AddItemToObject(o, pfad.c_str(), wert);
    else cJSON_AddItemToObject(o, pfad.substr(0, p).c_str(), verschachtelt(pfad.substr(p + 1), wert));
    return o;
}

cJSON *wert_json(const std::string &roh)
{
    if (roh == "true" || roh == "false") return cJSON_CreateBool(roh == "true");
    char *e;
    double d = strtod(roh.c_str(), &e);
    if (!roh.empty() && *e == 0) return cJSON_CreateNumber(d);
    return cJSON_CreateString(roh.c_str());
}

bool gleich(const cJSON *v, const std::string &roh)
{
    if (!v) return false;
    if (cJSON_IsNumber(v)) {
        double d = strtod(roh.c_str(), nullptr);
        return fabs(v->valuedouble - d) <= 1e-5 * fmax(1, fabs(d));
    }
    if (cJSON_IsBool(v)) return (roh == "true") == (bool)cJSON_IsTrue(v);
    if (cJSON_IsString(v)) return roh == v->valuestring;
    return false;
}

}  // namespace

// Schreibt eine Änderung (oder nimmt sie zurück) und liest sie zurück. Leer = gelungen.
std::string Assistent::aenderung_schreiben(const ki::Aenderung &a, bool zurueck)
{
    const std::string &ziel = zurueck ? a.wert_bisher : a.wert_neu, &erwartet_alt = zurueck ? a.wert_neu : a.wert_bisher;
    int nr = atoi(a.eintrag.c_str());
    if (a.parameter == "raum.target_c" || a.parameter == "raum.mode") {
        bool soll = a.parameter == "raum.target_c";
        std::string f = daten::befehl_warten(a.geraet, "POST", "/api/room/" + std::to_string(nr) + (soll ? "/target" : "/mode"),
                                             soll ? "{\"target_c\":" + ziel + "}" : "{\"mode\":\"" + ziel + "\"}");
        if (!f.empty()) return f;
        std::string st;
        if (!daten::lesen_warten(a.geraet, "/api/state", st)) return "Der Wert ließ sich nicht zurücklesen.";
        json::Baum j = json::lesen(st);
        for (const cJSON *r : json::liste(j.get(), "rooms"))
            if (json::ganz(r, "id") == nr) return gleich(json::weg(r, soll ? "target_c" : "mode"), ziel) ? "" : "Das Gerät hat den Wert nicht übernommen.";
        return "Der Raum fehlt in der Antwort des Geräts.";
    }
    if (a.parameter == "heizkreis.betriebsart" || a.parameter == "kesselkreispumpe.betriebsart") {
        bool kkp = a.parameter[0] == 'k';
        std::string f = kkp ? daten::befehl_warten(a.geraet, "POST", "/api/boilerpump/" + ziel, "")
                            : daten::befehl_warten(a.geraet, "POST", "/api/circuit/" + std::to_string(nr) + "/mode", "{\"mode\":\"" + ziel + "\"}");
        if (!f.empty()) return f;
        std::string st;
        if (!daten::lesen_warten(a.geraet, "/api/state", st)) return "Der Wert ließ sich nicht zurücklesen.";
        json::Baum j = json::lesen(st);
        if (kkp) return json::text(j.get(), "boiler_pump.mode") == ziel ? "" : "Das Gerät hat die Betriebsart nicht übernommen.";
        for (const cJSON *c : json::liste(j.get(), "circuits"))
            if (json::ganz(c, "id") == nr) return json::text(c, "mode") == ziel ? "" : "Das Gerät hat die Betriebsart nicht übernommen.";
        return "Der Heizkreis fehlt in der Antwort des Geräts.";
    }
    const katalog::Parameter *p = katalog::finden(a.parameter);
    if (!p) return "Unbekannte Einstellung.";
    // Frisch lesen und prüfen, dass niemand den Wert inzwischen geändert hat
    std::string kt;
    if (!daten::lesen_warten(a.geraet, "/api/config", kt)) return "Die Einstellungen ließen sich nicht lesen.";
    json::Baum k = json::lesen(kt);
    if (!k) return "Die Einstellungen ließen sich nicht lesen.";
    auto finde = [&](const char *liste) -> cJSON * {
        cJSON *l = cJSON_GetObjectItemCaseSensitive(k.get(), liste);
        for (cJSON *e = l ? l->child : nullptr; e; e = e->next)
            if (json::ganz(e, "id") == nr) return e;
        return nullptr;
    };
    cJSON *teil = nullptr;
    if (p->ort == katalog::Ort::raum) {
        cJSON *r = finde("rooms");
        if (!r) return "Der Raum fehlt in den Einstellungen.";
        if (!erwartet_alt.empty() && !gleich(cJSON_GetObjectItemCaseSensitive(r, p->pfad), erwartet_alt))
            return "Der Wert wurde inzwischen geändert; der Vorschlag gilt nicht mehr.";
        cJSON_ReplaceItemInObjectCaseSensitive(r, p->pfad, wert_json(ziel));
        // ⚠️ rooms ersetzt die ganze Liste: alle Räume vollständig senden (Schreibweg.swift)
        teil = cJSON_CreateObject();
        cJSON_AddItemToObject(teil, "rooms", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(k.get(), "rooms"), true));
    } else if (p->ort == katalog::Ort::heizkreis) {
        cJSON *c = finde("circuits");
        if (!c) return "Der Heizkreis fehlt in den Einstellungen.";
        if (!erwartet_alt.empty() && !gleich(cJSON_GetObjectItemCaseSensitive(c, p->pfad), erwartet_alt))
            return "Der Wert wurde inzwischen geändert; der Vorschlag gilt nicht mehr.";
        // ⚠️ circuits: alle Kennungen, nur das geänderte Feld (Schreibweg.swift)
        teil = cJSON_CreateObject();
        cJSON *l = cJSON_AddArrayToObject(teil, "circuits");
        for (cJSON *e = cJSON_GetObjectItemCaseSensitive(k.get(), "circuits")->child; e; e = e->next) {
            cJSON *x = cJSON_CreateObject();
            cJSON_AddNumberToObject(x, "id", json::ganz(e, "id"));
            if (json::ganz(e, "id") == nr) cJSON_AddItemToObject(x, p->pfad, wert_json(ziel));
            cJSON_AddItemToArray(l, x);
        }
    } else {
        if (!erwartet_alt.empty() && !gleich(json::weg(k.get(), p->pfad), erwartet_alt))
            return "Der Wert wurde inzwischen geändert; der Vorschlag gilt nicht mehr.";
        teil = verschachtelt(p->pfad, wert_json(ziel));
    }
    std::string rumpf = json::schreiben(teil);
    cJSON_Delete(teil);
    std::string f = daten::befehl_warten(a.geraet, "PUT", "/api/config", rumpf);
    if (!f.empty()) return f;
    // Zurücklesen und vergleichen
    if (!daten::lesen_warten(a.geraet, "/api/config", kt)) return "Der Wert ließ sich nicht zurücklesen.";
    json::Baum n = json::lesen(kt);
    const cJSON *w = nullptr;
    if (p->ort == katalog::Ort::geraet) w = json::weg(n.get(), p->pfad);
    else
        for (const cJSON *e : json::liste(n.get(), p->ort == katalog::Ort::raum ? "rooms" : "circuits"))
            if (json::ganz(e, "id") == nr) w = json::weg(e, p->pfad);
    return gleich(w, ziel) ? "" : "Das Gerät hat den Wert nicht übernommen.";
}

void Assistent::uebernehmen(const std::string &id)
{
    ki::Vorschlag v;
    {
        std::lock_guard<std::mutex> l(mx_);
        bool da = false;
        for (auto &x : st_.vorschlaege)
            if (x.id == id && x.status == ki::VStatus::offen) {
                x.status = ki::VStatus::laeuft;
                v = x;
                da = true;
            }
        if (!da) return;
        geaendert();
    }
    std::string fehler;
    if (v.aktion) {
        std::string pfad = v.aktion_art == "neustart"     ? "/api/system/restart"
                           : v.aktion_art == "fuehlersuche" ? "/api/probes/rescan"
                           : v.aktion_art == "schutzfahrt"  ? "/api/system/seize"
                                                            : "/api/calib/" + v.eintrag + "/start";
        fehler = daten::befehl_warten(v.geraet, "POST", pfad, "");
    } else {
        size_t i = 0;
        for (; i < v.aenderungen.size(); i++) {
            fehler = aenderung_schreiben(v.aenderungen[i], false);
            if (!fehler.empty()) break;
        }
        // Scheitert eine spätere Änderung, die früheren zurücknehmen
        if (!fehler.empty())
            for (size_t j = 0; j < i; j++) aenderung_schreiben(v.aenderungen[j], true);
    }
    std::lock_guard<std::mutex> l(mx_);
    for (auto &x : st_.vorschlaege)
        if (x.id == id) {
            x.status = fehler.empty() ? ki::VStatus::uebernommen : ki::VStatus::gescheitert;
            x.meldung = fehler;
            x.zeit = daten::jetzt();
        }
    if (fehler.empty() && !v.aktion)
        for (const auto &a : v.aenderungen)
            protokoll_.push_back(fmt::iso(daten::jetzt()) + ";" + a.ort + ";" + a.einstellung + ";" + a.bisher + ";" + a.neu +
                                 ";Vorschlag der KI, bestätigt");
    if (protokoll_.size() > 500) protokoll_.erase(protokoll_.begin());
    geaendert();
}

void Assistent::zuruecknehmen(const std::string &id)
{
    ki::Vorschlag v;
    {
        std::lock_guard<std::mutex> l(mx_);
        bool da = false;
        for (auto &x : st_.vorschlaege)
            if (x.id == id && x.status == ki::VStatus::uebernommen && !x.aktion) {
                v = x;
                da = true;
            }
        if (!da) return;
    }
    std::string fehler;
    for (const auto &a : v.aenderungen) {
        std::string f = aenderung_schreiben(a, true);
        if (!f.empty()) fehler = f;
    }
    std::lock_guard<std::mutex> l(mx_);
    for (auto &x : st_.vorschlaege)
        if (x.id == id) {
            if (fehler.empty()) x.status = ki::VStatus::zurueckgenommen;
            x.meldung = fehler.empty() ? "" : "Zurücknehmen gescheitert: " + fehler;
        }
    if (fehler.empty())
        for (const auto &a : v.aenderungen)
            protokoll_.push_back(fmt::iso(daten::jetzt()) + ";" + a.ort + ";" + a.einstellung + ";" + a.neu + ";" + a.bisher +
                                 ";Vorschlag zurückgenommen");
    geaendert();
}

void Assistent::verwerfen(const std::string &id)
{
    std::lock_guard<std::mutex> l(mx_);
    for (auto &x : st_.vorschlaege)
        if (x.id == id && (x.status == ki::VStatus::offen || x.status == ki::VStatus::gescheitert)) x.status = ki::VStatus::verworfen;
    geaendert();
}

std::string Assistent::verbindung_pruefen()
{
    claude::Anfrage a;
    a.modell = modell_;
    a.denken = false;
    a.aufwand = "low";
    a.max_tokens = 16;
    a.system = "Antworten Sie ausschließlich mit dem Wort OK.";
    std::vector<std::string> n = {claude::nutzer("Verbindungstest")};
    claude::Antwort an;
    std::string fehler;
    if (!runde(n, a, (size_t)-1, an, epoche_, fehler)) return fehler;
    return "";
}

// ---- Ablage --------------------------------------------------------------------------------

std::string Assistent::speichern()
{
    std::lock_guard<std::mutex> l(mx_);
    cJSON *o = cJSON_CreateObject();
    cJSON *vs = cJSON_AddArrayToObject(o, "vorschlaege");
    for (const auto &v : st_.vorschlaege) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddStringToObject(x, "id", v.id.c_str());
        cJSON_AddStringToObject(x, "titel", v.titel.c_str());
        cJSON_AddStringToObject(x, "begruendung", v.begruendung.c_str());
        cJSON_AddStringToObject(x, "wirkung", v.wirkung.c_str());
        cJSON_AddStringToObject(x, "geraete", v.geraete.c_str());
        cJSON_AddBoolToObject(x, "aktion", v.aktion);
        cJSON_AddStringToObject(x, "aktion_art", v.aktion_art.c_str());
        cJSON_AddStringToObject(x, "geraet", v.geraet.c_str());
        cJSON_AddStringToObject(x, "eintrag", v.eintrag.c_str());
        cJSON_AddNumberToObject(x, "status", (int)(v.status == ki::VStatus::laeuft ? ki::VStatus::offen : v.status));
        cJSON_AddNumberToObject(x, "zeit", (double)v.zeit);
        cJSON_AddStringToObject(x, "meldung", v.meldung.c_str());
        cJSON *as = cJSON_AddArrayToObject(x, "aenderungen");
        for (const auto &a : v.aenderungen) {
            cJSON *y = cJSON_CreateObject();
            for (auto kv : {std::make_pair("geraet", &a.geraet), std::make_pair("ort", &a.ort), std::make_pair("parameter", &a.parameter),
                            std::make_pair("eintrag", &a.eintrag), std::make_pair("einstellung", &a.einstellung),
                            std::make_pair("bisher", &a.bisher), std::make_pair("neu", &a.neu), std::make_pair("wert_bisher", &a.wert_bisher),
                            std::make_pair("wert_neu", &a.wert_neu)})
                cJSON_AddStringToObject(y, kv.first, kv.second->c_str());
            cJSON_AddItemToArray(as, y);
        }
        cJSON_AddItemToArray(vs, x);
    }
    cJSON *p = cJSON_AddArrayToObject(o, "protokoll");
    for (const auto &z : protokoll_) cJSON_AddItemToArray(p, cJSON_CreateString(z.c_str()));
    auto bericht = [](const ki::Lagebericht &b) {
        cJSON *x = cJSON_CreateObject();
        cJSON_AddNumberToObject(x, "zustand", b.zustand);
        cJSON_AddStringToObject(x, "kurztext", b.kurztext.c_str());
        cJSON_AddStringToObject(x, "modell", b.modell.c_str());
        cJSON_AddNumberToObject(x, "erstellt", (double)b.erstellt);
        cJSON *h = cJSON_AddArrayToObject(x, "hinweise");
        for (const auto &y : b.hinweise) {
            cJSON *z = cJSON_CreateObject();
            cJSON_AddNumberToObject(z, "schwere", y.schwere);
            cJSON_AddStringToObject(z, "text", y.text.c_str());
            cJSON_AddStringToObject(z, "bezug", y.bezug.c_str());
            cJSON_AddItemToArray(h, z);
        }
        return x;
    };
    if (st_.bericht.zustand >= 0 && !st_.bericht.aus_befunden) cJSON_AddItemToObject(o, "bericht", bericht(st_.bericht));
    cJSON *f = cJSON_AddArrayToObject(o, "fruehere");
    for (const auto &b : st_.fruehere) cJSON_AddItemToArray(f, bericht(b));
    cJSON_AddNumberToObject(o, "naechste_id", naechste_id_);
    std::string r = json::schreiben(o);
    cJSON_Delete(o);
    return r;
}

void Assistent::laden(const std::string &text)
{
    json::Baum j = json::lesen(text);
    if (!j) return;
    std::lock_guard<std::mutex> l(mx_);
    for (const cJSON *x : json::liste(j.get(), "vorschlaege")) {
        ki::Vorschlag v;
        v.id = json::text(x, "id");
        v.titel = json::text(x, "titel");
        v.begruendung = json::text(x, "begruendung");
        v.wirkung = json::text(x, "wirkung");
        v.geraete = json::text(x, "geraete");
        v.aktion = json::wahr(x, "aktion");
        v.aktion_art = json::text(x, "aktion_art");
        v.geraet = json::text(x, "geraet");
        v.eintrag = json::text(x, "eintrag");
        v.status = (ki::VStatus)json::ganz(x, "status");
        v.zeit = (int64_t)json::zahl_d(x, "zeit", 0);
        v.meldung = json::text(x, "meldung");
        for (const cJSON *y : json::liste(x, "aenderungen"))
            v.aenderungen.push_back({json::text(y, "geraet"), json::text(y, "ort"), json::text(y, "parameter"), json::text(y, "eintrag"),
                                     json::text(y, "einstellung"), json::text(y, "bisher"), json::text(y, "neu"),
                                     json::text(y, "wert_bisher"), json::text(y, "wert_neu")});
        st_.vorschlaege.push_back(v);
    }
    for (const cJSON *z : json::liste(j.get(), "protokoll"))
        if (cJSON_IsString(z)) protokoll_.push_back(z->valuestring);
    auto bericht = [](const cJSON *x) {
        ki::Lagebericht b;
        b.zustand = json::ganz(x, "zustand", -1);
        b.kurztext = json::text(x, "kurztext");
        b.modell = json::text(x, "modell");
        b.erstellt = (int64_t)json::zahl_d(x, "erstellt", 0);
        for (const cJSON *h : json::liste(x, "hinweise")) b.hinweise.push_back({json::ganz(h, "schwere"), json::text(h, "text"), json::text(h, "bezug")});
        return b;
    };
    if (const cJSON *b = json::weg(j.get(), "bericht")) st_.bericht = bericht(b);
    for (const cJSON *b : json::liste(j.get(), "fruehere")) st_.fruehere.push_back(bericht(b));
    naechste_id_ = (uint32_t)json::ganz(j.get(), "naechste_id", 1);
    geaendert();
}

}  // namespace gespraech
