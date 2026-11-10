// Helligkeit der Anzeige: eingestellter Wert, nach Untätigkeit gedimmt (Einstellung „Anzeige
// dunkler nach“). Läuft im LVGL-Takt (main.cpp).
#include "bsp/esp-bsp.h"
#include "eins.h"
#include "geraet.h"
#include "lvgl.h"

namespace anzeige {

void takt()
{
    static int zuletzt = -1;
    int hell = (int)eins::zahl(eins::k::HELL, 80);
    int min = (int)eins::zahl(eins::k::DUNKEL_MIN, 5);
    bool ruhe = min > 0 && lv_display_get_inactive_time(nullptr) > (uint32_t)min * 60000;
    int soll = ruhe ? (hell > 15 ? 15 : hell) : hell;
    if (soll != zuletzt) {
        bsp_display_brightness_set(soll);
        zuletzt = soll;
    }
}

}  // namespace anzeige
