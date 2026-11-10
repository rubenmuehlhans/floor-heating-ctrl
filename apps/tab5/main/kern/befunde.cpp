#include "befunde.h"

#include <algorithm>
#include "fmt.h"
#include "json.h"

using anlage::Befund;
using anlage::Schwere;

namespace befunde {

namespace {

const char *QA = "Prüfung der App";
const std::string F = fmt::FEST;

Befund neu(std::string id, Schwere s, std::string titel, std::string text, std::string ort, std::string quelle,
           int md = 0)
{
    Befund b;
    b.id = std::move(id);
    b.schwere = s;
    b.titel = std::move(titel);
    b.text = std::move(text);
    b.ort = std::move(ort);
    b.quelle = std::move(quelle);
    b.mindestdauer = md;
    return b;
}

std::string bezeichnung(const anlage::Geraetestand &s)
{
    const char *art = s.art == anlage::Art::verteiler ? "Verteiler" : s.art == anlage::Art::leitstand ? "Leitstand" : "Heizungsgerät";
    return s.ort.empty() ? std::string(art) + " " + s.id : std::string(art) + " " + s.ort;
}

Befund geraetebefund(const cJSON *f, const anlage::Geraetestand &s, int index)
{
    std::string code = json::text(f, "code"), where = json::text(f, "where");
    std::string ort = where.empty() ? s.ort : where;
    std::string id = "geraet:" + (code.empty() ? std::string("befund") : code) + ":" + (where.empty() ? std::to_string(index) : where);
    auto z = [&](const char *k) { return json::hat(f, k) ? fmt::zahl(json::zahl(f, k)) : std::string(fmt::STRICH); };
    if (code == "backflow") {
        std::string mal = json::hat(f, "events") ? std::to_string(json::ganz(f, "events")) + "-mal" : "Wiederholt";
        return neu(id, Schwere::warnung, "Warmes Wasser strömt in den Kesselrücklauf, obwohl nichts läuft",
                   mal + " beobachtet, zuletzt " + z("rise_k") + F +
                       "K Anstieg bei ausgeschaltetem Brenner und stehender Pumpe. Das ist ohne Strömung nicht möglich, "
                       "meist fehlt eine Schwerkraftbremse im Kesselrücklauf oder sie ist undicht. Jede Warmwasserzapfung "
                       "schiebt so Wärme in den kalten Kessel, wo sie durch den Schornstein verloren geht.",
                   ort, s.ort);
    }
    if (code == "flow_swapped")
        return neu(id, Schwere::warnung, "Vorlauf und Rücklauf sind vermutlich vertauscht",
                   "Seit " + (json::hat(f, "held_s") ? fmt::dauer(json::ganz(f, "held_s")) : std::string("längerer Zeit")) +
                       " durchgehend kälter im Vorlauf, obwohl die Pumpe läuft und der Speicher warm ist. Entweder sitzen "
                       "die Fühler an den falschen Rohren, oder ihre Rollen sind vertauscht zugeordnet.",
                   ort, s.ort);
    if (code == "probe_errors") {
        int fe = json::ganz(f, "errors"), ge = json::ganz(f, "reads") + fe;
        return neu(id, Schwere::warnung, "Ein Fühler verwirft auffällig viele Messungen",
                   std::to_string(fe) + " von " + std::to_string(ge) +
                       " Messungen verworfen, meist ein Wackelkontakt, eine zu lange Leitung oder ein zu schwacher Anschlusswiderstand.",
                   ort, s.ort);
    }
    if (code == "day_above_trend")
        return neu(id, Schwere::hinweis, "Der letzte Tag verbrauchte mehr, als zur Außenlage passt",
                   z("hours") + F + "h Brennerlauf statt der erwarteten " + z("expected_h") + F + "h, das sind " + z("sigma_off") +
                       " Streuungen über der Verbrauchslinie. So fällt ein offenes Fenster auf, ein klemmendes Ventil oder "
                       "eine Pumpe, die durchläuft.",
                   ort, s.ort);
    if (code == "flue_gap_rising") {
        std::string pp = json::hat(f, "delta_k") ? fmt::zahl(json::zahl(f, "delta_k") / 20) : std::string(fmt::STRICH);
        return neu(id, Schwere::hinweis, "Der Kessel überträgt schlechter als nach der letzten Reinigung",
                   z("now_k") + F + "K Abstand zwischen Abgas und Kesselvorlauf statt " + z("ref_k") + F + "K, also " +
                       z("delta_k") + F + "K mehr. Das sind grob " + pp + " Prozentpunkte Wirkungsgrad, meist Ruß im Wärmetauscher.",
                   ort, s.ort);
    }
    std::string t = json::text(f, "text");
    return neu(id, Schwere::hinweis, t.empty() ? (code.empty() ? "Meldung des Geräts" : code) : t, t, ort, s.ort);
}

}  // namespace

std::vector<Befund> pruefen(const std::vector<anlage::Geraetestand> &staende, const anlage::Bild &bild)
{
    std::vector<Befund> l;
    std::vector<json::Baum> st(staende.size()), kf(staende.size());
    for (size_t i = 0; i < staende.size(); i++) {
        st[i] = json::lesen(staende[i].state);
        kf[i] = json::lesen(staende[i].config);
    }

    // Erreichbarkeit
    for (size_t i = 0; i < staende.size(); i++) {
        const auto &s = staende[i];
        if (s.erreichbar || s.fehler.empty()) continue;
        std::string seit;
        if (s.kontakt && bild.stand > s.kontakt)
            seit = " Die letzte Antwort liegt " + fmt::dauer((int)(bild.stand - s.kontakt)) + " zurück.";
        l.push_back(neu("offline:" + s.id, Schwere::stoerung, bezeichnung(s) + " nicht erreichbar",
                        "Das Gerät unter " + s.ip + " antwortet nicht." + seit + " Solange zeigt die Tafel seine letzten Werte nicht an.",
                        s.ort, QA));
    }

    // Befunde der Heizungsgeräte; das Speichergerät meldet zuerst, jeder Befund einmal.
    std::vector<size_t> folge;
    for (size_t i = 0; i < staende.size(); i++)
        if (staende[i].erreichbar && staende[i].art == anlage::Art::heizung) folge.push_back(i);
    std::stable_sort(folge.begin(), folge.end(), [&](size_t a, size_t b) {
        auto sp = [&](size_t i) { return bild.geraet(staende[i].id) && bild.geraet(staende[i].id)->rolle == anlage::Rolle::speicher ? 0 : 1; };
        return sp(a) < sp(b);
    });
    std::set<std::string> gesehen;
    for (size_t i : folge) {
        int n = 0;
        for (const cJSON *f : json::liste(st[i].get(), "findings")) {
            std::string k = json::text(f, "code") + "|" + json::text(f, "where");
            if (gesehen.insert(k).second) l.push_back(geraetebefund(f, staende[i], n));
            n++;
        }
    }

    // Netz und Firmware
    std::map<anlage::Art, std::vector<std::pair<std::string, std::string>>> versionen;
    for (size_t i = 0; i < staende.size(); i++) {
        const auto &s = staende[i];
        if (!s.erreichbar) continue;
        const cJSON *j = st[i].get();
        std::string name = bezeichnung(s);
        if (json::hat(j, "net.time_valid") && !json::wahr(j, "net.time_valid"))
            l.push_back(neu("uhrzeit:" + s.id, Schwere::warnung, name + " hat keine gültige Uhrzeit",
                            "Ohne Uhrzeit entfallen der tägliche Neustart und die Schutzfahrt, und Protokolle und Verlauf des "
                            "Geräts tragen keine Zeit. Meist erreicht das Gerät den Zeitserver im Internet nicht.",
                            s.ort, QA, 600));
        int rssi = json::ganz(j, "net.rssi");
        if (rssi < 0 && rssi <= -80)
            l.push_back(neu("wlan:" + s.id, rssi <= -87 ? Schwere::warnung : Schwere::hinweis, "Schwaches WLAN an " + name,
                            "Das Gerät empfängt das WLAN mit " + std::to_string(rssi) + F +
                                "dBm. Unterhalb von etwa −80" + F + "dBm gehen Antworten verloren, und das Gerät verbindet sich "
                                "häufiger neu. Abhilfe schafft ein Repeater oder ein anderer Standort des Zugangspunkts.",
                            s.ort, QA, 900));
        std::string v = json::text(j, "version");
        if (!v.empty() && s.art != anlage::Art::leitstand) versionen[s.art].push_back({s.ort, v});
    }
    for (auto &kv : versionen) {
        std::set<std::string> vs;
        for (auto &p : kv.second) vs.insert(p.second);
        if (vs.size() < 2) continue;
        bool vt = kv.first == anlage::Art::verteiler;
        std::string t, ort;
        for (auto &p : kv.second) {
            t += (t.empty() ? "" : ", ") + p.first + " " + p.second;
            ort += (ort.empty() ? "" : ", ") + p.first;
        }
        l.push_back(neu(vt ? "firmware:verteiler" : "firmware:heizung", Schwere::hinweis,
                        vt ? "Unterschiedliche Firmware an den Verteilern" : "Unterschiedliche Firmware an den Heizungsgeräten",
                        t + ". Gleiche Fassungen schließen aus, dass sich Geräte derselben Art in Schnittstelle und Verhalten unterscheiden.",
                        ort, QA));
    }

    // Räume und Fühler der Verteiler
    for (size_t i = 0; i < staende.size(); i++) {
        const auto &s = staende[i];
        if (!s.erreichbar || s.art != anlage::Art::verteiler) continue;
        const cJSON *j = st[i].get();
        const std::string &etage = s.ort;
        for (const cJSON *r : json::liste(j, "rooms")) {
            int n = json::ganz(r, "id");
            std::string name = json::text(r, "name");
            if (name.empty()) name = "Raum " + std::to_string(n);
            bool heizt = json::text(r, "mode", "heat") != "off";
            std::string rid = s.id + "/" + std::to_string(n), ort = name + ", " + etage;
            bool zug = json::wahr(r, "sensor_set");
            if (heizt && json::hat(r, "sensor_set") && !zug)
                l.push_back(neu("thermometer:" + rid, Schwere::warnung, name + " hat kein Thermometer",
                                "Ohne Messwert setzt der Verteiler die Regelung des Raums aus; die Ventile bleiben in ihrer letzten "
                                "Stellung. Ein Thermometer ordnen Sie unter Geräte › " + etage + " › Räume und Kanäle zu.",
                                ort, QA));
            else if (heizt && zug && json::hat(r, "temp_valid") && !json::wahr(r, "temp_valid")) {
                int alter = json::ganz(r, "temp_age_s");
                std::string seit = alter > 0 ? "Das Thermometer hat sich seit " + fmt::dauer(alter) + " nicht gemeldet."
                                             : std::string("Vom Thermometer ist noch kein Messwert eingegangen.");
                l.push_back(neu("messwert:" + rid, Schwere::warnung, "Messwert von " + name + " veraltet",
                                seit + " Bis wieder ein Messwert kommt, setzt der Verteiler die Regelung aus; die Ventile bleiben "
                                       "in ihrer letzten Stellung. Häufige Ursachen sind eine leere Batterie und eine zu große "
                                       "Entfernung zum Verteiler.",
                                ort, QA, 300));
            }
            if (zug && json::hat(r, "battery")) {
                int b = json::ganz(r, "battery");
                if (b <= 15)
                    l.push_back(neu("batterie:" + rid, b <= 5 ? Schwere::warnung : Schwere::hinweis,
                                    "Batterie des Thermometers in " + name + " fast leer",
                                    "Noch " + std::to_string(b) + F +
                                        "%. Ist sie leer, meldet das Thermometer keine Werte mehr, und der Verteiler setzt die "
                                        "Regelung des Raums aus.",
                                    ort, QA));
            }
        }
        if (json::wahr(j, "outdoor.set")) {
            if (json::hat(j, "outdoor.valid") && !json::wahr(j, "outdoor.valid"))
                l.push_back(neu("aussen:" + s.id, Schwere::hinweis, "Außenfühler meldet sich nicht",
                                "Der Außenfühler am Verteiler " + etage +
                                    " sendet keine Werte. Ohne Außentemperatur fehlen den Heizungsgeräten die Heizgradtage für die "
                                    "Verbrauchslinie.",
                                etage, QA, 3600));
            int mv = json::ganz(j, "outdoor.battery_mv");
            if (mv > 0 && mv < 2400)
                l.push_back(neu("batterie-aussen:" + s.id, Schwere::hinweis, "Batterie des Außenfühlers schwach",
                                fmt::zahl(mv / 1000.0, 2) + F + "V. Unterhalb von etwa 2,3" + F + "V sendet der Fühler unzuverlässig.",
                                etage, QA));
        }
        if (json::hat(j, "local_sensors.hdc1080.valid") && !json::wahr(j, "local_sensors.hdc1080.valid"))
            l.push_back(neu("bordfuehler:" + s.id, Schwere::hinweis, "Klimafühler der Platine " + etage + " ohne Messwert",
                            "Der Fühler für Temperatur und Luftfeuchte im Gehäuse des Verteilers liefert keinen gültigen Wert.", etage,
                            QA, 600));
        int alle = 0, ungueltig = 0;
        for (const cJSON *d : json::liste(j, "local_sensors.ds18b20")) {
            alle++;
            ungueltig += !json::wahr(d, "valid");
        }
        if (ungueltig)
            l.push_back(neu("vorlauffuehler:" + s.id, Schwere::hinweis, "Vorlauffühler am Verteiler " + etage + " ohne Messwert",
                            std::to_string(ungueltig) + " von " + std::to_string(alle) +
                                " Fühlern am 1-Wire-Anschluss der Platine liefern keinen gültigen Wert.",
                            etage, QA, 600));
    }

    // Kanäle
    for (const auto &e : bild.etagen) {
        std::vector<int> ohne;
        for (const auto &k : e.kanaele)
            if (!k.raum.empty() && !k.kalibriert) ohne.push_back(k.nr);
        if (!ohne.empty()) {
            std::string liste;
            for (int n : ohne) liste += (liste.empty() ? "" : ", ") + std::to_string(n);
            l.push_back(neu("kalibrierung:" + e.geraet, Schwere::hinweis,
                            ohne.size() == 1 ? std::string("Ein belegter Kanal ohne Messfahrt")
                                             : std::to_string(ohne.size()) + " belegte Kanäle ohne Messfahrt",
                            e.name + ": Kanal " + liste +
                                ". Sie fahren nach den Vorgabezeiten statt nach gemessenen Fahrzeiten; die geschätzte Stellung "
                                "weicht deshalb ab. Eine Messfahrt je Kanal starten Sie unter Geräte › " + e.name + " › Messfahrt.",
                            e.name, QA));
        }
        for (const auto &k : e.kanaele)
            if (k.hand)
                l.push_back(neu("handbetrieb:" + e.geraet + "/" + std::to_string(k.nr), Schwere::hinweis,
                                "Kanal " + std::to_string(k.nr) + (k.raum.empty() ? "" : " (" + k.raum + ")") + " seit langem im Handbetrieb",
                                "Der Kanal folgt der Regelung nicht, bis er zurückgegeben wird: Geräte › " + e.name + " › Kanal " +
                                    std::to_string(k.nr) + " › Wieder an die Regelung übergeben.",
                                e.name, QA, 12 * 3600));
    }

    // Heizkreise und Relais
    std::vector<std::string> gestoert;
    auto relais_gestoert = [](const anlage::Relais &r) { return r.bekannt && !r.online && !r.stromlos && r.weg != "keiner"; };
    for (const auto &k : bild.heizkreise)
        if (relais_gestoert(k.relais)) gestoert.push_back(k.name);
    if (bild.kkp.vorhanden && relais_gestoert(bild.kkp.relais)) gestoert.push_back("Kesselkreispumpe");
    if (!gestoert.empty()) {
        std::string ort;
        for (auto &g : gestoert) ort += (ort.empty() ? "" : ", ") + g;
        l.push_back(neu("relais", Schwere::stoerung,
                        gestoert.size() == 1 ? "Relais " + gestoert[0] + " nicht erreichbar" : std::string("Relais der Pumpen nicht erreichbar"),
                        "Die Relais antworten nicht. Die Pumpen folgen der Steuerung deshalb nicht. Bleibt das Lebenszeichen aus, "
                        "schaltet die Ausfallregel im Relais die Pumpe nach 15 Minuten ein, sofern sie eingerichtet ist.",
                        ort, "Heizungsgeräte", 600));
    }
    for (const auto &k : bild.heizkreise) {
        if (k.gesperrt)
            l.push_back(neu("kreis-gesperrt:" + std::to_string(k.nr), Schwere::hinweis,
                            k.name + ": Wärmebedarf, aber die Kesselregelung gibt die Pumpe nicht frei",
                            "Die Räume fordern Wärme an, das Relais der Pumpe ist aber ohne Strom. Die vorgeschaltete Regelung am "
                            "Kessel hat ihren Pumpenausgang abgeschaltet, etwa in der Nachtabsenkung. Soll diese Steuerung über die "
                            "Pumpe entscheiden, stellen Sie den Ausgang der Kesselregelung auf Dauerbetrieb.",
                            k.name, QA, 3600));
        if (k.relais.abweichung)
            l.push_back(neu("relais-abweichung:" + std::to_string(k.nr), Schwere::warnung, "Pumpe " + k.name + " folgt dem Schaltbefehl nicht",
                            "Das Relais meldet einen anderen Schaltzustand, als das Heizungsgerät verlangt. Mögliche Ursachen: Das "
                            "Relais wurde von Hand geschaltet, eine Regel im Relais greift, oder der Befehl ging verloren.",
                            k.name, "Heizungsgeräte", 300));
        if (k.keiner_erreicht && !k.versorgt.empty())
            l.push_back(neu("kreis-ohne-bedarf:" + std::to_string(k.nr), Schwere::warnung, k.name + " erreicht keinen seiner Verteiler",
                            "Das Heizungsgerät hat keinen der versorgten Verteiler erreicht. Die Pumpe läuft deshalb nur auf „Ein“ "
                            "oder zum Frostschutz.",
                            k.name, "Heizungsgeräte", 300));
        else if (k.veraltet)
            l.push_back(neu("kreis-bedarf-veraltet:" + std::to_string(k.nr), Schwere::warnung, k.name + ": ein Verteiler antwortet nicht",
                            "Mindestens ein versorgter Verteiler antwortet dem Heizungsgerät nicht. Das liegt meist an einer Störung "
                            "im WLAN oder am Verteiler selbst.",
                            k.name, "Heizungsgeräte", 300));
    }
    if (bild.kkp.vorhanden && bild.kkp.relais.abweichung)
        l.push_back(neu("relais-abweichung:kkp", Schwere::warnung, "Kesselkreispumpe folgt dem Schaltbefehl nicht",
                        "Das Relais meldet einen anderen Schaltzustand, als das Heizungsgerät verlangt.", "Kesselkreispumpe",
                        "Heizungsgeräte"));

    // Speicher und Brenner
    if (bild.speicher.warmwasser_knapp)
        l.push_back(neu("warmwasser", Schwere::warnung, "Warmwasserreserve knapp",
                        "Der Pufferspeicher liegt unter der Warngrenze" +
                            (isnan(bild.speicher.warngrenze) ? std::string() : " von " + fmt::zahl(bild.speicher.warngrenze) + " °C") + ".",
                        "Pufferspeicher", "Pufferspeicher"));
    if (bild.speicher.geschaetzt)
        l.push_back(neu("ladung-geschaetzt", Schwere::hinweis, "Ladephase nur geschätzt",
                        "Dem Gerät fehlen die Werte von Kesselvor- und -rücklauf. Beginn und Ende einer Ladung erkennt es deshalb "
                        "nur aus der Speichertemperatur.",
                        "Pufferspeicher", "Pufferspeicher", 1800));
    if (bild.kessel.taktbetrieb)
        l.push_back(neu("taktung", Schwere::hinweis, "Der Brenner startet auffällig oft",
                        "Heute " + std::to_string(bild.kessel.starts_heute) + " Starts bei " + fmt::dauer(bild.kessel.laufzeit_heute) +
                            " Laufzeit. Kurze Brennerläufe haben einen schlechteren Wirkungsgrad als lange und belasten Zündung und Düse.",
                        "Brenner", "Kessel"));

    // Füllstand weicht ab
    std::vector<std::pair<std::string, float>> lv;
    for (size_t i = 0; i < staende.size(); i++)
        if (staende[i].erreichbar && staende[i].art == anlage::Art::heizung && json::hat(st[i].get(), "charge.level"))
            lv.push_back({staende[i].ort, json::zahl(st[i].get(), "charge.level")});
    if (lv.size() >= 2) {
        float mn = 2, mx = -1;
        std::string t;
        for (auto &p : lv) {
            mn = std::min(mn, p.second);
            mx = std::max(mx, p.second);
            t += (t.empty() ? "" : ", ") + p.first + " " + fmt::zahl(p.second * 100, 0) + F + "%";
        }
        if (mx - mn >= 0.15f)
            l.push_back(neu("fuellstand-abweichung", Schwere::hinweis, "Füllstand weicht zwischen den Heizungsgeräten ab",
                            t + ". Beide Geräte bewerten denselben Speicher; meist weichen die Grenzen „voll“ und „leer“ voneinander ab.",
                            "Pufferspeicher", QA, 900));
    }
    return l;
}

// ---- Gedächtnis ----------------------------------------------------------------

std::vector<Befund> Gedaechtnis::fortschreiben(const std::vector<Befund> &da, int64_t jetzt, bool vollstaendig)
{
    constexpr int RUHE = 3600;          // Rückkehr binnen einer Stunde setzt den alten Ablauf fort
    constexpr int64_t HALTEN = 30 * 86400;
    std::set<std::string> ids;
    for (const auto &b : da) {
        ids.insert(b.id);
        auto &e = laufend_[b.id];
        if (!e.beginn || jetzt - e.zuletzt > RUHE) e.beginn = jetzt;
        e.zuletzt = jetzt;
        e.b = b;
        if (jetzt - e.beginn >= b.mindestdauer) e.sichtbar = true;
    }
    // Was nicht mehr ansteht: nach der Ruhezeit erledigt — aber nur mit vollständigem Stand.
    for (auto it = laufend_.begin(); it != laufend_.end();) {
        if (ids.count(it->first) || !vollstaendig) {
            ++it;
            continue;
        }
        if (jetzt - it->second.zuletzt > RUHE || !it->second.sichtbar) {
            if (it->second.sichtbar)
                erledigt_.push_back({it->first, it->second.b.titel, it->second.b.ort, it->second.b.schwere, it->second.beginn,
                                     it->second.zuletzt});
            it = laufend_.erase(it);
        } else ++it;
    }
    erledigt_.erase(std::remove_if(erledigt_.begin(), erledigt_.end(), [&](const Erledigt &e) { return jetzt - e.bis > HALTEN; }),
                    erledigt_.end());

    std::vector<Befund> sichtbar;
    std::set<std::string> neu_sichtbar;
    for (auto &kv : laufend_) {
        if (!kv.second.sichtbar || !ids.count(kv.first)) continue;
        Befund b = kv.second.b;
        b.seit = kv.second.beginn;
        b.stumm = stumm_.count(b.id) > 0;
        sichtbar.push_back(b);
        neu_sichtbar.insert(b.id);
    }
    std::stable_sort(sichtbar.begin(), sichtbar.end(), [](const Befund &a, const Befund &b) {
        if (a.schwere != b.schwere) return a.schwere > b.schwere;
        return a.seit < b.seit;
    });
    for (const auto &id : neu_sichtbar)
        if (!sichtbar_alt_.count(id)) {
            gen_++;
            break;
        }
    sichtbar_alt_ = neu_sichtbar;
    return sichtbar;
}

void Gedaechtnis::stumm(const std::string &id, bool an)
{
    if (an) stumm_.insert(id);
    else stumm_.erase(id);
}

std::string Gedaechtnis::speichern() const
{
    cJSON *j = cJSON_CreateObject();
    cJSON *s = cJSON_AddArrayToObject(j, "stumm");
    for (const auto &id : stumm_) cJSON_AddItemToArray(s, cJSON_CreateString(id.c_str()));
    cJSON *e = cJSON_AddArrayToObject(j, "erledigt");
    for (const auto &x : erledigt_) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", x.id.c_str());
        cJSON_AddStringToObject(o, "titel", x.titel.c_str());
        cJSON_AddStringToObject(o, "ort", x.ort.c_str());
        cJSON_AddNumberToObject(o, "schwere", (int)x.schwere);
        cJSON_AddNumberToObject(o, "von", (double)x.von);
        cJSON_AddNumberToObject(o, "bis", (double)x.bis);
        cJSON_AddItemToArray(e, o);
    }
    std::string r = json::schreiben(j);
    cJSON_Delete(j);
    return r;
}

void Gedaechtnis::laden(const std::string &text)
{
    json::Baum j = json::lesen(text);
    if (!j) return;
    for (const cJSON *s : json::liste(j.get(), "stumm"))
        if (cJSON_IsString(s)) stumm_.insert(s->valuestring);
    for (const cJSON *o : json::liste(j.get(), "erledigt"))
        erledigt_.push_back({json::text(o, "id"), json::text(o, "titel"), json::text(o, "ort"), (Schwere)json::ganz(o, "schwere"),
                             (int64_t)json::zahl_d(o, "von", 0), (int64_t)json::zahl_d(o, "bis", 0)});
}

}  // namespace befunde
