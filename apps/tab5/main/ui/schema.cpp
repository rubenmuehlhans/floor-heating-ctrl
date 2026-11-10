// Anlagenschema — Querfassung wie apple/App/Ansichten/Heizung/Anlagenschema.swift
// (Entwurf 780 × 410, skaliert auf die Breite der Karte).
//
// Gezeichnet im Zeichenereignis: Leitungen, Kessel, Speicher, Mischer, Pumpen und die
// Messwerte dort, wo der Fühler sitzt.
#include <math.h>
#include "daten.h"
#include "fmt.h"
#include "stil.h"
#include "ui.h"

namespace ui {

lv_obj_t *anlagenschema(lv_obj_t *parent, int breite);

namespace {

struct Zeichner {
    lv_layer_t *layer;
    int ox, oy;
    float s;
    int X(float x) const { return ox + (int)lroundf(x * s); }
    int Y(float y) const { return oy + (int)lroundf(y * s); }

    void linie(float x1, float y1, float x2, float y2, lv_color_t f, float b = 3)
    {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = f;
        d.width = (int32_t)lroundf(b * s);
        d.round_start = d.round_end = 1;
        d.p1 = {(lv_value_precise_t)X(x1), (lv_value_precise_t)Y(y1)};
        d.p2 = {(lv_value_precise_t)X(x2), (lv_value_precise_t)Y(y2)};
        lv_draw_line(layer, &d);
    }
    void rechteck(float x, float y, float w, float h, float r, lv_color_t fuell, lv_color_t rand, float rb, lv_opa_t opa = LV_OPA_COVER)
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = (int32_t)lroundf(r * s);
        d.bg_color = fuell;
        d.bg_opa = opa;
        d.border_color = rand;
        d.border_width = (int32_t)lroundf(rb * s);
        d.border_opa = rb > 0 ? LV_OPA_COVER : LV_OPA_TRANSP;
        lv_area_t a = {X(x), Y(y), X(x + w) - 1, Y(y + h) - 1};
        lv_draw_rect(layer, &d, &a);
    }
    void kreis(float mx, float my, float r, lv_color_t fuell, lv_color_t rand, float rb)
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_color = fuell;
        d.border_color = rand;
        d.border_width = (int32_t)lroundf(rb * s);
        lv_area_t a = {X(mx - r), Y(my - r), X(mx + r), Y(my + r)};
        lv_draw_rect(layer, &d, &a);
    }
    void dreieck(float x1, float y1, float x2, float y2, float x3, float y3, lv_color_t f)
    {
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        d.color = f;
        d.opa = LV_OPA_COVER;
        d.p[0] = {(lv_value_precise_t)X(x1), (lv_value_precise_t)Y(y1)};
        d.p[1] = {(lv_value_precise_t)X(x2), (lv_value_precise_t)Y(y2)};
        d.p[2] = {(lv_value_precise_t)X(x3), (lv_value_precise_t)Y(y3)};
        lv_draw_triangle(layer, &d);
    }
    // anker: 0 Mitte, 1 links
    void schrift(const std::string &t, float x, float y, const lv_font_t *f, lv_color_t farbe, int anker, int sperrung = 0)
    {
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = f;
        d.color = farbe;
        d.letter_space = sperrung;
        d.text = t.c_str();
        d.text_local = 1;
        d.align = anker ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
        int h = f->line_height, b = 420;
        int cx = X(x), cy = Y(y);
        lv_area_t a = anker ? lv_area_t{cx, cy - h / 2, cx + b, cy + h / 2} : lv_area_t{cx - b / 2, cy - h / 2, cx + b / 2, cy + h / 2};
        lv_draw_label(layer, &d, &a);
    }
    void kicker(const std::string &t, float x, float y, int anker = 0, lv_color_t f = pal().gedaempft)
    {
        schrift(fmt::versal(t), x, y, &f_kicker, f, anker, 2);
    }
    void wert(const std::string &t, float x, float y, lv_color_t f, int anker = 0) { schrift(t, x, y, &f_wert, f, anker); }

    void pumpe(float mx, float my, bool laeuft, bool gestoert)
    {
        const Palette &p = pal();
        lv_color_t rand = gestoert ? p.stoerung : laeuft ? p.waerme : p.linie_stark;
        kreis(mx, my, 10, p.flaeche, rand, gestoert || laeuft ? 2 : 1.5f);
        dreieck(mx - 4, my - 6, mx + 6, my, mx - 4, my + 6, laeuft && !gestoert ? p.waerme : p.gedaempft);
        if (gestoert) {
            kreis(mx + 10, my - 9, 5, p.stoerung, p.stoerung, 0);
            linie(mx + 8, my - 11, mx + 12, my - 7, lv_color_white(), 1.4f);
            linie(mx + 12, my - 11, mx + 8, my - 7, lv_color_white(), 1.4f);
        }
    }
    void mischer(float mx, float my)
    {
        const Palette &p = pal();
        kreis(mx, my, 14, p.flaeche, p.linie_stark, 1.5f);
        dreieck(mx - 10, my - 7, mx, my, mx - 10, my + 7, p.waerme);
        dreieck(mx + 10, my - 7, mx, my, mx + 10, my + 7, p.waerme);
        linie(mx, my + 1, mx - 5, my + 10, p.kaelte, 1.3f);
        linie(mx - 5, my + 10, mx + 5, my + 10, p.kaelte, 1.3f);
        linie(mx + 5, my + 10, mx, my + 1, p.kaelte, 1.3f);
    }
};

bool relais_gestoert(const anlage::Relais &r) { return r.bekannt && !r.online && !r.stromlos && r.weg != "keiner"; }

void zeichnen(lv_event_t *e)
{
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const Palette &p = pal();
    auto b = daten::bild();
    float s = (float)(a.x2 - a.x1 + 1) / 780.f;
    Zeichner z{lv_event_get_layer(e), a.x1, a.y1 - (int)lroundf(56 * s), s};
    const auto &k = b->kessel;
    // Abgas
    z.linie(150, 150, 150, 92, p.waerme);
    z.kicker("Abgas", 168, 84, 1);
    z.wert(fmt::temperatur(k.abgas), 168, 108, p.waerme, 1);
    // Kessel mit Flamme
    z.rechteck(75, 150, 150, 160, 10, p.vertieft, p.linie_stark, 1.5f);
    z.schrift(sym("flame"), 150, 230, &f_sym48, k.laeuft ? p.waerme : mischen(p.waerme, p.vertieft, 190), 0);
    z.kicker("Ölkessel", 150, 334);
    // Kesselkreis
    z.linie(225, 185, 385, 185, p.waerme);
    z.kicker("Vorlauf", 312, 166);
    z.wert(fmt::temperatur(k.vorlauf), 312, 206, p.waerme);
    z.linie(385, 275, 225, 275, p.kaelte);
    z.kicker("Rücklauf", 312, 256);
    z.wert(fmt::temperatur(k.ruecklauf), 312, 296, p.kaelte);
    z.pumpe(248, 275, b->kkp.laeuft, b->kkp.vorhanden && relais_gestoert(b->kkp.relais));
    // Speicher mit Füllstand am rechten Innenrand
    z.rechteck(385, 95, 110, 250, 14, p.flaeche, p.linie_stark, 1.5f);
    z.rechteck(390, 100, 100, 240, 10, p.waerme_weich, p.waerme_weich, 0);
    float ladung = isnan(b->speicher.ladung) ? 0 : fminf(fmaxf(b->speicher.ladung, 0), 1);
    if (ladung > 0) z.rechteck(485, 100 + 240 * (1 - ladung), 5, 240 * ladung, 2, p.waerme, p.waerme, 0);
    z.kicker("Pufferspeicher", 440, 76);
    z.kicker("Oben", 440, 148);
    z.wert(fmt::temperatur(b->speicher.temperatur), 440, 172, p.waerme);
    z.kicker("Ladung", 440, 262);
    z.wert(fmt::prozent(b->speicher.ladung), 440, 286, p.tinte);
    // Heizkreise (höchstens zwei, wie die App)
    for (size_t i = 0; i < b->heizkreise.size() && i < 2; i++) {
        const auto &h = b->heizkreise[i];
        float vl = i == 0 ? 150 : 290, rl = vl + 50;
        z.linie(495, vl, 680, vl, p.waerme);
        z.linie(680, rl, 495, rl, p.kaelte);
        z.linie(568, vl + 14, 568, rl, p.kaelte);
        z.mischer(568, vl);
        z.pumpe(630, vl, h.pumpe, relais_gestoert(h.relais));
        z.kicker("Vorlauf", 690, vl - 13, 1);
        z.wert(fmt::temperatur(h.vorlauf), 690, vl + 10, p.waerme, 1);
        z.kicker("Rücklauf", 690, rl - 13, 1);
        z.wert(fmt::temperatur(h.ruecklauf), 690, rl + 10, p.kaelte, 1);
        std::string versorgt;
        for (size_t j = 0; j < h.versorgt.size(); j++)
            versorgt += (j ? (j + 1 == h.versorgt.size() ? " und " : ", ") : "") + h.versorgt[j];
        z.kicker(versorgt.empty() ? h.name : versorgt, 588, rl + 25);
        if (relais_gestoert(h.relais)) z.kicker("Relais nicht erreichbar", 588, rl + 43, 0, p.stoerung);
        else if (h.gesperrt) z.kicker("von der Kesselregelung gesperrt", 588, rl + 43, 0, p.warnung);
    }
}

}  // namespace

lv_obj_t *anlagenschema(lv_obj_t *parent, int breite)
{
    lv_obj_t *o = flaeche(parent);
    // Entwurf 780 × 410; oben fehlt nichts über y = 60, deshalb 56 abgeschnitten
    lv_obj_set_size(o, breite, (int)lroundf(breite * 360.f / 780.f));
    lv_obj_add_event_cb(o, zeichnen, LV_EVENT_DRAW_MAIN, nullptr);
    return o;
}

}  // namespace ui
