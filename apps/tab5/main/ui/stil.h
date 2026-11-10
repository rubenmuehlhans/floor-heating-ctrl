// Gestaltung der Tafel — Farben, Schriften und Bausteine wie in der App
// (apple/App/Ansichten/Gemeinsam/Gestaltung.swift, Bausteine.swift).
//
// Maße: rund 1,5 px je Punkt der App. Die Farben sind die der Farbsätze der App
// (Assets.xcassets), hell und dunkel.
//
// ⚠️ Nach einem Wechsel von hell und dunkel baut die Oberfläche neu auf (ui.cpp), statt
// Farben nachzuziehen.
#pragma once
#include <functional>
#include <string>
#include "lvgl.h"
#include "schriften.h"
#include "anlage.h"

namespace ui {

struct Palette {
    lv_color_t akzent, waerme, kaelte, gut, warnung, stoerung;
    lv_color_t grund, flaeche, vertieft, tinte, gedaempft, blass, linie, linie_stark;
    lv_color_t akzent_weich, waerme_weich, kaelte_weich;
    lv_color_t auf_akzent;   // Text auf Akzentfläche
};

void dunkel_setzen(bool dunkel);
bool dunkel();
const Palette &pal();

// Maße des Rahmens (Gerätepixel, Querformat)
inline constexpr int BREITE = 1280, HOEHE = 720;
inline constexpr int LEISTE_B = 132;        // Seitenleiste mit den fünf Bereichen
inline constexpr int KOPF_H = 84;           // Titelzeile
inline constexpr int RAND = 24;             // Abstand zum Rand der Seite
inline constexpr int FUGE = 20;             // zwischen den Spalten
inline constexpr int ABSTAND = 16;          // zwischen Karten (App: 14)
inline constexpr int INHALT_B = BREITE - LEISTE_B;
inline constexpr int INHALT_H = HOEHE - KOPF_H;
inline constexpr int SPALTE_B = (INHALT_B - 2 * RAND - FUGE) / 2;
inline constexpr int RADIUS_KARTE = 18;     // App 12
inline constexpr int RADIUS_KLEIN = 12;     // App 8

// UTF-8 eines Symbols der App (SF-Symbol-Name, etwa „flame“); unbekannte → „circle“.
const char *sym(const char *sf);
// Lucide-Name direkt
const char *lucide(const char *name);

// ---- Bausteine -------------------------------------------------------------------
// Alle setzen Farben aus der aktuellen Palette.
lv_obj_t *flaeche(lv_obj_t *parent);   // durchsichtig, ohne Rand und Rundung, nimmt keine Berührung an
lv_obj_t *karte(lv_obj_t *parent, bool betont = false);   // Fläche, Rand Linie, Radius 18, Innenabstand 24
lv_obj_t *kachel(lv_obj_t *parent);    // vertieft, Radius 12 (Kennzahl in der Karte)
lv_obj_t *text(lv_obj_t *parent, const std::string &t, const lv_font_t *f, lv_color_t farbe);
lv_obj_t *text_umbruch(lv_obj_t *parent, const std::string &t, const lv_font_t *f, lv_color_t farbe, int breite = LV_PCT(100));
// Einzeilig mit Pünktchen am Ende. ⚠️ Nur mit fester Höhe, sonst bricht LVGL doch um
// (camperSense, tab5/CLAUDE.md Falle 4).
void einzeilig(lv_obj_t *label);
lv_obj_t *symbol(lv_obj_t *parent, const char *sf, const lv_font_t *f, lv_color_t farbe);
lv_obj_t *kicker(lv_obj_t *parent, const std::string &t, lv_color_t farbe);   // Versalien, gesperrt
void zeile(lv_obj_t *o, int luecke, lv_flex_align_t quer = LV_FLEX_ALIGN_CENTER);
void spalte(lv_obj_t *o, int luecke);
lv_obj_t *zeile_neu(lv_obj_t *parent, int luecke, lv_flex_align_t quer = LV_FLEX_ALIGN_CENTER);   // Fläche, volle Breite
lv_obj_t *spalte_neu(lv_obj_t *parent, int luecke);
lv_obj_t *dehnung(lv_obj_t *parent);   // Spacer in einer Zeile
lv_obj_t *trennlinie(lv_obj_t *parent);

// Kartenkopf: Titel (headline) mit Symbol, darunter der Erklärsatz.
lv_obj_t *kartenkopf(lv_obj_t *karte, const std::string &titel, const char *sf = nullptr, const std::string &erklaerung = "");
// Abschnitt zwischen Karten: Kicker links, Zusatz rechts.
lv_obj_t *abschnitt(lv_obj_t *parent, const std::string &titel, const std::string &zusatz = "");
// Messzahl mit Einheit auf gemeinsamer Grundlinie: „21,5“ „°C“.
lv_obj_t *messzahl(lv_obj_t *parent, const std::string &zahl, const char *einheit, const lv_font_t *f, lv_color_t farbe);
// Kennzahl-Kachel: Kicker, Messzahl mit Einheit, Fuß.
lv_obj_t *kennzahl(lv_obj_t *parent, const std::string &titel, const std::string &wert, const char *einheit,
                   const std::string &fuss, lv_color_t farbe);
// Zustandsmarke (Kapsel): Text in der Farbe, Hintergrund 10 %, Rand 45 %.
lv_obj_t *marke(lv_obj_t *parent, const std::string &t, lv_color_t farbe, const char *sf = nullptr);
// Zustandszeile: Punkt, Text, Zusatz.
lv_obj_t *zustandszeile(lv_obj_t *parent, const std::string &t, lv_color_t farbe, const std::string &zusatz = "");
// Balken (Kapsel) mit Anteil 0..1
lv_obj_t *balken(lv_obj_t *parent, float anteil, lv_color_t farbe, int hoehe = 9);
// Knöpfe: Rahmen (Akzenttext auf Fläche) und betont (weiß auf Akzent).
lv_obj_t *knopf(lv_obj_t *parent, const std::string &t, bool betont, std::function<void()> wenn, const char *sf = nullptr);
lv_obj_t *rund_knopf(lv_obj_t *parent, const char *sf, int d, std::function<void()> wenn, bool betont = false);
// Kennwert-Zeile: Bezeichnung links, Wert rechts.
lv_obj_t *kennwert(lv_obj_t *parent, const std::string &name, const std::string &wert, lv_color_t farbe);
// Zeile, die zu einem Ziel führt: Symbol, Titel, Zusatz, Chevron.
lv_obj_t *link_zeile(lv_obj_t *parent, const char *sf, const std::string &titel, const std::string &zusatz,
                     std::function<void()> wenn, lv_color_t farbe);
// Segmentwahl (wie Picker .segmented); `wahl` wird mit dem Index gerufen.
lv_obj_t *segmente(lv_obj_t *parent, const std::vector<std::string> &optionen, int aktiv, std::function<void(int)> wahl);
// Hinweis auf einer leeren Seite: Symbol, Titel, Text.
lv_obj_t *leer_hinweis(lv_obj_t *parent, const char *sf, const std::string &titel, const std::string &text);

// Klick-Rückruf mit std::function; das Objekt hält die Funktion bis zum Löschen.
void bei_klick(lv_obj_t *o, std::function<void()> f);

// ---- Farblogik der App (Bausteine.swift) -----------------------------------------
lv_color_t schwere_farbe(anlage::Schwere s);
const char *schwere_sym(anlage::Schwere s);
lv_color_t raum_farbe(anlage::Raumzustand z);
const char *raum_sym(anlage::Raumzustand z);
const char *raum_text(anlage::Raumzustand z);
std::string abstand_text(const anlage::Raum &r);
lv_color_t abstand_farbe(const anlage::Raum &r);

// Farbe mit Deckung über dem Hintergrund `grund` gemischt (für Ränder mit 45 %).
lv_color_t mischen(lv_color_t farbe, lv_color_t grund, uint8_t deckung);

}  // namespace ui
