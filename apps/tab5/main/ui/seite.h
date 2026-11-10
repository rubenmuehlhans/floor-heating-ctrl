// Hilfen für Seiten, die bei neuen Daten neu aufgebaut werden.
//
// ⚠️ Eine Seite baut nur bei geänderter Generation neu, nie je Takt — ein Neuaufbau je Takt
// flackert (camperSense, tab5/CLAUDE.md). Beim Neuaufbau bleiben die Bildlaufstellen der
// Spalten erhalten.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "anlage.h"
#include "lvgl.h"
#include "ui.h"

namespace ui {

class Neubau {
public:
    void merken(lv_obj_t *inhalt, uint32_t gen)
    {
        inhalt_ = inhalt;
        gen_ = gen;
        links_ = rechts_ = nullptr;
    }
    void spalten(const Spalten &s)
    {
        links_ = s.links;
        rechts_ = s.rechts;
    }
    bool geaendert(uint32_t gen) const { return inhalt_ && gen != gen_ && !bedienung_kuerzlich() && !lv_indev_active(); }
    // Neuaufbau mit erhaltenen Bildlaufstellen; `bau` füllt den geleerten Inhalt.
    void neu(const std::function<void(lv_obj_t *)> &bau)
    {
        y_links_ = links_ ? lv_obj_get_scroll_y(links_) : 0;
        y_rechts_ = rechts_ ? lv_obj_get_scroll_y(rechts_) : 0;
        lv_obj_t *i = inhalt_;
        lv_obj_clean(i);
        kopf_leeren();
        wiederherstellen_offen_ = true;
        bau(i);
    }
    void wiederherstellen()
    {
        if (!wiederherstellen_offen_) return;
        wiederherstellen_offen_ = false;
        if (links_) {
            lv_obj_update_layout(links_);
            lv_obj_scroll_to_y(links_, y_links_, LV_ANIM_OFF);
        }
        if (rechts_) {
            lv_obj_update_layout(rechts_);
            lv_obj_scroll_to_y(rechts_, y_rechts_, LV_ANIM_OFF);
        }
    }

private:
    lv_obj_t *inhalt_ = nullptr, *links_ = nullptr, *rechts_ = nullptr;
    uint32_t gen_ = 0;
    int32_t y_links_ = 0, y_rechts_ = 0;
    bool wiederherstellen_offen_ = false;
};

// Bausteine, die mehrere Seiten teilen (uebersicht.cpp)
lv_obj_t *raumkachel(lv_obj_t *parent, const anlage::Raum &r, int breite);
void befund_zeile(lv_obj_t *c, const anlage::Befund &f);

}  // namespace ui
