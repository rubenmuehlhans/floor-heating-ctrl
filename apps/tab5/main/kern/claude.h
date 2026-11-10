// Messages-API von Anthropic: Anfrage bauen, Strom (SSE) lesen, Transkript fortschreiben —
// wie apple/Packages/Sprachmodelle (ClaudeAnfrage.swift, ClaudeStrom.swift).
//
// Unabhängig vom Transport: geraet/assist.cpp schickt die Anfrage über HTTPS und füttert den
// Strom mit den empfangenen Bytes; die Prüfungen auf dem Mac füttern ihn mit Mitschnitten.
//
// ⚠️ System und Werkzeuge gehen Byte für Byte gleich hinaus, sonst trifft der Prompt-Cache
// nicht (ClaudeAnfrage.swift). Das Transkript besteht aus fertig serialisierten Nachrichten,
// die nie neu geschrieben werden.
// ⚠️ Denkblöcke gehen mit ihrer Signatur unverändert zurück; zu jedem tool_use gehört ein
// tool_result, auch nach einem Abbruch.
#pragma once
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace claude {

inline constexpr const char *HOST = "api.anthropic.com";
inline constexpr const char *VERSION = "2023-06-01";

struct Block {
    std::string typ;        // text, thinking, redacted_thinking, tool_use
    std::string text;       // text bzw. thinking
    std::string signatur;   // thinking
    std::string daten;      // redacted_thinking
    std::string id, name;   // tool_use
    std::string eingabe;    // tool_use: JSON der Argumente (gesammelt aus input_json_delta)
    bool fertig = false;
};

struct Antwort {
    std::vector<Block> bloecke;
    std::string stop_reason, modell;
    std::string fehler_typ, fehler_text;   // Ereignis `error` im Strom
    bool angefangen = false;               // message_start gesehen
    bool beendet = false;                  // message_stop gesehen
    int eingabe = 0, ausgabe = 0, cache_lesen = 0, cache_schreiben = 0;
};

// Liest den Strom zeilenweise; nur `data:`-Zeilen zählen (die `event:`-Zeile ist redundant).
class Strom {
public:
    void futter(const char *daten, size_t n);
    const Antwort &antwort() const { return a_; }
    bool geaendert() { bool g = g_; g_ = false; return g; }
private:
    void ereignis(const std::string &json);
    std::string zeile_;
    Antwort a_;
    bool g_ = false;
};

struct Anfrage {
    std::string modell;
    bool denken = true;            // adaptives Denken (nicht bei Haiku 4.5)
    const char *aufwand = "medium";
    std::string system;            // Anweisungen und Wissen
    std::string werkzeuge;         // JSON-Liste der Werkzeuge, leer = keine
    std::string format;            // JSON-Schema für output_config.format, leer = frei
    int max_tokens = 64000;
};

// Rumpf für POST /v1/messages (mit stream: true).
std::string rumpf(const Anfrage &a, const std::vector<std::string> &nachrichten);

// Nachrichten für das Transkript
std::string nutzer(const std::string &text);
std::string assistent(const Antwort &a);   // Inhalt der Antwort mit Denkblöcken und Werkzeugaufrufen
std::string ergebnisse(const std::vector<std::pair<std::string, std::string>> &id_und_text);

// Fehler der API in Klartext (ClaudeFehler.swift, Assistenzdienst.swift).
std::string fehlertext(int status, const std::string &body);
bool wiederholen(int status, const std::string &fehler_typ);   // 5xx, 529, overloaded, api_error

// Lesbarer Name einer Modellkennung: claude-opus-5-5 → „Claude Opus 5.5“
std::string modellname(const std::string &id);

}  // namespace claude
