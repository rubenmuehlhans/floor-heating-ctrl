// Werkzeuge des Assistenten — dieselben zehn wie in der App (apple/Packages/Assistent/
// Sources/Assistent/Werkzeuge, Werkzeugsatz.swift): anlage_status, befunde, verlauf,
// ereignisse, feinverlauf, protokolle, kennzahlen, einstellungen_lesen,
// aenderung_vorschlagen, aktion_vorschlagen.
//
// Lesende Werkzeuge greifen auf daten.h zu (Bild, Gerätestände, Verlauf, Leitstand).
// Schreibende legen nur Vorschläge an; der Nutzer bestätigt jeden auf einer Karte.
//
// ⚠️ Zugangsdaten gehen nie hinaus: Konfigurationen und Rohdaten laufen durch
// json::ohne_zugangsdaten, Relaisadressen fehlen.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "ki.h"

namespace werkzeuge {

// Was die Werkzeuge vom Assistenten brauchen
struct Kontext {
    std::function<std::string(ki::Vorschlag)> vorschlag_anlegen;   // gibt die Kennung zurück
    std::function<std::vector<std::string>()> aenderungen;         // Zeilen des Änderungsprotokolls (CSV)
};

// JSON-Liste der Werkzeuge für die Anfrage (fest, Byte für Byte gleich).
const std::string &definitionen(bool nur_lesend = false);

struct Ergebnis {
    std::string text;           // tool_result
    std::string beschreibung;   // Werkzeugzeile in der Oberfläche
    std::string vorschlag;      // Kennung, wenn ein Vorschlag angelegt wurde
};

Ergebnis ausfuehren(const std::string &name, const std::string &eingabe_json, Kontext &k);

// Zeile, solange ein Werkzeug läuft („Liest den Zustand der Anlage …“)
std::string laeuft_text(const std::string &name);

}  // namespace werkzeuge
