// Heizung — wie apple/App/Ansichten/Heizung/HeizungAnsicht.swift.
//
// Oben über die volle Breite die Karte „Anlage“ mit dem Schema (die Tafel ist breit genug,
// es unverkleinert zu zeigen), darunter links Pufferspeicher und Brenner, rechts Pumpen und
// Auswertung. Die ganze Seite scrollt als eine.
#include <math.h>
#include <tuple>
#include "daten.h"
#include "fmt.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

namespace ui {

lv_obj_t *anlagenschema(lv_obj_t *parent, int breite);

namespace {

Neubau s_nb;

bool relais_gestoert(const anlage::Relais &r) { return r.bekannt && !r.online && !r.stromlos && r.weg != "keiner"; }

void kennzahlen_raster(lv_obj_t *k, const std::vector<std::tuple<std::string, std::string, const char *, std::string, lv_color_t>> &z,
                       int breite, int je_zeile)
{
    lv_obj_t *r = zeile_neu(k, 12, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(r, 12, 0);
    int b = (breite - 12 * (je_zeile - 1)) / je_zeile;
    for (const auto &x : z)
        lv_obj_set_width(kennzahl(r, std::get<0>(x), std::get<1>(x), std::get<2>(x), std::get<3>(x), std::get<4>(x)), b);
}

void anlage_karte(lv_obj_t *c, const anlage::Bild &b)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    kartenkopf(k, "Anlage", nullptr, "Kessel, Pufferspeicher und die angelegten Heizkreise. Messwerte stehen dort, wo der Fühler sitzt.");
    lv_obj_t *mitte = zeile_neu(k, 0);
    lv_obj_set_flex_align(mitte, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    anlagenschema(mitte, 960);
    trennlinie(k);
    // Außenzeile
    lv_obj_t *z = zeile_neu(k, 12);
    bool da = !isnan(b.aussen.temperatur);
    symbol(z, "thermometer.snowflake", &f_sym24, da ? p.kaelte : p.gedaempft);
    text(z, "Außen", &f_sub_m, p.tinte);
    if (da) text(z, fmt::temperatur(b.aussen.temperatur), &f_wert, p.kaelte);
    dehnung(z);
    if (da) {
        std::string zusatz = b.aussen.quelle;
        if (!isnan(b.aussen.feuchte)) zusatz += " · Feuchte " + fmt::mit_einheit(fmt::zahl(b.aussen.feuchte, 0), "%");
        if (b.aussen.alter >= 0) zusatz += " · " + fmt::alter(b.aussen.alter);
        text(z, zusatz, &f_mono_k, p.gedaempft);
    } else text(z, "kein Außenfühler zugeordnet", &f_sub, p.gedaempft);
}

void speicher_karte(lv_obj_t *c, const anlage::Speicher &s)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    kartenkopf(k, "Pufferspeicher", nullptr,
               "Der Füllstand ist eine Schätzung aus dem Pufferfühler. Ob eine Ladung läuft, erkennt das Gerät an Kesselvorlauf "
               "und Speichertemperatur.");
    std::string seit = s.phase_seit >= 0 ? "seit " + fmt::seit(s.phase_seit) : "";
    zustandszeile(k, s.phase.empty() ? std::string("unbekannt") : s.phase, s.phase == "wird geladen" ? p.waerme : p.blass, seit);
    std::string temp_fuss = "Fühler oben";
    if (s.korrektur != 0 && !isnan(s.korrektur)) temp_fuss += ", Korrektur " + fmt::kelvin(s.korrektur, 1, true);
    std::string lv = (isnan(s.leer) ? std::string(fmt::STRICH) : fmt::zahl(s.leer, 0)) + "/" + (isnan(s.voll) ? std::string(fmt::STRICH) : fmt::zahl(s.voll, 0));
    std::string lv_fuss = isnan(s.voll) ? "noch nicht gelesen" : s.leer_gelernt ? "Leerpunkt gelernt" : "eingestellt";
    kennzahlen_raster(k,
                      {{"Füllstand", fmt::prozentzahl(s.ladung), "%", "geschätzt aus dem Pufferfühler", p.waerme},
                       {"Temperatur", fmt::zahl(s.temperatur), "°C", temp_fuss, p.tinte},
                       {"Leer / Voll", lv, "°C", lv_fuss, p.tinte},
                       {"Ladungsende", fmt::zahl(s.hoechstwert), "°C", "Höchstwert der Kalibrierung",
                        !isnan(s.voll) && s.hoechstwert > s.voll ? p.warnung : p.tinte}},
                      SPALTE_B - 48, 2);
    balken(k, s.ladung, p.waerme, 12);
    if (s.warmwasser_knapp) marke(k, "Warmwasserreserve knapp", p.warnung, "drop.triangle");
}

void brenner_karte(lv_obj_t *c, const anlage::Kessel &ke)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    kartenkopf(k, "Brenner", nullptr,
               "Erkannt am Abgasfühler: Steigt das Abgas deutlich über die Bezugslinie, gilt der Brenner als laufend.");
    zustandszeile(k, ke.laeuft ? "läuft" : "aus", ke.laeuft ? p.waerme : p.blass, ke.seit >= 0 ? "seit " + fmt::seit(ke.seit) : "");
    kennzahlen_raster(k,
                      {{"Abgas", fmt::zahl(ke.abgas), "°C", isnan(ke.bezugslinie) ? "" : "Bezugslinie " + fmt::temperatur(ke.bezugslinie), p.waerme},
                       {"Heute", fmt::dauer(ke.laufzeit_heute), "", fmt::starts(ke.starts_heute) + " · " + fmt::liter(ke.liter_heute, 2), p.tinte},
                       {"Gestern", fmt::dauer(ke.laufzeit_gestern), "", fmt::starts(ke.starts_gestern), p.tinte},
                       {"Düse", fmt::zahl(ke.duese, 2), "l/h", isnan(ke.duese) ? "nicht eingetragen" : "angenommen, nicht gemessen", p.tinte}},
                      SPALTE_B - 48, 2);
    if (ke.taktbetrieb) marke(k, "Taktbetrieb: viele kurze Starts", p.warnung, "exclamationmark.triangle");
}

// Pumpenmarke: die erste zutreffende gilt (HeizungAnsicht.swift Z. 229–237)
void pumpenmarke(lv_obj_t *z, bool laeuft, bool gesperrt, const anlage::Relais &r)
{
    const Palette &p = pal();
    if (relais_gestoert(r)) marke(z, "Relais gestört", p.stoerung, "xmark.octagon");
    else if (gesperrt) marke(z, "Von der Kesselregelung gesperrt", p.warnung, "lock");
    else if (r.stromlos) marke(z, "Pumpe aus, Relais ohne Strom", p.gedaempft, "powerplug");
    else if (laeuft) marke(z, "Pumpe läuft", p.waerme, "fanblades");
    else marke(z, "Pumpe aus", p.gedaempft, "power");
}

void kasten(lv_obj_t *k, const char *sf, const std::string &t, lv_color_t farbe)
{
    const Palette &p = pal();
    lv_obj_t *x = zeile_neu(k, 12, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(x, farbe, 0);
    lv_obj_set_style_bg_opa(x, 20, 0);
    lv_obj_set_style_border_width(x, 1, 0);
    lv_obj_set_style_border_color(x, mischen(farbe, p.flaeche, 90), 0);
    lv_obj_set_style_radius(x, RADIUS_KLEIN, 0);
    lv_obj_set_style_pad_all(x, 14, 0);
    symbol(x, sf, &f_sym24, farbe);
    lv_obj_t *l = text_umbruch(x, t, &f_sub, p.tinte, 0);
    lv_obj_set_flex_grow(l, 1);
}

void heizkreis_karte(lv_obj_t *c, const anlage::Heizkreis &h)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    lv_obj_t *kopf = zeile_neu(k, 10);
    text(kopf, h.name, &f_body_s, p.tinte);
    dehnung(kopf);
    pumpenmarke(kopf, h.pumpe, h.gesperrt, h.relais);
    std::string v;
    for (size_t j = 0; j < h.versorgt.size(); j++) v += (j ? (j + 1 == h.versorgt.size() ? " und " : ", ") : "") + h.versorgt[j];
    text_umbruch(k, (v.empty() ? std::string("versorgt keinen Verteiler") : "Versorgt " + v) + (h.grund.empty() ? "" : " · " + h.grund),
                 &f_mono_k, p.gedaempft);
    int nr = h.nr;
    segmente(k, {"Automatik", "Hand ein", "Hand aus"}, (int)h.modus, [nr](int i) { daten::heizkreis_modus(nr, (anlage::Modus)i); });
    float sp = h.vorlauf - h.ruecklauf;
    kennzahlen_raster(k,
                      {{"Vorlauf", fmt::zahl(h.vorlauf), "°C", "", p.waerme},
                       {"Rücklauf", fmt::zahl(h.ruecklauf), "°C", "", p.kaelte},
                       {"Spreizung", fmt::zahl(sp), "K", !isnan(sp) && sp < 0 ? "Vorlauf kälter als Rücklauf" : "",
                        !isnan(sp) && sp < 0 ? p.warnung : p.tinte}},
                      SPALTE_B - 48, 3);
    if (h.keiner_erreicht && !h.versorgt.empty()) marke(k, "Kein versorgter Verteiler erreicht", p.stoerung, "wifi.exclamationmark");
    else if (h.veraltet) marke(k, "Ein versorgter Verteiler antwortet nicht", p.warnung, "wifi.exclamationmark");
    if (h.gesperrt)
        kasten(k, "lock", "Die Räume fordern Wärme an, aber die Kesselregelung gibt die Pumpe nicht frei: Das Relais ist ohne Strom.",
               p.warnung);
    else if (relais_gestoert(h.relais)) kasten(k, "xmark.octagon", "Das Relais antwortet nicht; die Pumpe folgt der Steuerung nicht.", p.stoerung);
}

void kkp_karte(lv_obj_t *c, const anlage::Kesselkreispumpe &kp)
{
    const Palette &p = pal();
    lv_obj_t *k = karte(c);
    lv_obj_t *kopf = zeile_neu(k, 10);
    text(kopf, "Kesselkreispumpe", &f_body_s, p.tinte);
    dehnung(kopf);
    pumpenmarke(kopf, kp.laeuft, false, kp.relais);
    text_umbruch(k,
                 "Fördert Wärme vom Kessel in den Pufferspeicher. In Automatik läuft sie, solange der Kessel deutlich wärmer ist "
                 "als der Speicher, und mit Nachlauf nach dem Brennerende.",
                 &f_sub, p.gedaempft);
    segmente(k, {"Automatik", "Hand ein", "Hand aus"}, (int)kp.modus, [](int i) { daten::kkp_modus((anlage::Modus)i); });
    if (!kp.grund.empty()) text_umbruch(k, "Zuletzt: " + kp.grund, &f_mono_k, p.gedaempft);
}

}  // namespace

void heizung_aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    auto b = daten::bild();
    s_nb.merken(inhalt, daten::generation());
    kopf_knopf("sparkles", "Fragen", [] { fragen("Heizung"); });
    Spalten ganz = doppelseite(inhalt, false);
    s_nb.spalten(ganz);
    if (!b->kessel.vorhanden && !b->speicher.vorhanden && b->heizkreise.empty()) {
        leer_hinweis(ganz.links, "flame", "Kein Heizungsgerät gefunden",
                     "Kessel, Pufferspeicher und Heizkreise erscheinen hier, sobald die Tafel ein Heizungsgerät im Heimnetz findet.");
        return;
    }
    anlage_karte(ganz.links, *b);
    lv_obj_t *reihe = zeile_neu(ganz.links, FUGE, LV_FLEX_ALIGN_START);
    lv_obj_t *l = spalte_neu(reihe, ABSTAND), *r = spalte_neu(reihe, ABSTAND);
    lv_obj_set_width(l, SPALTE_B);
    lv_obj_set_width(r, SPALTE_B);
    if (b->speicher.vorhanden) speicher_karte(l, b->speicher);
    if (b->kessel.vorhanden) brenner_karte(l, b->kessel);
    if (!b->heizkreise.empty() || b->kkp.vorhanden) abschnitt(r, "Pumpen");
    for (const auto &h : b->heizkreise) heizkreis_karte(r, h);
    if (b->kkp.vorhanden) kkp_karte(r, b->kkp);
    abschnitt(r, "Auswertung");
    lv_obj_t *k = karte(r);
    lv_obj_set_style_pad_ver(k, 8, 0);
    spalte(k, 0);
    link_zeile(k, "chart.xyaxis.line", "Verlauf", "", [] { oeffnen({Ziel::Art::verlauf, ""}); }, p.akzent);
    if (!isnan(b->kessel.abgas_abstand) || b->kessel.abgas_ladungen) {
        trennlinie(k);
        kennwert(k, "Abgas-Vorlauf-Abstand",
                 isnan(b->kessel.abgas_abstand) ? "ab 10 Ladungen (" + std::to_string(b->kessel.abgas_ladungen) + " erfasst)"
                                                : fmt::kelvin(b->kessel.abgas_abstand, 1, true),
                 p.gedaempft);
    }
    s_nb.wiederherstellen();
}

void heizung_takt()
{
    if (s_nb.geaendert(daten::generation())) s_nb.neu([](lv_obj_t *i) { heizung_aufbauen(i); });
}

// ---- Verlauf (Heizung › Verlauf) ------------------------------------------------------

static int s_vz = 1;   // 0 = 6 Std., 1 = 24 Std., 2 = 7 Tage
static lv_obj_t *s_v_inhalt;

void verlauf_aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    s_v_inhalt = inhalt;
    titel_setzen("Verlauf");
    auto b = daten::bild();
    Spalten sp = doppelseite(inhalt);
    lv_obj_t *wahl = karte(sp.links);
    lv_obj_set_style_pad_all(wahl, 12, 0);
    segmente(wahl, {"6 Std.", "24 Std.", "7 Tage"}, s_vz, [](int i) {
        s_vz = i;
        lv_async_call([](void *) {
            lv_obj_clean(s_v_inhalt);
            verlauf_aufbauen(s_v_inhalt);
        }, nullptr);
    });
    int64_t bis = daten::jetzt(), von = bis - (s_vz == 0 ? 6 * 3600 : s_vz == 1 ? 86400 : 7 * 86400);
    int raster = s_vz == 2 ? 1800 : 300;
    auto reihe = [&](const std::string &k) {
        std::vector<std::pair<int64_t, float>> v;
        for (const auto &pt : daten::reihe(k, von, bis, raster)) v.push_back({pt.t, pt.v});
        return v;
    };
    auto tafel = [&](lv_obj_t *c, const std::string &titel, std::vector<Linie> linien, int h, float spanne, bool prozent) {
        lv_obj_t *k = karte(c);
        text(k, titel, &f_body_s, p.tinte);
        bool leer = true;
        for (auto &l : linien) leer = leer && l.punkte.empty();
        if (leer) {
            text_umbruch(k, "Noch keine Werte in diesem Zeitraum.", &f_sub, p.gedaempft);
            return;
        }
        diagramm(k, h, von, bis, linien, spanne, prozent ? "%" : "°C", prozent);
        lv_obj_t *leg = zeile_neu(k, 20);
        lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW_WRAP);
        for (const auto &l : linien) {
            if (l.punkte.empty()) continue;
            lv_obj_t *z = zeile_neu(leg, 8);
            lv_obj_set_width(z, LV_SIZE_CONTENT);
            lv_obj_t *s = flaeche(z);
            lv_obj_set_size(s, 22, 4);
            lv_obj_set_style_bg_color(s, l.farbe, 0);
            lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
            text(z, l.name, &f_cap, p.gedaempft);
        }
    };
    lv_color_t orange = lv_color_hex(0xF09A37), gelb = lv_color_hex(0xE9C73F), braun = lv_color_hex(0xA2845E);
    tafel(sp.links, "Kessel und Speicher",
          {{reihe("fuehler.kessel_vl"), p.waerme, false, false, false, "Kessel Vorlauf"},
           {reihe("fuehler.kessel_rl"), p.kaelte, false, false, false, "Kessel Rücklauf"},
           {reihe("fuehler.puffer"), orange, false, false, false, "Puffer"},
           {reihe("fuehler.puffer_unten"), gelb, true, false, false, "Puffer unten"}},
          270, 10, false);
    tafel(sp.links, "Abgas", {{reihe("fuehler.abgas"), braun, false, false, false, "Abgas"}}, 180, 40, false);
    tafel(sp.links, "Brenner und Speicher",
          {{reihe("brenner"), p.waerme, false, false, true, "Brenneranteil"}, {reihe("fuellstand"), orange, false, false, false, "Füllstand"}},
          180, 1, true);
    lv_color_t vl[] = {lv_color_hex(0xE0508C), lv_color_hex(0xE5534B)}, rl[] = {lv_color_hex(0x6A63D9), lv_color_hex(0x3FA7A3)};
    for (size_t i = 0; i < b->heizkreise.size(); i++) {
        const auto &h = b->heizkreise[i];
        std::string n = std::to_string(h.nr);
        tafel(sp.rechts, h.name,
              {{reihe("fuehler.hk" + n + "_vl"), vl[i % 2], false, false, false, "Vorlauf"},
               {reihe("fuehler.hk" + n + "_rl"), rl[i % 2], false, false, false, "Rücklauf"},
               {reihe("aussen"), p.gut, false, false, false, "Außen"}},
              270, 10, false);
    }
    std::vector<Linie> raeume;
    static const uint32_t FARBEN[] = {0x3B82F6, 0x9B59D0, 0x3FC9A8, 0x37B6D8, 0xE0508C, 0x6A63D9, 0x4CAF50, 0xE9C73F, 0x8E8E93, 0xA2845E};
    int fi = 0;
    for (const auto &e : b->etagen)
        for (const auto &r : e.raeume)
            if (r.thermometer)
                raeume.push_back({reihe(e.geraet + "/raum." + std::to_string(r.nr) + ".ist"), lv_color_hex(FARBEN[fi++ % 10]), false, false, false, r.name});
    tafel(sp.rechts, "Räume", raeume, 300, 4, false);
}

void verlauf_takt() {}

}  // namespace ui
