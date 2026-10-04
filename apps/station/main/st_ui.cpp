/*
 * Anzeige des Leitstands mit M5Unified.
 *
 * Gezeichnet wird unmittelbar in den Bildspeicher der Anzeige, ohne
 * vollstaendigen Zwischenspeicher: Der Core Basic hat keinen PSRAM, und
 * 150 KB fuer ein Vollbild waeren mehr, als nach WLAN und Bluetooth frei ist.
 * Feste Teile einer Seite entstehen beim Seitenwechsel, Messwerte werden mit
 * Hintergrund ueberschrieben.
 */
#include "st_ui.h"
#include "driver/gpio.h"
#include "st_log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <M5Unified.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "netmgr.h"
#include "schriften.h"
#include "atc_ble.h"
#include "cJSON.h"
#include "protokoll.h"
#include "st_config.h"
#include "st_hap.h"
#include "st_poll.h"
#include "st_verlauf.h"
#include <strings.h>

static const char *TAG = "ui";

/* Farben nach dem dunklen Schema der App (apple/App/Assets.xcassets) */
static constexpr uint32_t GRUND = 0x121514u;
static constexpr uint32_t FLAECHE = 0x1a1e1du;
static constexpr uint32_t TINTE = 0xe8eceau;
static constexpr uint32_t GEDAEMPFT = 0x98a29eu;
static constexpr uint32_t LINIE = 0x2b312fu;
static constexpr uint32_t WAERME = 0xe8794bu;
static constexpr uint32_t WAERME_DUNKEL = 0x5c3222u;
static constexpr uint32_t KAELTE = 0x6f9fd6u;
static constexpr uint32_t GUT = 0x5fb37bu;
static constexpr uint32_t WARNUNG = 0xc9993cu;

static const lgfx::IFont *const KLEIN = &schrift::inter_m12;
static const lgfx::IFont *const NORMAL = &schrift::inter_14;
static const lgfx::IFont *const MITTEL = &schrift::inter_s16;
static const lgfx::IFont *const GROSS = &schrift::inter_s22;

/* Sechs Seiten auf dem Core2; ohne PSRAM (Core Basic) nur Anlage und
 * Leitstand, weil Verlauf und Ereignisse Speicher brauchen, der dort fehlt. */
enum seite_t { SEITE_ANLAGE = 0, SEITE_RAEUME, SEITE_VERLAUF, SEITE_MELDUNGEN, SEITE_GERAETE, SEITE_LEITSTAND, SEITEN };
static const char *const SEITENNAME[SEITEN] = {"anlage", "raeume", "verlauf", "meldungen", "geraete", "leitstand"};
static const char *const SEITENTITEL[SEITEN] = {"Anlage", "R\xc3\xa4ume", "Verlauf 24 h", "Meldungen", "Ger\xc3\xa4te", "Leitstand"};
static const char *const SEITENKURZ[SEITEN] = {"Anlage", "R\xc3\xa4ume", "Verlauf", "Meldungen", "Ger\xc3\xa4te", "Leitstand"};

static SemaphoreHandle_t s_lcd;
static volatile int s_seite = SEITE_ANLAGE;
static volatile bool s_an = true;
static volatile bool s_neu_lesen = true;
static char s_board[24] = "unbekannt";
static bool s_psram;
/* Versorgung (Core2): -1 = nicht messbar, 0 = Akku, 1 = Netzteil */
static int s_versorgt = -1;
static int s_akku = -1;

/* ------------------------------------------------------------------ */
/* Hilfen                                                              */
/* ------------------------------------------------------------------ */

static uint32_t jetzt_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Zahl mit Dezimalkomma; negative Werte mit echtem Minuszeichen. */
static void zahl(char *out, size_t len, float v, int stellen, const char *einheit)
{
    char roh[16];
    snprintf(roh, sizeof(roh), "%.*f", stellen, fabsf(v));
    for (char *p = roh; *p; p++) {
        if (*p == '.') {
            *p = ',';
        }
    }
    bool negativ = v < 0 && strcmp(roh, stellen ? "0,0" : "0") != 0;
    snprintf(out, len, "%s%s%s", negativ ? "\xe2\x88\x92" : "", roh, einheit);
}

/* Text ohne Hintergrund, fuer Flaechen, die ohnehin neu gezeichnet werden */
static void text_frei(int x, int y, const char *s, const lgfx::IFont *f, uint32_t farbe,
                      lgfx::textdatum_t lage = lgfx::textdatum_t::baseline_left)
{
    M5.Display.setFont(f);
    M5.Display.setTextColor(farbe);
    M5.Display.setTextDatum(lage);
    M5.Display.setTextPadding(0);
    M5.Display.drawString(s, x, y);
}

/*
 * Messwert: erst eine knappe Flaeche freiraeumen, dann ohne Hintergrund
 * zeichnen. Mit Hintergrundfarbe faerbt M5GFX die volle Schrifthoehe ein,
 * einschliesslich des Platzes fuer Umlautpunkte, und schneidet so die Zeile
 * darueber an. `oben` und `unten` reichen fuer Ziffern, Komma und Grad.
 */
static void wert(int x, int y, const char *s, const lgfx::IFont *f, uint32_t farbe, uint32_t grund,
                 lgfx::textdatum_t lage, int breite, int oben, int unten)
{
    int links = x;
    if (lage == lgfx::textdatum_t::baseline_right) {
        links = x - breite;
    } else if (lage == lgfx::textdatum_t::baseline_center) {
        links = x - breite / 2;
    }
    M5.Display.fillRect(links, y - oben, breite, oben + unten, grund);
    text_frei(x, y, s, f, farbe, lage);
}

/* Freiraum ueber und unter der Grundlinie je Schrift */
#define KLEIN_RAUM 11, 4
#define NORMAL_RAUM 12, 4
#define MITTEL_RAUM 14, 4
#define GROSS_RAUM 18, 5

static void karte(int x, int y, int w, int h)
{
    M5.Display.fillRoundRect(x, y, w, h, 6, FLAECHE);
}

/* ------------------------------------------------------------------ */
/* Kopf und Fuss                                                       */
/* ------------------------------------------------------------------ */

static void kopf_fest(int seite)
{
    M5.Display.fillRect(0, 0, 320, 22, GRUND);
    text_frei(8, 16, SEITENTITEL[seite], MITTEL, TINTE);
    M5.Display.drawFastHLine(0, 22, 320, LINIE);
}

static void kopf_werte(const st_plant_t *p, const netmgr_status_t *net)
{
    char zeit[8] = "--:--";
    if (net->time_valid) {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(zeit, sizeof(zeit), "%H:%M", &lt);
    }
    wert(312, 16, zeit, NORMAL, GEDAEMPFT, GRUND, lgfx::textdatum_t::baseline_right, 44, NORMAL_RAUM);

    /* WLAN-Balken nach Signalstaerke */
    int stufen = 0;
    if (net->sta_connected) {
        stufen = net->rssi > -60 ? 3 : net->rssi > -72 ? 2 : 1;
    }
    for (int i = 0; i < 3; i++) {
        int h = 4 + i * 3;
        M5.Display.fillRect(254 + i * 4, 16 - h, 3, h, i < stufen ? GEDAEMPFT : LINIE);
    }

    int befunde = 0;
    for (uint8_t i = 0; i < p->heat_count; i++) {
        befunde += p->heat[i].findings;
    }
    M5.Display.fillRect(222, 3, 26, 17, GRUND);
    if (befunde > 0) {
        char n[4];
        snprintf(n, sizeof(n), "%d", befunde > 9 ? 9 : befunde);
        M5.Display.fillCircle(236, 11, 7, WARNUNG);
        text_frei(236, 15, n, KLEIN, GRUND, lgfx::textdatum_t::baseline_center);
    }
}

static void fuss(const char *a, const char *b, const char *c)
{
    M5.Display.fillRect(0, 222, 320, 18, GRUND);
    M5.Display.drawFastHLine(0, 221, 320, LINIE);
    const char *l[3] = {a, b, c};
    const int x[3] = {64, 160, 256};
    for (int i = 0; i < 3; i++) {
        if (l[i] && l[i][0]) {
            text_frei(x[i], 235, l[i], KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_center);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Werte aus beiden Heizungsgeraeten                                   */
/* ------------------------------------------------------------------ */

static bool fuehler(const st_plant_t *p, const char *rolle, float *out)
{
    for (uint8_t i = 0; i < p->heat_count; i++) {
        const st_heat_t *h = &p->heat[i];
        if (!h->dev.reachable) {
            continue;
        }
        for (uint8_t k = 0; k < h->probe_count; k++) {
            if (strcmp(h->probes[k].role, rolle) == 0) {
                *out = h->probes[k].temp_c;
                return true;
            }
        }
    }
    return false;
}

/* Das Geraet, das den Brenner selbst misst; sonst eines, das ihn kennt. */
static const st_heat_t *brennergeraet(const st_plant_t *p)
{
    const st_heat_t *ersatz = NULL;
    for (uint8_t i = 0; i < p->heat_count; i++) {
        const st_heat_t *h = &p->heat[i];
        if (!h->dev.reachable || !h->burner_known) {
            continue;
        }
        if (h->burner_own) {
            return h;
        }
        ersatz = h;
    }
    return ersatz;
}

static const st_heat_t *speichergeraet(const st_plant_t *p)
{
    const st_heat_t *ersatz = NULL;
    for (uint8_t i = 0; i < p->heat_count; i++) {
        const st_heat_t *h = &p->heat[i];
        if (!h->dev.reachable || !h->charge_valid) {
            continue;
        }
        if (h->charge_own) {
            return h;
        }
        ersatz = h;
    }
    return ersatz;
}

static const st_heat_t *kreisgeraet(const st_plant_t *p)
{
    for (uint8_t i = 0; i < p->heat_count; i++) {
        if (p->heat[i].dev.reachable && p->heat[i].circuit_count > 0) {
            return &p->heat[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Seite Anlage                                                        */
/* ------------------------------------------------------------------ */

static void anlage_fest(void)
{
    M5.Display.fillRect(0, 23, 320, 198, GRUND);
    karte(6, 28, 102, 128);
    text_frei(14, 44, "Kessel", KLEIN, GEDAEMPFT);
    text_frei(14, 86, "Vorlauf", KLEIN, GEDAEMPFT);
    text_frei(14, 130, "RL", KLEIN, GEDAEMPFT);
    text_frei(14, 147, "Abgas", KLEIN, GEDAEMPFT);

    karte(114, 28, 92, 128);
    text_frei(122, 44, "Speicher", KLEIN, GEDAEMPFT);

    karte(212, 28, 102, 61);
    karte(212, 95, 102, 61);
    for (int i = 0; i < 2; i++) {
        int y = i == 0 ? 28 : 95;
        text_frei(220, y + 45, "VL", KLEIN, GEDAEMPFT);
        text_frei(220, y + 58, "RL", KLEIN, GEDAEMPFT);
    }

    karte(6, 162, 150, 56);
    text_frei(14, 178, "Au\xc3\x9f" "en", KLEIN, GEDAEMPFT);
    karte(162, 162, 152, 56);
    text_frei(170, 178, "Brenner heute", KLEIN, GEDAEMPFT);

}

static void pumpe(int cx, int cy, bool an)
{
    uint32_t f = an ? GUT : GEDAEMPFT;
    M5.Display.fillCircle(cx, cy, 8, FLAECHE);
    M5.Display.drawCircle(cx, cy, 7, f);
    M5.Display.drawCircle(cx, cy, 6, f);
    M5.Display.fillTriangle(cx - 2, cy - 4, cx + 4, cy, cx - 2, cy + 4, f);
}

static void anlage_werte(const st_plant_t *p)
{
    char s[40];

    /* Kessel */
    const st_heat_t *b = brennergeraet(p);
    M5.Display.fillRect(12, 50, 94, 21, FLAECHE);
    if (b == NULL) {
        text_frei(14, 64, "keine Daten", KLEIN, GEDAEMPFT);
    } else if (b->burner_running) {
        M5.Display.fillRoundRect(14, 51, 76, 18, 9, WAERME);
        text_frei(52, 64, "Brenner an", KLEIN, GRUND, lgfx::textdatum_t::baseline_center);
    } else {
        M5.Display.drawRoundRect(14, 51, 80, 18, 9, LINIE);
        text_frei(54, 64, "Brenner aus", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_center);
    }
    float v;
    if (fuehler(p, "kessel_vl", &v)) {
        zahl(s, sizeof(s), v, 1, "\xc2\xb0");
    } else {
        snprintf(s, sizeof(s), "\xe2\x80\x93");
    }
    wert(14, 111, s, GROSS, TINTE, FLAECHE, lgfx::textdatum_t::baseline_left, 90, GROSS_RAUM);
    if (fuehler(p, "kessel_rl", &v)) {
        zahl(s, sizeof(s), v, 1, "\xc2\xb0");
    } else {
        snprintf(s, sizeof(s), "\xe2\x80\x93");
    }
    wert(100, 130, s, NORMAL, TINTE, FLAECHE, lgfx::textdatum_t::baseline_right, 50, NORMAL_RAUM);
    bool abgas = false;
    if (b != NULL && b->abgas_valid) {
        v = b->abgas_c;
        abgas = true;
    } else {
        abgas = fuehler(p, "abgas", &v);
    }
    if (abgas) {
        zahl(s, sizeof(s), v, 0, "\xc2\xb0");
    } else {
        snprintf(s, sizeof(s), "\xe2\x80\x93");
    }
    wert(100, 147, s, NORMAL, TINTE, FLAECHE, lgfx::textdatum_t::baseline_right, 50, NORMAL_RAUM);

    /* Speicher: Behaelter mit Fuellstand */
    const st_heat_t *sp = speichergeraet(p);
    const int tx = 135, ty = 52, tw = 50, th = 96;
    M5.Display.fillRect(tx, ty, tw, th, FLAECHE);
    M5.Display.drawRoundRect(tx, ty, tw, th, 9, LINIE);
    M5.Display.drawRoundRect(tx + 1, ty + 1, tw - 2, th - 2, 8, LINIE);
    /* Temperatur und Fuellstand stehen in der Mitte des gefuellten oder des
     * leeren Teils, je nachdem, welcher groesser ist -- so kreuzt die
     * Fuellstandslinie die Schrift nicht. */
    int innen = th - 4;
    int mitte = ty + 2 + innen / 2;
    if (sp != NULL) {
        float f = sp->charge_level < 0 ? 0 : sp->charge_level > 1 ? 1 : sp->charge_level;
        int hoehe = (int)lroundf(innen * f);
        int linie = ty + 2 + innen - hoehe;
        if (hoehe > 0) {
            M5.Display.fillRoundRect(tx + 2, linie, tw - 4, hoehe, 7, WAERME_DUNKEL);
            M5.Display.fillRect(tx + 3, linie, tw - 6, 2, WAERME);
        }
        mitte = hoehe * 2 >= innen ? linie + hoehe / 2 : ty + 2 + (innen - hoehe) / 2;
        zahl(s, sizeof(s), f * 100.0f, 0, " %");
        text_frei(160, mitte + 14, s, KLEIN, TINTE, lgfx::textdatum_t::baseline_center);
    }
    if (fuehler(p, "puffer", &v)) {
        zahl(s, sizeof(s), v, 1, "\xc2\xb0");
        text_frei(160, mitte - 1, s, MITTEL, TINTE, lgfx::textdatum_t::baseline_center);
    }

    /* Heizkreise */
    const st_heat_t *k = kreisgeraet(p);
    for (int i = 0; i < 2; i++) {
        int y = i == 0 ? 28 : 95;
        const st_circuit_t *c = (k != NULL && i < k->circuit_count) ? &k->circuits[i] : NULL;
        M5.Display.fillRect(218, y + 4, 70, 28, FLAECHE);
        char titel[8];
        snprintf(titel, sizeof(titel), "HK %d", c ? c->id : i + 1);
        text_frei(220, y + 16, titel, KLEIN, GEDAEMPFT);
        /* Der Name nur, wenn er mehr sagt als die Nummer */
        if (c && c->name[0] && strncmp(c->name, "Heizkreis", 9) != 0) {
            text_frei(220, y + 29, c->name, KLEIN, GEDAEMPFT);
        }
        pumpe(300, y + 14, c != NULL && c->on);
        if (c && c->vl_valid) {
            zahl(s, sizeof(s), c->vl_c, 1, "\xc2\xb0");
        } else {
            snprintf(s, sizeof(s), "\xe2\x80\x93");
        }
        wert(306, y + 45, s, NORMAL, TINTE, FLAECHE, lgfx::textdatum_t::baseline_right, 60, NORMAL_RAUM);
        if (c && c->rl_valid) {
            zahl(s, sizeof(s), c->rl_c, 1, "\xc2\xb0");
        } else {
            snprintf(s, sizeof(s), "\xe2\x80\x93");
        }
        wert(306, y + 58, s, NORMAL, TINTE, FLAECHE, lgfx::textdatum_t::baseline_right, 60, NORMAL_RAUM);
    }

    /* Aussen */
    const st_outdoor_t *o = &p->outdoor;
    bool frisch = o->valid && o->age_s <= ST_OUTDOOR_STALE_S;
    if (frisch) {
        zahl(s, sizeof(s), o->temp_c, 1, "\xc2\xb0");
    } else {
        snprintf(s, sizeof(s), "\xe2\x80\x93");
    }
    wert(14, 207, s, GROSS, TINTE, FLAECHE, lgfx::textdatum_t::baseline_left, 80, GROSS_RAUM);
    M5.Display.fillRect(96, 180, 56, 32, FLAECHE);
    if (frisch && o->hum_valid) {
        zahl(s, sizeof(s), o->humidity, 0, " % rF");
        text_frei(148, 191, s, KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    }
    /* Vom Verteiler uebernommen heisst: der Leitstand hoert den Fuehler nicht
     * selbst. Angezeigt wird dann, ueber wen er kommt. */
    const char *quelle = frisch && o->quelle[0] ? o->quelle
                         : !o->assigned ? "kein F\xc3\xbchler" : !o->valid ? "kein Empfang" : "Funk";
    text_frei(148, 207, quelle, KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);

    /* Brenner heute */
    M5.Display.fillRect(168, 184, 142, 32, FLAECHE);
    if (b != NULL) {
        uint32_t min = b->runtime_today_s / 60;
        snprintf(s, sizeof(s), "%lu h %02lu min", (unsigned long)(min / 60), (unsigned long)(min % 60));
        text_frei(170, 200, s, MITTEL, TINTE);
        snprintf(s, sizeof(s), "%u Start%s", b->starts_today, b->starts_today == 1 ? "" : "s");
        text_frei(306, 200, s, KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
        char l[12];
        zahl(l, sizeof(l), b->litres_today, 1, " l");
        snprintf(s, sizeof(s), "\xc3\x96l gesch\xc3\xa4tzt %s", l);
        text_frei(170, 214, s, KLEIN, GEDAEMPFT);
    } else {
        text_frei(170, 200, "\xe2\x80\x93", MITTEL, GEDAEMPFT);
    }
}

/* ------------------------------------------------------------------ */
/* Seite Leitstand                                                     */
/* ------------------------------------------------------------------ */

/* Taste B zeigt auf der Seite Leitstand den Code fuer Home und blendet ihn
 * wieder aus. */
static volatile bool s_code_zeigen;
static bool s_code_gezeichnet;

static void leitstand_fest(void)
{
    M5.Display.fillRect(0, 23, 320, 198, GRUND);
    s_code_gezeichnet = false;
}

/* Code und QR-Code zum Hinzufuegen in Home. Einmal gezeichnet, nicht jede
 * Sekunde: Der QR-Code braucht dafuer zu lange. */
static void homekit_code(const st_hap_stand_t *hs)
{
    if (s_code_gezeichnet) {
        return;
    }
    s_code_gezeichnet = true;
    M5.Display.fillRect(0, 23, 320, 198, GRUND);
    text_frei(14, 48, "HomeKit", MITTEL, TINTE);
    if (hs->steuerungen > 0) {
        char s[48];
        snprintf(s, sizeof(s), "Gekoppelt mit %u %s.", hs->steuerungen, hs->steuerungen == 1 ? "Ger\xc3\xa4t" : "Ger\xc3\xa4ten");
        text_frei(14, 80, s, NORMAL, TINTE);
        text_frei(14, 104, "Weitere Personen l\xc3\xa4" "dt der", NORMAL, GEDAEMPFT);
        text_frei(14, 123, "Besitzer in der Home-App ein.", NORMAL, GEDAEMPFT);
        return;
    }
    text_frei(14, 80, "In der Home-App:", NORMAL, GEDAEMPFT);
    text_frei(14, 99, "Ger\xc3\xa4t hinzuf\xc3\xbcgen,", NORMAL, GEDAEMPFT);
    text_frei(14, 118, "QR-Code scannen", NORMAL, GEDAEMPFT);
    text_frei(14, 137, "oder Code eingeben:", NORMAL, GEDAEMPFT);
    text_frei(14, 176, hs->code, GROSS, WAERME);
    if (hs->nutzlast[0]) {
        M5.Display.fillRect(166, 36, 146, 146, 0xffffffu);
        M5.Display.qrcode(hs->nutzlast, 172, 42, 134, 2);
    }
}

static void zeile(int y, const char *name, const char *wert, uint32_t farbe = TINTE)
{
    ::wert(14, y, name, KLEIN, GEDAEMPFT, GRUND, lgfx::textdatum_t::baseline_left, 90, KLEIN_RAUM);
    ::wert(306, y, wert, KLEIN, farbe, GRUND, lgfx::textdatum_t::baseline_right, 210, KLEIN_RAUM);
}

static void leitstand_werte(const st_plant_t *p, const netmgr_status_t *net)
{
    char s[96];
    st_config_t cfg;
    st_cfg_copy(&cfg);

    if (net->ap_active && !net->sta_connected) {
        /* Einrichtung: Wer vor dem Geraet steht, braucht genau diese drei
         * Schritte. */
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        char ssid[40];
        snprintf(ssid, sizeof(ssid), "%.24s-%02X%02X", cfg.wifi.hostname[0] ? cfg.wifi.hostname : "floor-heating",
                 mac[4], mac[5]);
        M5.Display.fillRect(0, 23, 320, 198, GRUND);
        text_frei(14, 48, "Einrichtung", MITTEL, TINTE);
        text_frei(14, 76, "1. Verbinden Sie sich mit dem WLAN", NORMAL, TINTE);
        snprintf(s, sizeof(s), "    \xc2\xbb%s\xc2\xab.", ssid);
        text_frei(14, 95, s, NORMAL, WAERME);
        text_frei(14, 122, "2. \xc3\x96" "ffnen Sie im Browser", NORMAL, TINTE);
        snprintf(s, sizeof(s), "    http://%s", net->ap_ip[0] ? net->ap_ip : "192.168.4.1");
        text_frei(14, 141, s, NORMAL, WAERME);
        text_frei(14, 168, "3. W\xc3\xa4hlen Sie unter Einstellungen", NORMAL, TINTE);
        text_frei(14, 187, "    Ihr Heimnetz.", NORMAL, TINTE);
        return;
    }

    st_hap_stand_t hs;
    st_hap_stand(&hs);
    if (s_code_zeigen && hs.aktiv) {
        homekit_code(&hs);
        return;
    }

    /* Elf Zeilen; der Abstand ist so gewaehlt, dass die letzte ueber dem Fuss
     * endet. */
    const int ZEILE = 17;
    int y = 40;
    if (net->time_valid) {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        strftime(s, sizeof(s), "g\xc3\xbcltig \xc2\xb7 %d.%m. %H:%M:%S", &lt);
        zeile(y, "Zeit", s);
    } else {
        zeile(y, "Zeit", "noch nicht gestellt", WARNUNG);
    }
    y += ZEILE;
    if (net->sta_connected) {
        snprintf(s, sizeof(s), "%d dBm \xc2\xb7 %s", net->rssi, net->ip);
        zeile(y, "WLAN", s);
    } else {
        zeile(y, "WLAN", "nicht verbunden", WARNUNG);
    }
    y += ZEILE;
    snprintf(s, sizeof(s), "%u KB frei, Tiefstwert %u KB", (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(esp_get_minimum_free_heap_size() / 1024));
    zeile(y, "Speicher", s);
    y += ZEILE;

    int heiz = 0, heiz_da = 0, vert = 0, vert_da = 0;
    for (uint8_t i = 0; i < p->heat_count; i++) {
        heiz++;
        heiz_da += p->heat[i].dev.reachable;
    }
    for (uint8_t i = 0; i < p->manifold_count; i++) {
        vert++;
        vert_da += p->manifolds[i].dev.reachable;
    }
    snprintf(s, sizeof(s), "Heizung %d/%d \xc2\xb7 Verteiler %d/%d erreichbar", heiz_da, heiz, vert_da, vert);
    zeile(y, "Ger\xc3\xa4te", s, (heiz_da < heiz || vert_da < vert) ? WARNUNG : TINTE);
    y += ZEILE;

    const st_outdoor_t *o = &p->outdoor;
    if (!o->assigned) {
        zeile(y, "Au\xc3\x9f" "enf\xc3\xbchler", "keiner zugeordnet", GEDAEMPFT);
    } else if (!o->valid) {
        snprintf(s, sizeof(s), "%s \xc2\xb7 kein Empfang", o->mac);
        zeile(y, "Au\xc3\x9f" "enf\xc3\xbchler", s, WARNUNG);
    } else {
        char t[12];
        zahl(t, sizeof(t), o->temp_c, 1, "\xc2\xb0");
        snprintf(s, sizeof(s), "%s \xc2\xb7 %d dBm \xc2\xb7 vor %lu s", t, o->rssi, (unsigned long)o->age_s);
        zeile(y, "Au\xc3\x9f" "enf\xc3\xbchler", s, o->age_s > ST_OUTDOOR_STALE_S ? WARNUNG : TINTE);
    }
    y += ZEILE;

    st_log_status_t ls;
    st_log_status(&ls);
    if (!ls.karte) {
        zeile(y, "Protokoll", ls.meldung[0] ? ls.meldung : "keine Karte", WARNUNG);
    } else {
        snprintf(s, sizeof(s), "%lu MB frei \xc2\xb7 geschrieben %lu kB%s", (unsigned long)ls.frei_mb,
                 (unsigned long)(ls.heute_byte / 1024), ls.fehler ? " \xc2\xb7 Fehler" : "");
        zeile(y, "Protokoll", s, ls.fehler || ls.frei_mb * 10 < ls.groesse_mb ? WARNUNG : TINTE);
    }
    y += ZEILE;

    if (hs.aktiv) {
        if (hs.steuerungen > 0) {
            snprintf(s, sizeof(s), "%u gekoppelt \xc2\xb7 %u Zubeh\xc3\xb6r", hs.steuerungen, hs.zubehoer);
            zeile(y, "HomeKit", s);
        } else {
            zeile(y, "HomeKit", "bereit \xc2\xb7 Code mit Taste B", WAERME);
        }
    } else {
        zeile(y, "HomeKit", s_psram ? "l\xc3\xa4uft nicht" : "aus \xc2\xb7 braucht PSRAM", s_psram ? WARNUNG : GEDAEMPFT);
    }
    y += ZEILE;

    const esp_app_desc_t *d = esp_app_get_description();
    snprintf(s, sizeof(s), "%.24s \xc2\xb7 ESP-IDF %.8s", d->version, d->idf_ver + (d->idf_ver[0] == 'v'));
    zeile(y, "Firmware", s);
    y += ZEILE;
    char akku[24] = "";
    if (s_versorgt >= 0) {
        snprintf(akku, sizeof(akku), s_versorgt ? " \xc2\xb7 Netz" : " \xc2\xb7 Akku %d %%", s_akku);
    }
    snprintf(s, sizeof(s), "%s%s%s", s_board, s_psram ? " \xc2\xb7 PSRAM" : "", akku);
    zeile(y, "Ger\xc3\xa4t", s, s_versorgt == 0 ? WARNUNG : TINTE);
    y += ZEILE;
    snprintf(s, sizeof(s), "%s \xc2\xb7 %s.local", cfg.site, cfg.wifi.hostname);
    zeile(y, "Name", s);
}

/* ------------------------------------------------------------------ */
/* Ablauf                                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Seiten nur fuer den Core2 (PSRAM): Raeume, Verlauf, Meldungen,      */
/* Geraete. Sie zeichnen nur, wenn sich etwas geaendert hat; jede      */
/* Sekunde neu zu zeichnen liesse die Tabellen flackern.               */
/* ------------------------------------------------------------------ */

static constexpr uint32_t STOERUNG = 0xd9534fu;
static constexpr uint32_t PALETTE[] = {WAERME, KAELTE, GUT, WARNUNG, 0xb07cd6u, 0x4fb6b0u, 0xd66f9au, 0xa7b35fu};

/* Zustand der Seiten, von Taste B und Wischen umgeschaltet */
static int s_raum_seite;
static int s_raum_seiten = 1;
static bool s_verlauf_raeume;
static int s_melde_versatz;
static bool s_melde_mehr;
static bool s_geraete_funk;
/* zuletzt gezeichneter Stand; UINT32_MAX erzwingt Neuzeichnen */
static uint32_t s_gezeichnet = UINT32_MAX;

static void titelzusatz(const char *s)
{
    M5.Display.fillRect(150, 3, 70, 17, GRUND);
    if (s && s[0]) {
        text_frei(214, 16, s, NORMAL, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    }
}

/* Kuerzt einen Text auf eine Breite, mit Auslassungspunkten */
static void gekuerzt(char *out, size_t len, const char *s, const lgfx::IFont *f, int breite)
{
    M5.Display.setFont(f);
    snprintf(out, len, "%s", s);
    if (M5.Display.textWidth(out) <= breite) {
        return;
    }
    size_t n = strlen(out);
    while (n > 1) {
        /* ganze UTF-8-Zeichen entfernen */
        do {
            n--;
        } while (n > 0 && (out[n] & 0xC0) == 0x80);
        snprintf(out + n, len - n, "\xe2\x80\xa6");
        if (M5.Display.textWidth(out) <= breite) {
            return;
        }
    }
}

/* ---------------------------------------------------------- Raeume */

typedef struct {
    bool ueberschrift;
    const st_manifold_t *m;
    const st_room_t *r;
} raumzeile_t;

#define RAUMZEILEN 8

static void raeume_werte(const st_plant_t *p)
{
    uint32_t stand = st_poll_revision() * 16u + (uint32_t)s_raum_seite;
    if (stand == s_gezeichnet) {
        return;
    }
    s_gezeichnet = stand;

    /* im PSRAM: auf dem Core Basic gibt es diese Seite nicht */
    static raumzeile_t *z;
    if (z == nullptr) {
        z = (raumzeile_t *)heap_caps_malloc(sizeof(raumzeile_t) * ST_MAX_MANIFOLD * (ST_MAX_ROOMS + 1), MALLOC_CAP_SPIRAM);
        if (z == nullptr) {
            return;
        }
    }
    int n = 0;
    for (int m = 0; m < p->manifold_count; m++) {
        const st_manifold_t *mf = &p->manifolds[m];
        if (mf->room_count == 0) {
            continue;
        }
        z[n++] = {true, mf, nullptr};
        for (int k = 0; k < mf->room_count; k++) {
            z[n++] = {false, mf, &mf->rooms[k]};
        }
    }
    s_raum_seiten = n > 0 ? (n + RAUMZEILEN - 1) / RAUMZEILEN : 1;
    if (s_raum_seite >= s_raum_seiten) {
        s_raum_seite = 0;
    }
    char s[48];
    snprintf(s, sizeof(s), "%d/%d", s_raum_seite + 1, s_raum_seiten);
    titelzusatz(s_raum_seiten > 1 ? s : "");

    M5.Display.fillRect(0, 24, 320, 196, GRUND);
    text_frei(8, 38, "Raum", KLEIN, GEDAEMPFT);
    text_frei(176, 38, "Ist", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    text_frei(220, 38, "Soll", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    text_frei(230, 38, "Ventile", KLEIN, GEDAEMPFT);
    text_frei(312, 38, "rF", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    M5.Display.drawFastHLine(6, 44, 308, LINIE);
    if (n == 0) {
        text_frei(8, 70, "Noch kein Verteiler abgefragt.", NORMAL, GEDAEMPFT);
        return;
    }

    int y = 62;
    for (int i = s_raum_seite * RAUMZEILEN; i < n && i < (s_raum_seite + 1) * RAUMZEILEN; i++) {
        if (z[i].ueberschrift) {
            gekuerzt(s, sizeof(s), z[i].m->dev.site[0] ? z[i].m->dev.site : z[i].m->dev.id, KLEIN, 200);
            text_frei(8, y, s, KLEIN, z[i].m->dev.reachable ? GEDAEMPFT : WARNUNG);
            if (!z[i].m->dev.reachable) {
                text_frei(312, y, "nicht erreichbar", KLEIN, WARNUNG, lgfx::textdatum_t::baseline_right);
            }
        } else {
            const st_room_t *r = z[i].r;
            gekuerzt(s, sizeof(s), r->name[0] ? r->name : "Raum", NORMAL, 130);
            text_frei(8, y, s, NORMAL, TINTE);
            if (r->temp_valid) {
                bool kalt = r->heat && r->temp_c < r->target_c - 0.3f;
                zahl(s, sizeof(s), r->temp_c, 1, "");
                text_frei(176, y, s, NORMAL, kalt ? KAELTE : TINTE, lgfx::textdatum_t::baseline_right);
            } else {
                text_frei(176, y, "\xe2\x80\x93", NORMAL, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
            }
            if (r->heat) {
                zahl(s, sizeof(s), r->target_c, 1, "");
            } else {
                snprintf(s, sizeof(s), "aus");
            }
            text_frei(220, y, s, NORMAL, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
            M5.Display.fillRoundRect(230, y - 8, 44, 6, 3, LINIE);
            float pos = r->position < 0 ? 0 : r->position > 1 ? 1 : r->position;
            if (pos > 0.02f) {
                M5.Display.fillRoundRect(230, y - 8, (int)(6 + 38 * pos), 6, 3, WAERME);
            }
            if (r->hum_valid) {
                snprintf(s, sizeof(s), "%d %%", (int)lroundf(r->humidity));
                text_frei(312, y, s, NORMAL, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
            }
        }
        y += 19;
    }
}

/* --------------------------------------------------------- Verlauf */

static const int VX0 = 34, VX1 = 312, VY0 = 30, VY1 = 150;

static int vx(int i)
{
    return VX0 + (int)((long)i * (VX1 - VX0) / (SV_PLAETZE - 1));
}

static void kurve(int reihe, uint32_t erster, float lo, float hi, uint32_t farbe, bool gestrichelt)
{
    int px = -1, py = 0;
    for (int i = 0; i < SV_PLAETZE; i++) {
        float v;
        if (!sv_wert(reihe, erster + (uint32_t)i, &v)) {
            px = -1;
            continue;
        }
        if (v < lo) {
            v = lo;
        }
        if (v > hi) {
            v = hi;
        }
        int x = vx(i);
        int y = VY1 - (int)((v - lo) / (hi - lo) * (VY1 - VY0));
        if (px >= 0 && (!gestrichelt || (i / 3) % 2 == 0)) {
            M5.Display.drawLine(px, py, x, y, farbe);
            M5.Display.drawLine(px, py + 1, x, y + 1, farbe);
        }
        px = x;
        py = y;
    }
}

static void bereich(const int *reihen, int n, uint32_t erster, float schritt, float min_spanne, float *lo, float *hi)
{
    float a = INFINITY, b = -INFINITY;
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < SV_PLAETZE; i++) {
            float v;
            if (sv_wert(reihen[k], erster + (uint32_t)i, &v)) {
                a = fminf(a, v);
                b = fmaxf(b, v);
            }
        }
    }
    if (!isfinite(a)) {
        a = 0;
        b = min_spanne;
    }
    a = floorf(a / schritt) * schritt;
    b = ceilf(b / schritt) * schritt;
    if (b - a < min_spanne) {
        b = a + min_spanne;
    }
    *lo = a;
    *hi = b;
}

static void verlauf_werte(void)
{
    uint32_t stand = sv_revision() * 2u + (s_verlauf_raeume ? 1u : 0u);
    if (stand == s_gezeichnet) {
        return;
    }
    s_gezeichnet = stand;
    M5.Display.fillRect(0, 24, 320, 196, GRUND);
    uint32_t jetzt = sv_platz();
    if (jetzt == 0) {
        text_frei(8, 60, "Die Uhr ist noch nicht gestellt.", NORMAL, GEDAEMPFT);
        return;
    }
    uint32_t erster = jetzt - SV_PLAETZE + 1;

    float lo, hi, schritt;
    int reihen[SV_FEST + SV_RAEUME];
    int n = 0;
    const sv_raum_t *raeume;
    int anzahl = sv_raeume(&raeume);
    if (s_verlauf_raeume) {
        for (int i = 0; i < anzahl; i++) {
            reihen[n++] = SV_FEST + i;
        }
        schritt = 1.0f;
        bereich(reihen, n, erster, schritt, 4.0f, &lo, &hi);
        if (hi - lo > 8) {
            schritt = 2.0f;
        }
    } else {
        reihen[n++] = SV_SPEICHER;
        reihen[n++] = SV_KESSEL;
        reihen[n++] = SV_AUSSEN;
        schritt = 20.0f;
        bereich(reihen, n, erster, schritt, 80.0f, &lo, &hi);
        lo = fminf(lo, 0.0f);
    }
    char s[24];
    for (float v = lo; v <= hi + 0.01f; v += schritt) {
        int y = VY1 - (int)((v - lo) / (hi - lo) * (VY1 - VY0));
        M5.Display.drawFastHLine(VX0, y, VX1 - VX0, LINIE);
        zahl(s, sizeof(s), v, 0, "\xc2\xb0");
        text_frei(VX0 - 4, y + 4, s, KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    }

    if (s_verlauf_raeume) {
        for (int i = 0; i < n; i++) {
            kurve(reihen[i], erster, lo, hi, PALETTE[i % 8], i >= 8);
        }
        /* Legende: so viele Raeume, wie in zwei Zeilen passen */
        int x = 8, y = 190;
        for (int i = 0; i < anzahl && y <= 210; i++) {
            char name[24];
            gekuerzt(name, sizeof(name), raeume[i].name, KLEIN, 80);
            M5.Display.setFont(KLEIN);
            int w = M5.Display.textWidth(name) + 20;
            if (x + w > 316) {
                x = 8;
                y += 16;
                if (y > 210) {
                    break;
                }
            }
            M5.Display.fillRect(x, y - 5, 10, 3, PALETTE[i % 8]);
            text_frei(x + 13, y, name, KLEIN, GEDAEMPFT);
            x += w;
        }
    } else {
        /* Brennerlauf als Balken unter dem Diagramm */
        M5.Display.fillRoundRect(VX0, 156, VX1 - VX0, 8, 3, FLAECHE);
        for (int i = 0; i < SV_PLAETZE; i++) {
            float b;
            if (sv_wert(SV_BRENNER, erster + (uint32_t)i, &b) && b > 0.05f) {
                M5.Display.fillRect(vx(i), 157, 2, 6, b >= 0.5f ? WAERME : WAERME_DUNKEL);
            }
        }
        kurve(SV_AUSSEN, erster, lo, hi, KAELTE, false);
        kurve(SV_KESSEL, erster, lo, hi, WAERME, true);
        kurve(SV_SPEICHER, erster, lo, hi, WAERME, false);
        int y = 206;
        M5.Display.fillRect(8, y - 5, 14, 3, WAERME);
        text_frei(26, y, "Speicher", KLEIN, GEDAEMPFT);
        for (int k = 0; k < 3; k++) {
            M5.Display.fillRect(92 + k * 5, y - 5, 3, 3, WAERME);
        }
        text_frei(110, y, "Kessel", KLEIN, GEDAEMPFT);
        M5.Display.fillRect(166, y - 5, 14, 3, KAELTE);
        text_frei(184, y, "Au\xc3\x9f" "en", KLEIN, GEDAEMPFT);
        M5.Display.fillRoundRect(236, y - 7, 14, 7, 3, WAERME);
        text_frei(254, y, "Brenner", KLEIN, GEDAEMPFT);
    }
    const char *marken[] = {"\xe2\x88\x92" "24 h", "\xe2\x88\x92" "18 h", "\xe2\x88\x92" "12 h", "\xe2\x88\x92" "6 h", "jetzt"};
    for (int k = 0; k < 5; k++) {
        int x = vx(k * (SV_PLAETZE - 1) / 4);
        lgfx::textdatum_t lage = k == 0 ? lgfx::textdatum_t::baseline_left
                                 : k == 4 ? lgfx::textdatum_t::baseline_right
                                          : lgfx::textdatum_t::baseline_center;
        text_frei(x, s_verlauf_raeume ? 172 : 180, marken[k], KLEIN, GEDAEMPFT, lage);
    }
}

/* ------------------------------------------------------- Meldungen */

/* Titel eines Befunds der Firmware wie in der App (Befundregeln.swift) */
static const char *befundtitel(const char *code, const char *text)
{
    if (strcmp(code, "backflow") == 0) {
        return "Warmes Wasser str\xc3\xb6mt in den Kesselr\xc3\xbc" "cklauf";
    }
    if (strcmp(code, "flow_swapped") == 0) {
        return "Vorlauf und R\xc3\xbc" "cklauf vermutlich vertauscht";
    }
    if (strcmp(code, "probe_errors") == 0) {
        return "Ein F\xc3\xbchler verwirft viele Messungen";
    }
    if (strcmp(code, "day_above_trend") == 0) {
        return "Mehr verbraucht, als zur Au\xc3\x9f" "enlage passt";
    }
    if (strcmp(code, "flue_gap_rising") == 0) {
        return "Kessel \xc3\xbc" "bertr\xc3\xa4gt schlechter als nach der Reinigung";
    }
    return text[0] ? text : code;
}

static const char *geraetename(const st_plant_t *p, const char *id)
{
    for (int i = 0; i < p->heat_count; i++) {
        if (strcmp(p->heat[i].dev.id, id) == 0 && p->heat[i].dev.site[0]) {
            return p->heat[i].dev.site;
        }
    }
    for (int i = 0; i < p->manifold_count; i++) {
        if (strcmp(p->manifolds[i].dev.id, id) == 0 && p->manifolds[i].dev.site[0]) {
            return p->manifolds[i].dev.site;
        }
    }
    return strncmp(id, "lst_", 4) == 0 ? "Leitstand" : id;
}

static const char *raumname(const st_plant_t *p, const char *id, int raum)
{
    for (int i = 0; i < p->manifold_count; i++) {
        if (strcmp(p->manifolds[i].dev.id, id) == 0) {
            for (int k = 0; k < p->manifolds[i].room_count; k++) {
                if (p->manifolds[i].rooms[k].id == raum) {
                    return p->manifolds[i].rooms[k].name;
                }
            }
        }
    }
    return "Raum";
}

static void dauer_text(char *out, size_t len, double s)
{
    long m = lround(s / 60.0);
    if (m < 60) {
        snprintf(out, len, "%ld min", m);
    } else {
        snprintf(out, len, "%ld h %ld min", m / 60, m % 60);
    }
}

/* Ein Ereignis als eine Zeile, wie sie auf die Anzeige passt */
static uint32_t ereignistext(const cJSON *e, const st_plant_t *p, char *out, size_t len)
{
    const char *art = cJSON_GetStringValue(cJSON_GetObjectItem(e, "art"));
    const char *geraet = cJSON_GetStringValue(cJSON_GetObjectItem(e, "geraet"));
    const cJSON *ein = cJSON_GetObjectItem(e, "ein");
    const cJSON *vorher = cJSON_GetObjectItem(e, "dauer_vorher_s");
    char d[24] = "";
    if (!art || !geraet) {
        snprintf(out, len, "?");
        return GEDAEMPFT;
    }
    if (strcmp(art, "brenner") == 0) {
        if (cJSON_IsFalse(ein) && cJSON_IsNumber(vorher)) {
            dauer_text(d, sizeof(d), vorher->valuedouble);
            snprintf(out, len, "Brenner aus, Lauf %s", d);
        } else {
            snprintf(out, len, "Brenner %s", cJSON_IsTrue(ein) ? "ein" : "aus");
        }
        return TINTE;
    }
    if (strcmp(art, "pumpe") == 0) {
        const char *pu = cJSON_GetStringValue(cJSON_GetObjectItem(e, "pumpe"));
        if (pu && strcmp(pu, "kkp") == 0) {
            snprintf(out, len, "Kesselkreispumpe %s", cJSON_IsTrue(ein) ? "ein" : "aus");
        } else {
            snprintf(out, len, "Pumpe HK %s %s", pu ? pu : "?", cJSON_IsTrue(ein) ? "ein" : "aus");
        }
        return TINTE;
    }
    if (strcmp(art, "sollwert") == 0) {
        char a[12], b[12];
        const cJSON *alt = cJSON_GetObjectItem(e, "alt"), *neu = cJSON_GetObjectItem(e, "neu");
        zahl(a, sizeof(a), cJSON_IsNumber(alt) ? (float)alt->valuedouble : 0, 1, "");
        zahl(b, sizeof(b), cJSON_IsNumber(neu) ? (float)neu->valuedouble : 0, 1, "");
        const cJSON *r = cJSON_GetObjectItem(e, "raum");
        snprintf(out, len, "%s Soll %s \xe2\x86\x92 %s", raumname(p, geraet, cJSON_IsNumber(r) ? r->valueint : -1), a, b);
        return TINTE;
    }
    if (strcmp(art, "betriebsart") == 0) {
        const char *neu = cJSON_GetStringValue(cJSON_GetObjectItem(e, "neu"));
        const cJSON *r = cJSON_GetObjectItem(e, "raum");
        snprintf(out, len, "%s %s", raumname(p, geraet, cJSON_IsNumber(r) ? r->valueint : -1),
                 neu && strcmp(neu, "aus") == 0 ? "ausgeschaltet" : "eingeschaltet");
        return TINTE;
    }
    if (strcmp(art, "neustart") == 0) {
        const char *g = cJSON_GetStringValue(cJSON_GetObjectItem(e, "grund"));
        bool schlimm = g && (strstr(g, "panic") || strstr(g, "wdt") || strstr(g, "brownout"));
        snprintf(out, len, "Neustart%s%s", g ? ", " : "", g ? g : "");
        return schlimm ? STOERUNG : WARNUNG;
    }
    if (strcmp(art, "nicht_erreichbar") == 0) {
        snprintf(out, len, "nicht erreichbar");
        return WARNUNG;
    }
    if (strcmp(art, "erreichbar") == 0) {
        const cJSON *ds = cJSON_GetObjectItem(e, "dauer_s");
        if (cJSON_IsNumber(ds)) {
            dauer_text(d, sizeof(d), ds->valuedouble);
        }
        snprintf(out, len, "wieder erreichbar%s%s", d[0] ? " nach " : "", d);
        return TINTE;
    }
    if (strcmp(art, "version") == 0) {
        const char *neu = cJSON_GetStringValue(cJSON_GetObjectItem(e, "neu"));
        snprintf(out, len, "Firmware %s", neu ? neu : "");
        return TINTE;
    }
    if (strcmp(art, "befund") == 0) {
        const char *code = cJSON_GetStringValue(cJSON_GetObjectItem(e, "code"));
        const char *stand = cJSON_GetStringValue(cJSON_GetObjectItem(e, "stand"));
        snprintf(out, len, "%s: %s", stand && strcmp(stand, "erledigt") == 0 ? "erledigt" : "Befund",
                 befundtitel(code ? code : "", ""));
        return stand && strcmp(stand, "erledigt") == 0 ? TINTE : WARNUNG;
    }
    if (strcmp(art, "funk") == 0) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(e, "name"));
        const char *stand = cJSON_GetStringValue(cJSON_GetObjectItem(e, "stand"));
        snprintf(out, len, "%s %s", name ? name : "Funk",
                 stand && strcmp(stand, "wieder") == 0 ? "wieder da"
                 : stand && strcmp(stand, "verloren") == 0 ? "stumm" : "Schl\xc3\xbcssel falsch");
        return stand && strcmp(stand, "wieder") == 0 ? TINTE : WARNUNG;
    }
    if (strcmp(art, "leitstand") == 0) {
        const char *was = cJSON_GetStringValue(cJSON_GetObjectItem(e, "was"));
        if (was && strcmp(was, "versorgung_aus") == 0) {
            snprintf(out, len, "Versorgung aus, l\xc3\xa4uft auf Akku");
            return STOERUNG;
        }
        snprintf(out, len, "%s", was && strcmp(was, "start") == 0 ? "gestartet"
                                 : was && strcmp(was, "versorgung_wieder") == 0 ? "Versorgung wieder da"
                                 : was ? was : "");
        return TINTE;
    }
    snprintf(out, len, "%s", art);
    return TINTE;
}

#define MELDEZEILEN_MAX 9

static void meldungen_werte(const st_plant_t *p)
{
    uint32_t stand = st_poll_revision() * 64u + (uint32_t)s_melde_versatz;
    if (stand == s_gezeichnet) {
        return;
    }
    s_gezeichnet = stand;
    M5.Display.fillRect(0, 24, 320, 196, GRUND);

    /* Offene Befunde als Karten, hoechstens zwei */
    int y = 28;
    int karten = 0;
    char s[96], t[64];
    for (int i = 0; i < p->heat_count && karten < 2 && s_melde_versatz == 0; i++) {
        const st_heat_t *h = &p->heat[i];
        if (h->findings == 0) {
            continue;
        }
        karte(6, y, 308, 44);
        M5.Display.fillRect(6, y + 4, 3, 36, WARNUNG);
        gekuerzt(s, sizeof(s), befundtitel(h->finding_code, h->finding), NORMAL, 292);
        text_frei(16, y + 18, s, NORMAL, TINTE);
        snprintf(t, sizeof(t), "%s%s", h->dev.site, h->findings > 1 ? " \xc2\xb7 weitere Befunde" : "");
        gekuerzt(s, sizeof(s), t, KLEIN, 292);
        text_frei(16, y + 36, s, KLEIN, GEDAEMPFT);
        y += 50;
        karten++;
    }

    /* Ereignisse von heute, neueste zuerst, von der Karte */
    int platz = (212 - (y + 18)) / 17;
    platz = platz < 1 ? 1 : platz > MELDEZEILEN_MAX ? MELDEZEILEN_MAX : platz;
    text_frei(8, y + 12, s_melde_versatz ? "Ereignisse heute, \xc3\xa4lter" : "Ereignisse heute", KLEIN, GEDAEMPFT);
    y += 30;

    char pfad[128];
    time_t jetzt = time(NULL);
    s_melde_mehr = false;
    if (jetzt < 1700000000 || !st_log_datei((uint32_t)jetzt, "ereignisse.jsonl", pfad, sizeof(pfad))) {
        text_frei(8, y, "Kein Protokoll auf der Karte.", NORMAL, GEDAEMPFT);
        return;
    }
    FILE *f = fopen(pfad, "r");
    if (f == NULL) {
        text_frei(8, y, "Heute noch keine Ereignisse.", NORMAL, GEDAEMPFT);
        return;
    }
    /* Das Ende der Datei genuegt: hoechstens so viele Zeilen, wie gezeigt und
     * uebersprungen werden. */
    const size_t puffer = 24 * 1024;
    char *b = (char *)heap_caps_malloc(puffer + 1, MALLOC_CAP_SPIRAM);
    if (b == NULL) {
        fclose(f);
        return;
    }
    fseek(f, 0, SEEK_END);
    long groesse = ftell(f);
    long ab = groesse > (long)puffer ? groesse - (long)puffer : 0;
    fseek(f, ab, SEEK_SET);
    size_t n = fread(b, 1, puffer, f);
    fclose(f);
    b[n] = '\0';

    int uebersprungen = 0, gezeigt = 0;
    char *ende = b + n;
    while (ende > b && gezeigt < platz) {
        char *z = ende - 1;
        while (z > b && *(z - 1) != '\n') {
            z--;
        }
        if (z == b && ab > 0) {
            break; /* angeschnittene erste Zeile */
        }
        *(ende - (ende > b && *(ende - 1) == '\n' ? 1 : 0)) = '\0';
        ende = z;
        if (*z == '\0') {
            continue;
        }
        if (uebersprungen < s_melde_versatz) {
            uebersprungen++;
            continue;
        }
        cJSON *e = cJSON_Parse(z);
        if (e == NULL) {
            continue;
        }
        uint32_t zeit = 0;
        pk_ereignis_zeit(z, &zeit);
        time_t tt = (time_t)zeit;
        struct tm lt;
        localtime_r(&tt, &lt);
        strftime(t, sizeof(t), "%H:%M", &lt);
        text_frei(8, y, t, NORMAL, GEDAEMPFT);
        const char *g = cJSON_GetStringValue(cJSON_GetObjectItem(e, "geraet"));
        gekuerzt(s, sizeof(s), g ? geraetename(p, g) : "", NORMAL, 96);
        text_frei(52, y, s, NORMAL, TINTE);
        uint32_t farbe = ereignistext(e, p, t, sizeof(t));
        gekuerzt(s, sizeof(s), t, NORMAL, 160);
        text_frei(154, y, s, NORMAL, farbe);
        cJSON_Delete(e);
        y += 17;
        gezeigt++;
    }
    s_melde_mehr = ende > b;
    free(b);
    if (gezeigt == 0) {
        text_frei(8, y, s_melde_versatz ? "Keine \xc3\xa4lteren Ereignisse heute." : "Heute noch keine Ereignisse.",
                  NORMAL, GEDAEMPFT);
    }
}

/* --------------------------------------------------------- Geraete */

static void laufzeit_text(char *out, size_t len, uint32_t s)
{
    if (s < 3600) {
        snprintf(out, len, "%lu min", (unsigned long)(s / 60));
    } else if (s < 86400) {
        snprintf(out, len, "%lu h", (unsigned long)(s / 3600));
    } else if (s < 10 * 86400) {
        snprintf(out, len, "%lu T %lu h", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600));
    } else {
        snprintf(out, len, "%lu T", (unsigned long)(s / 86400));
    }
}

/* v0.4.0-1-g47399df-dirty → 0.4.0 */
static void fassung(char *out, size_t len, const char *v)
{
    snprintf(out, len, "%s", v[0] == 'v' ? v + 1 : v);
    out[strcspn(out, "-+ ")] = '\0';
}

static void geraetezeile(int y, bool da, const char *name, int rssi, uint32_t laufzeit, uint32_t heap, const char *version)
{
    char s[32];
    M5.Display.fillCircle(12, y - 5, 4, da ? GUT : STOERUNG);
    gekuerzt(s, sizeof(s), name, NORMAL, 110);
    text_frei(22, y, s, NORMAL, TINTE);
    if (!da) {
        text_frei(312, y, "nicht erreichbar", KLEIN, STOERUNG, lgfx::textdatum_t::baseline_right);
        return;
    }
    snprintf(s, sizeof(s), "%d", rssi);
    char minus[12];
    zahl(minus, sizeof(minus), (float)rssi, 0, "");
    text_frei(158, y, minus, NORMAL, rssi < -75 ? WARNUNG : GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    laufzeit_text(s, sizeof(s), laufzeit);
    text_frei(222, y, s, NORMAL, laufzeit < 86400 ? WARNUNG : GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    snprintf(s, sizeof(s), "%lu KB", (unsigned long)(heap / 1024));
    text_frei(268, y, s, NORMAL, heap < 20 * 1024 ? WARNUNG : GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    fassung(s, sizeof(s), version);
    gekuerzt(minus, sizeof(minus), s, KLEIN, 40);
    text_frei(312, y, minus, KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
}

static const char *schluesseltext(atc_key_state_t k, uint32_t *farbe)
{
    switch (k) {
    case ATC_KEY_OK:
        *farbe = GUT;
        return "Schl\xc3\xbcssel g\xc3\xbcltig";
    case ATC_KEY_MISSING:
        *farbe = WARNUNG;
        return "Schl\xc3\xbcssel fehlt";
    case ATC_KEY_WRONG:
        *farbe = STOERUNG;
        return "Schl\xc3\xbcssel falsch";
    default:
        *farbe = GEDAEMPFT;
        return "unverschl\xc3\xbcsselt";
    }
}

static void funkwerte(char *out, size_t len, const atc_device_t *d)
{
    char t[16] = "\xe2\x80\x93", rssi[12];
    if (d->has_temp) {
        zahl(t, sizeof(t), d->temp_c, 1, "\xc2\xb0");
    }
    zahl(rssi, sizeof(rssi), (float)d->rssi, 0, " dBm");
    char h[16] = "";
    if (d->has_humidity) {
        snprintf(h, sizeof(h), " \xc2\xb7 %d %% rF", (int)lroundf(d->humidity));
    }
    char b[24] = "";
    if (d->battery) {
        snprintf(b, sizeof(b), " \xc2\xb7 Batterie %u %%", d->battery);
    }
    snprintf(out, len, "%s%s%s \xc2\xb7 %s", t, h, b, rssi);
}

static void geraete_werte(const st_plant_t *p, const netmgr_status_t *net)
{
    uint32_t stand = st_poll_revision() * 2u + (s_geraete_funk ? 1u : 0u);
    /* Die eigene Zeile aendert sich auch ohne neue Abfrage; einmal je Minute */
    static uint32_t minute;
    uint32_t m = jetzt_ms() / 60000u;
    if (stand == s_gezeichnet && m == minute) {
        return;
    }
    s_gezeichnet = stand;
    minute = m;
    M5.Display.fillRect(0, 24, 320, 196, GRUND);

    static atc_device_t *funk;
    if (funk == nullptr) {
        funk = (atc_device_t *)heap_caps_malloc(sizeof(atc_device_t) * ATC_MAX_DEVICES, MALLOC_CAP_SPIRAM);
        if (funk == nullptr) {
            return;
        }
    }
    size_t nf = atc_ble_devices(funk, ATC_MAX_DEVICES);
    char s[96];

    if (s_geraete_funk) {
        text_frei(8, 38, "Funkthermometer in Reichweite", KLEIN, GEDAEMPFT);
        M5.Display.drawFastHLine(6, 44, 308, LINIE);
        int y = 62;
        for (size_t i = 0; i < nf && y < 214; i++) {
            const atc_device_t *d = &funk[i];
            char mac[18];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", d->mac[0], d->mac[1], d->mac[2], d->mac[3],
                     d->mac[4], d->mac[5]);
            bool aussen = p->outdoor.assigned && strcasecmp(mac, p->outdoor.mac) == 0;
            snprintf(s, sizeof(s), "%s%s", d->name[0] ? d->name : mac, aussen ? " (au\xc3\x9f" "en)" : "");
            char g[48];
            gekuerzt(g, sizeof(g), s, NORMAL, 170);
            text_frei(8, y, g, NORMAL, TINTE);
            uint32_t farbe;
            const char *k = schluesseltext(d->key, &farbe);
            if (d->encrypted) {
                text_frei(312, y, k, KLEIN, farbe, lgfx::textdatum_t::baseline_right);
            }
            funkwerte(s, sizeof(s), d);
            gekuerzt(g, sizeof(g), s, KLEIN, 300);
            text_frei(8, y + 15, g, KLEIN, GEDAEMPFT);
            y += 36;
        }
        if (nf == 0) {
            text_frei(8, 62, "Kein Funkthermometer empfangen.", NORMAL, GEDAEMPFT);
        }
        return;
    }

    text_frei(8, 38, "Ger\xc3\xa4t", KLEIN, GEDAEMPFT);
    text_frei(158, 38, "WLAN", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    text_frei(222, 38, "Laufzeit", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    text_frei(268, 38, "frei", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    text_frei(312, 38, "Fassung", KLEIN, GEDAEMPFT, lgfx::textdatum_t::baseline_right);
    M5.Display.drawFastHLine(6, 44, 308, LINIE);
    int y = 62;
    for (int i = 0; i < p->heat_count && y < 170; i++) {
        const st_dev_t *d = &p->heat[i].dev;
        geraetezeile(y, d->reachable, d->site[0] ? d->site : d->id, d->rssi, d->uptime_s, d->heap, d->version);
        y += 18;
    }
    for (int i = 0; i < p->manifold_count && y < 170; i++) {
        const st_dev_t *d = &p->manifolds[i].dev;
        geraetezeile(y, d->reachable, d->site[0] ? d->site : d->id, d->rssi, d->uptime_s, d->heap, d->version);
        y += 18;
    }
    st_config_t cfg;
    st_cfg_copy(&cfg);
    geraetezeile(y, true, cfg.site[0] ? cfg.site : "Leitstand", net->sta_connected ? net->rssi : 0,
                 (uint32_t)(esp_timer_get_time() / 1000000), esp_get_free_heap_size(), esp_app_get_description()->version);

    /* Aussenfuehler */
    const st_outdoor_t *o = &p->outdoor;
    M5.Display.drawFastHLine(6, 178, 308, LINIE);
    if (!o->assigned) {
        text_frei(8, 198, "Kein Au\xc3\x9f" "enf\xc3\xbchler zugeordnet", NORMAL, GEDAEMPFT);
        return;
    }
    const atc_device_t *d = nullptr;
    for (size_t i = 0; i < nf; i++) {
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", funk[i].mac[0], funk[i].mac[1], funk[i].mac[2],
                 funk[i].mac[3], funk[i].mac[4], funk[i].mac[5]);
        if (strcasecmp(mac, o->mac) == 0) {
            d = &funk[i];
        }
    }
    snprintf(s, sizeof(s), "%s au\xc3\x9f" "en", o->name[0] ? o->name : "F\xc3\xbchler");
    char g[48];
    gekuerzt(g, sizeof(g), s, NORMAL, 180);
    text_frei(8, 196, g, NORMAL, TINTE);
    if (d == nullptr) {
        text_frei(312, 196, "kein Empfang", KLEIN, WARNUNG, lgfx::textdatum_t::baseline_right);
        return;
    }
    uint32_t farbe;
    const char *k = schluesseltext(d->key, &farbe);
    if (d->encrypted) {
        text_frei(312, 196, k, KLEIN, farbe, lgfx::textdatum_t::baseline_right);
    }
    funkwerte(s, sizeof(s), d);
    gekuerzt(g, sizeof(g), s, KLEIN, 300);
    text_frei(8, 213, g, KLEIN, GEDAEMPFT);
}

/* ------------------------------------------------------------------ */
/* Seitenwahl                                                          */
/* ------------------------------------------------------------------ */

static bool verfuegbar(int seite)
{
    return s_psram || seite == SEITE_ANLAGE || seite == SEITE_LEITSTAND;
}

static int nachbar(int seite, int richtung)
{
    int n = seite;
    do {
        n = (n + richtung + SEITEN) % SEITEN;
    } while (!verfuegbar(n) && n != seite);
    return n;
}

/* Was Taste B auf der Seite tut; leer, wenn nichts */
static const char *b_text(int seite)
{
    switch (seite) {
    case SEITE_RAEUME:
        return s_raum_seiten > 1 ? "Weiter" : "";
    case SEITE_VERLAUF:
        return s_verlauf_raeume ? "Anlage" : "Raumwerte";
    case SEITE_MELDUNGEN:
        return s_melde_versatz ? (s_melde_mehr ? "\xc3\x84ltere" : "Neueste") : "\xc3\x84ltere";
    case SEITE_GERAETE:
        return s_geraete_funk ? "Ger\xc3\xa4te" : "Funk";
    case SEITE_LEITSTAND: {
        st_hap_stand_t hs;
        st_hap_stand(&hs);
        return hs.aktiv ? (s_code_zeigen ? "Zur\xc3\xbc" "ck" : "HomeKit") : "";
    }
    default:
        return "";
    }
}

/* Taste B oder das mittlere Feld: innerhalb der Seite umschalten */
static void b_druecken(int seite)
{
    switch (seite) {
    case SEITE_RAEUME:
        s_raum_seite = (s_raum_seite + 1) % (s_raum_seiten > 0 ? s_raum_seiten : 1);
        break;
    case SEITE_VERLAUF:
        s_verlauf_raeume = !s_verlauf_raeume;
        break;
    case SEITE_MELDUNGEN:
        s_melde_versatz = s_melde_mehr ? s_melde_versatz + 8 : 0;
        break;
    case SEITE_GERAETE:
        s_geraete_funk = !s_geraete_funk;
        break;
    case SEITE_LEITSTAND:
        s_code_zeigen = !s_code_zeigen;
        break;
    default:
        return;
    }
    s_neu_lesen = true;
}

static void fuss_zeichnen(int seite)
{
    char a[32], c[32];
    snprintf(a, sizeof(a), "\xe2\x80\xb9 %s", SEITENKURZ[nachbar(seite, -1)]);
    snprintf(c, sizeof(c), "%s \xe2\x80\xba", SEITENKURZ[nachbar(seite, 1)]);
    fuss(a, b_text(seite), c);
}

static void seite_fest(int seite)
{
    kopf_fest(seite);
    s_gezeichnet = UINT32_MAX;
    switch (seite) {
    case SEITE_ANLAGE:
        anlage_fest();
        break;
    case SEITE_LEITSTAND:
        leitstand_fest();
        break;
    default:
        M5.Display.fillRect(0, 23, 320, 198, GRUND);
        break;
    }
    fuss_zeichnen(seite);
}

static void seite_werte(int seite, const st_plant_t *p, const netmgr_status_t *net)
{
    kopf_werte(p, net);
    switch (seite) {
    case SEITE_ANLAGE:
        anlage_werte(p);
        break;
    case SEITE_RAEUME: {
        int seiten = s_raum_seiten;
        raeume_werte(p);
        if (seiten != s_raum_seiten) {
            fuss_zeichnen(seite); /* „Weiter“ erst, wenn es mehr als eine Seite gibt */
        }
        break;
    }
    case SEITE_VERLAUF:
        verlauf_werte();
        break;
    case SEITE_MELDUNGEN: {
        bool mehr = s_melde_mehr;
        meldungen_werte(p);
        if (mehr != s_melde_mehr) {
            fuss_zeichnen(seite); /* „Aeltere“ oder „Neueste“ */
        }
        break;
    }
    case SEITE_GERAETE:
        geraete_werte(p, net);
        break;
    default:
        leitstand_werte(p, net);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Echtzeituhr und Versorgung (Core2)                                  */
/* ------------------------------------------------------------------ */

/* Nach einem Stromausfall hat das Geraet keine Zeit, bis das Netz sie
 * liefert, und das Protokoll schreibt nichts. Die Echtzeituhr des Core2
 * ueberbrueckt das; sie haelt UTC. */
static void uhr_lesen(void)
{
    m5::rtc_datetime_t dt;
    if (!M5.Rtc.isEnabled() || time(NULL) > 1700000000 || !M5.Rtc.getDateTime(&dt) || dt.date.year < 2026) {
        return;
    }
    /* Tage seit 1970 nach dem Kalender, ohne Zeitzone */
    int y = dt.date.year - (dt.date.month <= 2);
    int era = y / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(dt.date.month + (dt.date.month > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)dt.date.date - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    long tage = era * 146097L + (long)doe - 719468L;
    struct timeval tv = {.tv_sec = (time_t)(tage * 86400L + dt.time.hours * 3600L + dt.time.minutes * 60L + dt.time.seconds),
                         .tv_usec = 0};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Zeit aus der Echtzeituhr: %04d-%02d-%02d %02d:%02d UTC", dt.date.year, dt.date.month, dt.date.date,
             dt.time.hours, dt.time.minutes);
}

/* Stellt die Echtzeituhr stuendlich nach und meldet Ausfall und Rueckkehr
 * der Versorgung als Ereignis. Ausserhalb der Sperre der Anzeige aufrufen. */
static void uhr_und_versorgung(uint32_t t)
{
    static uint32_t uhr_ms, strom_ms;
    if (M5.Rtc.isEnabled() && time(NULL) > 1700000000 && (uhr_ms == 0 || t - uhr_ms > 3600000u)) {
        time_t jetzt = time(NULL);
        struct tm u;
        gmtime_r(&jetzt, &u);
        M5.Rtc.setDateTime(&u);
        uhr_ms = t;
    }
    if (strom_ms != 0 && t - strom_ms < 5000) {
        return;
    }
    strom_ms = t;
    int16_t vbus = M5.Power.getVBUSVoltage();
    if (vbus < 0) {
        return; /* Core Basic: nicht messbar */
    }
    int versorgt = vbus > 4000 ? 1 : 0;
    s_akku = (int)M5.Power.getBatteryLevel();
    if (versorgt != s_versorgt) {
        bool erstes = s_versorgt < 0;
        s_versorgt = versorgt;
        if (!erstes || !versorgt) {
            char f[40];
            snprintf(f, sizeof(f), "\"akku_prozent\":%d", s_akku);
            st_log_leitstand(versorgt ? "versorgung_wieder" : "versorgung_aus", f);
            ESP_LOGW(TAG, "Versorgung %s, Akku %d %%", versorgt ? "wieder da" : "ausgefallen", s_akku);
        }
    }
}

/* Nachts aus: von <= Stunde < bis, ueber Mitternacht hinweg. */
static bool nachts(const st_config_t *cfg, const netmgr_status_t *net)
{
    if (cfg->night_from_h < 0 || !net->time_valid || cfg->night_from_h == cfg->night_to_h) {
        return false;
    }
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    int h = lt.tm_hour;
    if (cfg->night_from_h < cfg->night_to_h) {
        return h >= cfg->night_from_h && h < cfg->night_to_h;
    }
    return h >= cfg->night_from_h || h < cfg->night_to_h;
}

/*
 * Beruehrung (Core2). Die drei Felder unter dem Bildschirm meldet M5Unified
 * ohnehin als Tasten A, B und C. Auf dem Bildschirm blaettert Wischen nach
 * links oder rechts, Wischen nach oben oder unten schaltet wie Taste B
 * innerhalb der Seite, und Tippen auf den Fuss wirkt wie die Taste darunter.
 * Die erste Beruehrung eines dunklen Bildschirms weckt ihn nur.
 */
static void beruehrung(uint32_t t, uint32_t *bedient_ms, int helligkeit, int hell)
{
    if (!M5.Touch.isEnabled()) {
        return;
    }
    int n = M5.Touch.getCount();
    for (int i = 0; i < n; i++) {
        auto d = M5.Touch.getDetail(i);
        if (d.base_y >= 240 || (!d.wasFlicked() && !d.wasClicked())) {
            continue;
        }
        bool dunkel = !s_an || helligkeit < hell;
        *bedient_ms = t;
        if (dunkel) {
            s_an = true;
            return;
        }
        if (d.wasFlicked()) {
            int dx = d.distanceX(), dy = d.distanceY();
            if (abs(dx) >= abs(dy) && abs(dx) > 40) {
                s_seite = nachbar(s_seite, dx < 0 ? 1 : -1);
            } else if (abs(dy) > 30) {
                b_druecken(s_seite);
            }
        } else if (d.y >= 221) {
            if (d.x < 107) {
                s_seite = nachbar(s_seite, -1);
            } else if (d.x < 213) {
                b_druecken(s_seite);
            } else {
                s_seite = nachbar(s_seite, 1);
            }
        }
        return;
    }
}

/* Ein neuer Befund schaltet die Anzeige ein und zeigt die Seite Meldungen
 * (nur mit PSRAM, wo es die Seite gibt). */
static void befunde_beobachten(uint32_t *bedient_ms)
{
    static uint32_t rev = UINT32_MAX;
    static int vorher = -1;
    static st_plant_t *q;
    if (!s_psram) {
        return;
    }
    uint32_t r = st_poll_revision();
    if (r == rev) {
        return;
    }
    rev = r;
    if (q == nullptr) {
        q = (st_plant_t *)heap_caps_malloc(sizeof(st_plant_t), MALLOC_CAP_SPIRAM);
        if (q == nullptr) {
            return;
        }
    }
    st_poll_snapshot(q);
    int n = 0;
    for (int i = 0; i < q->heat_count; i++) {
        n += q->heat[i].dev.reachable ? q->heat[i].findings : 0;
    }
    if (vorher >= 0 && n > vorher) {
        s_an = true;
        *bedient_ms = jetzt_ms();
        s_seite = SEITE_MELDUNGEN;
        s_melde_versatz = 0;
        s_neu_lesen = true;
    }
    vorher = n;
}

static void ui_task(void *arg)
{
    (void)arg;
    static st_plant_t p;
    int gezeigt = -1;
    uint32_t revision = 0;
    uint32_t zuletzt_ms = 0;
    uint32_t bedient_ms = jetzt_ms();
    int helligkeit = -1;
    bool lang_gedrueckt = false;
    netmgr_status_t net_alt = {};

    for (;;) {
        M5.update();
        uint32_t t = jetzt_ms();
        bool gedrueckt = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();

        st_config_t cfg;
        st_cfg_copy(&cfg);
        netmgr_status_t net;
        netmgr_status(&net);

        if (gedrueckt) {
            bool war_dunkel = !s_an || helligkeit < cfg.brightness;
            bedient_ms = t;
            if (!s_an) {
                s_an = true;
            } else if (!war_dunkel) {
                /* Der erste Druck weckt nur; erst der naechste blaettert. */
                if (M5.BtnA.wasPressed()) {
                    s_seite = nachbar(s_seite, -1);
                } else if (M5.BtnC.wasPressed()) {
                    s_seite = nachbar(s_seite, 1);
                }
            }
        }
        /* Kurz auf B: innerhalb der Seite umschalten */
        if (M5.BtnB.wasClicked() && s_an) {
            b_druecken(s_seite);
        }
        beruehrung(t, &bedient_ms, helligkeit, cfg.brightness);
        if (s_seite != SEITE_LEITSTAND && s_code_zeigen) {
            s_code_zeigen = false;
        }
        if (M5.BtnB.pressedFor(1500)) {
            if (!lang_gedrueckt) {
                s_an = !s_an;
                lang_gedrueckt = true;
                bedient_ms = t;
            }
        } else if (M5.BtnB.isReleased()) {
            lang_gedrueckt = false;
        }

        uhr_und_versorgung(t);
        befunde_beobachten(&bedient_ms);

        /* Helligkeit: an, abgedunkelt oder aus */
        int soll = cfg.brightness;
        if (!s_an || (nachts(&cfg, &net) && t - bedient_ms > 60000)) {
            soll = 0;
        } else if (cfg.dim_after_min > 0 && t - bedient_ms > cfg.dim_after_min * 60000u) {
            soll = cfg.brightness / 6 < 12 ? 12 : cfg.brightness / 6;
        }

        xSemaphoreTake(s_lcd, portMAX_DELAY);
        if (soll != helligkeit) {
            M5.Display.setBrightness(soll);
            helligkeit = soll;
        }
        if (soll > 0) {
            int seite = s_seite;
            bool einrichtung = net.ap_active && !net.sta_connected;
            bool einrichtung_alt = net_alt.ap_active && !net_alt.sta_connected;
            if (seite != gezeigt || einrichtung != einrichtung_alt || s_neu_lesen) {
                seite_fest(seite);
                gezeigt = seite;
                revision = 0;
                s_neu_lesen = false;
            }
            uint32_t r = st_poll_revision();
            if (r != revision || t - zuletzt_ms >= 1000) {
                st_poll_snapshot(&p);
                if (sv_aktiv()) {
                    sv_wiederherstellen(&p);
                    sv_abtasten(&p);
                }
                seite_werte(seite, &p, &net);
                revision = r;
                zuletzt_ms = t;
            }
        } else {
            gezeigt = -1;
        }
        xSemaphoreGive(s_lcd);
        net_alt = net;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* ------------------------------------------------------------------ */
/* Bildschirmaufnahme                                                  */
/* ------------------------------------------------------------------ */

int st_ui_width(void)
{
    return M5.Display.width();
}

int st_ui_height(void)
{
    return M5.Display.height();
}

bool st_ui_read_row(int y, uint8_t *rgb)
{
    if (s_lcd == NULL || !M5.Display.isReadable()) {
        return false;
    }
    xSemaphoreTake(s_lcd, portMAX_DELAY);
    M5.Display.readRectRGB(0, y, M5.Display.width(), 1, rgb);
    xSemaphoreGive(s_lcd);
    return true;
}

/*
 * Befehlszeile an der seriellen Schnittstelle, fuer die Pruefung am Tisch:
 *   bild   gibt den Bildschirm lauflaengencodiert aus (tools/leitstand_bild.py)
 *   heap   freier Speicher
 *   seite  naechste Seite
 */
static void bild_ausgeben(void)
{
    int w = st_ui_width(), h = st_ui_height();
    static uint8_t zeile_rgb[320 * 3];
    /* Protokollzeilen anderer Auftraege wuerden die Ausgabe zerschneiden. */
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("BILD %d %d\n", w, h);
    for (int y = 0; y < h; y++) {
        if (!st_ui_read_row(y, zeile_rgb)) {
            printf("FEHLER nicht lesbar\n");
            esp_log_level_set("*", ESP_LOG_INFO);
            return;
        }
        printf("Z%d", y);
        int x = 0;
        while (x < w) {
            const uint8_t *px = &zeile_rgb[x * 3];
            int n = 1;
            while (x + n < w && memcmp(&zeile_rgb[(x + n) * 3], px, 3) == 0) {
                n++;
            }
            if (n > 1) {
                printf(" %02x%02x%02x*%d", px[0], px[1], px[2], n);
            } else {
                printf(" %02x%02x%02x", px[0], px[1], px[2]);
            }
            x += n;
        }
        printf("\n");
    }
    printf("ENDE\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);
}

static void konsole_task(void *arg)
{
    (void)arg;
    char zeile[32];
    size_t n = 0;
    for (;;) {
        uint8_t c;
        int r = uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY);
        if (r <= 0) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            zeile[n] = '\0';
            if (strcmp(zeile, "bild") == 0) {
                bild_ausgeben();
            } else if (strcmp(zeile, "heap") == 0) {
                printf("HEAP frei %u, Tiefstwert %u, groesster Block %u, PSRAM frei %u\n",
                       (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            } else if (strcmp(zeile, "seite") == 0) {
                s_seite = nachbar(s_seite, 1);
            }
            n = 0;
        } else if (n < sizeof(zeile) - 1) {
            zeile[n++] = (char)c;
        }
    }
}

/* ------------------------------------------------------------------ */

int st_ui_sd_cs(void)
{
    int pin = M5.getPin(m5::pin_name_t::sd_spi_ss);
    return pin >= 0 && pin < 40 ? pin : -1;
}

void st_ui_bus_sperren(void)
{
    if (s_lcd != NULL) {
        xSemaphoreTake(s_lcd, portMAX_DELAY);
    }
}

void st_ui_bus_freigeben(void)
{
    if (s_lcd != NULL) {
        xSemaphoreGive(s_lcd);
    }
}

const char *st_ui_board(void)
{
    return s_board;
}

bool st_ui_psram(void)
{
    return s_psram;
}

void st_ui_config_changed(void)
{
    s_neu_lesen = true;
}

const char *st_ui_page(void)
{
    return SEITENNAME[s_seite];
}

bool st_ui_on(void)
{
    return s_an;
}

bool st_ui_set_page(const char *name)
{
    for (int i = 0; i < SEITEN; i++) {
        if (strcmp(name, SEITENNAME[i]) == 0 && verfuegbar(i)) {
            s_seite = i;
            return true;
        }
    }
    return false;
}

void st_ui_set_on(bool an)
{
    s_an = an;
}

esp_err_t st_ui_start(void)
{
    s_lcd = xSemaphoreCreateMutex();

    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = false;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.internal_rtc = true;
    cfg.led_brightness = 0;
    M5.begin(cfg);
    /* Der Lautsprecherverstaerker des Core Basic haengt an GPIO 25 und ist
     * immer versorgt. Offen gelassen, verstaerkt er, was die Lasten auf dem
     * SPI-Bus einstreuen: Unter Dauerlast auf der Karte piepte und knisterte
     * es. Fest auf null ist er still. */
    if (M5.getBoard() == m5::board_t::board_M5Stack) {
        gpio_reset_pin(GPIO_NUM_25);
        gpio_set_direction(GPIO_NUM_25, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_25, 0);
    }

    switch (M5.getBoard()) {
    case m5::board_t::board_M5Stack:
        snprintf(s_board, sizeof(s_board), "M5Stack Core");
        break;
    case m5::board_t::board_M5StackCore2:
        snprintf(s_board, sizeof(s_board), "M5Stack Core2");
        break;
    case m5::board_t::board_M5StackCoreS3:
        snprintf(s_board, sizeof(s_board), "M5Stack CoreS3");
        break;
    default:
        snprintf(s_board, sizeof(s_board), "Board %d", (int)M5.getBoard());
        break;
    }
    s_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    uhr_lesen();
    if (s_psram && !sv_start()) {
        ESP_LOGW(TAG, "Kein Speicher fuer den Verlauf der Anzeige");
    }
    ESP_LOGI(TAG, "%s, Anzeige %dx%d, %s", s_board, (int)M5.Display.width(), (int)M5.Display.height(),
             s_psram ? "mit PSRAM" : "ohne PSRAM");

    M5.Display.fillScreen(GRUND);
    M5.Display.setBrightness(0);

    /* Befehlszeile: Treiber fuer UART0, damit Lesen blockieren darf */
    if (!uart_is_driver_installed(UART_NUM_0)) {
        uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    }
    uart_vfs_dev_use_driver(UART_NUM_0);

    if (xTaskCreate(ui_task, "ui", 6144, NULL, 3, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    xTaskCreate(konsole_task, "konsole", 3072, NULL, 2, NULL);
    return ESP_OK;
}
