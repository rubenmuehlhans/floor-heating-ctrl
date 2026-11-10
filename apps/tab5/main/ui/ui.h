// Die Oberfläche: ein Bildschirm, dessen Inhalt je Seite neu aufgebaut wird.
//
// Aufbau wie die App auf dem iPad im Querformat (apple/App/Ansichten/Hauptansicht.swift):
// fünf Bereiche in einer Leiste links — Übersicht, Räume, Assistent, Heizung, Geräte —,
// je Bereich ein Stapel von Seiten, darüber die Titelzeile mit „Zurück“. Breite Seiten
// teilen sich in zwei Spalten (Doppelseite, Spalten.swift).
//
// ⚠️ Alle Funktionen laufen im LVGL-Task (Timer, Ereignisse) oder unter der Sperre des
// Displays. Daten kommen ausschließlich über daten.h, ki.h und eins.h.
// ⚠️ Seitenwechsel sind Schnitte, keine Gleitanimation: die Ausgabe über die PPA begrenzt
// die Bildrate (camperSense, PLAN-tab5.md §4.7).
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "lvgl.h"

namespace ui {

enum class Bereich : uint8_t { uebersicht, raeume, assistent, heizung, geraete };

// Ein Ziel im Stapel eines Bereichs (Navigation.swift: `Ziel`).
struct Ziel {
    enum class Art : uint8_t {
        wurzel,      // Startseite des Bereichs
        raum,        // id = Raum-ID
        befunde,
        befund,      // id = Befund-ID
        verlauf,     // Heizung › Verlauf
        einstellungen,   // Einstellungen der Tafel
        wlan,        // Einstellungen › WLAN
        lageberichte,
    } art = Art::wurzel;
    std::string id;
};

void start(lv_display_t *disp);   // unter der Sperre des Displays
Bereich bereich();
void bereich_waehlen(Bereich b);  // Stapel des Bereichs bleibt erhalten
void oeffnen(const Ziel &z);      // auf den Stapel des aktuellen Bereichs
void zurueck();
void neu_aufbauen();              // nach Wechsel von hell/dunkel
// Assistent mit Bezug öffnen (Knopf „Fragen“, Befund, Hinweis des Lageberichts).
void fragen(const std::string &bezug);

// Für die Konsole und die Vorschau: Seite nach Name öffnen („uebersicht“, „raeume“,
// „raum:<id>“, „heizung“, „geraete“, „geraet:<id>“, „assistent“, „befunde“,
// „einstellungen“, …). false = unbekannt.
bool seite_nach_name(const std::string &name);

// ---- Seiten (je eine Datei). `aufbauen` füllt `inhalt` (INHALT_B × INHALT_H),
// `takt` läuft alle 250 ms, solange die Seite sichtbar ist. Titel und Knöpfe der
// Titelzeile setzt die Seite über `titel_setzen` und `kopf_knopf`.
void titel_setzen(const std::string &t);
void kopf_knopf(const char *sf, const std::string &text, std::function<void()> wenn, bool betont = false);
void kopf_leeren();   // Knöpfe der Titelzeile entfernen (Neuaufbau einer Seite)
// Bedienung läuft (Stellrad, Bildlauf): Seiten bauen dann zwei Sekunden lang nicht neu,
// sonst risse ein Datenstand dem Finger das Rad weg.
void bedienung_merken();
bool bedienung_kuerzlich();
// Doppelseite: zwei Spalten, die je für sich scrollen. `rechts` = nullptr, wenn die
// Seite einspaltig ist (dann eine breite, mittige Spalte bis `breite`).
struct Spalten {
    lv_obj_t *links = nullptr, *rechts = nullptr;
};
Spalten doppelseite(lv_obj_t *inhalt, bool zweispaltig = true, int breite = 0);

void uebersicht_aufbauen(lv_obj_t *inhalt);
void uebersicht_takt();
void befunde_aufbauen(lv_obj_t *inhalt, const std::string &auswahl);
void befunde_takt();
void raeume_aufbauen(lv_obj_t *inhalt);
void raeume_takt();
void raum_aufbauen(lv_obj_t *inhalt, const std::string &id);
void raum_takt();
void heizung_aufbauen(lv_obj_t *inhalt);
void heizung_takt();
void verlauf_aufbauen(lv_obj_t *inhalt);
void verlauf_takt();
void geraete_aufbauen(lv_obj_t *inhalt, const std::string &auswahl);
void geraete_takt();
void assistent_aufbauen(lv_obj_t *inhalt);
void assistent_takt();
void lageberichte_aufbauen(lv_obj_t *inhalt);
void einstellungen_aufbauen(lv_obj_t *inhalt);
void einstellungen_takt();
void wlan_aufbauen(lv_obj_t *inhalt);
void wlan_takt();
void zustimmung_zeigen();   // Fenster „Daten an Anthropic“

// Bausteine, die mehrere Seiten teilen
// Stellrad für den Sollwert (Stellrad.swift): waagerecht wischbare Skala 5…35 °C in
// Schritten von 0,5, feste Nadel in der Mitte. `geaendert` kommt bei jedem Rastschritt
// durch die Hand, nicht beim Nachführen von außen.
lv_obj_t *stellrad(lv_obj_t *parent, float wert, bool gedimmt, std::function<void(float)> geaendert);
void stellrad_setzen(lv_obj_t *rad, float wert);   // von außen, ohne Rückruf
// Sollwert-Stellrad mit Zahl darüber und Hinweis darunter (SollwertStellrad)
lv_obj_t *sollwert_stellrad(lv_obj_t *parent, const std::string &raum_id, bool mit_hinweis);
// Liniendiagramm über Zeit (Verlaufstafeln.swift, vereinfacht)
struct Linie {
    std::vector<std::pair<int64_t, float>> punkte;
    lv_color_t farbe;
    bool gestrichelt = false, stufen = false, flaeche = false;
    std::string name;
};
lv_obj_t *diagramm(lv_obj_t *parent, int hoehe, int64_t von, int64_t bis, const std::vector<Linie> &linien, float min_spanne,
                   const char *einheit, bool prozent = false);
// Rückmeldung oben (Hauptansicht.swift: RueckmeldungsLeiste)
void rueckmeldung_zeigen(const std::string &t, bool fehler);

// Bildschirmtastatur (QWERTZ). `fertig` bekommt den Text.
void tastatur_oeffnen(const std::string &titel, const std::string &vorgabe, std::function<void(const std::string &)> fertig,
                      bool geheim = false, const char *ok_text = "Fertig");
void tastatur_schliessen();
bool tastatur_offen();

// Fenster mit Rückfrage („Neu starten?“): Titel, Text, Knöpfe.
void rueckfrage(const std::string &titel, const std::string &text, const std::string &ja, std::function<void()> wenn_ja,
                bool zerstoerend = false);

}  // namespace ui
