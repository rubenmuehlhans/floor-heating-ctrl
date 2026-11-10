// Kleine Helfer über cJSON: Werte über Pfade wie "burner.abgas_c" lesen.
//
// ⚠️ Keine ESP-IDF-Header: baut auch für die Prüfungen und die Vorschau auf dem Mac.
// ⚠️ Fehlt ein Wert oder hat er den falschen Typ, kommt die Vorgabe (bei Zahlen NAN)
// zurück. `null` gilt als fehlend — `charge.level` kommt als null, wenn der Füllstand
// nicht bekannt ist.
#pragma once
#include <cmath>
#include <memory>
#include <string>
#include "cJSON.h"

namespace json {

// Eigentum an einem cJSON-Baum.
struct Loescher {
    void operator()(cJSON *j) const { cJSON_Delete(j); }
};
using Baum = std::unique_ptr<cJSON, Loescher>;

Baum lesen(const std::string &text);
std::string schreiben(const cJSON *j);   // kompakt, ohne Formatierung

// Kind über einen Pfad mit Punkten; Zahlen im Pfad wählen ein Listenelement nicht aus.
const cJSON *weg(const cJSON *j, const char *pfad);
float zahl(const cJSON *j, const char *pfad, float vorgabe = NAN);
double zahl_d(const cJSON *j, const char *pfad, double vorgabe = NAN);
int ganz(const cJSON *j, const char *pfad, int vorgabe = 0);
bool wahr(const cJSON *j, const char *pfad, bool vorgabe = false);
bool hat(const cJSON *j, const char *pfad);   // vorhanden und nicht null
std::string text(const cJSON *j, const char *pfad, const char *vorgabe = "");

// Listen durchlaufen: for (const cJSON *e : json::liste(j, "rooms")) …
struct Liste {
    const cJSON *erster;
    struct It {
        const cJSON *p;
        const cJSON *operator*() const { return p; }
        It &operator++() { p = p->next; return *this; }
        bool operator!=(const It &o) const { return p != o.p; }
    };
    It begin() const { return {erster}; }
    It end() const { return {nullptr}; }
};
Liste liste(const cJSON *j, const char *pfad);

// Zugangsdaten rekursiv entfernen (pass, ap_pass, password, user, ssid, uri, host, topic,
// bindkey) — wie JSONWert.ohneZugangsdaten() der App. `pass_set` bleibt stehen.
void ohne_zugangsdaten(cJSON *j);

}  // namespace json
