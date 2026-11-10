// Der Assistent der Tafel: Gespräch mit Claude, Werkzeuge über die Anlage, Vorschläge
// mit Bestätigung, Lagebericht — wie der Assistent der App (apple/Packages/Assistent,
// Sprachmodelle; App/Modell/Assistenzdienst.swift, AppModell+Assistent.swift).
//
// Die Strukturen hier liest die Oberfläche; die Funktionen erfüllt geraet/assist.cpp
// (eigener Task mit eigener HTTPS-Verbindung zu api.anthropic.com) bzw. in der Vorschau
// host/plattform.cpp. Anfrage, Strom und Werkzeuge stehen in kern/claude.* und
// kern/werkzeuge.* und bauen auch auf dem Mac.
//
// ⚠️ Ohne Zustimmung („Daten an Anthropic“) und ohne API-Schlüssel stellt die Tafel keine
// Anfrage. Kennwörter, WLAN-Zugangsdaten und die Adressen der Relais gehen nie hinaus.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace ki {

struct Werkzeugzeile {
    std::string name, beschreibung, ergebnis;
    bool fertig = false;
};

struct Baustein {
    enum class Art : uint8_t { text, ueberlegung, werkzeug, vorschlag, fehler } art = Art::text;
    std::string text;            // Text, Überlegung, Fehler; bei vorschlag die Kennung
    Werkzeugzeile werkzeug;
};

struct Beitrag {
    bool nutzer = false;
    std::string text;            // Frage des Nutzers
    std::vector<Baustein> bausteine;
};

enum class VStatus : uint8_t { offen, laeuft, uebernommen, verworfen, zurueckgenommen, gescheitert };

struct Aenderung {
    std::string geraet;          // Kennung
    std::string ort;             // Ort des Geräts
    std::string parameter;       // Kennung im Katalog, etwa raum.target_c
    std::string eintrag;         // Raum-, Heizkreisnummer o. Ä.
    std::string einstellung;     // lesbar: „Küche, Sollwert“
    std::string bisher, neu;     // lesbar mit Einheit
    std::string wert_bisher, wert_neu;   // wie an das Gerät gesendet
};

struct Vorschlag {
    std::string id, titel, begruendung, wirkung, geraete;
    bool aktion = false;
    std::string aktion_art;      // neustart, fuehlersuche, schutzfahrt, messfahrt
    std::string geraet, eintrag; // für Aktionen
    std::vector<Aenderung> aenderungen;
    VStatus status = VStatus::offen;
    int64_t zeit = 0;            // Anlage bzw. Übernahme
    std::string meldung;         // Ergebnis oder Fehler der Übernahme
};

struct Hinweis {
    int schwere = 0;             // 0 Hinweis, 1 Warnung, 2 Störung
    std::string text, bezug;
};

struct Lagebericht {
    int zustand = -1;            // -1 keiner, 0 in Ordnung, 1 beobachten, 2 handeln
    std::string kurztext, modell;
    std::vector<Hinweis> hinweise;
    int64_t erstellt = 0;
    bool aus_befunden = false;   // ohne KI aus den Befunden gebildet
};

enum class Lage : uint8_t { bereit, liest, antwortet, fehler };

struct Stand {
    Lage lage = Lage::bereit;
    std::vector<Beitrag> beitraege;
    std::vector<Vorschlag> vorschlaege;
    Lagebericht bericht;
    std::vector<Lagebericht> fruehere;
    bool bericht_laeuft = false;
    std::string bezug;           // Bezug der nächsten Frage („Bad“, „Lagebericht“)
    std::string entwurf;         // vorbelegte Frage (Startfragen)
};

// Was einer Anfrage im Weg steht, leer = bereit: kein Schlüssel, keine Zustimmung.
std::string hindernis();
bool braucht_zustimmung();
const char *modellname();        // „Claude Opus 5.5“
const char *modell_id();

Stand stand();
uint32_t generation();
bool fragen(const std::string &text);   // false = es läuft schon eine
void anhalten();
void neues_gespraech();
void bezug_setzen(const std::string &bezug);
void entwurf_setzen(const std::string &text);
void uebernehmen(const std::string &vorschlag);
void verwerfen(const std::string &vorschlag);
void zuruecknehmen(const std::string &vorschlag);
void lagebericht_erstellen();
void zustimmen(bool ja);
// Schlüssel prüfen (Verbindungstest wie die App): Ergebnis als Rückmeldung.
void verbindung_pruefen();

}  // namespace ki
