// Gesprächsführung des Assistenten: Werkzeugschleife, Abbruch, Vorschläge übernehmen und
// zurücknehmen, Lagebericht — wie AppModell+Assistent.swift, Assistenzdienst.swift und
// Vorschlagsablauf.swift der App.
//
// Unabhängig vom Transport: `Senden` schickt den Rumpf an die Messages-API und reicht die
// empfangenen Bytes weiter (geraet/assist.cpp über HTTPS).
//
// ⚠️ Alle blockierenden Funktionen laufen im Task des Assistenten, nie im LVGL-Task. Die
// Oberfläche liest nur `stand()` und `generation()`.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "claude.h"
#include "ki.h"

namespace gespraech {

// Rückgabe: HTTP-Status (0 = kein Austausch). `empfang` gibt false zurück, um abzubrechen.
using Senden = std::function<int(const std::string &rumpf, std::function<bool(const char *, size_t)> empfang, std::string &fehlerkoerper)>;

class Assistent {
public:
    void senden_setzen(Senden s) { senden_ = std::move(s); }
    void schlafen_setzen(std::function<void(int)> f) { schlafen_ = std::move(f); }
    void modell_setzen(const std::string &id) { modell_ = id; }
    const std::string &modell() const { return modell_; }

    ki::Stand stand();
    uint32_t generation() const { return gen_; }

    // Blockierend (Task des Assistenten)
    void frage(const std::string &text);
    void lagebericht();
    void uebernehmen(const std::string &id);
    void zuruecknehmen(const std::string &id);
    std::string verbindung_pruefen();   // leer = in Ordnung

    // Aus jedem Task
    void anhalten() { epoche_++; }
    void neues_gespraech();
    void verwerfen(const std::string &id);
    void bezug_setzen(const std::string &b);
    void entwurf_setzen(const std::string &t);
    void bericht_aus_befunden();        // ohne KI: Lagebericht aus den Befunden
    void status(ki::Lage l, bool bericht_laeuft = false);

    // Ablage (geraet/assist.cpp schreibt sie in LittleFS)
    std::string speichern();
    void laden(const std::string &json);
    std::vector<std::string> aenderungsprotokoll();

private:
    bool runde(std::vector<std::string> &nachrichten, const claude::Anfrage &a, size_t beitrag, claude::Antwort &out, uint32_t epoche,
               std::string &fehler);
    std::string vorschlag_anlegen(ki::Vorschlag v);
    void geaendert() { gen_++; }
    std::string aenderung_schreiben(const ki::Aenderung &a, bool zurueck);

    std::mutex mx_;
    ki::Stand st_;
    std::vector<std::string> transkript_;   // Nachrichten des laufenden Gesprächs
    std::vector<std::string> protokoll_;    // Änderungsprotokoll (CSV-Zeilen)
    std::atomic<uint32_t> gen_{1}, epoche_{0};
    Senden senden_;
    std::function<void(int)> schlafen_;
    std::string modell_ = "claude-opus-5-5";
    uint32_t naechste_id_ = 1;
};

}  // namespace gespraech
