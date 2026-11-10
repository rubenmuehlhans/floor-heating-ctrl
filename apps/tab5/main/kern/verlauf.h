// Verlauf der Tafel: Mittelwerte je Reihe in Plätzen zu fünf Minuten (UTC), sieben Tage —
// wie die Aufzeichnung der App (apple/Packages/Verlauf, Raster.swift, Abtastung.swift).
//
// Schlüssel folgen docs/katalog-messgroessen.md: gerätebezogene Reihen tragen die Kennung
// des Geräts davor („fbh_5d20e7/raum.3.ist“), anlagenweite nicht („fuehler.puffer“,
// „brenner“, „fuellstand“, „pumpe.1“, „kkp“, „aussen“). Schaltzustände werden zum Anteil
// (Brenner 3 von 5 min = 0,6).
//
// ⚠️ Nicht threadsicher; geraet/daten.cpp hält eine Sperre darum.
// ⚠️ Keine ESP-IDF-Header: baut auch auf dem Mac.
#pragma once
#include <stdint.h>
#include <map>
#include <string>
#include <vector>
#include "anlage.h"

namespace verlauf {

inline constexpr int PLATZ_S = 300;
inline constexpr int PLAETZE = 7 * 24 * 12;   // sieben Tage

struct Punkt {
    int64_t t;   // Beginn des Platzes
    float v;
};

class Speicher {
public:
    // Werte eines Augenblicks übernehmen; sie gehen in den Mittelwert des laufenden Platzes ein.
    void abtasten(int64_t jetzt, const std::map<std::string, float> &werte);
    // Einen fertigen Wert in einen Platz setzen, wenn dort noch keiner steht (Lücken füllen,
    // Verlauf des Heizungsgeräts).
    void nachtragen(const std::string &schluessel, int64_t t, float v);

    // Punkte von `von` bis `bis` im Raster `raster_s` (Vielfaches von 300), Mittel je Raster.
    std::vector<Punkt> reihe(const std::string &schluessel, int64_t von, int64_t bis, int raster_s = PLATZ_S) const;
    std::vector<std::string> schluessel() const;
    bool hat(const std::string &schluessel) const { return reihen_.count(schluessel) > 0; }
    int64_t aeltester() const;   // Beginn des ältesten belegten Platzes, 0 = leer

    // Ablage (binär, geraet/speicher.cpp schreibt sie in LittleFS).
    std::string speichern() const;
    bool laden(const std::string &daten);

private:
    struct Reihe {
        std::vector<float> wert;   // PLAETZE, Ring; NAN = leer
        double summe = 0;
        int anzahl = 0;
        int64_t platz = 0;         // laufender Platz der Summe
    };
    int64_t kopf_ = 0;             // jüngster Platz (Beginn), 0 = leer
    std::map<std::string, Reihe> reihen_;
    void vorruecken(int64_t platz);
    static int index(int64_t platz) { return (int)((platz / PLATZ_S) % PLAETZE); }
};

// Werte eines Bilds für den Verlauf (Schlüssel siehe oben).
std::map<std::string, float> abtastung(const anlage::Bild &b);

// Lesbare Bezeichnung und Einheit eines Schlüssels: „Küche, Ist“, „°C“.
std::string bezeichnung(const anlage::Bild &b, const std::string &schluessel, std::string *einheit = nullptr);

}  // namespace verlauf
