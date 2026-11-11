// Übersicht — wie apple/App/Ansichten/Uebersicht/UebersichtAnsicht.swift, zweispaltig:
// links Geräteleiste, Lagebericht, Wärmeerzeugung und Befunde, rechts die Räume.
#include <math.h>
#include "daten.h"
#include "fmt.h"
#include "ki.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

Neubau s_nb;

lv_obj_t *geraetemarke(lv_obj_t *parent, const anlage::Geraet &g)
{
    const Palette &p = pal();
    lv_obj_t *m = flaeche(parent);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m, p.flaeche, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m, 1, 0);
    lv_obj_set_style_border_color(m, p.linie_stark, 0);
    lv_obj_set_style_pad_hor(m, 15, 0);
    lv_obj_set_style_pad_ver(m, 7, 0);
    lv_obj_set_style_bg_color(m, p.vertieft, LV_STATE_PRESSED);
    zeile(m, 8);
    lv_obj_t *punkt = flaeche(m);
    lv_obj_set_size(punkt, 9, 9);
    lv_obj_set_style_radius(punkt, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(punkt, g.erreichbar ? p.gut : p.stoerung, 0);
    lv_obj_set_style_bg_opa(punkt, LV_OPA_COVER, 0);
    text(m, g.ort, &f_cap_m, p.akzent);
    std::string id = g.id;
    bei_klick(m, [id] {
        bereich_waehlen(Bereich::geraete);
        oeffnen({Ziel::Art::wurzel, id});
    });
    return m;
}

void geraeteleiste(lv_obj_t *c, const anlage::Bild &b)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(c, 10);
    lv_obj_set_flex_flow(z, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(z, 10, 0);
    bool hz = false, vt = false;
    for (const auto &g : b.geraete) hz |= g.art == anlage::Art::heizung, vt |= g.art == anlage::Art::verteiler;
    if (hz) {
        kicker(z, "Heizung", p.gedaempft);
        for (const auto &g : b.geraete)
            if (g.art == anlage::Art::heizung) geraetemarke(z, g);
    }
    if (vt) {
        lv_obj_t *k = kicker(z, "Verteiler", p.gedaempft);
        if (hz) lv_obj_set_style_margin_left(k, 10, 0);
        for (const auto &g : b.geraete)
            if (g.art == anlage::Art::verteiler) geraetemarke(z, g);
    }
}

void beispielkarte(lv_obj_t *c)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    kartenkopf(k, "Beispielanlage", "theatermasks",
               "Die Tafel zeigt einen Mitschnitt, bis sie Geräte im Heimnetz findet. Befehle verändern hier nur die "
               "Beispieldaten.");
    lv_obj_t *z = zeile_neu(k, 12);
    knopf(z, "WLAN einrichten", true, [] {
        bereich_waehlen(Bereich::geraete);
        oeffnen({Ziel::Art::wlan, ""});
    });
    (void)p;
}

void lagebericht(lv_obj_t *c, const anlage::Bild &b)
{
    const Palette &p = pal();
    ki::Stand st = ki::stand();
    const ki::Lagebericht &l = st.bericht;
    lv_obj_t *k = karte(c, true);
    lv_obj_t *kopf = zeile_neu(k, 10);
    symbol(kopf, "sparkles", &f_sym24, p.akzent);
    text(kopf, "Lagebericht", &f_body_s, p.tinte);
    dehnung(kopf);
    static const char *TEXT[] = {"In Ordnung", "Beobachten", "Handeln"};
    static const char *SYM[] = {"checkmark", "eye", "exclamationmark.triangle"};
    lv_color_t FARBE[] = {p.gut, p.warnung, p.stoerung};
    if (l.zustand >= 0 && l.zustand <= 2) marke(kopf, TEXT[l.zustand], FARBE[l.zustand], SYM[l.zustand]);
    text_umbruch(k, l.kurztext.empty() ? std::string("Noch kein Lagebericht.") : l.kurztext, &f_t3, p.tinte);
    if (!l.hinweise.empty()) {
        lv_obj_t *kasten = flaeche(k);
        lv_obj_set_width(kasten, LV_PCT(100));
        lv_obj_set_style_bg_color(kasten, p.vertieft, 0);
        lv_obj_set_style_bg_opa(kasten, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(kasten, RADIUS_KLEIN, 0);
        lv_obj_set_style_border_width(kasten, 1, 0);
        lv_obj_set_style_border_color(kasten, p.linie, 0);
        spalte(kasten, 0);
        bool erste = true;
        for (const auto &h : l.hinweise) {
            if (!erste) trennlinie(kasten);
            erste = false;
            lv_obj_t *z = zeile_neu(kasten, 12, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_hor(z, 16, 0);
            lv_obj_set_style_pad_ver(z, 14, 0);
            lv_obj_set_style_bg_color(z, p.linie, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
            anlage::Schwere s = (anlage::Schwere)h.schwere;
            lv_obj_t *sy = symbol(z, schwere_sym(s), &f_sym19, schwere_farbe(s));
            lv_obj_set_width(sy, 26);
            lv_obj_t *t = text_umbruch(z, fmt::einheiten_fest(h.text), &f_sub, p.tinte, 0);
            lv_obj_set_flex_grow(t, 1);
            symbol(z, "chevron.right", &f_sym19, p.blass);
            std::string frage = h.text;
            bei_klick(z, [frage] { fragen(frage); });
        }
    }
    std::string fuss;
    if (l.erstellt) {
        int alter = (int)(daten::jetzt() - l.erstellt);
        fuss = (l.aus_befunden ? std::string("aus den Befunden") : l.modell) + " · " + fmt::alter(alter < 0 ? 0 : alter);
    }
    if (st.bericht_laeuft) fuss += std::string(fuss.empty() ? "" : " · ") + "neuer Bericht entsteht …";
    if (!fuss.empty()) text(k, fuss, &f_mono_k, p.gedaempft);
    lv_obj_t *z = zeile_neu(k, 12);
    int offen = 0;
    for (const auto &v : st.vorschlaege) offen += v.status == ki::VStatus::offen;
    if (offen) knopf(z, std::to_string(offen) + (offen == 1 ? " Vorschlag" : " Vorschläge"), false, [] { bereich_waehlen(Bereich::assistent); });
    knopf(z, "Nachfragen", true, [] { fragen("Lagebericht"); });
    dehnung(z);
    if (!b.beispiel && ki::hindernis().empty()) {
        lv_obj_t *n = rund_knopf(z, "arrow.clockwise", 52, [] { ki::lagebericht_erstellen(); });
        if (st.bericht_laeuft) lv_obj_set_state(n, LV_STATE_DISABLED, true);
    }
}

void waermeerzeugung(lv_obj_t *c, const anlage::Bild &b)
{
    const Palette &p = pal();
    abschnitt(c, "Wärmeerzeugung", b.stand ? "Stand " + fmt::uhrzeit(b.stand) : "");
    lv_obj_t *k = karte(c);
    lv_obj_set_style_pad_all(k, 18, 0);
    bei_klick(k, [] { bereich_waehlen(Bereich::heizung); });
    lv_obj_t *r = zeile_neu(k, 12, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(r, 12, 0);
    auto kz = [&](const std::string &t, const std::string &w, const char *e, const std::string &f, lv_color_t farbe) {
        lv_obj_t *x = kennzahl(r, t, w, e, f, farbe);
        lv_obj_set_width(x, (SPALTE_B - 36 - 12) / 2);
    };
    const auto &s = b.speicher;
    std::string sf = "Ladung " + fmt::prozent(s.ladung) + (s.phase.empty() ? "" : " · " + s.phase);
    kz("Speicher", fmt::zahl(s.temperatur), "°C", sf, p.waerme);
    const auto &ke = b.kessel;
    kz("Brenner", ke.laeuft ? "läuft" : "aus", "", "gestern " + fmt::dauer(ke.laufzeit_gestern) + " · " + fmt::starts(ke.starts_gestern),
       ke.laeuft ? p.waerme : p.tinte);
    std::string af = b.aussen.quelle;
    if (!isnan(b.aussen.feuchte)) af += " · Feuchte " + fmt::mit_einheit(fmt::zahl(b.aussen.feuchte, 0), "%");
    kz("Außen", fmt::zahl(b.aussen.temperatur), "°C", af, p.kaelte);
    int gestoert = b.pumpen_gestoert(), alle = b.pumpen_relais();
    kz("Pumpen", gestoert ? std::to_string(gestoert) + " gestört" : "in Ordnung", "",
       gestoert ? "Relais nicht erreichbar" : std::to_string(alle) + " Relais erreichbar", gestoert ? p.stoerung : p.gut);
}

void befundzeile(lv_obj_t *c, const anlage::Befund &f, bool chevron = true)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(c, 14, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_ver(z, 12, 0);
    lv_obj_set_style_radius(z, RADIUS_KLEIN, 0);
    lv_obj_set_style_bg_color(z, p.vertieft, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t *s = symbol(z, schwere_sym(f.schwere), &f_sym24, schwere_farbe(f.schwere));
    lv_obj_set_width(s, 30);
    lv_obj_t *sp = spalte_neu(z, 4);
    lv_obj_set_width(sp, 0);
    lv_obj_set_flex_grow(sp, 1);
    text_umbruch(sp, fmt::einheiten_fest(f.titel), &f_sub_m, p.tinte);
    std::string meta = f.ort + " · " + f.quelle;
    if (f.seit) meta += " · seit " + fmt::seit((int)(daten::jetzt() - f.seit));
    text_umbruch(sp, meta, &f_mono_k, p.gedaempft);
    if (f.stumm) symbol(z, "bell.slash", &f_sym19, p.gedaempft);
    if (chevron) symbol(z, "chevron.right", &f_sym19, p.blass);
    std::string id = f.id;
    bei_klick(z, [id] { oeffnen({Ziel::Art::befund, id}); });
}

void befunde(lv_obj_t *c, const anlage::Bild &b)
{
    const Palette &p = pal();
    abschnitt(c, "Befunde", std::to_string(b.befunde.size()));
    lv_obj_t *k = karte(c);
    lv_obj_set_style_pad_ver(k, 10, 0);
    spalte(k, 0);
    if (b.befunde.empty()) {
        lv_obj_t *z = zeile_neu(k, 12);
        lv_obj_set_style_pad_ver(z, 14, 0);
        symbol(z, "checkmark.circle", &f_sym24, p.gut);
        text(z, "Zurzeit liegt kein Befund vor.", &f_sub, p.tinte);
        return;
    }
    for (size_t i = 0; i < b.befunde.size() && i < 3; i++) {
        if (i) trennlinie(k);
        befundzeile(k, b.befunde[i]);
    }
    trennlinie(k);
    link_zeile(k, nullptr, "Alle " + std::to_string(b.befunde.size()) + " Befunde", "", [] { oeffnen({Ziel::Art::befunde, ""}); },
               p.akzent);
}

}  // namespace

// Raumkachel (RaumKachel): Name, Zustand im Kreis, Ist, Untertitel, Balken der Zielstellung.
lv_obj_t *raumkachel(lv_obj_t *parent, const anlage::Raum &r, int breite)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(parent);
    lv_obj_set_width(k, breite);
    lv_obj_set_style_pad_all(k, 18, 0);
    spalte(k, 8);
    auto z = r.zustand();
    lv_obj_t *kopf = zeile_neu(k, 8);
    lv_obj_t *n = text(kopf, r.name, &f_sub_s, p.tinte);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(n, 1);
    lv_obj_t *kreis = flaeche(kopf);
    lv_obj_set_size(kreis, 33, 33);
    lv_obj_set_style_radius(kreis, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(kreis, raum_farbe(z), 0);
    lv_obj_set_style_bg_opa(kreis, 31, 0);
    lv_obj_set_style_border_width(kreis, 1, 0);
    lv_obj_set_style_border_color(kreis, mischen(raum_farbe(z), p.flaeche, 102), 0);
    lv_obj_t *s = symbol(kreis, raum_sym(z), &f_sym19, raum_farbe(z));
    lv_obj_center(s);
    if (isnan(r.ist)) text(k, "–,–", &f_messwert, p.blass);
    else messzahl(k, fmt::zahl(r.ist), "°C", &f_messwert, p.tinte);
    std::string unter = z == anlage::Raumzustand::heizt || z == anlage::Raumzustand::soll_erreicht
                            ? "Soll " + fmt::zahl(r.soll) + " · " + fmt::kelvin(r.ist - r.soll, 1, true)
                            : abstand_text(r);
    lv_obj_t *u = text(k, unter, &f_mono_k, abstand_farbe(r));
    einzeilig(u);
    balken(k, r.zielstellung, r.an ? p.waerme : p.blass, 6);
    std::string id = r.id;
    bei_klick(k, [id] {
        bereich_waehlen(Bereich::raeume);
        oeffnen({Ziel::Art::raum, id});
    });
    return k;
}

void befund_zeile(lv_obj_t *c, const anlage::Befund &f) { befundzeile(c, f); }

void uebersicht_aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    auto b = daten::bild();
    s_nb.merken(inhalt, anzeige_generation() + ki::generation());
    kopf_knopf("sparkles", "Fragen", [] { fragen("Übersicht"); });
    Spalten sp = doppelseite(inhalt);
    s_nb.spalten(sp);
    geraeteleiste(sp.links, *b);
    if (b->beispiel) beispielkarte(sp.links);
    if (!b->vollstaendig && !b->beispiel && b->geraete.empty()) {
        lv_obj_t *z = zeile_neu(sp.links, 12);
        text(z, "Die Geräte werden gesucht …", &f_sub, p.gedaempft);
    }
    lagebericht(sp.links, *b);
    if (b->kessel.vorhanden || b->speicher.vorhanden) waermeerzeugung(sp.links, *b);
    befunde(sp.links, *b);

    // Räume, rechts
    abschnitt(sp.rechts, "Räume", std::to_string(b->raeume_heizen()) + " von " + std::to_string(b->raeume()) + " heizen");
    for (const auto &e : b->etagen) {
        if (e.raeume.empty()) continue;
        lv_obj_t *z = zeile_neu(sp.rechts, 10);
        lv_obj_set_style_pad_hor(z, 6, 0);
        lv_obj_set_style_pad_top(z, 4, 0);
        text(z, e.name, &f_sub_s, p.tinte);
        if (e.bedarf) marke(z, "Wärmebedarf", p.waerme, "flame");
        lv_obj_t *raster = zeile_neu(sp.rechts, 14, LV_FLEX_ALIGN_START);
        lv_obj_set_flex_flow(raster, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(raster, 14, 0);
        for (const auto &r : e.raeume) raumkachel(raster, r, (SPALTE_B - 14) / 2);
    }
    if (b->etagen.empty()) leer_hinweis(sp.rechts, "square.grid.2x2", "Kein Verteiler gefunden", "Verteiler im Heimnetz erscheinen hier von selbst.");
    s_nb.wiederherstellen();
}

void uebersicht_takt()
{
    if (s_nb.geaendert(anzeige_generation() + ki::generation())) s_nb.neu([](lv_obj_t *i) { uebersicht_aufbauen(i); });
}

}  // namespace ui
