// Stellrad für den Sollwert — wie apple/App/Ansichten/Gemeinsam/Stellrad.swift.
//
// Eine waagerecht wischbare Skala 5…35 °C in Schritten von 0,5 rastet ein; die feste Nadel
// in der Mitte zeigt den Wert. Ganze Grade tragen einen langen Strich und ihre Zahl, halbe
// einen kurzen. An den Rändern blendet die Skala aus.
//
// ⚠️ Nur eine Bewegung durch die Hand setzt den Wert, nicht Aufbau oder Nachführung.
// Gesendet wird erst 900 ms nach dem letzten Rastschritt (daten::sollwert).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "daten.h"
#include "fmt.h"
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

constexpr float MIN = 5, MAX = 35, SCHRITT = 0.5f;
constexpr int TEILUNG = 21;   // px je 0,5 K (App: 14 pt)
constexpr int HOEHE_RAD = 96;
constexpr int SCHRITTE = (int)((MAX - MIN) / SCHRITT);

struct Rad {
    std::function<void(float)> geaendert;
    float wert;
    bool hand = false;        // Bewegung durch die Hand läuft
    bool nachfuehren = false; // Bildlauf von außen
    lv_obj_t *lauf = nullptr;
};

float wert_aus(lv_obj_t *lauf) { return MIN + (float)lroundf((float)lv_obj_get_scroll_x(lauf) / TEILUNG) * SCHRITT; }

void einrasten(Rad *r)
{
    float w = wert_aus(r->lauf);
    w = fminf(MAX, fmaxf(MIN, w));
    int x = (int)lroundf((w - MIN) / SCHRITT) * TEILUNG;
    if (lv_obj_get_scroll_x(r->lauf) != x) {
        r->nachfuehren = true;
        lv_obj_scroll_to_x(r->lauf, x, LV_ANIM_ON);
    }
}

void ereignis(lv_event_t *e)
{
    Rad *r = (Rad *)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    switch (c) {
    case LV_EVENT_PRESSED:
        r->hand = true;
        r->nachfuehren = false;
        bedienung_merken();
        break;
    case LV_EVENT_SCROLL: {
        if (r->nachfuehren && !r->hand) break;
        float w = fminf(MAX, fmaxf(MIN, wert_aus(r->lauf)));
        if (r->hand && w != r->wert) {
            r->wert = w;
            bedienung_merken();
            if (r->geaendert) r->geaendert(w);
        }
        break;
    }
    case LV_EVENT_SCROLL_END:
        // nach dem Auslaufen des Wurfs: einrasten
        if (r->hand) {
            r->hand = false;
            einrasten(r);
        }
        r->nachfuehren = false;
        break;
    case LV_EVENT_RELEASED:
        // Das Rad läuft nach dem Loslassen noch aus; eingerastet wird bei SCROLL_END.
        if (!lv_obj_is_scrolling(r->lauf)) {
            r->hand = false;
            einrasten(r);
        }
        break;
    case LV_EVENT_SHORT_CLICKED: {
        // Tipp auf einen Strich springt auf diesen Wert
        lv_point_t pt;
        lv_indev_get_point(lv_indev_active(), &pt);
        lv_area_t a;
        lv_obj_get_coords(r->lauf, &a);
        int mitte = (a.x1 + a.x2) / 2;
        float w = fminf(MAX, fmaxf(MIN, r->wert + (float)lroundf((float)(pt.x - mitte) / TEILUNG) * SCHRITT));
        if (w != r->wert) {
            r->wert = w;
            if (r->geaendert) r->geaendert(w);
            r->nachfuehren = true;
            lv_obj_scroll_to_x(r->lauf, (int)lroundf((w - MIN) / SCHRITT) * TEILUNG, LV_ANIM_ON);
        }
        r->hand = false;
        break;
    }
    case LV_EVENT_DELETE: delete r; break;
    default: break;
    }
}

// Striche und Zahlen der Skala, nur im sichtbaren Ausschnitt des Bildlaufs
void skala_zeichnen(lv_event_t *e)
{
    const Palette &p = pal();
    lv_obj_t *o = (lv_obj_t *)lv_event_get_current_target(e);
    lv_obj_t *lauf = lv_obj_get_parent(o);
    lv_layer_t *layer = lv_event_get_layer(e);
    int rand = (int)(intptr_t)lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int32_t sx = lv_obj_get_scroll_x(lauf), b = lv_obj_get_width(lauf);
    int von = (sx - rand) / TEILUNG - 2, bis = (sx - rand + b) / TEILUNG + 2;
    if (von < 0) von = 0;
    if (bis > SCHRITTE) bis = SCHRITTE;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 2;
    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.font = &f_mono_k;
    l.color = p.gedaempft;
    l.align = LV_TEXT_ALIGN_CENTER;
    char zahl[4];
    for (int i = von; i <= bis; i++) {
        bool ganz = i % 2 == 0;
        int sb = ganz ? 3 : 2, sh = ganz ? 38 : 20;
        int x = a.x1 + rand + i * TEILUNG - sb / 2;
        r.bg_color = ganz ? p.gedaempft : p.linie_stark;
        lv_area_t s = {x, a.y1 + 4, x + sb - 1, a.y1 + 4 + sh - 1};
        lv_draw_rect(layer, &r, &s);
        if (ganz) {
            snprintf(zahl, sizeof zahl, "%d", (int)(MIN + i * SCHRITT));
            l.text = zahl;
            l.text_local = 1;
            int mx = a.x1 + rand + i * TEILUNG;
            lv_area_t t = {mx - 20, a.y1 + 51, mx + 20, a.y1 + 51 + f_mono_k.line_height};
            lv_draw_label(layer, &l, &t);
        }
    }
}

}  // namespace

lv_obj_t *stellrad(lv_obj_t *parent, float wert, bool gedimmt, std::function<void(float)> geaendert)
{
    const Palette &p = pal();
    if (isnan(wert)) wert = 20;
    lv_obj_t *huelle = flaeche(parent);
    lv_obj_set_size(huelle, LV_PCT(100), HOEHE_RAD);
    if (gedimmt) lv_obj_set_style_opa(huelle, 115, 0);

    lv_obj_t *lauf = flaeche(huelle);
    lv_obj_set_size(lauf, LV_PCT(100), HOEHE_RAD);
    lv_obj_set_scrollable(lauf, true);
    lv_obj_set_clickable(lauf, true);
    lv_obj_set_scroll_dir(lauf, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(lauf, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(lauf, LV_SCROLL_SNAP_NONE);
    lv_obj_update_layout(huelle);
    int b = lv_obj_get_width(huelle);
    if (b <= 0) b = 480;
    // Seitliche Ränder: der erste und der letzte Strich kommen unter die Nadel
    int rand = b / 2;
    // ⚠️⚠️ Die Skala ist EIN Objekt, das nur die sichtbaren Striche zeichnet. Mit einem Objekt je
    // Strich und Zahl (92 je Rad, acht Räder auf der Seite Räume) dauerten Aufbau und Abbau der
    // Seite am Gerät je rund 1,3 s (gemessen am 11.11.2026).
    lv_obj_t *skala = flaeche(lauf);
    lv_obj_set_size(skala, SCHRITTE * TEILUNG + 2 * rand, HOEHE_RAD);
    lv_obj_add_event_cb(skala, skala_zeichnen, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)rand);
    // Randverlauf in der Hintergrundfarbe der Karte
    for (int seite = 0; seite < 2; seite++) {
        lv_obj_t *v = flaeche(huelle);
        lv_obj_set_size(v, b * 18 / 100, HOEHE_RAD);
        lv_obj_align(v, seite ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(v, p.flaeche, 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_color(v, p.flaeche, 0);
        lv_obj_set_style_bg_grad_dir(v, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_main_opa(v, seite ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_opa(v, seite ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    // Nadel: Kapsel 3 × 34 pt in Akzentfarbe, oben mittig
    lv_obj_t *nadel = flaeche(huelle);
    lv_obj_set_size(nadel, 5, 52);
    lv_obj_align(nadel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(nadel, 3, 0);
    lv_obj_set_style_bg_color(nadel, p.akzent, 0);
    lv_obj_set_style_bg_opa(nadel, LV_OPA_COVER, 0);

    Rad *r = new Rad{std::move(geaendert), wert};
    r->lauf = lauf;
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_SCROLL, LV_EVENT_SCROLL_END, LV_EVENT_RELEASED, LV_EVENT_SHORT_CLICKED})
        lv_obj_add_event_cb(lauf, ereignis, c, r);
    lv_obj_add_event_cb(lauf, ereignis, LV_EVENT_DELETE, r);
    lv_obj_update_layout(lauf);
    r->nachfuehren = true;
    lv_obj_scroll_to_x(lauf, (int)lroundf((fminf(MAX, fmaxf(MIN, wert)) - MIN) / SCHRITT) * TEILUNG, LV_ANIM_OFF);
    r->nachfuehren = false;
    lv_obj_set_user_data(huelle, lauf);
    return huelle;
}

void stellrad_setzen(lv_obj_t *rad, float wert)
{
    lv_obj_t *lauf = (lv_obj_t *)lv_obj_get_user_data(rad);
    if (!lauf || isnan(wert)) return;
    lv_obj_scroll_to_x(lauf, (int)lroundf((fminf(MAX, fmaxf(MIN, wert)) - MIN) / SCHRITT) * TEILUNG, LV_ANIM_ON);
}

lv_obj_t *sollwert_stellrad(lv_obj_t *parent, const std::string &raum_id, bool mit_hinweis)
{
    const Palette &p = pal();
    auto b = daten::bild();
    const anlage::Raum *r = b->raum(raum_id);
    float soll = daten::soll_ausstehend(raum_id);
    if (isnan(soll) && r) soll = r->soll;
    bool gedimmt = r && !r->an;
    lv_obj_t *s = spalte_neu(parent, 10);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *kopf = spalte_neu(s, 2);
    lv_obj_set_flex_align(kopf, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (gedimmt) lv_obj_set_style_opa(kopf, 115, 0);
    lv_obj_t *zahl = messzahl(kopf, fmt::zahl(soll), "°C", &f_messwert, p.akzent);
    kicker(kopf, "Sollwert", p.gedaempft);
    lv_obj_t *wl = lv_obj_get_child(zahl, 0);
    std::string id = raum_id;
    stellrad(s, soll, gedimmt, [id, wl](float w) {
        lv_label_set_text(wl, fmt::zahl(w).c_str());
        daten::sollwert(id, w);
    });
    if (mit_hinweis) {
        lv_obj_t *h = text_umbruch(s, "Wird an den Verteiler gesendet, sobald das Rad steht.", &f_mono_k, p.gedaempft);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    }
    return s;
}

}  // namespace ui
