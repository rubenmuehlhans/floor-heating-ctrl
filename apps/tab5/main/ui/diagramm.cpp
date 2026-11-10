// Liniendiagramm über der Zeit — vereinfachte Fassung der Verlaufstafeln der App
// (apple/App/Ansichten/Heizung/Verlaufstafeln.swift): Linien, gestrichelte Stufenlinie
// für den Sollwert, Fläche für Anteile, Achse links, Uhrzeiten unten.
//
// Gezeichnet wird im Zeichenereignis des Objekts (lv_draw_line), nicht mit einem Objekt je
// Punkt: 288 Punkte je Linie wären sonst 288 Objekte.
#include <math.h>
#include <algorithm>
#include "fmt.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

struct Daten {
    int64_t von, bis;
    std::vector<Linie> linien;
    float lo, hi;
    bool prozent;
    float schritt = 0.5f;
    std::string einheit;
};

constexpr int ACHSE_B = 62, UNTEN_H = 30;


void zeichnen(lv_event_t *e)
{
    Daten *d = (Daten *)lv_event_get_user_data(e);
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const Palette &p = pal();
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int x0 = a.x1 + ACHSE_B, x1 = a.x2 - 4, y0 = a.y1 + 8, y1 = a.y2 - UNTEN_H;
    if (x1 <= x0 || y1 <= y0) return;
    auto X = [&](int64_t t) { return x0 + (int)((double)(t - d->von) / (double)(d->bis - d->von) * (x1 - x0)); };
    auto Y = [&](float v) { return y1 - (int)((v - d->lo) / (d->hi - d->lo) * (y1 - y0)); };

    // Gitter und Achse
    lv_draw_line_dsc_t gl;
    lv_draw_line_dsc_init(&gl);
    gl.color = p.linie;
    gl.width = 1;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = &f_mono_k;
    ld.color = p.gedaempft;
    int stufen = d->prozent ? 2 : (int)lroundf((d->hi - d->lo) / d->schritt);
    for (int i = 0; i <= stufen; i++) {
        float v = d->lo + (d->hi - d->lo) * i / stufen;
        int y = Y(v);
        gl.p1 = {(lv_value_precise_t)x0, (lv_value_precise_t)y};
        gl.p2 = {(lv_value_precise_t)x1, (lv_value_precise_t)y};
        lv_draw_line(layer, &gl);
        std::string t = d->prozent ? std::to_string((int)lroundf(v * 100)) : fmt::zahl(v, d->schritt < 1 ? 1 : 0);
        ld.text = t.c_str();
        ld.text_local = 1;
        lv_area_t la = {a.x1, y - 11, x0 - 10, y + 11};
        ld.align = LV_TEXT_ALIGN_RIGHT;
        lv_draw_label(layer, &ld, &la);
    }
    // Uhrzeiten bzw. Tage unten
    int64_t spanne = d->bis - d->von;
    int64_t schritt = spanne > 2 * 86400 ? 86400 : spanne > 12 * 3600 ? 6 * 3600 : 3600;
    int64_t versatz = fmt::ortsversatz(d->bis);
    for (int64_t t = ((d->von + versatz) / schritt + 1) * schritt - versatz; t < d->bis; t += schritt) {
        int x = X(t);
        gl.p1 = {(lv_value_precise_t)x, (lv_value_precise_t)y0};
        gl.p2 = {(lv_value_precise_t)x, (lv_value_precise_t)y1};
        gl.opa = 110;
        lv_draw_line(layer, &gl);
        gl.opa = LV_OPA_COVER;
        std::string t2 = schritt >= 86400 ? fmt::wochentag_kurz(t + 43200) : fmt::uhrzeit(t);
        ld.text = t2.c_str();
        ld.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t la = {x - 40, y1 + 6, x + 40, y1 + 28};
        if (schritt >= 86400) la = {x, y1 + 6, x + X(t + schritt) - X(t), y1 + 28};
        if (la.x2 <= a.x2 + 4) lv_draw_label(layer, &ld, &la);
    }

    for (const auto &l : d->linien) {
        if (l.punkte.empty()) continue;
        if (l.flaeche) {
            // Fläche als senkrechte Striche je Punkt
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.bg_color = l.farbe;
            rd.bg_opa = 70;
            int platz = std::max(1, X(d->von + 300) - X(d->von));
            for (const auto &pt : l.punkte) {
                if (isnan(pt.second)) continue;
                int x = X(pt.first), y = Y(pt.second);
                lv_area_t r = {x, std::min(y, y1), x + platz, y1};
                lv_draw_rect(layer, &rd, &r);
            }
        }
        lv_draw_line_dsc_t ln;
        lv_draw_line_dsc_init(&ln);
        ln.color = l.farbe;
        ln.width = l.flaeche ? 2 : 3;
        ln.round_start = ln.round_end = 1;
        if (l.gestrichelt) {
            ln.dash_width = 8;
            ln.dash_gap = 6;
        }
        int64_t luecke = (d->bis - d->von) / 40;
        for (size_t i = 1; i < l.punkte.size(); i++) {
            const auto &a0 = l.punkte[i - 1], &a1 = l.punkte[i];
            if (isnan(a0.second) || isnan(a1.second) || a1.first - a0.first > luecke) continue;
            int xa = X(a0.first), xb = X(a1.first), ya = Y(a0.second), yb = Y(a1.second);
            if (l.stufen) {
                ln.p1 = {(lv_value_precise_t)xa, (lv_value_precise_t)ya};
                ln.p2 = {(lv_value_precise_t)xb, (lv_value_precise_t)ya};
                lv_draw_line(layer, &ln);
                if (ya != yb) {
                    ln.p1 = {(lv_value_precise_t)xb, (lv_value_precise_t)ya};
                    ln.p2 = {(lv_value_precise_t)xb, (lv_value_precise_t)yb};
                    lv_draw_line(layer, &ln);
                }
            } else {
                ln.p1 = {(lv_value_precise_t)xa, (lv_value_precise_t)ya};
                ln.p2 = {(lv_value_precise_t)xb, (lv_value_precise_t)yb};
                lv_draw_line(layer, &ln);
            }
        }
    }
}

void loeschen(lv_event_t *e) { delete (Daten *)lv_event_get_user_data(e); }

float hi_max(const std::vector<Linie> &linien)
{
    float hi = -INFINITY;
    for (const auto &l : linien)
        for (const auto &pt : l.punkte)
            if (!isnan(pt.second)) hi = std::max(hi, pt.second);
    return hi;
}

}  // namespace

lv_obj_t *diagramm(lv_obj_t *parent, int hoehe, int64_t von, int64_t bis, const std::vector<Linie> &linien, float min_spanne,
                   const char *einheit, bool prozent)
{
    Daten *d = new Daten{von, bis, linien, 0, 1, prozent, 0.5f, einheit ? einheit : ""};
    if (!prozent) {
        float lo = INFINITY, hi = -INFINITY;
        for (const auto &l : linien)
            for (const auto &pt : l.punkte)
                if (!isnan(pt.second)) {
                    lo = std::min(lo, pt.second);
                    hi = std::max(hi, pt.second);
                }
        if (!isfinite(lo)) lo = 0, hi = 1;
        // Wertebereich wie die App: mindestens `min_spanne`, auf ein Raster gerundet, das vier
        // Teile mit runden Werten ergibt
        if (hi - lo < min_spanne) {
            float m = (hi + lo) / 2;
            lo = m - min_spanne / 2;
            hi = m + min_spanne / 2;
        }
        static const float RASTER[] = {0.25f, 0.5f, 1, 2, 2.5f, 5, 10, 20, 25, 50};
        float r = 50;
        for (float x : RASTER)
            if ((hi - lo) / x <= 4) {
                r = x;
                break;
            }
        lo = floorf(lo / r) * r;
        hi = lo + 4 * r;
        while (hi < hi_max(linien)) hi += r;
        d->lo = lo;
        d->hi = hi;
        d->schritt = r;
    }
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_size(o, LV_PCT(100), hoehe);
    lv_obj_add_event_cb(o, zeichnen, LV_EVENT_DRAW_MAIN, d);
    lv_obj_add_event_cb(o, loeschen, LV_EVENT_DELETE, d);
    return o;
}

}  // namespace ui
