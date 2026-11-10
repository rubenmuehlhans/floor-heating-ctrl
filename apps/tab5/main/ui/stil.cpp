#include "stil.h"

#include <math.h>
#include <string.h>
#include "fmt.h"
#include "symbole.h"

namespace ui {

static lv_color_t hex(uint32_t v) { return lv_color_hex(v); }

// Farbsätze der App (Assets.xcassets), hell und dunkel
static const Palette HELL = {
    hex(0x10646F), hex(0xD2542A), hex(0x3B6EA5), hex(0x3E8A57), hex(0x96690F), hex(0xB3261E),
    hex(0xEEF0EE), hex(0xFFFFFF), hex(0xF6F8F6), hex(0x141817), hex(0x697370), hex(0x98A19D), hex(0xDDE1DE), hex(0xC6CCC8),
    hex(0xE3EEF0), hex(0xFBEAE3), hex(0xE5EDF6), hex(0xFFFFFF)};
static const Palette DUNKEL = {
    hex(0x57B6C2), hex(0xE8794B), hex(0x6F9FD6), hex(0x5FB37B), hex(0xC9993C), hex(0xE4695E),
    hex(0x121514), hex(0x1A1E1D), hex(0x161A19), hex(0xE8ECEA), hex(0x98A29E), hex(0x6D7773), hex(0x2B312F), hex(0x3B423F),
    hex(0x173338), hex(0x33201A), hex(0x1A2836), hex(0x0B1A1C)};

static bool s_dunkel = true;
void dunkel_setzen(bool d) { s_dunkel = d; }
bool dunkel() { return s_dunkel; }
const Palette &pal() { return s_dunkel ? DUNKEL : HELL; }

lv_color_t mischen(lv_color_t f, lv_color_t g, uint8_t d) { return lv_color_mix(f, g, d); }

// ---- Symbole -----------------------------------------------------------------------

const char *lucide(const char *name)
{
    int lo = 0, hi = SYMBOLE_N - 1;
    while (name && lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(name, SYMBOLE[mid].name);
        if (c == 0) return SYMBOLE[mid].utf8;
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return name && strcmp(name, "circle") ? lucide("circle") : "";
}

const char *sym(const char *sf)
{
    // SF-Symbole der App → Lucide. Varianten mit .fill fallen auf die Grundform.
    static const struct { const char *sf, *lu; } T[] = {
        {"antenna.radiowaves.left.and.right", "antenna"}, {"arrow.clockwise", "refresh-cw"},
        {"arrow.counterclockwise", "rotate-ccw"}, {"arrow.down.to.line", "arrow-down-to-line"},
        {"arrow.left.and.right", "move-horizontal"}, {"arrow.right", "arrow-right"},
        {"arrow.right.circle", "circle-arrow-right"}, {"arrow.triangle.2.circlepath", "repeat"}, {"arrow.up", "arrow-up"},
        {"arrow.up.circle", "circle-arrow-up"}, {"arrow.up.to.line", "arrow-up-to-line"}, {"arrow.uturn.backward", "undo-2"},
        {"bell", "bell"}, {"bell.badge", "bell-ring"}, {"bell.slash", "bell-off"}, {"brain", "brain"},
        {"chart.bar.xaxis", "chart-column"}, {"chart.dots.scatter", "chart-scatter"}, {"chart.xyaxis.line", "chart-line"},
        {"checkmark", "check"}, {"checkmark.circle", "circle-check"}, {"checkmark.seal", "badge-check"},
        {"checkmark.shield", "shield-check"}, {"chevron.down", "chevron-down"}, {"chevron.left", "chevron-left"},
        {"chevron.right", "chevron-right"}, {"chevron.up", "chevron-up"}, {"circle", "circle"},
        {"clock", "clock"}, {"clock.arrow.circlepath", "history"}, {"clock.badge.exclamationmark", "clock-alert"},
        {"cpu", "cpu"}, {"cylinder.split.1x2", "cylinder"}, {"doc.text", "file-text"},
        {"doc.text.magnifyingglass", "file-search"}, {"dot.radiowaves.left.and.right", "radio"}, {"drop", "droplet"},
        {"drop.triangle", "droplet"}, {"ellipsis", "ellipsis"}, {"exclamationmark.circle", "circle-alert"},
        {"exclamationmark.triangle", "triangle-alert"}, {"externaldrive", "hard-drive"}, {"eye", "eye"},
        {"eye.slash", "eye-off"}, {"fanblades", "fan"}, {"flame", "flame"}, {"gauge", "gauge"}, {"gear", "settings"},
        {"gearshape", "settings"}, {"hand.raised", "hand"}, {"heat.waves", "waves"}, {"hourglass", "hourglass"},
        {"house", "house"}, {"info.circle", "info"}, {"key", "key"}, {"list.bullet.rectangle", "list"}, {"lock", "lock"},
        {"moon", "moon"}, {"paperplane", "send"}, {"play.circle", "circle-play"}, {"plus", "plus"},
        {"plus.circle", "circle-plus"}, {"power", "power"}, {"powerplug", "plug"}, {"record.circle", "circle-dot"},
        {"restart", "rotate-ccw"}, {"ruler", "ruler"}, {"scope", "crosshair"}, {"sensor", "radar"},
        {"shippingbox", "package"}, {"slider.horizontal.3", "sliders-horizontal"}, {"slider.vertical.3", "sliders-vertical"},
        {"snowflake", "snowflake"}, {"sparkles", "sparkles"}, {"square.and.arrow.down", "download"},
        {"square.and.pencil", "square-pen"}, {"square.grid.2x2", "layout-grid"}, {"square.split.2x2", "layout-grid"},
        {"stethoscope", "stethoscope"}, {"stop", "square"}, {"sun.max", "sun"}, {"switch.2", "toggle-left"},
        {"text.bubble", "message-square-text"}, {"theatermasks", "drama"}, {"thermometer.medium", "thermometer"},
        {"thermometer.snowflake", "thermometer-snowflake"}, {"trash", "trash-2"}, {"wand.and.stars", "wand-sparkles"},
        {"waveform.path.ecg", "activity"}, {"wifi", "wifi"}, {"wifi.exclamationmark", "wifi-off"},
        {"wifi.router", "router"}, {"wrench.and.screwdriver", "wrench"}, {"xmark", "x"}, {"xmark.circle", "circle-x"},
        {"xmark.octagon", "octagon-x"}, {"delete.left", "delete"}, {"shift", "arrow-big-up"}, {"calendar", "calendar"},
        {"zap", "zap"}, {"cloud", "cloud"}, {"icloud.slash", "cloud-off"},
    };
    if (!sf) return lucide("circle");
    char basis[64];
    strncpy(basis, sf, sizeof basis - 1);
    basis[sizeof basis - 1] = 0;
    size_t n = strlen(basis);
    if (n > 5 && !strcmp(basis + n - 5, ".fill")) basis[n - 5] = 0;
    for (const auto &e : T)
        if (!strcmp(e.sf, basis)) return lucide(e.lu);
    return lucide(basis);   // vielleicht schon ein Lucide-Name
}

// ---- Bausteine -----------------------------------------------------------------------

namespace {
struct Rueckruf {
    std::function<void()> f;
};
void klick_cb(lv_event_t *e)
{
    auto *r = (Rueckruf *)lv_event_get_user_data(e);
    if (r && r->f) {
        auto f = r->f;   // ⚠️ Kopie: der Rückruf kann die Seite (und damit r) löschen
        f();
    }
}
void loesch_cb(lv_event_t *e) { delete (Rueckruf *)lv_event_get_user_data(e); }
}  // namespace

void bei_klick(lv_obj_t *o, std::function<void()> f)
{
    auto *r = new Rueckruf{std::move(f)};
    lv_obj_set_clickable(o, true);
    lv_obj_add_event_cb(o, klick_cb, LV_EVENT_CLICKED, r);
    lv_obj_add_event_cb(o, loesch_cb, LV_EVENT_DELETE, r);
}

lv_obj_t *flaeche(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(o, false);
    // ⚠️ Flächen nehmen keine Berührung an, sonst fängt jeder Zwischenbehälter den Tipp ab
    // (camperSense, tab5/CLAUDE.md Falle 8). Wer antippbar sein soll, setzt es ausdrücklich.
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *karte(lv_obj_t *parent, bool betont)
{
    const Palette &p = pal();
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_width(o, LV_PCT(100));
    lv_obj_set_style_bg_color(o, p.flaeche, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RADIUS_KARTE, 0);
    lv_obj_set_style_border_width(o, betont ? 2 : 1, 0);
    lv_obj_set_style_border_color(o, betont ? mischen(p.akzent, p.flaeche, 115) : p.linie, 0);
    lv_obj_set_style_pad_all(o, 24, 0);
    // Schatten wie .karte(): schwach, nur im Hellen sichtbar
    if (!dunkel()) {
        lv_obj_set_style_shadow_width(o, 18, 0);
        lv_obj_set_style_shadow_offset_y(o, 8, 0);
        lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(o, 16, 0);
    }
    lv_obj_set_style_bg_color(o, p.vertieft, LV_STATE_PRESSED);
    spalte(o, 14);
    return o;
}

lv_obj_t *kachel(lv_obj_t *parent)
{
    const Palette &p = pal();
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_style_bg_color(o, p.vertieft, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RADIUS_KLEIN, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, p.linie, 0);
    lv_obj_set_style_pad_all(o, 18, 0);
    spalte(o, 4);
    return o;
}

lv_obj_t *text(lv_obj_t *parent, const std::string &t, const lv_font_t *f, lv_color_t farbe)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, t.c_str());
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, farbe, 0);
    return l;
}

lv_obj_t *text_umbruch(lv_obj_t *parent, const std::string &t, const lv_font_t *f, lv_color_t farbe, int breite)
{
    lv_obj_t *l = text(parent, t, f, farbe);
    lv_obj_set_width(l, breite);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(l, f->line_height / 8, 0);
    return l;
}

void einzeilig(lv_obj_t *l)
{
    const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_height(l, f ? f->line_height : 24);
}

lv_obj_t *symbol(lv_obj_t *parent, const char *sf, const lv_font_t *f, lv_color_t farbe) { return text(parent, sym(sf), f, farbe); }

lv_obj_t *kicker(lv_obj_t *parent, const std::string &t, lv_color_t farbe)
{
    lv_obj_t *l = text(parent, fmt::versal(t), &f_kicker, farbe);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    return l;
}

void zeile(lv_obj_t *o, int luecke, lv_flex_align_t quer)
{
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, quer, quer);
    lv_obj_set_style_pad_column(o, luecke, 0);
}

void spalte(lv_obj_t *o, int luecke)
{
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(o, luecke, 0);
}

lv_obj_t *zeile_neu(lv_obj_t *parent, int luecke, lv_flex_align_t quer)
{
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_width(o, LV_PCT(100));
    zeile(o, luecke, quer);
    return o;
}

lv_obj_t *spalte_neu(lv_obj_t *parent, int luecke)
{
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_width(o, LV_PCT(100));
    spalte(o, luecke);
    return o;
}

lv_obj_t *dehnung(lv_obj_t *parent)
{
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_height(o, 1);
    lv_obj_set_flex_grow(o, 1);
    return o;
}

lv_obj_t *trennlinie(lv_obj_t *parent)
{
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_size(o, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(o, pal().linie, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t *kartenkopf(lv_obj_t *k, const std::string &titel, const char *sf, const std::string &erklaerung)
{
    const Palette &p = pal();
    lv_obj_t *s = spalte_neu(k, 6);
    lv_obj_t *z = zeile_neu(s, 10);
    if (sf) symbol(z, sf, &f_sym24, p.akzent);
    text(z, titel, &f_body_s, p.tinte);
    if (!erklaerung.empty()) text_umbruch(s, erklaerung, &f_sub, p.gedaempft);
    return s;
}

lv_obj_t *abschnitt(lv_obj_t *parent, const std::string &titel, const std::string &zusatz)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(parent, 8);
    lv_obj_set_style_pad_top(z, 12, 0);
    lv_obj_set_style_pad_hor(z, 6, 0);
    kicker(z, titel, p.gedaempft);
    dehnung(z);
    if (!zusatz.empty()) text(z, zusatz, &f_mono_k, p.gedaempft);
    return z;
}

lv_obj_t *messzahl(lv_obj_t *parent, const std::string &zahl, const char *einheit, const lv_font_t *f, lv_color_t farbe)
{
    const Palette &p = pal();
    lv_obj_t *z = flaeche(parent);
    zeile(z, 6, LV_FLEX_ALIGN_END);
    // ⚠️ Ziffernschriften tragen keine Buchstaben: ein Wort setzt die Textschrift.
    bool ziffern = fmt::nur_ziffern(zahl);
    const lv_font_t *ff = f;
    if (!ziffern && (f == &f_gross || f == &f_tafelwert || f == &f_raum)) ff = f == &f_gross ? &f_messwert : &f_tafel;
    lv_obj_t *l = text(z, zahl, ff, farbe);
    if (einheit && *einheit && zahl != fmt::STRICH && zahl != "–,–") {
        lv_obj_t *e = text(z, einheit, &f_callout, p.gedaempft);
        // Grundlinien angleichen: Unterlänge der großen Schrift ausgleichen
        lv_obj_set_style_translate_y(e, -(ff->base_line - f_callout.base_line), 0);
    }
    (void)l;
    return z;
}

lv_obj_t *kennzahl(lv_obj_t *parent, const std::string &titel, const std::string &wert, const char *einheit,
                   const std::string &fuss, lv_color_t farbe)
{
    const Palette &p = pal();
    lv_obj_t *k = kachel(parent);
    kicker(k, titel, p.gedaempft);
    messzahl(k, wert, einheit, &f_messwert, farbe);
    if (!fuss.empty()) {
        lv_obj_t *f = text_umbruch(k, fuss, &f_mono_k, p.gedaempft);
        lv_label_set_long_mode(f, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_max_height(f, f_mono_k.line_height * 2 + 4, 0);
    }
    return k;
}

lv_obj_t *marke(lv_obj_t *parent, const std::string &t, lv_color_t farbe, const char *sf)
{
    const Palette &p = pal();
    lv_obj_t *o = flaeche(parent);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, farbe, 0);
    lv_obj_set_style_bg_opa(o, 26, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, mischen(farbe, p.flaeche, 115), 0);
    lv_obj_set_style_pad_hor(o, 13, 0);
    lv_obj_set_style_pad_ver(o, 5, 0);
    zeile(o, 6);
    if (sf) symbol(o, sf, &f_sym19, farbe);
    text(o, t, &f_cap_m, farbe);
    return o;
}

lv_obj_t *zustandszeile(lv_obj_t *parent, const std::string &t, lv_color_t farbe, const std::string &zusatz)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(parent, 12);
    lv_obj_t *punkt = flaeche(z);
    lv_obj_set_size(punkt, 13, 13);
    lv_obj_set_style_radius(punkt, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(punkt, farbe, 0);
    lv_obj_set_style_bg_opa(punkt, LV_OPA_COVER, 0);
    text(z, t, &f_body_s, p.tinte);
    if (!zusatz.empty()) text(z, zusatz, &f_sub, p.gedaempft);
    return z;
}

lv_obj_t *balken(lv_obj_t *parent, float anteil, lv_color_t farbe, int hoehe)
{
    const Palette &p = pal();
    lv_obj_t *spur = flaeche(parent);
    lv_obj_set_size(spur, LV_PCT(100), hoehe);
    lv_obj_set_style_radius(spur, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(spur, p.vertieft, 0);
    lv_obj_set_style_bg_opa(spur, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(spur, 1, 0);
    lv_obj_set_style_border_color(spur, p.linie, 0);
    if (!isnan(anteil) && anteil > 0.005f) {
        lv_obj_t *f = flaeche(spur);
        lv_obj_set_size(f, LV_PCT((int)lroundf(fminf(anteil, 1.f) * 100)), LV_PCT(100));
        lv_obj_set_style_min_width(f, hoehe, 0);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f, farbe, 0);
        lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    }
    return spur;
}

lv_obj_t *knopf(lv_obj_t *parent, const std::string &t, bool betont, std::function<void()> wenn, const char *sf)
{
    const Palette &p = pal();
    lv_obj_t *b = flaeche(parent);
    lv_obj_set_style_radius(b, RADIUS_KLEIN, 0);
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_pad_ver(b, 13, 0);
    lv_obj_set_style_min_height(b, 56, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, betont ? p.akzent : p.linie_stark, 0);
    lv_obj_set_style_bg_color(b, betont ? p.akzent : p.flaeche, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(b, 178, LV_STATE_PRESSED);   // gedrückt 70 %
    lv_obj_set_style_opa(b, 110, LV_STATE_DISABLED);
    zeile(b, 9);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_color_t f = betont ? p.auf_akzent : p.akzent;
    if (sf) symbol(b, sf, &f_sym24, f);
    if (!t.empty()) text(b, t, &f_sub_m, f);
    if (wenn) bei_klick(b, std::move(wenn));
    return b;
}

lv_obj_t *rund_knopf(lv_obj_t *parent, const char *sf, int d, std::function<void()> wenn, bool betont)
{
    const Palette &p = pal();
    lv_obj_t *b = flaeche(parent);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, betont ? p.akzent : p.flaeche, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, betont ? 0 : 1, 0);
    lv_obj_set_style_border_color(b, p.linie_stark, 0);
    lv_obj_set_style_opa(b, 178, LV_STATE_PRESSED);
    lv_obj_set_style_opa(b, 110, LV_STATE_DISABLED);
    lv_obj_t *s = symbol(b, sf, d >= 64 ? &f_sym32 : &f_sym24, betont ? p.auf_akzent : p.akzent);
    lv_obj_center(s);
    if (wenn) bei_klick(b, std::move(wenn));
    return b;
}

lv_obj_t *kennwert(lv_obj_t *parent, const std::string &name, const std::string &wert, lv_color_t farbe)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(parent, 16);
    lv_obj_set_style_pad_ver(z, 4, 0);
    text(z, name, &f_sub, p.tinte);
    dehnung(z);
    lv_obj_t *w = text(z, wert, &f_mono, farbe);
    lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_RIGHT, 0);
    return z;
}

lv_obj_t *link_zeile(lv_obj_t *parent, const char *sf, const std::string &titel, const std::string &zusatz,
                     std::function<void()> wenn, lv_color_t farbe)
{
    const Palette &p = pal();
    lv_obj_t *z = zeile_neu(parent, 14);
    lv_obj_set_style_pad_ver(z, 14, 0);
    lv_obj_set_style_radius(z, RADIUS_KLEIN, 0);
    lv_obj_set_style_bg_color(z, p.vertieft, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(z, LV_OPA_COVER, LV_STATE_PRESSED);
    if (sf) {
        lv_obj_t *s = symbol(z, sf, &f_sym24, farbe);
        lv_obj_set_width(s, 30);
    }
    text(z, titel, &f_sub_m, farbe);
    dehnung(z);
    if (!zusatz.empty()) text(z, zusatz, &f_mono, p.gedaempft);
    symbol(z, "chevron.right", &f_sym19, p.blass);
    if (wenn) bei_klick(z, std::move(wenn));
    return z;
}

lv_obj_t *segmente(lv_obj_t *parent, const std::vector<std::string> &optionen, int aktiv, std::function<void(int)> wahl)
{
    const Palette &p = pal();
    lv_obj_t *s = flaeche(parent);
    lv_obj_set_width(s, LV_PCT(100));
    lv_obj_set_style_radius(s, RADIUS_KLEIN, 0);
    lv_obj_set_style_bg_color(s, p.vertieft, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s, 1, 0);
    lv_obj_set_style_border_color(s, p.linie, 0);
    lv_obj_set_style_pad_all(s, 4, 0);
    zeile(s, 4);
    for (int i = 0; i < (int)optionen.size(); i++) {
        lv_obj_t *o = flaeche(s);
        lv_obj_set_height(o, 50);
        lv_obj_set_flex_grow(o, 1);
        lv_obj_set_style_radius(o, RADIUS_KLEIN - 3, 0);
        bool an = i == aktiv;
        lv_obj_set_style_bg_color(o, an ? p.flaeche : p.vertieft, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        if (an) {
            lv_obj_set_style_border_width(o, 1, 0);
            lv_obj_set_style_border_color(o, p.linie_stark, 0);
        }
        lv_obj_set_style_bg_color(o, p.linie, LV_STATE_PRESSED);
        lv_obj_t *l = text(o, optionen[i], an ? &f_sub_s : &f_sub, an ? p.tinte : p.gedaempft);
        lv_obj_center(l);
        if (wahl) bei_klick(o, [wahl, i] { wahl(i); });
    }
    return s;
}

lv_obj_t *leer_hinweis(lv_obj_t *parent, const char *sf, const std::string &titel, const std::string &t)
{
    const Palette &p = pal();
    lv_obj_t *s = spalte_neu(parent, 14);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(s, 60, 0);
    symbol(s, sf, &f_sym48, p.gedaempft);
    text(s, titel, &f_t3_s, p.tinte);
    lv_obj_t *l = text_umbruch(s, t, &f_sub, p.gedaempft, 640);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return s;
}

// ---- Farblogik ------------------------------------------------------------------------

lv_color_t schwere_farbe(anlage::Schwere s)
{
    return s == anlage::Schwere::stoerung ? pal().stoerung : s == anlage::Schwere::warnung ? pal().warnung : pal().gedaempft;
}

const char *schwere_sym(anlage::Schwere s)
{
    return s == anlage::Schwere::stoerung ? "xmark.octagon" : s == anlage::Schwere::warnung ? "exclamationmark.triangle" : "info.circle";
}

lv_color_t raum_farbe(anlage::Raumzustand z)
{
    switch (z) {
    case anlage::Raumzustand::aus: return pal().gedaempft;
    case anlage::Raumzustand::kein_thermometer: return pal().stoerung;
    case anlage::Raumzustand::messwert_veraltet: return pal().warnung;
    case anlage::Raumzustand::heizt: return pal().waerme;
    default: return pal().gut;
    }
}

const char *raum_sym(anlage::Raumzustand z)
{
    switch (z) {
    case anlage::Raumzustand::aus: return "power";
    case anlage::Raumzustand::kein_thermometer: return "exclamationmark.triangle";
    case anlage::Raumzustand::messwert_veraltet: return "clock.badge.exclamationmark";
    case anlage::Raumzustand::heizt: return "flame";
    default: return "checkmark";
    }
}

const char *raum_text(anlage::Raumzustand z)
{
    switch (z) {
    case anlage::Raumzustand::aus: return "ausgeschaltet";
    case anlage::Raumzustand::kein_thermometer: return "kein Thermometer";
    case anlage::Raumzustand::messwert_veraltet: return "Messwert veraltet";
    case anlage::Raumzustand::heizt: return "heizt";
    default: return "Soll erreicht";
    }
}

std::string abstand_text(const anlage::Raum &r)
{
    switch (r.zustand()) {
    case anlage::Raumzustand::aus: return "ausgeschaltet";
    case anlage::Raumzustand::kein_thermometer: return "ohne Thermometer";
    case anlage::Raumzustand::messwert_veraltet: return "Messwert veraltet";
    default: break;
    }
    float d = r.ist - r.soll;
    if (fabsf(d) < 0.05f) return "am Soll";
    return fmt::kelvin(fabsf(d)) + (d > 0 ? " über Soll" : " unter Soll");
}

lv_color_t abstand_farbe(const anlage::Raum &r)
{
    return r.zustand() == anlage::Raumzustand::heizt ? pal().waerme : pal().gedaempft;
}

}  // namespace ui
