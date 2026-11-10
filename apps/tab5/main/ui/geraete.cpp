// Geräte — wie apple/App/Ansichten/Geraete/GeraeteAnsicht.swift (ListeUndEintrag):
// links die Liste, rechts das gewählte Gerät, ohne Auswahl das erste.
//
// Noch nicht übernommen (PLAN.md): Einstellungsformulare der Geräte, Messfahrt, Räume und
// Kanäle, Sensoren, Sicherung, Firmware. Die Weboberfläche des Geräts bleibt dafür der Weg.
#include <math.h>
#include "daten.h"
#include "eins.h"
#include "fmt.h"
#include "json.h"
#include "verlauf.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

Neubau s_nb;
std::string s_auswahl;

void geraetzeile(lv_obj_t *c, const anlage::Geraet &g, bool gewaehlt)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(c, 16);
    lv_obj_set_style_pad_all(z, 14, 0);
    lv_obj_set_style_radius(z, RADIUS_KLEIN, 0);
    lv_obj_set_style_bg_color(z, gewaehlt ? p.akzent_weich : p.vertieft, 0);
    lv_obj_set_style_bg_opa(z, gewaehlt ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t *s = symbol(z, g.symbol(), &f_sym32, p.akzent);
    lv_obj_set_width(s, 40);
    lv_obj_t *sp = spalte_neu(z, 3);
    lv_obj_set_width(sp, 0);
    lv_obj_set_flex_grow(sp, 1);
    text(sp, g.ort, &f_body, p.tinte);
    std::string unter = g.ip;
    if (g.erreichbar) unter += g.version.empty() ? "" : " · " + g.version;
    else unter += g.gesehen || !g.fehler.empty() ? " · nicht erreichbar" : " · wird abgefragt";
    lv_obj_t *u = text(sp, unter, &f_mono_k, g.erreichbar ? p.gedaempft : p.stoerung);
    einzeilig(u);
    symbol(z, g.erreichbar ? "circle.fill" : "xmark.octagon", &f_sym19, g.erreichbar ? p.gut : p.stoerung);
    std::string id = g.id;
    bei_klick(z, [id] { oeffnen({Ziel::Art::wurzel, id}); });
}

void liste(lv_obj_t *c, const anlage::Bild &b, const std::string &gewaehlt)
{
    const Palette &p = pal();
    struct Gruppe {
        const char *titel;
        anlage::Art art;
    } GRUPPEN[] = {{"Heizungsgeräte", anlage::Art::heizung}, {"Verteiler", anlage::Art::verteiler}, {"Leitstand", anlage::Art::leitstand}};
    for (const auto &gr : GRUPPEN) {
        bool da = false;
        for (const auto &g : b.geraete) da |= g.art == gr.art;
        if (!da) continue;
        abschnitt(c, gr.titel);
        lv_obj_t *k = karte(c);
        lv_obj_set_style_pad_all(k, 8, 0);
        spalte(k, 4);
        for (const auto &g : b.geraete)
            if (g.art == gr.art) geraetzeile(k, g, g.id == gewaehlt);
    }
    if (b.geraete.empty()) leer_hinweis(c, "wifi.router", "Noch kein Gerät gefunden", "Die Tafel sucht im Heimnetz nach Verteilern und Heizungsgeräten.");
    abschnitt(c, "Tafel");
    lv_obj_t *k = karte(c);
    lv_obj_set_style_pad_ver(k, 8, 0);
    spalte(k, 0);
    link_zeile(k, "wifi", "WLAN", daten::wlan().ssid, [] { oeffnen({Ziel::Art::wlan, ""}); }, p.akzent);
    trennlinie(k);
    link_zeile(k, "gearshape", "Einstellungen der Tafel", "", [] { oeffnen({Ziel::Art::einstellungen, ""}); }, p.akzent);
}

void kanalzeile(lv_obj_t *c, const anlage::Kanal &k)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(c, 12);
    lv_obj_set_style_pad_ver(z, 6, 0);
    lv_obj_t *n = text(z, std::to_string(k.nr), &f_wert, p.tinte);
    lv_obj_set_width(n, 38);
    lv_obj_t *r = text(z, k.raum.empty() ? "frei" : k.raum, &f_sub, k.raum.empty() ? p.gedaempft : p.tinte);
    lv_obj_set_width(r, 170);
    lv_label_set_long_mode(r, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *bh = flaeche(z);
    lv_obj_set_height(bh, 9);
    lv_obj_set_flex_grow(bh, 1);
    balken(bh, k.stellung, k.raum.empty() ? p.gedaempft : k.hand ? p.warnung : p.waerme, 9);
    if (k.hand) symbol(z, "hand.raised", &f_sym19, p.warnung);
    if (!k.kalibriert && !k.raum.empty()) symbol(z, "ruler", &f_sym19, p.gedaempft);
    if (k.op != "idle") symbol(z, "arrow.left.and.right", &f_sym19, p.waerme);
    lv_obj_t *pr = text(z, fmt::prozent(k.stellung), &f_mono, p.tinte);
    lv_obj_set_width(pr, 72);
    lv_obj_set_style_text_align(pr, LV_TEXT_ALIGN_RIGHT, 0);
}

void eintrag(lv_obj_t *c, const anlage::Bild &b, const anlage::Geraet &g)
{
    const Palette &p = pal();
    // Gerätezustand
    lv_obj_t *k = karte(c);
    lv_obj_t *kopf = zeile_neu(k, 12);
    symbol(kopf, g.symbol(), &f_sym32, p.akzent);
    text(kopf, g.ort, &f_t3_s, p.tinte);
    spalte(k, 2);
    kennwert(k, "Zustand", g.erreichbar ? "erreichbar" : "nicht erreichbar", g.erreichbar ? p.gut : p.stoerung);
    kennwert(k, "Art", g.art_text(), p.gedaempft);
    kennwert(k, "Adresse", g.ip + (g.hostname.empty() ? "" : " · " + g.hostname + ".local"), p.gedaempft);
    kennwert(k, "WLAN", fmt::signal(g.rssi), p.gedaempft);
    if (g.laufzeit >= 0) kennwert(k, "Laufzeit seit Start", fmt::dauer(g.laufzeit), p.gedaempft);
    if (g.heap >= 0) kennwert(k, "Freier Speicher", fmt::mit_einheit(std::to_string(g.heap / 1024), "kB"), p.gedaempft);
    kennwert(k, "Firmware", g.version.empty() ? std::string(fmt::STRICH) : g.version, p.gedaempft);
    kennwert(k, "Kennung", g.id, p.gedaempft);

    if (g.art == anlage::Art::verteiler) {
        if (const anlage::Etage *e = b.etage(g.id)) {
            abschnitt(c, "Kanäle");
            lv_obj_t *kk = karte(c);
            spalte(kk, 2);
            for (const auto &kn : e->kanaele) kanalzeile(kk, kn);
            text_umbruch(kk, "Je Messgruppe (1+2, 3+4, 5+6, 7+8, 9+10, 11) fährt immer nur ein Kanal.", &f_cap, p.gedaempft);
            lv_obj_t *zz = zeile_neu(kk, 10);
            lv_obj_set_flex_flow(zz, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_row(zz, 10, 0);
            std::string id = g.id;
            auto alle = [id](const char *cmd, const char *titel, const char *text) {
                std::string c2 = cmd;
                rueckfrage(titel, text, titel, [id, c2] { daten::kanal(id, 0, c2.c_str()); });
            };
            knopf(zz, "Alle auf", false, [alle] { alle("open", "Alle auf", "Alle Kanäle öffnen ganz und bleiben im Handbetrieb, bis Sie sie an die Regelung zurückgeben."); });
            knopf(zz, "Alle zu", false, [alle] { alle("close", "Alle zu", "Alle Kanäle schließen und bleiben im Handbetrieb, bis Sie sie an die Regelung zurückgeben."); });
            knopf(zz, "Alle an die Regelung", false, [alle] { alle("auto", "Alle an die Regelung", "Alle Kanäle folgen wieder der Regelung ihres Raums."); });
        }
    } else if (g.art == anlage::Art::heizung) {
        // Fühler am Bus aus dem Rohzustand
        for (const auto &s : daten::staende()) {
            if (s.id != g.id) continue;
            json::Baum st = json::lesen(s.state);
            abschnitt(c, "Fühler am Bus",
                      std::to_string(json::ganz(st.get(), "onewire.assigned")) + " von " + std::to_string(json::ganz(st.get(), "onewire.found")) +
                          " zugeordnet");
            lv_obj_t *kf = karte(c);
            spalte(kf, 2);
            for (const cJSON *pr : json::liste(st.get(), "probes")) {
                lv_obj_t *z = zeile_neu(kf, 12);
                lv_obj_set_style_pad_ver(z, 5, 0);
                std::string rolle = json::text(pr, "role");
                std::string name = rolle.empty() ? json::text(pr, "rom") : verlauf::bezeichnung(*daten::bild(), "fuehler." + rolle);
                lv_obj_t *n = text(z, name, &f_sub, p.tinte);
                lv_obj_set_flex_grow(n, 1);
                float d30 = json::zahl(pr, "delta30_c");
                if (!isnan(d30) && fabsf(d30) >= 0.05f)
                    text(z, fmt::kelvin(d30, 1, true) + "/30 s", &f_mono_k, fabsf(d30) >= 0.4f ? p.waerme : p.gedaempft);
                text(z, fmt::temperatur(json::zahl(pr, "temp_c")), &f_mono, p.tinte);
            }
        }
    }
    // System
    abschnitt(c, "System");
    lv_obj_t *ks = karte(c);
    std::string id = g.id, ort = g.ort;
    lv_obj_t *z = zeile_neu(ks, 12);
    knopf(z, "Neu starten", false, [id, ort] {
        rueckfrage("Neu starten", ort + " startet neu und ist rund 15 Sekunden nicht erreichbar. Die Regelung setzt danach von selbst fort.",
                   "Neu starten", [id] { daten::neustart(id); });
    }, "restart");
    text_umbruch(ks, "Einstellungen, Messfahrt, Sicherung und Firmware erreichen Sie in der Weboberfläche des Geräts unter http://" + g.ip + ".",
                 &f_cap, p.gedaempft);
}

}  // namespace

void geraete_aufbauen(lv_obj_t *inhalt, const std::string &auswahl)
{
    auto b = daten::bild();
    s_auswahl = auswahl;
    s_nb.merken(inhalt, daten::generation());
    kopf_knopf("sparkles", "Fragen", [] { fragen("Geräte"); });
    Spalten sp = doppelseite(inhalt);
    s_nb.spalten(sp);
    std::string gew = auswahl.empty() && !b->geraete.empty() ? b->geraete.front().id : auswahl;
    liste(sp.links, *b, gew);
    if (const anlage::Geraet *g = b->geraet(gew)) eintrag(sp.rechts, *b, *g);
    s_nb.wiederherstellen();
}

void geraete_takt()
{
    if (s_nb.geaendert(daten::generation())) s_nb.neu([](lv_obj_t *i) { geraete_aufbauen(i, s_auswahl); });
}

}  // namespace ui
