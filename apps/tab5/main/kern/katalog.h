// Einstellungen, die der Assistent vorschlagen darf — Auszug aus dem Parameterkatalog der
// App (apple/Packages/Anlage/Sources/Anlage/Parameterkatalog.swift, Einträge mit `ki: true`).
// Bereiche und Schritte folgen der Prüfung in config_store.c der Firmware.
//
// ⚠️ Keine ESP-IDF-Header: baut auch auf dem Mac.
#pragma once
#include <string>
#include <vector>
#include "anlage.h"

namespace katalog {

enum class Ort : uint8_t { geraet, raum, heizkreis };

struct Parameter {
    const char *id;          // „raum.target_c“, „buffer.voll_c“ — so nennt ihn der Assistent
    anlage::Art art;         // Gerätetyp
    Ort ort;
    const char *pfad;        // JSON-Pfad in der Konfiguration bzw. im Raum/Heizkreis
    const char *name, *einheit;
    double min, max, schritt;
    bool minuten;            // gespeichert in Sekunden, vorgeschlagen in Minuten
    bool anlagenweit;        // an allen Heizungsgeräten gleich (Speicherwerte)
    const char *auswahl;     // „heat=Regelung aktiv|off=ausgeschaltet“, sonst nullptr
    const char *hilfe;
};

const std::vector<Parameter> &alle();
const Parameter *finden(const std::string &id);

// Sonderparameter ohne Konfiguration: heizkreis.betriebsart, kesselkreispumpe.betriebsart
// (auto, ein, aus) — sie gehen über eigene Befehle.

}  // namespace katalog
