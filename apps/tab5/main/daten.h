// Schnittstelle zwischen Oberfläche und Anlage.
//
// Auf dem Tab5 erfüllt sie geraet/daten.cpp (Abfrage-Task, Befehle über HTTP), in der
// Vorschau auf dem Mac host/plattform.cpp (Beispielanlage). Die Oberfläche kennt nur
// diese Funktionen.
//
// ⚠️ Grundregel wie am Tab5 von camperSense: Netz in eigenen Tasks, Oberfläche im
// LVGL-Task, dazwischen Zustände mit `generation()`. Eine Seite vergleicht im Takt die
// Generation und baut nur bei einer Änderung neu. Kein HTTP-Aufruf im LVGL-Task.
// Befehle reihen nur ein; ihr Ergebnis kommt als Rückmeldung.
#pragma once
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>
#include "anlage.h"
#include "befunde.h"
#include "verlauf.h"

namespace daten {

// Jüngstes Bild der Anlage; nie nullptr (anfangs leer).
std::shared_ptr<const anlage::Bild> bild();
uint32_t generation();
// Rohdaten für den Assistenten (anlage_status mit rohdaten, einstellungen_lesen).
std::vector<anlage::Geraetestand> staende();
int64_t jetzt();   // Unix-Sekunden; im Beispielbetrieb der Stand der Beispielanlage

// Verlauf einer Reihe (verlauf.h) — Kopie unter der Sperre.
std::vector<verlauf::Punkt> reihe(const std::string &schluessel, int64_t von, int64_t bis, int raster_s = verlauf::PLATZ_S);
std::vector<std::string> reihen();
std::vector<befunde::Erledigt> erledigte_befunde();

// ---- Befehle (reihen ein; Ergebnis als Rückmeldung) -----------------------------
// Sollwert: auf 0,5 gerundet, auf 5…35 begrenzt; erst 900 ms nach der letzten
// Änderung gesendet (Stellrad). Bis dahin liefert `soll_ausstehend` den neuen Wert.
void sollwert(const std::string &raum_id, float wert);
float soll_ausstehend(const std::string &raum_id);   // NAN = keiner
void betriebsart(const std::string &raum_id, bool heizen);
void raum_pruefen(const std::string &raum_id);
void heizkreis_modus(int nr, anlage::Modus m);
void kkp_modus(anlage::Modus m);
void kanal(const std::string &geraet, int nr, const char *cmd);   // open, close, stop, auto; nr 0 = alle
void neustart(const std::string &geraet);
void befund_stumm(const std::string &id, bool stumm);
void jetzt_abfragen();

struct Rueckmeldung {
    uint32_t nr = 0;   // steigt mit jeder neuen
    std::string text;
    bool fehler = false;
};
Rueckmeldung rueckmeldung();
void melden(const std::string &text, bool fehler);   // Rückmeldung aus einem anderen Teil (Assistent)

// Beliebiger Befehl an ein Gerät, für die Vorschläge des Assistenten. Blockiert bis zur
// Antwort — nur aus einem eigenen Task (Assistent), nie aus dem LVGL-Task.
// Rückgabe: leer bei Erfolg, sonst der Fehlertext.
std::string befehl_warten(const std::string &geraet, const char *methode, const std::string &pfad, const std::string &rumpf);
// GET an ein Gerät (Leitstand: Ereignisse, Feinverlauf). Blockiert wie oben.
bool lesen_warten(const std::string &geraet, const std::string &pfad, std::string &antwort, int *status = nullptr);

// ---- Netz ----------------------------------------------------------------------
enum class Netz : uint8_t { aus, sucht, verbindet, verbunden, fehler };
struct Wlan {
    Netz zustand = Netz::aus;
    std::string ssid, grund;
    int rssi = 0;
    std::string ip;
};
Wlan wlan();
struct Funknetz {
    std::string ssid;
    int rssi;
    bool offen;
};
void wlan_suchen();
bool wlan_sucht();
std::vector<Funknetz> wlan_netze();
void wlan_verbinden(const std::string &ssid, const std::string &kennwort);
void wlan_vergessen();
uint32_t wlan_generation();

}  // namespace daten
