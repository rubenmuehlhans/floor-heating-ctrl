// Befundregeln und Befundgedächtnis — wie apple/Packages/Anlage/Sources/Anlage/
// Befundregeln.swift und apple/Packages/Diagnose/Sources/Diagnose/Befundgedaechtnis.swift.
//
// Die Regeln prüfen nur den jetzigen Stand. Wie lange ein Befund schon ansteht, weiß das
// Gedächtnis: Ein Befund erscheint erst, wenn er seine Mindestdauer ununterbrochen ansteht,
// und kehrt er binnen einer Stunde zurück, setzt er seinen alten Ablauf fort. Erledigte
// Befunde bleiben 30 Tage in der Liste.
//
// Noch nicht übernommen (PLAN.md): Sicherungen, „Pumpe läuft, die Wärme kommt nicht an“,
// „Speicher gilt früher als voll“ (braucht das Ladungsprotokoll).
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "anlage.h"

namespace befunde {

// Alle Befunde, die der Stand jetzt zeigt (ohne Gedächtnis, ohne Mindestdauer).
std::vector<anlage::Befund> pruefen(const std::vector<anlage::Geraetestand> &staende, const anlage::Bild &bild);

struct Erledigt {
    std::string id, titel, ort;
    anlage::Schwere schwere;
    int64_t von, bis;
};

class Gedaechtnis {
public:
    // Übernimmt die Befunde des Stands und gibt die sichtbaren zurück, nach Schwere sortiert.
    // `vollstaendig`: alle Geräte abgefragt — erst dann gilt ein fehlender Befund als erledigt.
    std::vector<anlage::Befund> fortschreiben(const std::vector<anlage::Befund> &jetzt_da, int64_t jetzt, bool vollstaendig);
    void stumm(const std::string &id, bool an);
    bool ist_stumm(const std::string &id) const { return stumm_.count(id) > 0; }
    const std::vector<Erledigt> &erledigt() const { return erledigt_; }
    // Neue sichtbare Befund-IDs seit dem letzten Aufruf (für den Lagebericht).
    uint32_t generation() const { return gen_; }

    // Ablage als JSON (geraet/speicher.cpp schreibt sie in LittleFS).
    std::string speichern() const;
    void laden(const std::string &json);

private:
    struct Eintrag {
        int64_t beginn = 0, zuletzt = 0;
        bool sichtbar = false;
        anlage::Befund b;
    };
    std::map<std::string, Eintrag> laufend_;
    std::set<std::string> stumm_;
    std::vector<Erledigt> erledigt_;
    std::set<std::string> sichtbar_alt_;
    uint32_t gen_ = 0;
};

}  // namespace befunde
