// Rahmen der Oberfläche: Leiste mit den fünf Bereichen, Titelzeile, Stapel je Bereich,
// Rückmeldung — wie Hauptansicht.swift auf dem iPad.
#include "ui.h"

#include <map>
#include <vector>
#include "daten.h"
#include "eins.h"
#include "fmt.h"
#include "ki.h"
#include "stil.h"

namespace ui {

namespace {

lv_obj_t *s_scr, *s_titel, *s_kopf_rechts, *s_zurueck, *s_inhalt, *s_uhr, *s_netz, *s_marke_befunde, *s_meldung;
Bereich s_bereich = Bereich::uebersicht;
std::map<Bereich, std::vector<Ziel>> s_stapel;
uint32_t s_meldung_nr, s_meldung_zeit;
int64_t s_uhr_min = -2;
uint32_t s_netz_gen;

const Ziel &oben()
{
    static const Ziel WURZEL;
    auto &st = s_stapel[s_bereich];
    return st.empty() ? WURZEL : st.back();
}

const char *bereich_name(Bereich b)
{
    switch (b) {
    case Bereich::uebersicht: return "Übersicht";
    case Bereich::raeume: return "Räume";
    case Bereich::assistent: return "Assistent";
    case Bereich::heizung: return "Heizung";
    default: return "Geräte";
    }
}

const char *bereich_sym(Bereich b)
{
    switch (b) {
    case Bereich::uebersicht: return "house";
    case Bereich::raeume: return "square.grid.2x2";
    case Bereich::assistent: return "sparkles";
    case Bereich::heizung: return "flame";
    default: return "cpu";
    }
}

void netz_setzen()
{
    if (!s_netz) return;
    const Palette &p = pal();
    daten::Wlan w = daten::wlan();
    auto b = daten::bild();
    const char *s = w.zustand == daten::Netz::verbunden ? "wifi" : "wifi.exclamationmark";
    lv_label_set_text(s_netz, sym(s));
    lv_obj_set_style_text_color(s_netz, w.zustand == daten::Netz::verbunden && !b->beispiel ? p.gedaempft : p.warnung, 0);
}

void leiste_aufbauen()
{
    const Palette &p = pal();
    lv_obj_t *l = flaeche(s_scr);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_size(l, LEISTE_B, HOEHE);
    lv_obj_set_style_bg_color(l, p.flaeche, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(l, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(l, 1, 0);
    lv_obj_set_style_border_color(l, p.linie, 0);
    lv_obj_set_style_pad_top(l, 22, 0);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(l, 8, 0);
    for (Bereich b : {Bereich::uebersicht, Bereich::raeume, Bereich::assistent, Bereich::heizung, Bereich::geraete}) {
        bool an = b == s_bereich;
        lv_obj_t *k = flaeche(l);
        lv_obj_set_size(k, LEISTE_B - 20, 104);
        lv_obj_set_style_radius(k, 16, 0);
        lv_obj_set_style_bg_color(k, an ? p.akzent_weich : p.vertieft, 0);
        lv_obj_set_style_bg_opa(k, an ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, LV_STATE_PRESSED);
        spalte(k, 8);
        lv_obj_set_flex_align(k, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_color_t f = an ? p.akzent : p.gedaempft;
        symbol(k, bereich_sym(b), &f_sym32, f);
        text(k, bereich_name(b), an ? &f_cap_m : &f_cap, f);
        bei_klick(k, [b] {
            // Ein zweiter Tipp auf den aktiven Bereich führt zu seiner Wurzel, wie die Tab-Leiste.
            if (b == s_bereich) s_stapel[b].clear();
            bereich_waehlen(b);
        });
        if (b == Bereich::uebersicht) {
            s_marke_befunde = flaeche(k);
            lv_obj_set_floating(s_marke_befunde, true);
            lv_obj_set_size(s_marke_befunde, LV_SIZE_CONTENT, 28);
            lv_obj_set_style_min_width(s_marke_befunde, 28, 0);
            lv_obj_set_style_radius(s_marke_befunde, 14, 0);
            lv_obj_set_style_pad_hor(s_marke_befunde, 7, 0);
            lv_obj_set_style_bg_color(s_marke_befunde, p.stoerung, 0);
            lv_obj_set_style_bg_opa(s_marke_befunde, LV_OPA_COVER, 0);
            lv_obj_align(s_marke_befunde, LV_ALIGN_TOP_RIGHT, -12, 8);
            lv_obj_t *t = text(s_marke_befunde, "", &f_cap_m, lv_color_white());
            lv_obj_center(t);
            lv_obj_set_hidden(s_marke_befunde, true);
        }
    }
    dehnung(l);
    // Uhr und Netz unten in der Leiste
    s_uhr = text(l, "", &f_wert, p.tinte);
    s_netz = text(l, "", &f_sym24, p.gedaempft);
    lv_obj_set_style_margin_bottom(s_netz, 20, 0);
    lv_obj_set_flex_grow(l, 0);
}

void marke_setzen()
{
    if (!s_marke_befunde) return;
    auto b = daten::bild();
    int n = b->befunde_mit(anlage::Schwere::stoerung);
    lv_obj_set_hidden(s_marke_befunde, n == 0);
    if (n) lv_label_set_text(lv_obj_get_child(s_marke_befunde, 0), std::to_string(n).c_str());
}

void kopf_aufbauen()
{
    const Palette &p = pal();
    lv_obj_t *k = flaeche(s_scr);
    lv_obj_set_pos(k, LEISTE_B, 0);
    lv_obj_set_size(k, INHALT_B, KOPF_H);
    lv_obj_set_style_bg_color(k, p.grund, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(k, RAND, 0);
    zeile(k, 14);
    bool tief = !s_stapel[s_bereich].empty();
    s_zurueck = nullptr;
    if (tief) {
        // Runder Zurück-Knopf wie der Kopf der Nebenspalte (Zielfuehrung.swift)
        s_zurueck = rund_knopf(k, "chevron.left", 52, [] { zurueck(); });
    }
    s_titel = text(k, bereich_name(s_bereich), &f_titel, p.tinte);
    lv_label_set_long_mode(s_titel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(s_titel, INHALT_B - 2 * RAND - 420, 0);
    dehnung(k);
    s_kopf_rechts = flaeche(k);
    zeile(s_kopf_rechts, 12);
}

}  // namespace

// ---- öffentliche Schnittstelle -------------------------------------------------------

void titel_setzen(const std::string &t)
{
    if (s_titel) lv_label_set_text(s_titel, t.c_str());
}

void kopf_knopf(const char *sf, const std::string &t, std::function<void()> wenn, bool betont)
{
    if (!s_kopf_rechts) return;
    lv_obj_t *b = knopf(s_kopf_rechts, t, betont, std::move(wenn), sf);
    lv_obj_set_style_min_height(b, 52, 0);
    lv_obj_set_style_pad_ver(b, 8, 0);
}

static uint32_t s_bedienung;
void bedienung_merken() { s_bedienung = lv_tick_get(); }
bool bedienung_kuerzlich() { return s_bedienung && lv_tick_elaps(s_bedienung) < 2000; }

void kopf_leeren()
{
    if (s_kopf_rechts) lv_obj_clean(s_kopf_rechts);
}

Spalten doppelseite(lv_obj_t *inhalt, bool zweispaltig, int breite)
{
    Spalten s;
    auto spalte_bauen = [&](int x, int b) {
        lv_obj_t *c = flaeche(inhalt);
        lv_obj_set_pos(c, x, 0);
        lv_obj_set_size(c, b, INHALT_H);
        lv_obj_set_style_pad_bottom(c, RAND, 0);
        lv_obj_set_style_pad_top(c, 4, 0);
        lv_obj_set_style_pad_hor(c, 4, 0);   // Platz für Schatten
        spalte(c, ABSTAND);
        lv_obj_set_scrollable(c, true);
        lv_obj_set_clickable(c, true);
        lv_obj_set_scroll_dir(c, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_ACTIVE);
        return c;
    };
    if (zweispaltig) {
        s.links = spalte_bauen(RAND - 4, SPALTE_B + 8);
        s.rechts = spalte_bauen(RAND + SPALTE_B + FUGE - 4, SPALTE_B + 8);
    } else {
        int b = breite > 0 && breite < INHALT_B - 2 * RAND ? breite : INHALT_B - 2 * RAND;
        s.links = spalte_bauen((INHALT_B - b) / 2 - 4, b + 8);
    }
    return s;
}

Bereich bereich() { return s_bereich; }

static void zeigen()
{
    lv_obj_clean(s_scr);
    const Palette &p = pal();
    lv_obj_set_style_bg_color(s_scr, p.grund, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    s_marke_befunde = nullptr;
    s_meldung = nullptr;
    leiste_aufbauen();
    kopf_aufbauen();
    s_inhalt = flaeche(s_scr);
    lv_obj_set_pos(s_inhalt, LEISTE_B, KOPF_H);
    lv_obj_set_size(s_inhalt, INHALT_B, INHALT_H);
    const Ziel &z = oben();
    switch (z.art) {
    case Ziel::Art::raum: raum_aufbauen(s_inhalt, z.id); break;
    case Ziel::Art::befunde:
    case Ziel::Art::befund: befunde_aufbauen(s_inhalt, z.id); break;
    case Ziel::Art::verlauf: verlauf_aufbauen(s_inhalt); break;
    case Ziel::Art::einstellungen: einstellungen_aufbauen(s_inhalt); break;
    case Ziel::Art::wlan: wlan_aufbauen(s_inhalt); break;
    case Ziel::Art::lageberichte: lageberichte_aufbauen(s_inhalt); break;
    default:
        switch (s_bereich) {
        case Bereich::uebersicht: uebersicht_aufbauen(s_inhalt); break;
        case Bereich::raeume: raeume_aufbauen(s_inhalt); break;
        case Bereich::assistent: assistent_aufbauen(s_inhalt); break;
        case Bereich::heizung: heizung_aufbauen(s_inhalt); break;
        case Bereich::geraete: geraete_aufbauen(s_inhalt, z.id); break;
        }
    }
    s_uhr_min = -2;
    marke_setzen();
    netz_setzen();
}

void bereich_waehlen(Bereich b)
{
    tastatur_schliessen();
    s_bereich = b;
    // ⚠️ Asynchron: meist ruft das ein Knopf, den der Neuaufbau löscht.
    lv_async_call([](void *) { zeigen(); }, nullptr);
}

void oeffnen(const Ziel &z)
{
    tastatur_schliessen();
    // Geräte: die Auswahl ersetzt den Eintrag rechts (ListeUndEintrag), kein neuer Stapel.
    if (s_bereich == Bereich::geraete && z.art == Ziel::Art::wurzel) {
        s_stapel[s_bereich].clear();
        if (!z.id.empty()) s_stapel[s_bereich].push_back(z);
        lv_async_call([](void *) { zeigen(); }, nullptr);
        return;
    }
    // Befunde: die Auswahl ersetzt die rechte Spalte
    auto &st = s_stapel[s_bereich];
    if (z.art == Ziel::Art::befund && !st.empty() && (st.back().art == Ziel::Art::befunde || st.back().art == Ziel::Art::befund))
        st.back() = z;
    else st.push_back(z);
    // ⚠️ Asynchron: der gedrückte Knopf wird beim Neuaufbau gelöscht.
    lv_async_call([](void *) { zeigen(); }, nullptr);
}

void zurueck()
{
    tastatur_schliessen();
    auto &st = s_stapel[s_bereich];
    if (!st.empty()) st.pop_back();
    lv_async_call([](void *) { zeigen(); }, nullptr);
}

void neu_aufbauen() { zeigen(); }

void fragen(const std::string &bezug)
{
    ki::bezug_setzen(bezug);
    s_stapel[Bereich::assistent].clear();
    s_bereich = Bereich::assistent;
    lv_async_call([](void *) { zeigen(); }, nullptr);
}

bool seite_nach_name(const std::string &n)
{
    auto nach = [&](Bereich b, Ziel z = {}) {
        s_bereich = b;
        s_stapel[b].clear();
        if (z.art != Ziel::Art::wurzel || !z.id.empty()) s_stapel[b].push_back(z);
        zeigen();
        return true;
    };
    std::string id = n.find(':') != std::string::npos ? n.substr(n.find(':') + 1) : "";
    if (n == "uebersicht") return nach(Bereich::uebersicht);
    if (n == "befunde") return nach(Bereich::uebersicht, {Ziel::Art::befunde, ""});
    if (n.rfind("befund:", 0) == 0) return nach(Bereich::uebersicht, {Ziel::Art::befund, id});
    if (n == "raeume") return nach(Bereich::raeume);
    if (n.rfind("raum:", 0) == 0) return nach(Bereich::raeume, {Ziel::Art::raum, id});
    if (n == "assistent") return nach(Bereich::assistent);
    if (n == "lageberichte") return nach(Bereich::assistent, {Ziel::Art::lageberichte, ""});
    if (n == "heizung") return nach(Bereich::heizung);
    if (n == "verlauf") return nach(Bereich::heizung, {Ziel::Art::verlauf, ""});
    if (n == "geraete") return nach(Bereich::geraete);
    if (n.rfind("geraet:", 0) == 0) return nach(Bereich::geraete, {Ziel::Art::wurzel, id});
    if (n == "einstellungen") return nach(Bereich::geraete, {Ziel::Art::einstellungen, ""});
    if (n == "wlan") return nach(Bereich::geraete, {Ziel::Art::wlan, ""});
    if (n == "zustimmung") {
        nach(Bereich::geraete, {Ziel::Art::einstellungen, ""});
        zustimmung_zeigen();
        return true;
    }
    return false;
}

void rueckmeldung_zeigen(const std::string &t, bool fehler)
{
    const Palette &p = pal();
    if (s_meldung) lv_obj_delete(s_meldung);
    s_meldung = flaeche(lv_layer_top());
    lv_obj_set_style_max_width(s_meldung, 840, 0);
    lv_obj_set_style_bg_color(s_meldung, p.flaeche, 0);
    lv_obj_set_style_bg_opa(s_meldung, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_meldung, 20, 0);
    lv_obj_set_style_border_width(s_meldung, 1, 0);
    lv_obj_set_style_border_color(s_meldung, p.linie_stark, 0);
    lv_obj_set_style_shadow_width(s_meldung, 30, 0);
    lv_obj_set_style_shadow_opa(s_meldung, 60, 0);
    lv_obj_set_style_shadow_color(s_meldung, lv_color_black(), 0);
    lv_obj_set_style_pad_hor(s_meldung, 22, 0);
    lv_obj_set_style_pad_ver(s_meldung, 16, 0);
    zeile(s_meldung, 14);
    symbol(s_meldung, fehler ? "xmark.octagon" : "checkmark.circle", &f_sym24, fehler ? p.stoerung : p.gut);
    lv_obj_t *l = text(s_meldung, fmt::einheiten_fest(t), &f_callout, p.tinte);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_width(l, 700, 0);
    lv_obj_align(s_meldung, LV_ALIGN_TOP_MID, LEISTE_B / 2, 12);
    bei_klick(s_meldung, [] {
        lv_async_call([](void *) {
            if (s_meldung) lv_obj_delete(s_meldung);
            s_meldung = nullptr;
        }, nullptr);
    });
    // Fehler bleiben länger stehen
    s_meldung_zeit = lv_tick_get() + (fehler ? 12000 : 4000);
}

static void takt(lv_timer_t *)
{
    int64_t jetzt = daten::jetzt();
    int64_t min = jetzt > 1600000000 ? jetzt / 60 : -1;
    if (min != s_uhr_min && s_uhr) {
        s_uhr_min = min;
        lv_label_set_text(s_uhr, fmt::uhrzeit(jetzt).c_str());
    }
    if (daten::wlan_generation() != s_netz_gen) {
        s_netz_gen = daten::wlan_generation();
        netz_setzen();
    }
    daten::Rueckmeldung r = daten::rueckmeldung();
    if (r.nr != s_meldung_nr) {
        s_meldung_nr = r.nr;
        if (!r.text.empty()) rueckmeldung_zeigen(r.text, r.fehler);
    }
    if (s_meldung && (int32_t)(lv_tick_get() - s_meldung_zeit) > 0) {
        lv_obj_delete(s_meldung);
        s_meldung = nullptr;
    }
    static uint32_t gen;
    if (daten::generation() != gen) {
        gen = daten::generation();
        marke_setzen();
        netz_setzen();
    }
    switch (oben().art) {
    case Ziel::Art::raum: raum_takt(); return;
    case Ziel::Art::befunde:
    case Ziel::Art::befund: befunde_takt(); return;
    case Ziel::Art::verlauf: verlauf_takt(); return;
    case Ziel::Art::einstellungen: einstellungen_takt(); return;
    case Ziel::Art::wlan: wlan_takt(); return;
    case Ziel::Art::lageberichte: return;
    default: break;
    }
    switch (s_bereich) {
    case Bereich::uebersicht: uebersicht_takt(); break;
    case Bereich::raeume: raeume_takt(); break;
    case Bereich::assistent: assistent_takt(); break;
    case Bereich::heizung: heizung_takt(); break;
    case Bereich::geraete: geraete_takt(); break;
    }
}

void start(lv_display_t *disp)
{
    (void)disp;
    s_scr = lv_screen_active();
    lv_obj_set_scrollable(s_scr, false);
    int a = (int)eins::zahl(eins::k::ANSICHT, 2);
    dunkel_setzen(a != 1);
    zeigen();
    lv_timer_create(takt, 250, nullptr);
}

}  // namespace ui
