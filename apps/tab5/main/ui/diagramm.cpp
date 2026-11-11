// Liniendiagramm über der Zeit — vereinfachte Fassung der Verlaufstafeln der App
// (apple/App/Ansichten/Heizung/Verlaufstafeln.swift): Linien, gestrichelte Stufenlinie
// für den Sollwert, Fläche für Anteile, Achse links, Uhrzeiten unten.
//
// ⚠️⚠️ Gezeichnet wird EINMAL in einen Bildpuffer (lv_canvas), nicht im Zeichenereignis: dort
// zeichnete LVGL bei jedem Bild alle Linien neu, auch bei jedem Schritt des Bildlaufs — bei
// 288 Punkten je Reihe und bis zu zehn Reihen tausende kantengeglättete Linienstücke, und die
// Seiten mit Diagrammen ruckelten (am Gerät, 11.11.2026). Ein fertiges Bild kopiert die PPA des
// P4 in Hardware. Die Puffer bleiben zwischengespeichert, solange sich Daten und Größe nicht
// ändern; so kostet auch der Neuaufbau einer Seite bei neuen Messwerten kein Neuzeichnen.
#include <math.h>
#include <string.h>
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


void zeichnen(lv_layer_t *layer, const lv_area_t &a, const Daten *d)
{
    const Palette &p = pal();
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

// ---- Zwischenspeicher der fertigen Bilder ----------------------------------------------
struct Eintrag {
    uint64_t schluessel;
    lv_draw_buf_t *buf;
    int benutzt;       // Diagramme, die den Puffer gerade zeigen
    uint32_t zuletzt;  // lv_tick
};
std::vector<Eintrag> s_cache;
constexpr size_t CACHE_MAX = 16;

uint64_t mischen64(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001B3ull; }

uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

uint64_t schluessel(const Daten &d, int b, int h)
{
    uint64_t k = 0xCBF29CE484222325ull;
    k = mischen64(k, (uint64_t)b << 32 | (uint32_t)h);
    k = mischen64(k, dunkel());
    k = mischen64(k, (uint64_t)(d.bis - d.von));
    k = mischen64(k, (uint64_t)(d.bis / 300));   // Achse rückt mit jedem Platz von fünf Minuten
    k = mischen64(k, bits(d.lo));
    k = mischen64(k, bits(d.hi));
    for (const auto &l : d.linien) {
        k = mischen64(k, lv_color_to_u32(l.farbe) ^ (uint32_t)l.gestrichelt << 1 ^ (uint32_t)l.stufen << 2 ^ (uint32_t)l.flaeche << 3);
        k = mischen64(k, l.punkte.size());
        for (const auto &pt : l.punkte) k = mischen64(k, (uint64_t)pt.first << 32 ^ bits(pt.second));
    }
    return k;
}

void freigeben(lv_event_t *e)
{
    lv_draw_buf_t *buf = (lv_draw_buf_t *)lv_event_get_user_data(e);
    for (auto &c : s_cache)
        if (c.buf == buf) c.benutzt--;
}

// Platz schaffen: ungenutzte Einträge, die am längsten nicht gezeigt wurden, zuerst.
void aufraeumen()
{
    while (s_cache.size() >= CACHE_MAX) {
        int alt = -1;
        for (int i = 0; i < (int)s_cache.size(); i++)
            if (s_cache[i].benutzt <= 0 && (alt < 0 || s_cache[i].zuletzt < s_cache[alt].zuletzt)) alt = i;
        if (alt < 0) return;   // alle sichtbar: dann wächst der Speicher kurz über die Grenze
        lv_draw_buf_destroy(s_cache[alt].buf);
        s_cache.erase(s_cache.begin() + alt);
    }
}

float hi_max(const std::vector<Linie> &linien)
{
    float hi = -INFINITY;
    for (const auto &l : linien)
        for (const auto &pt : l.punkte)
            if (!isnan(pt.second)) hi = std::max(hi, pt.second);
    return hi;
}

// Bild aus dem Zwischenspeicher im Platzhalter zeigen
void zeigen(lv_obj_t *platz, uint64_t key, lv_draw_buf_t *buf)
{
    for (auto &c : s_cache)
        if (c.schluessel == key) {
            c.benutzt++;
            c.zuletzt = lv_tick_get();
        }
    lv_obj_t *c = lv_canvas_create(platz);
    lv_canvas_set_draw_buf(c, buf);
    lv_obj_add_event_cb(c, freigeben, LV_EVENT_DELETE, buf);
}

struct Auftrag {
    lv_obj_t *platz;
    Daten *d;
    int breite, hoehe;
    uint64_t key;
};
std::vector<Auftrag> s_auftraege;
lv_timer_t *s_timer;

// Seite verlassen, bevor das Diagramm gezeichnet war: Auftrag streichen
void auftrag_weg(lv_event_t *e)
{
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    for (auto it = s_auftraege.begin(); it != s_auftraege.end(); ++it)
        if (it->platz == o) {
            delete it->d;
            s_auftraege.erase(it);
            return;
        }
}

void nachzeichnen(lv_timer_t *)
{
    if (s_auftraege.empty()) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
        return;
    }
    // ⚠️ Nicht während der Finger auf dem Glas ist: dann hat der Bildlauf Vorrang
    if (lv_indev_active()) return;
    Auftrag a = s_auftraege.front();
    s_auftraege.erase(s_auftraege.begin());
    lv_obj_remove_event_cb(a.platz, auftrag_weg);
    lv_draw_buf_t *buf = nullptr;
    for (auto &c : s_cache)
        if (c.schluessel == a.key) buf = c.buf;   // inzwischen von einem gleichen gezeichnet
    if (!buf) {
        aufraeumen();
        buf = lv_draw_buf_create(a.breite, a.hoehe, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (!buf) {   // kein Speicher: leer lassen statt abzustürzen
            delete a.d;
            return;
        }
        lv_obj_t *c = lv_canvas_create(a.platz);
        lv_canvas_set_draw_buf(c, buf);
        lv_canvas_fill_bg(c, pal().flaeche, LV_OPA_COVER);
        lv_layer_t layer;
        lv_canvas_init_layer(c, &layer);
        lv_area_t ar = {0, 0, a.breite - 1, a.hoehe - 1};
        zeichnen(&layer, ar, a.d);
        lv_canvas_finish_layer(c, &layer);
        s_cache.push_back({a.key, buf, 1, lv_tick_get()});
        lv_obj_add_event_cb(c, freigeben, LV_EVENT_DELETE, buf);
    } else zeigen(a.platz, a.key, buf);
    delete a.d;
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
    // ⚠️ Der laufende Platz fehlt: sein Mittel ändert sich mit jeder Abfrage (alle 5 s), und
    // mit ihm verfehlte jedes Diagramm den Zwischenspeicher und wurde neu gezeichnet.
    int64_t laufend = bis - bis % 300;
    for (auto &l : d->linien)
        while (!l.punkte.empty() && l.punkte.back().first >= laufend) l.punkte.pop_back();
    // Breite aus dem Elternelement (Karte); der Puffer braucht feste Maße
    lv_obj_update_layout(parent);
    int breite = lv_obj_get_content_width(parent);
    if (breite <= 0) breite = 480;
    // Mehr Punkte, als die Breite Pixel hat, zeichnen nichts Sichtbares mehr
    int ziel = (breite - ACHSE_B) / 2;
    for (auto &l : d->linien)
        if ((int)l.punkte.size() > ziel && ziel > 10) {
            std::vector<std::pair<int64_t, float>> k;
            size_t schritt = (l.punkte.size() + ziel - 1) / ziel;
            for (size_t i = 0; i < l.punkte.size(); i += schritt) {
                double s2 = 0;
                int n = 0;
                for (size_t j = i; j < i + schritt && j < l.punkte.size(); j++)
                    if (!isnan(l.punkte[j].second)) s2 += l.punkte[j].second, n++;
                k.push_back({l.punkte[i].first, n ? (float)(s2 / n) : NAN});
            }
            l.punkte.swap(k);
        }
    uint64_t key = schluessel(*d, breite, hoehe);
    lv_draw_buf_t *buf = nullptr;
    for (auto &c : s_cache)
        if (c.schluessel == key) buf = c.buf;
    // Platzhalter in der Größe des Bildes; das Bild kommt sofort aus dem Zwischenspeicher oder
    // wird im Takt nachgezeichnet (eines je Durchlauf), damit die Seite sofort erscheint.
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_size(o, breite, hoehe);
    if (buf) {
        zeigen(o, key, buf);
        delete d;
    } else {
        s_auftraege.push_back({o, d, breite, hoehe, key});
        lv_obj_add_event_cb(o, auftrag_weg, LV_EVENT_DELETE, nullptr);
        if (!s_timer) s_timer = lv_timer_create(nachzeichnen, 15, nullptr);
    }
    return o;
}

}  // namespace ui
