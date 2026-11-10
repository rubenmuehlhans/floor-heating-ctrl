// Assistent — wie apple/App/Ansichten/Assistent/AssistentAnsicht.swift: Kopf, Beiträge
// (Frage rechts in der Blase, Antwort links aus Bausteinen), Vorschlagskarten mit
// Übernehmen und Verwerfen, Startfragen, Eingabeleiste unten.
//
// ⚠️ Während eine Antwort streamt, baut die Seite bei jeder Generation neu; der Bildlauf
// bleibt am Ende, solange er dort war.
#include <string.h>
#include <set>
#include "daten.h"
#include "fmt.h"
#include "ki.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

constexpr int SPALTE = 980;   // Breite des Gesprächs (App: 820 pt)
lv_obj_t *s_inhalt, *s_lauf;
uint32_t s_gen;
bool s_am_ende = true;
std::set<std::string> s_offen;   // aufgeklappte Werkzeugzeilen und Überlegungen

// ---- Markdown, nur inline (fett, kursiv, Code), wie inlineOnlyPreservingWhitespace ----
void absatz(lv_obj_t *c, const std::string &t, lv_color_t farbe)
{
    lv_obj_t *sg = lv_spangroup_create(c);
    lv_obj_set_width(sg, LV_PCT(100));
    lv_obj_set_style_text_font(sg, &f_body, 0);
    lv_obj_set_style_text_color(sg, farbe, 0);
    lv_obj_set_style_text_line_space(sg, 4, 0);
    std::string teil;
    const lv_font_t *f = &f_body;
    auto ausgeben = [&]() {
        if (teil.empty()) return;
        lv_span_t *sp = lv_spangroup_add_span(sg);
        lv_span_set_text(sp, teil.c_str());
        lv_style_set_text_font(lv_span_get_style(sp), f);
        if (f == &f_mono) lv_style_set_text_color(lv_span_get_style(sp), pal().akzent);
        teil.clear();
    };
    for (size_t i = 0; i < t.size(); i++) {
        if (t.compare(i, 2, "**") == 0) {
            ausgeben();
            f = f == &f_body_s ? &f_body : &f_body_s;
            i++;
        } else if (t[i] == '`') {
            ausgeben();
            f = f == &f_mono ? &f_body : &f_mono;
        } else teil += t[i];
    }
    ausgeben();
    lv_spangroup_refresh(sg);
}

void markdown(lv_obj_t *c, const std::string &text, lv_color_t farbe)
{
    std::string t = fmt::einheiten_fest(text);
    size_t a = 0;
    while (a < t.size()) {
        size_t e = t.find("\n\n", a);
        std::string abs = t.substr(a, e == std::string::npos ? std::string::npos : e - a);
        // Überschriften und Aufzählungszeichen der Antwort in die Schreibweise der App
        size_t z = 0;
        while ((z = abs.find("\n- ", z)) != std::string::npos) abs.replace(z, 3, "\n– ");
        if (abs.rfind("- ", 0) == 0) abs.replace(0, 2, "– ");
        while (!abs.empty() && abs[0] == '#') abs.erase(0, 1);
        if (!abs.empty() && abs[0] == ' ') abs.erase(0, 1);
        if (!abs.empty()) absatz(c, abs, farbe);
        if (e == std::string::npos) break;
        a = e + 2;
    }
}

const char *status_text(const ki::Vorschlag &v, lv_color_t &f)
{
    const Palette &p = pal();
    switch (v.status) {
    case ki::VStatus::uebernommen: f = p.gut; return v.aktion ? "Ausgelöst" : "Übernommen";
    case ki::VStatus::verworfen: f = p.gedaempft; return "Verworfen";
    case ki::VStatus::zurueckgenommen: f = p.gedaempft; return "Zurückgenommen";
    case ki::VStatus::gescheitert: f = p.warnung; return "Nicht übernommen";
    case ki::VStatus::laeuft: f = p.akzent; return v.aktion ? "Wird ausgelöst …" : "Wird übernommen …";
    default: return nullptr;
    }
}

void vorschlagkarte(lv_obj_t *c, const ki::Vorschlag &v)
{
    const Palette &p = pal();
    bool offen = v.status == ki::VStatus::offen;
    lv_obj_t *k = karte(c, offen);
    lv_obj_t *kopf = zeile_neu(k, 10);
    symbol(kopf, v.aktion ? "play.circle" : "wand.and.stars", &f_sym19, p.akzent);
    text(kopf, v.aktion ? "Aktion" : "Vorschlag", &f_cap_m, p.akzent);
    dehnung(kopf);
    lv_color_t sf = p.gedaempft;
    if (const char *st = status_text(v, sf)) {
        std::string t = st;
        if (v.status == ki::VStatus::uebernommen && v.zeit) t += " " + fmt::uhrzeit(v.zeit);
        marke(kopf, t, sf);
    }
    text_umbruch(k, v.titel, &f_body_s, p.tinte);
    if (!v.geraete.empty()) text(k, v.geraete, &f_cap, p.gedaempft);
    if (v.aenderungen.size() == 1) {
        const auto &a = v.aenderungen[0];
        lv_obj_t *z = zeile_neu(k, 14);
        lv_obj_t *b1 = kennzahl(z, "Bisher", a.bisher, "", a.einstellung, p.tinte);
        lv_obj_set_flex_grow(b1, 1);
        symbol(z, "arrow.right", &f_sym24, p.gedaempft);
        lv_obj_t *b2 = kennzahl(z, "Neu", a.neu, "", a.einstellung, p.akzent);
        lv_obj_set_flex_grow(b2, 1);
    } else {
        for (const auto &a : v.aenderungen) {
            lv_obj_t *z = zeile_neu(k, 10);
            lv_obj_t *n = text(z, a.ort.empty() ? a.einstellung : a.ort + ": " + a.einstellung, &f_sub, p.tinte);
            lv_obj_set_flex_grow(n, 1);
            text(z, a.bisher + " → ", &f_mono, p.gedaempft);
            text(z, a.neu, &f_mono, p.akzent);
        }
    }
    if (!v.begruendung.empty()) text_umbruch(k, fmt::einheiten_fest(v.begruendung), &f_sub, p.tinte);
    if (!v.wirkung.empty()) {
        kicker(k, v.aktion ? "Was geschieht" : "Erwartete Wirkung", p.gedaempft);
        text_umbruch(k, fmt::einheiten_fest(v.wirkung), &f_sub, p.gedaempft);
    }
    if (!v.meldung.empty()) text_umbruch(k, v.meldung, &f_sub, v.status == ki::VStatus::gescheitert ? p.warnung : p.gedaempft);
    std::string id = v.id;
    if (offen) {
        lv_obj_t *z = zeile_neu(k, 12);
        knopf(z, v.aktion ? "Ausführen" : "Übernehmen", true, [id] { ki::uebernehmen(id); });
        knopf(z, "Verwerfen", false, [id] { ki::verwerfen(id); });
        text_umbruch(k,
                     v.aktion ? "Die Aktion ändert keine Einstellung. Die Tafel löst sie am Gerät aus und zeigt das Ergebnis hier."
                              : "Die Tafel schreibt den Wert an das Gerät, liest ihn danach zurück und vergleicht ihn.",
                     &f_cap, p.gedaempft);
    } else if (v.status == ki::VStatus::uebernommen && !v.aktion) {
        lv_obj_t *z = zeile_neu(k, 12);
        knopf(z, "Rückgängig", false, [id] { ki::zuruecknehmen(id); }, "arrow.uturn.backward");
    } else if (v.status == ki::VStatus::gescheitert) {
        lv_obj_t *z = zeile_neu(k, 12);
        knopf(z, "Schließen", false, [id] { ki::verwerfen(id); });
    }
}

void aufklapper(lv_obj_t *c, const std::string &schluessel, const char *sf, const std::string &titel, bool fertig, const std::string &inhalt)
{
    const Palette &p = pal();
    bool auf = s_offen.count(schluessel) > 0;
    lv_obj_t *z = zeile_neu(c, 10);
    lv_obj_set_style_pad_ver(z, 4, 0);
    if (fertig) symbol(z, sf, &f_sym19, sf && !strcmp(sf, "checkmark.circle") ? p.gut : p.gedaempft);
    else symbol(z, "hourglass", &f_sym19, p.gedaempft);
    lv_obj_t *t = text(z, titel, &f_mono_k, p.gedaempft);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(t, SPALTE - 120, 0);
    if (!inhalt.empty()) symbol(z, auf ? "chevron.up" : "chevron.down", &f_sym19, p.blass);
    if (!inhalt.empty())
        bei_klick(z, [schluessel] {
            if (s_offen.count(schluessel)) s_offen.erase(schluessel);
            else s_offen.insert(schluessel);
            s_gen = 0;   // neu aufbauen
        });
    if (auf && !inhalt.empty()) {
        lv_obj_t *l = text_umbruch(c, inhalt.size() > 600 ? inhalt.substr(0, 600) + " …" : inhalt, &f_mono_k, p.gedaempft);
        lv_obj_set_style_pad_left(l, 30, 0);
    }
}

void beitrag(lv_obj_t *c, const ki::Beitrag &b, size_t nr, const ki::Stand &st)
{
    const Palette &p = pal();
    if (b.nutzer) {
        lv_obj_t *z = zeile_neu(c, 0);
        lv_obj_set_flex_align(z, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t *blase = flaeche(z);
        lv_obj_set_style_max_width(blase, SPALTE - 72, 0);
        lv_obj_set_style_bg_color(blase, p.akzent_weich, 0);
        lv_obj_set_style_bg_opa(blase, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(blase, 20, 0);
        lv_obj_set_style_border_width(blase, 1, 0);
        lv_obj_set_style_border_color(blase, mischen(p.akzent, p.akzent_weich, 64), 0);
        lv_obj_set_style_pad_hor(blase, 20, 0);
        lv_obj_set_style_pad_ver(blase, 14, 0);
        lv_obj_t *l = text(blase, b.text, &f_body, p.tinte);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_max_width(l, SPALTE - 112, 0);
        return;
    }
    lv_obj_t *s = spalte_neu(c, 10);
    for (size_t i = 0; i < b.bausteine.size(); i++) {
        const ki::Baustein &x = b.bausteine[i];
        std::string schl = std::to_string(nr) + "/" + std::to_string(i);
        switch (x.art) {
        case ki::Baustein::Art::text: markdown(s, x.text, p.tinte); break;
        case ki::Baustein::Art::ueberlegung: aufklapper(s, schl, "brain", "Überlegung", true, x.text); break;
        case ki::Baustein::Art::werkzeug:
            aufklapper(s, schl, "checkmark.circle", x.werkzeug.beschreibung, x.werkzeug.fertig,
                       x.werkzeug.ergebnis.empty() ? "" : x.werkzeug.name + ": " + x.werkzeug.ergebnis);
            break;
        case ki::Baustein::Art::fehler: {
            lv_obj_t *z = zeile_neu(s, 10, LV_FLEX_ALIGN_START);
            symbol(z, "exclamationmark.triangle", &f_sym19, p.warnung);
            lv_obj_t *l = text_umbruch(z, x.text, &f_sub, p.warnung, 0);
            lv_obj_set_flex_grow(l, 1);
            break;
        }
        case ki::Baustein::Art::vorschlag:
            for (const auto &v : st.vorschlaege)
                if (v.id == x.text) vorschlagkarte(s, v);
            break;
        }
    }
}

void kopf(lv_obj_t *c, const ki::Stand &st)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(c, 14, LV_FLEX_ALIGN_START);
    symbol(z, "sparkles", &f_sym32, p.akzent);
    lv_obj_t *s = spalte_neu(z, 4);
    lv_obj_set_width(s, 0);
    lv_obj_set_flex_grow(s, 1);
    text(s, "Assistent für Ihre Heizung", &f_body_s, p.tinte);
    text_umbruch(s, std::string(ki::modellname()) + " · liest Messwerte, Einstellungen und Befunde, ändert nichts ohne Ihre Bestätigung",
                 &f_cap, p.gedaempft);
    std::string h = ki::hindernis();
    if (!h.empty()) {
        lv_obj_t *hz = zeile_neu(s, 10, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_top(hz, 6, 0);
        symbol(hz, "exclamationmark.circle", &f_sym19, p.warnung);
        lv_obj_t *l = text_umbruch(hz, h, &f_sub, p.warnung, 0);
        lv_obj_set_flex_grow(l, 1);
        lv_obj_t *kz = zeile_neu(s, 10);
        if (ki::braucht_zustimmung()) knopf(kz, "Zustimmen …", true, [] { oeffnen({Ziel::Art::einstellungen, "ki"}); });
        else knopf(kz, "Einstellungen der Tafel", false, [] { oeffnen({Ziel::Art::einstellungen, "ki"}); }, "gearshape");
    }
    int offen = 0;
    for (const auto &v : st.vorschlaege) offen += v.status == ki::VStatus::offen;
    if (offen) {
        abschnitt(c, "Offene Vorschläge", std::to_string(offen));
        for (const auto &v : st.vorschlaege)
            if (v.status == ki::VStatus::offen) {
                lv_obj_t *zz = zeile_neu(c, 12);
                symbol(zz, "wand.and.stars", &f_sym19, p.akzent);
                text(zz, v.titel, &f_sub_m, p.tinte);
                dehnung(zz);
                text(zz, v.geraete, &f_cap, p.gedaempft);
            }
    }
    trennlinie(c);
}

void eingabe(lv_obj_t *inhalt, const ki::Stand &st)
{
    const Palette &p = pal();
    lv_obj_t *leiste = flaeche(inhalt);
    lv_obj_set_size(leiste, SPALTE, LV_SIZE_CONTENT);
    lv_obj_align(leiste, LV_ALIGN_BOTTOM_MID, 0, -16);
    spalte(leiste, 10);
    if (!st.bezug.empty()) {
        lv_obj_t *chip = flaeche(leiste);
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(chip, p.akzent_weich, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(chip, 14, 0);
        lv_obj_set_style_pad_ver(chip, 6, 0);
        zeile(chip, 8);
        symbol(chip, "scope", &f_sym19, p.akzent);
        text(chip, "Bezug: " + st.bezug, &f_cap_m, p.akzent);
        symbol(chip, "xmark.circle", &f_sym19, p.gedaempft);
        bei_klick(chip, [] { ki::bezug_setzen(""); });
    }
    lv_obj_t *z = zeile_neu(leiste, 12);
    lv_obj_t *feld = flaeche(z);
    lv_obj_set_height(feld, 64);
    lv_obj_set_flex_grow(feld, 1);
    lv_obj_set_style_radius(feld, 32, 0);
    lv_obj_set_style_bg_color(feld, p.flaeche, 0);
    lv_obj_set_style_bg_opa(feld, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(feld, 1, 0);
    lv_obj_set_style_border_color(feld, p.linie_stark, 0);
    lv_obj_set_style_pad_hor(feld, 24, 0);
    lv_obj_t *l = text(feld, st.entwurf.empty() ? "Frage zur Anlage …" : st.entwurf, &f_body, st.entwurf.empty() ? p.blass : p.tinte);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    bool laeuft = st.lage == ki::Lage::liest || st.lage == ki::Lage::antwortet;
    auto senden = [](const std::string &t) {
        if (t.find_first_not_of(" ") == std::string::npos) return;
        ki::entwurf_setzen("");
        if (!ki::hindernis().empty()) {
            ki::entwurf_setzen(t);
            oeffnen({Ziel::Art::einstellungen, "ki"});
            return;
        }
        ki::fragen(t);
    };
    std::string entwurf = st.entwurf;
    bei_klick(feld, [entwurf, senden] { tastatur_oeffnen("Frage zur Anlage", entwurf, senden, false, "Senden"); });
    if (laeuft) rund_knopf(z, "stop.fill", 64, [] { ki::anhalten(); }, true);
    else rund_knopf(z, "arrow.up", 64, [entwurf, senden] {
        if (entwurf.empty()) tastatur_oeffnen("Frage zur Anlage", "", senden, false, "Senden");
        else senden(entwurf);
    }, true);
}

void aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    ki::Stand st = ki::stand();
    s_gen = ki::generation();
    kopf_knopf("square.and.pencil", "Neues Gespräch", [] { ki::neues_gespraech(); });
    kopf_knopf("list.bullet.rectangle", "Lageberichte", [] { oeffnen({Ziel::Art::lageberichte, ""}); });
    s_lauf = flaeche(inhalt);
    lv_obj_set_size(s_lauf, INHALT_B, INHALT_H - 120);
    lv_obj_set_pos(s_lauf, 0, 0);
    lv_obj_set_scrollable(s_lauf, true);
    lv_obj_set_clickable(s_lauf, true);
    lv_obj_set_scroll_dir(s_lauf, LV_DIR_VER);
    lv_obj_set_style_pad_hor(s_lauf, (INHALT_B - SPALTE) / 2, 0);
    lv_obj_set_style_pad_bottom(s_lauf, 20, 0);
    spalte(s_lauf, 22);
    kopf(s_lauf, st);
    if (st.beitraege.empty()) {
        text(s_lauf, "Zum Beispiel", &f_sub_s, p.gedaempft);
        for (const char *f : {"Wie läuft die Anlage heute?", "Warum wird das Bad nicht warm?", "Lässt sich der Ölverbrauch senken?",
                              "Was sollte vor dem Winter geprüft werden?"}) {
            std::string t = f;
            knopf(s_lauf, t, false, [t] { ki::entwurf_setzen(t); });
        }
    }
    for (size_t i = 0; i < st.beitraege.size(); i++) beitrag(s_lauf, st.beitraege[i], i, st);
    if (st.lage == ki::Lage::liest || st.lage == ki::Lage::antwortet) {
        lv_obj_t *z = zeile_neu(s_lauf, 10);
        symbol(z, "hourglass", &f_sym19, p.gedaempft);
        text(z, st.lage == ki::Lage::liest ? "Liest die Anlage …" : "Antwortet …", &f_sub, p.gedaempft);
    }
    eingabe(inhalt, st);
    lv_obj_update_layout(s_lauf);
    if (s_am_ende) lv_obj_scroll_to_y(s_lauf, LV_COORD_MAX, LV_ANIM_OFF);
}

}  // namespace

void assistent_aufbauen(lv_obj_t *inhalt)
{
    s_inhalt = inhalt;
    s_am_ende = true;
    aufbauen(inhalt);
}

void assistent_takt()
{
    if (!s_inhalt || ki::generation() == s_gen || lv_indev_active()) return;
    // Am Ende geblieben? Dann nach dem Neuaufbau wieder dorthin.
    s_am_ende = !s_lauf || lv_obj_get_scroll_bottom(s_lauf) < 40;
    int32_t y = s_lauf ? lv_obj_get_scroll_y(s_lauf) : 0;
    lv_obj_clean(s_inhalt);
    kopf_leeren();
    aufbauen(s_inhalt);
    if (!s_am_ende) lv_obj_scroll_to_y(s_lauf, y, LV_ANIM_OFF);
}

// ---- Lageberichte ------------------------------------------------------------------

void lageberichte_aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    titel_setzen("Lageberichte");
    ki::Stand st = ki::stand();
    Spalten sp = doppelseite(inhalt, false, 980);
    std::vector<ki::Lagebericht> alle;
    if (st.bericht.zustand >= 0) alle.push_back(st.bericht);
    for (auto it = st.fruehere.rbegin(); it != st.fruehere.rend(); ++it) alle.push_back(*it);
    if (alle.empty()) leer_hinweis(sp.links, "sparkles", "Noch kein Lagebericht", "Lageberichte erstellt der Assistent alle sechs Stunden.");
    static const char *TEXT[] = {"In Ordnung", "Beobachten", "Handeln"};
    for (const auto &l : alle) {
        lv_obj_t *k = karte(sp.links);
        lv_obj_t *z = zeile_neu(k, 12);
        lv_color_t f[] = {p.gut, p.warnung, p.stoerung};
        if (l.zustand >= 0 && l.zustand <= 2) marke(z, TEXT[l.zustand], f[l.zustand]);
        dehnung(z);
        text(z, fmt::tag_mit_zeit(l.erstellt), &f_mono_k, p.gedaempft);
        text_umbruch(k, l.kurztext, &f_body, p.tinte);
        for (const auto &h : l.hinweise) {
            lv_obj_t *hz = zeile_neu(k, 12, LV_FLEX_ALIGN_START);
            symbol(hz, schwere_sym((anlage::Schwere)h.schwere), &f_sym19, schwere_farbe((anlage::Schwere)h.schwere));
            lv_obj_t *t = text_umbruch(hz, h.text, &f_sub, p.tinte, 0);
            lv_obj_set_flex_grow(t, 1);
        }
        text(k, l.aus_befunden ? "aus den Befunden" : l.modell, &f_mono_k, p.gedaempft);
    }
}

}  // namespace ui
