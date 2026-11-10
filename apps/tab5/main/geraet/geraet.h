// Start der Teile, die es nur auf dem Tab5 gibt (main.cpp ruft sie in dieser Reihenfolge).
#pragma once
#include <string>

namespace daten {
bool funk_starten();   // C6 einschalten und verbinden; false = meldet sich nicht
void wlan_start();     // nach funk_starten; verbindet mit dem gespeicherten Netz
void start();          // Abfrage-Task, Ablage in LittleFS, mDNS
std::string leitstand_info();   // Stand des Verlaufsabgleichs mit dem Leitstand (Konsole)
}  // namespace daten

namespace ki {
void start();          // Task des Assistenten
}

namespace konsole {
void start();          // Befehle über USB (info, seite, shot)
}

namespace anzeige {
void takt();           // Helligkeit und Abdunkeln nach Untätigkeit (im LVGL-Takt)
}
