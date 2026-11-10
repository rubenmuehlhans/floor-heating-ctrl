// Fenster über der Seite: Rückfrage vor Befehlen und die Bildschirmtastatur (QWERTZ).
//
// ⚠️ Eigene Tasten statt lv_keyboard: dessen Steuertasten sind Symbole der eingebauten
// Montserrat-Schrift, die unsere Schriften nicht tragen (camperSense, ui/tastatur.h).
// ⚠️ Beides liegt auf lv_layer_top(); ein Seitenwechsel schließt die Tastatur.
#include <string.h>
#include "stil.h"
#include "ui.h"

namespace ui {

namespace {

lv_obj_t *s_dunkel;   // Rückfrage
lv_obj_t *s_tast;     // Tastatur
lv_obj_t *s_feld;
std::string s_text;
bool s_geheim, s_gross, s_zeichen, s_aufgedeckt;
std::function<void(const std::string &)> s_fertig;

void schliessen(lv_obj_t **o)
{
    if (*o) {
        lv_obj_t *x = *o;
        *o = nullptr;
        lv_obj_delete_async(x);
    }
}

lv_obj_t *grund()
{
    lv_obj_t *d = flaeche(lv_layer_top());
    lv_obj_set_size(d, BREITE, HOEHE);
    lv_obj_set_style_bg_color(d, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(d, 140, 0);
    lv_obj_set_clickable(d, true);   // fängt Tipps auf die Seite darunter ab
    return d;
}

void feld_zeigen()
{
    if (!s_feld) return;
    std::string t = s_text;
    if (s_geheim && !s_aufgedeckt) {
        t.clear();
        for (size_t i = 0; i < s_text.size(); i++)
            if (((unsigned char)s_text[i] & 0xC0) != 0x80) t += "\xE2\x80\xA2";
    }
    lv_label_set_text(s_feld, (t + "|").c_str());
}

void tasten_bauen();

void taste(lv_obj_t *reihe, const std::string &t, int breite, std::function<void()> f, bool betont = false)
{
    const Palette &p = pal();
    lv_obj_t *k = flaeche(reihe);
    lv_obj_set_size(k, breite, 66);
    lv_obj_set_style_radius(k, 10, 0);
    lv_obj_set_style_bg_color(k, betont ? p.akzent : p.flaeche, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_set_style_border_color(k, p.linie_stark, 0);
    lv_obj_set_style_bg_color(k, p.linie_stark, LV_STATE_PRESSED);
    const char *s = nullptr;
    if (t == "⇧") s = "shift";
    else if (t == "⌫") s = "delete.left";
    lv_obj_t *l = s ? symbol(k, s, &f_sym24, p.tinte) : text(k, t, &f_body_s, betont ? p.auf_akzent : p.tinte);
    lv_obj_center(l);
    bei_klick(k, std::move(f));
}

void tasten_bauen()
{
    lv_obj_t *t = lv_obj_get_child(s_tast, 1);   // Tastenblock
    lv_obj_clean(t);
    static const char *BUCHST[] = {"qwertzuiopü", "asdfghjklöä", "yxcvbnmß"};
    static const char *ZEICH[] = {"1234567890-", "!\"§$%&/()=?", "@#+*_.,:;'"};
    const char **reihen = s_zeichen ? ZEICH : BUCHST;
    for (int r = 0; r < 3; r++) {
        lv_obj_t *z = zeile_neu(t, 8);
        lv_obj_set_flex_align(z, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (r == 2 && !s_zeichen) taste(z, "⇧", 100, [] {
            s_gross = !s_gross;
            lv_async_call([](void *) { tasten_bauen(); }, nullptr);
        }, s_gross);
        const char *p = reihen[r];
        while (*p) {
            int n = ((unsigned char)*p >= 0xC0) ? 2 : 1;
            std::string c(p, n);
            p += n;
            std::string zeichen = c;
            if (s_gross && !s_zeichen) {
                if (n == 1 && c[0] >= 'a' && c[0] <= 'z') zeichen[0] = (char)(c[0] - 32);
                else if (n == 2 && (unsigned char)c[1] >= 0xA4 && c != "ß") zeichen[1] = (char)(c[1] - 0x20);
            }
            taste(z, zeichen, 92, [zeichen] {
                s_text += zeichen;
                if (s_gross) {
                    s_gross = false;
                    lv_async_call([](void *) { tasten_bauen(); }, nullptr);
                }
                feld_zeigen();
            });
        }
        if (r == 2) taste(z, "⌫", 120, [] {
            while (!s_text.empty()) {
                unsigned char c = (unsigned char)s_text.back();
                s_text.pop_back();
                if ((c & 0xC0) != 0x80) break;
            }
            feld_zeigen();
        });
    }
    lv_obj_t *z = zeile_neu(t, 8);
    lv_obj_set_flex_align(z, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    taste(z, s_zeichen ? "ABC" : "123", 130, [] {
        s_zeichen = !s_zeichen;
        lv_async_call([](void *) { tasten_bauen(); }, nullptr);
    });
    taste(z, "", 560, [] {
        s_text += " ";
        feld_zeigen();
    });
    taste(z, ".", 92, [] {
        s_text += ".";
        feld_zeigen();
    });
}

}  // namespace

void rueckfrage(const std::string &titel, const std::string &t, const std::string &ja, std::function<void()> wenn_ja, bool zerstoerend)
{
    const Palette &p = pal();
    schliessen(&s_dunkel);
    s_dunkel = grund();
    lv_obj_t *k = karte(s_dunkel);
    lv_obj_set_width(k, 620);
    lv_obj_center(k);
    spalte(k, 16);
    text(k, titel, &f_t3_s, p.tinte);
    text_umbruch(k, t, &f_body, p.tinte);
    lv_obj_t *z = zeile_neu(k, 12);
    dehnung(z);
    knopf(z, "Abbrechen", false, [] { schliessen(&s_dunkel); });
    lv_obj_t *b = knopf(z, ja, true, [wenn_ja] {
        schliessen(&s_dunkel);
        wenn_ja();
    });
    if (zerstoerend) {
        lv_obj_set_style_bg_color(b, p.stoerung, 0);
        lv_obj_set_style_border_color(b, p.stoerung, 0);
    }
}

void tastatur_oeffnen(const std::string &titel, const std::string &vorgabe, std::function<void(const std::string &)> fertig, bool geheim,
                      const char *ok_text)
{
    const Palette &p = pal();
    schliessen(&s_tast);
    s_text = vorgabe;
    s_geheim = geheim;
    s_aufgedeckt = false;
    s_gross = s_zeichen = false;
    s_fertig = std::move(fertig);
    s_tast = grund();
    lv_obj_set_style_bg_opa(s_tast, 200, 0);
    lv_obj_set_style_pad_all(s_tast, 24, 0);
    spalte(s_tast, 18);
    lv_obj_set_flex_align(s_tast, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Kopf mit Feld
    lv_obj_t *kopf = karte(s_tast);
    lv_obj_set_width(kopf, 1160);
    spalte(kopf, 10);
    lv_obj_t *z = zeile_neu(kopf, 12);
    text(z, titel, &f_sub_s, p.gedaempft);
    dehnung(z);
    if (geheim) knopf(z, "Zeigen", false, [] {
        s_aufgedeckt = !s_aufgedeckt;
        feld_zeigen();
    }, "eye");
    knopf(z, "Abbrechen", false, [] { schliessen(&s_tast); });
    knopf(z, ok_text ? ok_text : "Fertig", true, [] {
        auto f = s_fertig;
        std::string t = s_text;
        schliessen(&s_tast);
        if (f) f(t);
    });
    s_feld = text_umbruch(kopf, "", &f_body, p.tinte);
    feld_zeigen();
    // Tastenblock (Kind 1)
    lv_obj_t *t = flaeche(s_tast);
    lv_obj_set_width(t, 1160);
    spalte(t, 8);
    lv_obj_set_style_bg_color(t, p.vertieft, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, RADIUS_KARTE, 0);
    lv_obj_set_style_pad_all(t, 14, 0);
    lv_obj_move_to_index(t, 1);
    lv_obj_move_to_index(kopf, 0);
    tasten_bauen();
}

void tastatur_schliessen() { schliessen(&s_tast); }
bool tastatur_offen() { return s_tast != nullptr; }

}  // namespace ui
