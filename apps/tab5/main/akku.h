// Akku des Tab5: Spannung und Strom am INA226, Ladestand, Laden ein/aus.
// Übernommen vom Tab5 von camperSense (akku.h). Erfüllt von geraet/akku.cpp, in der
// Vorschau von host/plattform.cpp.
//
// ⚠️⚠️ Negativer Strom heißt LADEN (dort am Gerät gemessen, kern/akkustand.h).
#pragma once
#include <stdint.h>
#include <string>

namespace akku {

enum class Zustand : uint8_t {
    unbekannt,   // INA226 nicht gefunden oder noch keine Messung
    entlaedt,    // läuft auf Akku
    laedt,       // externe Versorgung, Akku nimmt Strom auf
    voll,        // externe Versorgung, Ladegerät regelt ab
    netz,        // externe Versorgung, kein Ladestrom, Akku nicht voll (Laden aus)
};

struct Stand {
    Zustand zustand = Zustand::unbekannt;
    float spannung = 0;        // V, Akkupack (2S)
    float strom = 0;           // A, Betrag, geglättet
    int prozent = -1;          // −1 = nicht bestimmbar (beim Laden ohne Ruhewert)
    int restzeit_min = -1;     // nur beim Entladen
    bool lade_an = false;      // CHG_EN am Portexpander
    float strom_roh = 0;       // A, letzte Messung mit Vorzeichen (negativ = Laden), zur Prüfung
    float strom_mittel = 0;    // A, geglättet mit Vorzeichen
};

void start();
bool vorhanden();
Stand stand();
// Laden ein/aus (CHG_EN, P7 am zweiten Portexpander). ⚠️⚠️ Das BSP von Espressif setzt den
// Pin nie, und sein Treiber setzt den Expander beim Anlegen zurück: ohne diesen Aufruf lädt
// der Akku unter dieser Firmware nicht (camperSense, Falle 27).
void laden(bool an);
// Register des zweiten Portexpanders (0x44) als Klartext, für die Konsole: Richtung, Ausgang,
// hochohmig, Eingang. Bit 7 ist CHG_EN, Bit 6 CHG_STAT.
std::string register_text();

}  // namespace akku
