// Zahlen und Zeiten in deutscher Schreibweise — wie `Format` der App
// (apple/App/Ansichten/Gemeinsam/Format.swift).
//
// ⚠️ Zwischen Zahl und Einheit steht ein geschütztes Leerzeichen (U+00A0), damit
// „72,3 °C“ nicht umbricht. Ein fehlender Wert (NAN) erscheint als „–“.
// ⚠️ Keine ESP-IDF-Header: baut auch auf dem Mac.
#pragma once
#include <stdint.h>
#include <string>

namespace fmt {

inline constexpr const char *FEST = "\xC2\xA0";   // geschütztes Leerzeichen
inline constexpr const char *STRICH = "\xE2\x80\x93";   // „–“

std::string mit_einheit(const std::string &zahl, const char *einheit);
std::string zahl(double wert, int stellen = 1);        // „21,5“; NAN → „–“
std::string temperatur(double wert, int stellen = 1);  // „21,5 °C“
std::string kelvin(double wert, int stellen = 1, bool vorzeichen = false);   // „+3,1 K“
std::string prozent(double anteil);                    // 0,45 → „45 %“
std::string prozentzahl(double anteil);                // 0,45 → „45“
std::string liter(double wert, int stellen = 1);
std::string starts(int anzahl);                        // „1 Start“, „3 Starts“
std::string dauer(int sekunden);                       // „45 s“, „57 min“, „1 h 12 min“, „28 h“
std::string alter(int sekunden);                       // „vor 12 min“; < 0 → „–“
std::string seit(int sekunden);                        // „weniger als einer Minute“, „5 min“, „3 h“, „2 Tagen“
std::string signal(int dbm);                           // „−60 dBm · gut“
std::string wochentag(int nummer);                     // 0 = Sonntag

// Ortszeit (Mitteleuropa); leer, solange die Uhr nicht steht.
std::string uhrzeit(int64_t unix);                     // „14:05“
std::string tag(int64_t unix);                         // „22.09.2026“
std::string tag_kurz(int64_t unix);                    // „22.09.“
std::string tag_mit_zeit(int64_t unix);
std::string wochentag_kurz(int64_t unix);              // „Mo“
std::string iso(int64_t unix);                         // „2026-09-22T14:05:00+02:00“ (Ortszeit mit Versatz)
int ortsversatz(int64_t unix);                         // Sekunden, MEZ/MESZ nach EU-Regel

// UTC: „2026-09-23T05:35:00Z“ → Unix (0 = nicht lesbar); Unix → „2026-09-23“
int64_t aus_iso_utc(const std::string &s);
std::string tag_utc(int64_t unix);

// Geschütztes Leerzeichen zwischen Zahl und Einheit in Fließtext (Befunde, Antworten der KI).
std::string einheiten_fest(const std::string &text);

// Nur Ziffern, Komma, Punkt, Minus, %, °, : und Leerzeichen? Dann trägt eine Ziffernschrift den Text.
bool nur_ziffern(const std::string &s);

// Erster Buchstabe groß („heizt“ → „Heizt“); nur ASCII und die deutschen Umlaute.
std::string gross(const std::string &s);
// Versalien für Kicker („Räume“ → „RÄUME“).
std::string versal(const std::string &s);

}  // namespace fmt
