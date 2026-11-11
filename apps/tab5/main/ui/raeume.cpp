// Räume und Raumdetail — wie apple/App/Ansichten/Raeume/RaeumeAnsicht.swift.
//
// Räume: eine gemeinsam scrollende Fläche; je Etage ein Abschnitt, der Verteilerbalken über
// die volle Breite und darunter die Raumkarten mit Stellrad, zwei je Zeile. Raum: links Messung und Bedienung, Kennzahlen; rechts
// Ventile und Verlauf.
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include "daten.h"
#include "fmt.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

Neubau s_nb_raeume, s_nb_raum;
std::string s_raum;
int s_zeitraum;   // 0 = 24 Std., 1 = 7 Tage

// Verteilerbalken: je Kanal eine Säule, gruppiert nach Räumen.
void verteilerbalken(lv_obj_t *c, const anlage::Etage &e)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    kartenkopf(k, "Verteiler " + e.name, nullptr, "Die Füllung zeigt die Ventilstellung, gruppiert nach Räumen.");
    lv_obj_t *lauf = zeile_neu(k, 20, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(lauf, true);
    lv_obj_set_clickable(lauf, true);
    lv_obj_set_scroll_dir(lauf, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(lauf, LV_SCROLLBAR_MODE_OFF);
    size_t i = 0;
    while (i < e.kanaele.size()) {
        const std::string &raum = e.kanaele[i].raum;
        lv_obj_t *g = spalte_neu(lauf, 8);
        lv_obj_set_width(g, LV_SIZE_CONTENT);
        lv_obj_t *saeulen = zeile_neu(g, 8, LV_FLEX_ALIGN_END);
        lv_obj_set_width(saeulen, LV_SIZE_CONTENT);
        size_t j = i;
        while (j < e.kanaele.size() && e.kanaele[j].raum == raum) {
            const anlage::Kanal &kn = e.kanaele[j];
            lv_obj_t *sp = spalte_neu(saeulen, 6);
            lv_obj_set_width(sp, LV_SIZE_CONTENT);
            lv_obj_set_flex_align(sp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *s = flaeche(sp);
            lv_obj_set_size(s, 33, 111);
            lv_obj_set_style_radius(s, 9, 0);
            lv_obj_set_style_bg_color(s, p.vertieft, 0);
            lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
            lv_obj_set_style_clip_corner(s, true, 0);
            bool frei = kn.raum.empty(), faehrt = kn.op != "idle";
            lv_obj_set_style_border_width(s, faehrt ? 3 : frei ? 1 : 1, 0);
            lv_obj_set_style_border_color(s, faehrt ? p.waerme : p.linie_stark, 0);
            if (frei) lv_obj_set_style_border_opa(s, 140, 0);
            if (!frei && !isnan(kn.stellung) && kn.stellung > 0.01f) {
                lv_obj_t *f = flaeche(s);
                lv_obj_set_size(f, LV_PCT(100), (int)(111 * fminf(kn.stellung, 1)));
                lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, 0);
                lv_obj_set_style_bg_color(f, kn.hand ? p.warnung : p.waerme, 0);
                lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
            }
            if (kn.hand) {
                lv_obj_t *h = symbol(s, "hand.raised", &f_sym19, lv_color_white());
                lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -6);
            }
            text(sp, std::to_string(kn.nr), &f_mono_k, p.gedaempft);
            j++;
        }
        lv_obj_t *strich = flaeche(g);
        lv_obj_set_size(strich, LV_PCT(100), 2);
        lv_obj_set_style_bg_color(strich, p.linie_stark, 0);
        lv_obj_set_style_bg_opa(strich, LV_OPA_COVER, 0);
        lv_obj_t *n = text(g, raum.empty() ? "ohne Raum" : raum, &f_cap, raum.empty() ? p.blass : p.tinte);
        // Mindestens so breit wie die Säulen; kurze Namen ganz, lange gekürzt
        lv_obj_set_width(n, LV_SIZE_CONTENT);
        lv_obj_set_style_min_width(n, (int)(j - i) * 41 - 8, 0);
        lv_obj_set_style_max_width(n, 220, 0);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(n, f_cap.line_height);   // ⚠️ Pünktchen nur mit fester Höhe, sonst bricht es um
        i = j;
    }
}

// `unten`: Zeile mit dem Schaltknopf; ab ihr rückt der Rest nach unten, wenn die Karte höher gezogen wird.
lv_obj_t *raumkarte(lv_obj_t *c, const anlage::Raum &r, const anlage::Etage &e, lv_obj_t **unten)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    auto z = r.zustand();
    std::string id = r.id;
    lv_obj_t *kopf = zeile_neu(k, 10);
    lv_obj_t *n = text(kopf, r.name, &f_body_s, p.tinte);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(n, SPALTE_B - 280, 0);
    symbol(kopf, "chevron.right", &f_sym19, p.blass);
    dehnung(kopf);
    marke(kopf, raum_text(z), raum_farbe(z), raum_sym(z));
    bei_klick(kopf, [id] { oeffnen({Ziel::Art::raum, id}); });

    lv_obj_t *mess = zeile_neu(k, 12, LV_FLEX_ALIGN_END);
    if (isnan(r.ist)) text(mess, "–,–", &f_gross, p.blass);
    else messzahl(mess, fmt::zahl(r.ist), "°C", &f_gross, p.tinte);
    dehnung(mess);
    if (z == anlage::Raumzustand::heizt || z == anlage::Raumzustand::soll_erreicht)
        text(mess, abstand_text(r), &f_mono, abstand_farbe(r));
    if (r.thermometer) {
        std::string m;
        if (!isnan(r.feuchte)) m = "Luftfeuchte " + fmt::mit_einheit(fmt::zahl(r.feuchte, 0), "%");
        if (r.batterie >= 0) m += (m.empty() ? "" : ", ") + std::string("Batterie ") + fmt::mit_einheit(std::to_string(r.batterie), "%");
        if (r.alter >= 0) m += (m.empty() ? "" : ", ") + std::string("Messwert ") + fmt::dauer(r.alter) + " alt";
        if (!m.empty()) text_umbruch(k, m, &f_cap, p.gedaempft);
        sollwert_stellrad(k, r.id, false);
    } else {
        text_umbruch(k, "Ohne zugeordnetes Thermometer wird nicht geregelt.", &f_cap, p.gedaempft);
    }
    lv_obj_t *zk = zeile_neu(k, 10);
    *unten = zk;
    dehnung(zk);
    bool an = r.an;
    knopf(zk, an ? "Ausschalten" : "Einschalten", false, [id, an] { daten::betriebsart(id, !an); });
    trennlinie(k);
    for (int nr : r.kanaele) {
        const anlage::Kanal *kn = nullptr;
        for (const auto &x : e.kanaele)
            if (x.nr == nr) kn = &x;
        lv_obj_t *kz = zeile_neu(k, 12);
        lv_obj_t *l = text(kz, "Kreis " + std::to_string(nr), &f_mono_k, p.gedaempft);
        lv_obj_set_width(l, 100);
        lv_obj_t *bh = flaeche(kz);
        lv_obj_set_height(bh, 9);
        lv_obj_set_flex_grow(bh, 1);
        balken(bh, kn ? kn->stellung : NAN, kn && kn->hand ? p.warnung : p.waerme, 9);
        lv_obj_t *pr = text(kz, kn ? fmt::prozent(kn->stellung) : std::string(fmt::STRICH), &f_mono_k, p.tinte);
        lv_obj_set_width(pr, 66);
        lv_obj_set_style_text_align(pr, LV_TEXT_ALIGN_RIGHT, 0);
    }
    trennlinie(k);
    std::string fuss = "Zielstellung " + fmt::prozent(r.zielstellung);
    if (r.naechste_pruefung >= 0) fuss += " · nächste Prüfung in " + fmt::dauer(r.naechste_pruefung);
    text(k, fuss, &f_mono_k, p.gedaempft);
    return k;
}

}  // namespace

void raeume_aufbauen(lv_obj_t *inhalt)
{
    auto b = daten::bild();
    s_nb_raeume.merken(inhalt, anzeige_generation());
    kopf_knopf("sparkles", "Fragen", [] { fragen("Räume"); });
    // ⚠️ Eine Fläche, die als Ganzes scrollt — nicht die Doppelseite. Mit zwei unabhängig
    // scrollenden Spalten standen Raumkarten neben Karten einer anderen Etage, und der
    // Verteilerbalken lief aus dem Blick (Rückmeldung Ruben, 11.11.2026).
    Spalten sp = doppelseite(inhalt, false);
    s_nb_raeume.spalten(sp);
    lv_obj_t *c = sp.links;
    struct Karte { lv_obj_t *k, *unten; };
    std::vector<std::pair<Karte, Karte>> paare;
    bool etwas = false;
    for (const auto &e : b->etagen) {
        if (e.nur_aussen && e.raeume.empty()) continue;
        etwas = true;
        abschnitt(c, e.name, e.bedarf ? "Wärmebedarf" : "kein Bedarf");
        if (e.raeume.empty()) {
            text_umbruch(c, "Noch kein Raum eingerichtet. Räume legen Sie in der Weboberfläche des Verteilers unter Räume an.",
                         &f_sub, pal().gedaempft);
            continue;
        }
        // Verteilerbalken über die volle Breite, darunter die Räume der Etage paarweise
        verteilerbalken(c, e);
        // Räume in der Reihenfolge ihrer Heizkreise, wie im Verteilerbalken darüber
        std::vector<const anlage::Raum *> rs;
        for (const auto &r : e.raeume) rs.push_back(&r);
        auto erster = [](const anlage::Raum *r) {
            return r->kanaele.empty() ? 1000 : *std::min_element(r->kanaele.begin(), r->kanaele.end());
        };
        std::stable_sort(rs.begin(), rs.end(), [&](auto a, auto b) { return erster(a) < erster(b); });
        for (size_t i = 0; i < rs.size(); i += 2) {
            lv_obj_t *z = zeile_neu(c, FUGE, LV_FLEX_ALIGN_START);
            // ⚠️ Kinder werden auf die Zeile beschnitten (lv_obj_redraw: Fläche + ext draw size);
            // ohne den Zuschlag fehlt der Schatten der Karten unten (hell, 18 px + 8 px Versatz).
            lv_obj_set_overflow_visible(z, true);
            lv_obj_add_event_cb(z, [](lv_event_t *ev) { lv_event_set_ext_draw_size(ev, 28); }, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
            lv_obj_refresh_ext_draw_size(z);
            Karte l{}, r{};
            l.k = raumkarte(z, *rs[i], e, &l.unten);
            lv_obj_set_width(l.k, SPALTE_B);
            if (i + 1 < rs.size()) {
                r.k = raumkarte(z, *rs[i + 1], e, &r.unten);
                lv_obj_set_width(r.k, SPALTE_B);
            }
            paare.push_back({l, r});
        }
    }
    if (!etwas)
        leer_hinweis(c, "square.grid.2x2", "Kein Verteiler gefunden", "Verteiler im Heimnetz erscheinen hier von selbst.");
    // Karten einer Zeile auf gleiche Höhe, damit die Zeilen als Raster lesbar bleiben
    if (!paare.empty()) {
        lv_obj_update_layout(c);
        for (auto &[l, r] : paare) {
            if (!r.k) continue;
            int hl = lv_obj_get_height(l.k), hr = lv_obj_get_height(r.k);
            if (hl == hr) continue;
            // Die kürzere Karte wächst; ihr unterer Teil (Knopf, Kreise, Fuß) rückt nach unten,
            // damit beide Karten unten auf einer Linie enden
            Karte &kurz = hl < hr ? l : r;
            lv_obj_t *d = dehnung(kurz.k);
            lv_obj_move_to_index(d, lv_obj_get_index(kurz.unten));
            lv_obj_set_height(kurz.k, std::max(hl, hr));
        }
    }
    s_nb_raeume.wiederherstellen();
}

void raeume_takt()
{
    if (s_nb_raeume.geaendert(anzeige_generation())) s_nb_raeume.neu([](lv_obj_t *i) { raeume_aufbauen(i); });
}

// ---- Raumdetail ----------------------------------------------------------------------

void raum_aufbauen(lv_obj_t *inhalt, const std::string &id)
{
    const Palette &p = pal();
    s_raum = id;
    auto b = daten::bild();
    s_nb_raum.merken(inhalt, anzeige_generation());
    const anlage::Raum *r = b->raum(id);
    const anlage::Etage *e = r ? b->etage(r->geraet) : nullptr;
    if (!r || !e) {
        leer_hinweis(doppelseite(inhalt, false).links, "questionmark.circle", "Raum nicht gefunden", "");
        return;
    }
    titel_setzen(r->name);
    std::string name = r->name;
    kopf_knopf("sparkles", "Fragen", [name] { fragen(name); });
    Spalten sp = doppelseite(inhalt);
    s_nb_raum.spalten(sp);
    auto z = r->zustand();

    // Messung und Bedienung
    lv_obj_t *k = karte(sp.links);
    lv_obj_t *kopf = zeile_neu(k, 10);
    kicker(kopf, e->name, p.gedaempft);
    dehnung(kopf);
    marke(kopf, raum_text(z), raum_farbe(z), raum_sym(z));
    lv_obj_t *mess = zeile_neu(k, 14, LV_FLEX_ALIGN_END);
    if (isnan(r->ist)) text(mess, "–,–", &f_raum, p.blass);
    else messzahl(mess, fmt::zahl(r->ist), "°C", &f_raum, p.tinte);
    dehnung(mess);
    text(mess, abstand_text(*r), &f_mono, abstand_farbe(*r));
    if (r->thermometer) sollwert_stellrad(k, id, true);
    lv_obj_t *zk = zeile_neu(k, 12);
    knopf(zk, "Jetzt prüfen", false, [id] { daten::raum_pruefen(id); }, "arrow.clockwise");
    dehnung(zk);
    bool an = r->an;
    knopf(zk, an ? "Ausschalten" : "Einschalten", false, [id, an] { daten::betriebsart(id, !an); });

    // Kennzahlen
    lv_obj_t *raster = zeile_neu(sp.links, 12, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_flow(raster, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(raster, 12, 0);
    int kb = (SPALTE_B - 12) / 2;
    lv_obj_set_width(kennzahl(raster, "Luftfeuchte", fmt::zahl(r->feuchte, 0), "%", "", p.tinte), kb);
    lv_obj_set_width(kennzahl(raster, "Batterie", r->batterie >= 0 ? std::to_string(r->batterie) : std::string(fmt::STRICH), "%", "",
                              r->batterie >= 0 && r->batterie < 10 ? p.stoerung : p.tinte),
                     kb);
    lv_obj_set_width(kennzahl(raster, "Messwert", r->alter >= 0 ? std::to_string(r->alter) : std::string(fmt::STRICH), "s alt",
                              r->thermometer_name, p.tinte),
                     kb);
    lv_obj_set_width(kennzahl(raster, "Zielstellung", fmt::prozentzahl(r->zielstellung), "%", "", p.waerme), kb);

    // Ventile
    lv_obj_t *kv = karte(sp.rechts);
    kartenkopf(kv, "Ventile", nullptr,
               isnan(r->p_band) ? std::string("Am Sollwert stehen die Ventile halb offen; darunter öffnen sie weiter, darüber schließen sie.")
                                : "Am Sollwert stehen die Ventile halb offen, " + fmt::kelvin(r->p_band) +
                                      " darunter ganz offen, ebenso viel darüber ganz zu.");
    for (int nr : r->kanaele) {
        const anlage::Kanal *kn = nullptr;
        for (const auto &x : e->kanaele)
            if (x.nr == nr) kn = &x;
        lv_obj_t *zz = zeile_neu(kv, 12);
        lv_obj_t *nn = text(zz, std::to_string(nr), &f_wert, p.tinte);
        lv_obj_set_width(nn, 40);
        lv_obj_t *bh = flaeche(zz);
        lv_obj_set_height(bh, 9);
        lv_obj_set_flex_grow(bh, 1);
        balken(bh, kn ? kn->stellung : NAN, kn && kn->hand ? p.warnung : p.waerme, 9);
        if (kn && kn->hand) marke(zz, "Handbetrieb", p.warnung, "hand.raised");
        if (kn && !kn->kalibriert) marke(zz, "ohne Messfahrt", p.gedaempft, "ruler");
        if (kn && kn->op != "idle") marke(zz, kn->op == "opening" ? "öffnet" : "schließt", p.waerme, "arrow.left.and.right");
        lv_obj_t *pr = text(zz, kn ? fmt::prozent(kn->stellung) : std::string(fmt::STRICH), &f_mono, p.tinte);
        lv_obj_set_width(pr, 70);
        lv_obj_set_style_text_align(pr, LV_TEXT_ALIGN_RIGHT, 0);
    }

    // Verlauf
    lv_obj_t *kl = karte(sp.rechts);
    kartenkopf(kl, "Verlauf", nullptr, "Raumtemperatur, Sollwert und Ventilstellung.");
    segmente(kl, {"24 Std.", "7 Tage"}, s_zeitraum, [](int i) {
        s_zeitraum = i;
        lv_async_call([](void *) { s_nb_raum.neu([](lv_obj_t *x) { raum_aufbauen(x, s_raum); }); }, nullptr);
    });
    int64_t bis = daten::jetzt(), von = bis - (s_zeitraum ? 7 * 86400 : 86400);
    int raster_s = s_zeitraum ? 1800 : 300;
    std::string pre = r->geraet + "/raum." + std::to_string(r->nr) + ".";
    auto linie = [&](const std::string &k) {
        std::vector<std::pair<int64_t, float>> v;
        for (const auto &pt : daten::reihe(pre + k, von, bis, raster_s)) v.push_back({pt.t, pt.v});
        return v;
    };
    auto ist = linie("ist"), soll = linie("soll"), st = linie("stellung");
    if (ist.empty() && soll.empty()) {
        text_umbruch(kl, "Noch kein Verlauf. Der Verteiler speichert keinen; die Tafel zeichnet die Werte auf, solange sie läuft.",
                     &f_sub, p.gedaempft);
    } else {
        diagramm(kl, 250, von, bis, {{ist, p.waerme, false, false, false, "Ist"}, {soll, p.akzent, true, true, false, "Soll"}}, 2, "°C");
        diagramm(kl, 96, von, bis, {{st, p.waerme, false, false, true, "Ventilstellung"}}, 1, "%", true);
        lv_obj_t *leg = zeile_neu(kl, 22);
        auto eintrag = [&](lv_color_t f, const char *t) {
            lv_obj_t *z2 = zeile_neu(leg, 8);
            lv_obj_set_width(z2, LV_SIZE_CONTENT);
            lv_obj_t *s = flaeche(z2);
            lv_obj_set_size(s, 22, 4);
            lv_obj_set_style_bg_color(s, f, 0);
            lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
            text(z2, t, &f_cap, p.gedaempft);
        };
        eintrag(p.waerme, "Ist");
        eintrag(p.akzent, "Soll");
        eintrag(mischen(p.waerme, p.flaeche, 64), "Ventilstellung");
    }
    s_nb_raum.wiederherstellen();
}

void raum_takt()
{
    if (s_nb_raum.geaendert(anzeige_generation())) s_nb_raum.neu([](lv_obj_t *i) { raum_aufbauen(i, s_raum); });
}

}  // namespace ui
