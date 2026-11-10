// Einstellungen der Tafel (wie „Einstellungen der App“, EinstellungenAnsichten.swift):
// Darstellung, Assistent (Schlüssel, Zustimmung, Modell, Lagebericht) und Angaben zur Tafel.
// Dazu die WLAN-Seite.
#include "daten.h"
#include "eins.h"
#include "fmt.h"
#include "ki.h"
#include "seite.h"
#include "stil.h"
#include "ui.h"

#if defined(ESP_PLATFORM)
#include "esp_app_desc.h"
#endif

namespace ui {

namespace {

lv_obj_t *s_inhalt;
uint32_t s_gen;

void neu()
{
    lv_async_call([](void *) {
        if (!s_inhalt) return;
        lv_obj_clean(s_inhalt);
        kopf_leeren();
        einstellungen_aufbauen(s_inhalt);
    }, nullptr);
}

const char *firmware()
{
#if defined(ESP_PLATFORM)
    return esp_app_get_description()->version;
#else
    return "Vorschau";
#endif
}

void zustimmung_zeigen()
{
    const Palette &p = pal();
    // Wortlaut der App (ClaudeZustimmung.swift), auf die Tafel bezogen
    std::string t =
        "Für Antworten mit Claude überträgt die Tafel bei jeder Frage und bei jedem Lagebericht Daten an Anthropic, den Anbieter "
        "von Claude mit Sitz in den USA:\n"
        "– Messwerte, Einstellungen, Befunde und Protokolle der Anlage\n"
        "– Namen der Räume und Geräte\n"
        "– Ihre Fragen und den bisherigen Verlauf des Gesprächs\n"
        "– das Handbuch der Anlage\n\n"
        "Kennwörter, WLAN-Zugangsdaten und die Adressen der Relais überträgt die Tafel nicht.\n\n"
        "Die Übertragung läuft über Ihr eigenes Konto bei Anthropic; dort gelten die Bedingungen und die Datenschutzerklärung von "
        "Anthropic. Ohne Zustimmung stellt die Tafel keine Anfrage an Claude. Widerrufen können Sie die Zustimmung jederzeit hier.";
    (void)p;
    rueckfrage("Daten an Anthropic", t, "Zustimmen", [] {
        ki::zustimmen(true);
        neu();
    });
}

}  // namespace

void einstellungen_aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    s_inhalt = inhalt;
    s_gen = eins::generation() + ki::generation();
    titel_setzen("Einstellungen der Tafel");
    Spalten sp = doppelseite(inhalt);

    // Darstellung
    abschnitt(sp.links, "Darstellung");
    lv_obj_t *k = karte(sp.links);
    text(k, "Erscheinungsbild", &f_sub_m, p.tinte);
    int a = (int)eins::zahl(eins::k::ANSICHT, 2);
    segmente(k, {"Hell", "Dunkel"}, a == 1 ? 0 : 1, [](int i) {
        eins::zahl_setzen(eins::k::ANSICHT, i == 0 ? 1 : 2);
        dunkel_setzen(i != 0);
        lv_async_call([](void *) { neu_aufbauen(); }, nullptr);
    });
    text(k, "Helligkeit", &f_sub_m, p.tinte);
    int hell = (int)eins::zahl(eins::k::HELL, 80);
    segmente(k, {"30 %", "55 %", "80 %", "100 %"}, hell <= 30 ? 0 : hell <= 55 ? 1 : hell <= 80 ? 2 : 3, [](int i) {
        static const int W[] = {30, 55, 80, 100};
        eins::zahl_setzen(eins::k::HELL, W[i]);
        neu();
    });
    text(k, "Anzeige dunkler nach", &f_sub_m, p.tinte);
    int dm = (int)eins::zahl(eins::k::DUNKEL_MIN, 5);
    segmente(k, {"nie", "2 min", "5 min", "15 min"}, dm == 0 ? 0 : dm <= 2 ? 1 : dm <= 5 ? 2 : 3, [](int i) {
        static const int W[] = {0, 2, 5, 15};
        eins::zahl_setzen(eins::k::DUNKEL_MIN, W[i]);
        neu();
    });

    abschnitt(sp.links, "Tafel");
    lv_obj_t *kt = karte(sp.links);
    spalte(kt, 2);
    daten::Wlan w = daten::wlan();
    kennwert(kt, "Firmware", firmware(), p.gedaempft);
    kennwert(kt, "WLAN", w.ssid.empty() ? std::string(fmt::STRICH) : w.ssid, p.gedaempft);
    kennwert(kt, "Adresse", w.ip.empty() ? std::string(fmt::STRICH) : w.ip, p.gedaempft);
    kennwert(kt, "Signal", fmt::signal(w.rssi), p.gedaempft);
    auto b = daten::bild();
    kennwert(kt, "Geräte", std::to_string(b->geraete.size()) + (b->beispiel ? " (Beispielanlage)" : ""), p.gedaempft);

    // Assistent
    abschnitt(sp.rechts, "Assistent");
    lv_obj_t *kk = karte(sp.rechts);
    kartenkopf(kk, "Claude von Anthropic", "sparkles",
               "Der Assistent nutzt Ihr eigenes Konto bei Anthropic. Den API-Schlüssel legen Sie unter console.anthropic.com an.");
    std::string schl = eins::text(eins::k::KI_SCHLUESSEL);
    kennwert(kk, "API-Schlüssel", schl.empty() ? "nicht hinterlegt" : "hinterlegt, endet auf …" + schl.substr(schl.size() > 4 ? schl.size() - 4 : 0),
             schl.empty() ? p.warnung : p.gedaempft);
    lv_obj_t *z = zeile_neu(kk, 12);
    lv_obj_set_flex_flow(z, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(z, 10, 0);
    knopf(z, schl.empty() ? "Schlüssel eingeben" : "Schlüssel ersetzen", schl.empty(), [] {
        tastatur_oeffnen("API-Schlüssel von Anthropic (sk-ant-…)", "", [](const std::string &t) {
            std::string s = t;
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (s.size() < 20) {
                rueckmeldung_zeigen("Der Schlüssel ist zu kurz.", true);
                return;
            }
            eins::text_setzen(eins::k::KI_SCHLUESSEL, s);
            ki::verbindung_pruefen();
            neu();
        }, true, "Sichern");
    }, "key");
    if (!schl.empty()) {
        knopf(z, "Verbindung prüfen", false, [] { ki::verbindung_pruefen(); }, "checkmark.shield");
        knopf(z, "Entfernen", false, [] {
            rueckfrage("Schlüssel entfernen", "Ohne Schlüssel kann der Assistent nicht antworten.", "Entfernen", [] {
                eins::loeschen(eins::k::KI_SCHLUESSEL);
                neu();
            }, true);
        }, "trash");
    }
    trennlinie(kk);
    bool zu = eins::zahl(eins::k::KI_ZUSTIMMUNG, 0) == 1;
    kennwert(kk, "Übertragung an Anthropic", zu ? "zugestimmt" : "nicht zugestimmt", zu ? p.gut : p.warnung);
    lv_obj_t *z2 = zeile_neu(kk, 12);
    if (zu) knopf(z2, "Zustimmung widerrufen", false, [] {
        ki::zustimmen(false);
        neu();
    });
    else knopf(z2, "Übertragung zustimmen …", true, [] { zustimmung_zeigen(); });
    trennlinie(kk);
    text(kk, "Modell", &f_sub_m, p.tinte);
    segmente(kk, {"Opus", "Sonnet", "Haiku"}, (int)eins::zahl(eins::k::KI_MODELL, 0), [](int i) {
        eins::zahl_setzen(eins::k::KI_MODELL, i);
        neu();
    });
    text_umbruch(kk, "Opus antwortet am gründlichsten, Haiku am schnellsten und günstigsten.", &f_cap, p.gedaempft);
    trennlinie(kk);
    bool lb = eins::zahl(eins::k::KI_BERICHT, 1) == 1;
    lv_obj_t *z3 = zeile_neu(kk, 12);
    lv_obj_t *l3 = text_umbruch(z3, "Lagebericht von selbst, höchstens alle sechs Stunden", &f_sub, p.tinte, 0);
    lv_obj_set_flex_grow(l3, 1);
    knopf(z3, lb ? "an" : "aus", lb, [lb] {
        eins::zahl_setzen(eins::k::KI_BERICHT, lb ? 0 : 1);
        neu();
    });
}

void einstellungen_takt()
{
    if (s_inhalt && eins::generation() + ki::generation() != s_gen && !tastatur_offen() && !lv_indev_active()) {
        s_gen = eins::generation() + ki::generation();
        lv_obj_clean(s_inhalt);
        kopf_leeren();
        einstellungen_aufbauen(s_inhalt);
    }
}

// ---- WLAN ------------------------------------------------------------------------------

static lv_obj_t *s_w_inhalt;
static uint32_t s_w_gen;

void wlan_aufbauen(lv_obj_t *inhalt)
{
    const Palette &p = pal();
    s_w_inhalt = inhalt;
    s_w_gen = daten::wlan_generation();
    titel_setzen("WLAN");
    kopf_knopf("arrow.clockwise", "Netze suchen", [] { daten::wlan_suchen(); });
    Spalten sp = doppelseite(inhalt);
    daten::Wlan w = daten::wlan();
    lv_obj_t *k = karte(sp.links);
    kartenkopf(k, "Verbindung", "wifi");
    const char *zt = w.zustand == daten::Netz::verbunden ? "verbunden"
                     : w.zustand == daten::Netz::verbindet ? "verbindet …"
                     : w.zustand == daten::Netz::sucht     ? "sucht …"
                     : w.zustand == daten::Netz::fehler    ? "keine Verbindung"
                                                           : "aus";
    zustandszeile(k, zt, w.zustand == daten::Netz::verbunden ? p.gut : w.zustand == daten::Netz::fehler ? p.stoerung : p.blass,
                  w.ssid);
    if (!w.grund.empty()) text_umbruch(k, w.grund, &f_sub, p.warnung);
    if (w.zustand == daten::Netz::verbunden) {
        kennwert(k, "Adresse", w.ip, p.gedaempft);
        kennwert(k, "Signal", fmt::signal(w.rssi), p.gedaempft);
    }
    if (!w.ssid.empty())
        knopf(k, "Netz vergessen", false, [] {
            rueckfrage("Netz vergessen", "Die Tafel trennt sich vom WLAN und fragt die Geräte nicht mehr ab, bis Sie ein Netz wählen.",
                       "Vergessen", [] { daten::wlan_vergessen(); }, true);
        });
    text_umbruch(sp.links,
                 "Die Tafel findet Verteiler und Heizungsgeräte im selben Netz von selbst (mDNS, Dienst _fbhctrl._tcp). Wählen Sie "
                 "deshalb das Netz, in dem auch die Geräte sind.",
                 &f_cap, p.gedaempft);
    abschnitt(sp.rechts, "Netze in Reichweite", daten::wlan_sucht() ? "sucht …" : "");
    lv_obj_t *kn = karte(sp.rechts);
    lv_obj_set_style_pad_ver(kn, 8, 0);
    spalte(kn, 0);
    auto netze = daten::wlan_netze();
    if (netze.empty()) text(kn, daten::wlan_sucht() ? "Suche läuft …" : "Noch keine Netze gefunden.", &f_sub, p.gedaempft);
    bool erste = true;
    for (const auto &n : netze) {
        if (!erste) trennlinie(kn);
        erste = false;
        std::string ssid = n.ssid;
        bool offen = n.offen;
        lv_obj_t *z = link_zeile(kn, n.offen ? "wifi" : "lock", n.ssid, fmt::signal(n.rssi), [ssid, offen] {
            if (offen) {
                daten::wlan_verbinden(ssid, "");
                return;
            }
            tastatur_oeffnen("Kennwort für „" + ssid + "“", "", [ssid](const std::string &k) { daten::wlan_verbinden(ssid, k); }, true,
                             "Verbinden");
        }, p.tinte);
        (void)z;
    }
}

void wlan_takt()
{
    if (s_w_inhalt && daten::wlan_generation() != s_w_gen && !tastatur_offen() && !lv_indev_active()) {
        lv_obj_clean(s_w_inhalt);
        kopf_leeren();
        wlan_aufbauen(s_w_inhalt);
    }
}

}  // namespace ui
