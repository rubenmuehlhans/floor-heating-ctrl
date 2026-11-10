// Einstellungen der Tafel, die einen Neustart überdauern (NVS, Namensraum „tafel“).
// Auf dem Mac (Vorschau) hält host/plattform.cpp sie im Speicher.
//
// ⚠️ Schlüssel höchstens 15 Zeichen (NVS); alle Namen stehen in `eins::k`.
#pragma once
#include <stdint.h>
#include <string>

namespace eins {

void init();
std::string text(const char *key, const char *vorgabe = "");
void text_setzen(const char *key, const std::string &wert);
int64_t zahl(const char *key, int64_t vorgabe = 0);
void zahl_setzen(const char *key, int64_t wert);
void loeschen(const char *key);
uint32_t generation();   // steigt bei jeder Änderung

namespace k {
inline constexpr const char *WLAN_SSID = "wlan_ssid";
inline constexpr const char *WLAN_PSK = "wlan_psk";
inline constexpr const char *ANSICHT = "ansicht";        // 0 automatisch, 1 hell, 2 dunkel
inline constexpr const char *HELL = "hell";              // Helligkeit in Prozent
inline constexpr const char *DUNKEL_MIN = "dunkel_min";  // Anzeige dunkler nach … min, 0 = nie
// Assistent: Schlüssel von Anthropic (⚠️ nie über die Konsole ausgeben), Zustimmung,
// Modellreihe (0 Opus, 1 Sonnet, 2 Haiku), Lagebericht von selbst
inline constexpr const char *KI_SCHLUESSEL = "ki_key";
inline constexpr const char *KI_ZUSTIMMUNG = "ki_ok";
inline constexpr const char *KI_MODELL = "ki_modell";
inline constexpr const char *KI_BERICHT = "ki_bericht";
// Geräte, die nicht per mDNS gefunden werden (Liste „ip,ip,…“)
inline constexpr const char *GERAETE = "geraete";
}  // namespace k

}  // namespace eins
