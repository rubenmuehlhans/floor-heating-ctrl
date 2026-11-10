// Befunde und Befund — wie die Unterseiten „Befunde“ und „Befund“ der Übersicht
// (UebersichtAnsicht.swift Z. 494–614): links die Liste nach Schwere, rechts der gewählte
// Befund, ohne Auswahl der schwerste.
#include "daten.h"
#include "fmt.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

Neubau s_nb;
std::string s_auswahl;

void detail(lv_obj_t *c, const anlage::Befund &f)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    spalte(k, 16);
    marke(k, anlage::schwere_text(f.schwere), schwere_farbe(f.schwere), schwere_sym(f.schwere));
    text_umbruch(k, fmt::einheiten_fest(f.titel), &f_t3_s, p.tinte);
    text_umbruch(k, fmt::einheiten_fest(f.text), &f_body, p.tinte);
    lv_obj_t *felder = spalte_neu(k, 0);
    kennwert(felder, "Ort", f.ort, p.gedaempft);
    kennwert(felder, "Gemeldet von", f.quelle, p.gedaempft);
    kennwert(felder, "Seit", f.seit ? fmt::tag_mit_zeit(f.seit) : std::string(fmt::STRICH), p.gedaempft);
    trennlinie(k);
    lv_obj_t *zz = zeile_neu(k, 12);
    lv_obj_set_flex_flow(zz, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(zz, 10, 0);
    std::string id = f.id, titel = f.titel;
    bool stumm = f.stumm;
    knopf(zz, stumm ? "Wieder melden" : "Nicht mehr melden", false, [id, stumm] { daten::befund_stumm(id, !stumm); },
          stumm ? "bell" : "bell.slash");
    knopf(zz, "Assistenten fragen", true, [titel] { fragen(titel); }, "sparkles");
}

}  // namespace

void befunde_aufbauen(lv_obj_t *inhalt, const std::string &auswahl)
{
    const Palette &p = pal();
    auto b = daten::bild();
    s_auswahl = auswahl;
    s_nb.merken(inhalt, daten::generation());
    titel_setzen("Befunde");
    Spalten sp = doppelseite(inhalt);
    s_nb.spalten(sp);
    if (b->befunde.empty()) {
        lv_obj_t *k = karte(sp.links);
        lv_obj_t *z = zeile_neu(k, 12);
        symbol(z, "checkmark.circle", &f_sym24, p.gut);
        text(z, "Zurzeit liegt kein Befund vor.", &f_sub, p.tinte);
    }
    struct {
        anlage::Schwere s;
        const char *titel;
    } GR[] = {{anlage::Schwere::stoerung, "Störungen"}, {anlage::Schwere::warnung, "Warnungen"}, {anlage::Schwere::hinweis, "Hinweise"}};
    for (const auto &g : GR) {
        if (!b->befunde_mit(g.s)) continue;
        abschnitt(sp.links, g.titel, std::to_string(b->befunde_mit(g.s)));
        lv_obj_t *k = karte(sp.links);
        lv_obj_set_style_pad_ver(k, 8, 0);
        spalte(k, 0);
        bool erste = true;
        for (const auto &f : b->befunde) {
            if (f.schwere != g.s) continue;
            if (!erste) trennlinie(k);
            erste = false;
            befund_zeile(k, f);
        }
    }
    auto erl = daten::erledigte_befunde();
    if (!erl.empty()) {
        abschnitt(sp.links, "Erledigt in den letzten 30 Tagen");
        lv_obj_t *k = karte(sp.links);
        spalte(k, 10);
        for (auto it = erl.rbegin(); it != erl.rend(); ++it) {
            lv_obj_t *z = zeile_neu(k, 12, LV_FLEX_ALIGN_START);
            symbol(z, "checkmark.circle", &f_sym19, p.gut);
            lv_obj_t *s = spalte_neu(z, 2);
            lv_obj_set_width(s, 0);
            lv_obj_set_flex_grow(s, 1);
            text_umbruch(s, it->titel, &f_sub, p.tinte);
            text(s, it->ort + " · " + fmt::tag_mit_zeit(it->von) + " bis " + fmt::uhrzeit(it->bis), &f_mono_k, p.gedaempft);
        }
    }
    text_umbruch(sp.links,
                 "Befunde erscheinen erst, wenn sie ihre Mindestdauer anstehen; kehrt ein Befund binnen einer Stunde zurück, setzt er "
                 "seinen Ablauf fort.",
                 &f_cap, p.gedaempft);
    // Rechts: gewählter Befund, sonst der schwerste
    const anlage::Befund *f = nullptr;
    for (const auto &x : b->befunde)
        if (x.id == auswahl) f = &x;
    if (!f && !b->befunde.empty()) f = &b->befunde.front();
    if (f) detail(sp.rechts, *f);
    else if (!auswahl.empty()) leer_hinweis(sp.rechts, "checkmark.circle", "Befund erledigt", "Der Befund steht nicht mehr an.");
    s_nb.wiederherstellen();
}

void befunde_takt()
{
    if (s_nb.geaendert(daten::generation())) s_nb.neu([](lv_obj_t *i) { befunde_aufbauen(i, s_auswahl); });
}

}  // namespace ui
