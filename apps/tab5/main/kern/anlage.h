// Das zusammengeführte Bild der Anlage — wie `Anlagenbild` der App
// (apple/Packages/Anlage/Sources/Anlage/Anlagenbild.swift, Zusammenfuehrung.swift).
//
// Eingang sind die Gerätestände (letzte Antwort von /api/state, /api/config und
// /api/ble je Gerät); daraus entsteht ein unveränderliches Bild, das Oberfläche und
// Assistent lesen. Die Abfrage (geraet/abfrage.cpp) baut nach jeder Änderung ein neues.
//
// ⚠️ Keine ESP-IDF-Header: baut auch auf dem Mac.
#pragma once
#include <math.h>
#include <stdint.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace anlage {

enum class Art : uint8_t { verteiler, heizung, leitstand };
// Ein Heizungsgerät ist Kessel- oder Speichergerät — oder beides, wenn es nur eines gibt.
enum class Rolle : uint8_t { keine, kessel, speicher, beides };

// Letzte Antworten eines Geräts, wie sie die Abfrage hält.
struct Geraetestand {
    std::string id;           // fbh_…, heiz_…, lst_…
    Art art = Art::verteiler;
    std::string ort;          // site
    std::string ip, hostname;
    bool erreichbar = false;  // ⚠️ erst die zweite Fehlabfrage in Folge gilt als Ausfall
    bool gesehen = false;     // hat seit dem Start geantwortet
    std::string fehler;       // Klartext der letzten Fehlabfrage
    int64_t kontakt = 0;      // Unix-Sekunden der letzten Antwort
    std::string state, config, ble;   // JSON-Texte, leer = noch nicht gelesen
};

enum class Raumzustand : uint8_t { aus, kein_thermometer, messwert_veraltet, heizt, soll_erreicht };

struct Kanal {
    int nr = 0;
    std::string raum;         // Name, leer = frei
    float stellung = NAN;     // 0..1
    bool bekannt = false, hand = false, kalibriert = false, reserviert = false, wartet = false;
    std::string op;           // idle, opening, closing
    int gruppe = 0;
    int gegenspannung = 0;    // mV
};

struct Raum {
    std::string id;           // "<fbh_id>/<nr>"
    int nr = 0;
    std::string name, etage, geraet;
    bool an = true;
    float soll = NAN;
    bool thermometer = false; // sensor_set
    float ist = NAN, feuchte = NAN;
    int batterie = -1, alter = -1;
    float zielstellung = NAN;
    int naechste_pruefung = -1;
    float p_band = NAN;       // aus der Konfiguration
    std::string thermometer_name;
    std::vector<int> kanaele;
    Raumzustand zustand() const;
};

struct Etage {
    std::string geraet, name;
    int rang = 9;
    bool bedarf = false;
    bool nur_aussen = false;  // Platine ohne Stellantriebe, nur Außenfühler
    std::vector<Raum> raeume;
    std::vector<Kanal> kanaele;
    int heizen() const;       // Räume, die gerade heizen
};

struct Aussen {
    float temperatur = NAN, feuchte = NAN;
    int alter = -1;
    std::string quelle;
};

struct Kessel {
    bool vorhanden = false;
    bool laeuft = false;
    int seit = -1;
    float abgas = NAN, bezugslinie = NAN, vorlauf = NAN, ruecklauf = NAN;
    int laufzeit_heute = 0, starts_heute = 0, laufzeit_gestern = 0, starts_gestern = 0;
    float liter_heute = NAN, duese = NAN;
    bool taktbetrieb = false;
    float abgas_abstand = NAN;   // flue.now_k − flue.ref_k
    int abgas_ladungen = 0;
};

struct Speicher {
    bool vorhanden = false;
    float temperatur = NAN, korrektur = 0;
    float ladung = NAN;          // 0..1, NAN = unbekannt
    std::string phase;
    int phase_seit = -1;
    bool warmwasser_knapp = false, geschaetzt = false;
    float voll = NAN, leer = NAN, warngrenze = NAN, hoechstwert = NAN;
    bool leer_gelernt = false;
};

struct Relais {
    bool bekannt = false, ein = false, online = false, abweichung = false, stromlos = false;
    std::string weg;             // mqtt, http, keiner
};

enum class Modus : uint8_t { automatik, ein, aus };
const char *modus_text(Modus m);   // „Automatik“, „Hand ein“, „Hand aus“
const char *modus_wert(Modus m);   // auto, ein, aus (Firmware)

struct Heizkreis {
    int nr = 0;
    std::string name, grund;
    Modus modus = Modus::automatik;
    bool pumpe = false, bedarf = false, gesperrt = false, veraltet = false, keiner_erreicht = true;
    float vorlauf = NAN, ruecklauf = NAN;
    Relais relais;
    std::vector<std::string> versorgt;   // Orte der Verteiler
};

struct Kesselkreispumpe {
    bool vorhanden = false;
    Modus modus = Modus::automatik;
    bool laeuft = false;
    int seit = -1;
    std::string grund;
    Relais relais;
};

enum class Schwere : uint8_t { hinweis = 0, warnung = 1, stoerung = 2 };
const char *schwere_text(Schwere s);   // „Hinweis“, „Warnung“, „Störung“

struct Befund {
    std::string id, titel, text, ort, quelle;
    Schwere schwere = Schwere::hinweis;
    int mindestdauer = 0;        // s, so lange muss er anstehen
    int64_t seit = 0;            // vom Befundgedächtnis gesetzt
    bool stumm = false;
};

struct Geraet {
    std::string id, ort, ip, hostname, version, fehler;
    Art art = Art::verteiler;
    Rolle rolle = Rolle::keine;
    bool erreichbar = false, gesehen = false;
    int rssi = 0, laufzeit = -1, heap = -1;
    int64_t kontakt = 0;
    bool zeit_gueltig = true;
    const char *art_text() const;   // „Verteiler“, „Kessel“, „Pufferspeicher“, „Heizungsgerät“, „Leitstand“
    const char *symbol() const;     // SF-Name der App
};

struct Bild {
    int64_t stand = 0;           // Zeitpunkt der jüngsten Antwort
    bool beispiel = false;       // Beispielanlage statt echter Geräte
    bool vollstaendig = false;   // alle Geräte haben geantwortet oder sind ausgefallen
    std::vector<Geraet> geraete;
    std::vector<Etage> etagen;   // nach Rang (Keller, Erdgeschoss, …)
    Aussen aussen;
    Kessel kessel;
    Speicher speicher;
    std::vector<Heizkreis> heizkreise;
    Kesselkreispumpe kkp;
    std::vector<Befund> befunde; // nach Schwere absteigend (Befundgedächtnis)
    std::map<std::string, float> fuehler;   // Rolle → °C, eigene vor fremden

    const Raum *raum(const std::string &id) const;
    const Etage *etage(const std::string &geraet) const;
    const Geraet *geraet(const std::string &id) const;
    int raeume() const;
    int raeume_heizen() const;
    int pumpen_gestoert() const;
    int pumpen_relais() const;
    int befunde_mit(Schwere s) const;
};

// Baut das Bild aus den Gerätestaenden. `jetzt` in Unix-Sekunden.
std::shared_ptr<Bild> zusammenfuehren(const std::vector<Geraetestand> &staende, int64_t jetzt);

// Rang einer Etage nach ihrem Namen (Keller 0, Erdgeschoss 1, Obergeschoss 2, …, sonst 9).
int etagenrang(const std::string &name);

// Art aus Kennung und Rolle (TXT-Satz oder device.role).
Art art_aus(const std::string &id, const std::string &rolle);

}  // namespace anlage
