// Ladestand und Restzeit aus Spannung und Strom — ohne ESP-IDF, damit die
// Prüfungen auf dem Mac es prüfen (test/pruefung.cpp).
//
// Akku des Tab5: 2S-Li-Ion, 7,4 V, 2000 mAh (14,8 Wh). Übernommen vom Tab5 von camperSense.
// ⚠️ Es gibt keinen Ladestandsmesser: der Stand kommt aus der Spannung. Das trägt
// nur ohne Ladestrom — beim Laden hebt das Ladegerät die Klemmenspannung an, und
// eine Spannungsdeutung zeigte dauerhaft „fast voll“ (dieselbe Lehre wie am Core2,
// satellite-display/CLAUDE.md „Der Ladestand“). Beim Laden wird deshalb ab dem
// letzten Ruhewert mitgezählt; ohne Ruhewert gibt es keine Zahl.
// ⚠️⚠️ Vorzeichen am INA226 (am Gerät gemessen 2026-10-07): LADEN ist NEGATIV —
// mit eingeschaltetem Laden −0,49 A bei steigender Spannung. Die Werksfirmware
// blendet ihre Pfeile bei Strom ≥ 0 ein; das sind dort Entladepfeile.
#pragma once
#include <stdint.h>

namespace akkustand {

constexpr float KAPAZITAET_AH = 2.0f;
constexpr float INNENWIDERSTAND_OHM = 0.15f;   // Pack mit Schutzschaltung, geschätzt
constexpr float RUHE_A = 0.03f;                // darunter fließt praktisch nichts

// Leerlaufspannung des Packs → Prozent (0…100), aus einer Tabelle je Zelle.
float prozent_aus_spannung(float pack_v);

// Spannung unter Last → Leerlaufspannung (Spannungsabfall am Innenwiderstand).
// `strom` mit dem Vorzeichen des INA226: > 0 entladen.
float leerlauf(float pack_v, float strom);

struct Rechner {
    float soc = -1;           // Prozent, −1 = unbekannt
    float strom_mittel = 0;   // A, geglättet (≈ 1 min)
    bool voll = false;

    // Eine Messung je `dt_s` Sekunden, Strom mit dem Vorzeichen des INA226.
    void schritt(float pack_v, float strom, float dt_s);

    // Minuten bis leer (nur beim Entladen) bzw. bis voll (nur beim Laden); −1 = unbekannt.
    int restzeit_min() const;
    int bis_voll_min() const;
};

}  // namespace akkustand
