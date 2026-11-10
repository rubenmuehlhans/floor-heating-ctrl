#include "claude.h"

#include <ctype.h>
#include <stdio.h>
#include "json.h"

namespace claude {

namespace {

std::string escape(const std::string &s)
{
    cJSON *j = cJSON_CreateString(s.c_str());
    std::string r = json::schreiben(j);
    cJSON_Delete(j);
    return r;
}

Block *block(Antwort &a, int index)
{
    if (index < 0 || index > 256) return nullptr;
    while ((int)a.bloecke.size() <= index) a.bloecke.push_back({});
    return &a.bloecke[index];
}

}  // namespace

void Strom::futter(const char *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = d[i];
        if (c == '\n') {
            if (!zeile_.empty() && zeile_.back() == '\r') zeile_.pop_back();
            if (zeile_.rfind("data:", 0) == 0) {
                size_t s = 5;
                while (s < zeile_.size() && zeile_[s] == ' ') s++;
                ereignis(zeile_.substr(s));
            }
            zeile_.clear();
        } else if (zeile_.size() < 2 * 1024 * 1024) zeile_ += c;
    }
}

void Strom::ereignis(const std::string &text)
{
    json::Baum j = json::lesen(text);
    if (!j) return;
    const cJSON *e = j.get();
    std::string typ = json::text(e, "type");
    g_ = true;
    if (typ == "message_start") {
        a_.angefangen = true;
        a_.modell = json::text(e, "message.model");
        a_.eingabe = json::ganz(e, "message.usage.input_tokens");
        a_.cache_lesen = json::ganz(e, "message.usage.cache_read_input_tokens");
        a_.cache_schreiben = json::ganz(e, "message.usage.cache_creation_input_tokens");
    } else if (typ == "content_block_start") {
        Block *b = block(a_, json::ganz(e, "index", -1));
        if (!b) return;
        b->typ = json::text(e, "content_block.type");
        if (b->typ == "tool_use") {
            b->id = json::text(e, "content_block.id");
            b->name = json::text(e, "content_block.name");
        } else if (b->typ == "redacted_thinking") b->daten = json::text(e, "content_block.data");
        else if (b->typ == "text") b->text = json::text(e, "content_block.text");
        else if (b->typ == "fallback") {
            // Wechsel zum Ersatzmodell: bisherige Denkblöcke und Werkzeugaufrufe gelten nicht
            for (auto &x : a_.bloecke)
                if (&x != b && (x.typ == "thinking" || x.typ == "redacted_thinking" || x.typ == "tool_use")) x.typ = "verworfen";
        }
    } else if (typ == "content_block_delta") {
        Block *b = block(a_, json::ganz(e, "index", -1));
        if (!b) return;
        std::string dt = json::text(e, "delta.type");
        if (dt == "text_delta") b->text += json::text(e, "delta.text");
        else if (dt == "thinking_delta") b->text += json::text(e, "delta.thinking");
        else if (dt == "signature_delta") b->signatur += json::text(e, "delta.signature");
        else if (dt == "input_json_delta") b->eingabe += json::text(e, "delta.partial_json");
    } else if (typ == "content_block_stop") {
        if (Block *b = block(a_, json::ganz(e, "index", -1))) b->fertig = true;
    } else if (typ == "message_delta") {
        std::string s = json::text(e, "delta.stop_reason");
        if (!s.empty()) a_.stop_reason = s;
        a_.ausgabe = json::ganz(e, "usage.output_tokens", a_.ausgabe);
    } else if (typ == "message_stop") {
        a_.beendet = true;
    } else if (typ == "error") {
        a_.fehler_typ = json::text(e, "error.type");
        a_.fehler_text = json::text(e, "error.message");
    }
}

std::string rumpf(const Anfrage &a, const std::vector<std::string> &nachrichten)
{
    // ⚠️ Feste Reihenfolge und feste Texte: der Anfang bleibt von Anfrage zu Anfrage gleich.
    std::string r = "{\"model\":" + escape(a.modell) + ",\"max_tokens\":" + std::to_string(a.max_tokens) +
                    ",\"stream\":true,\"cache_control\":{\"type\":\"ephemeral\"}";
    if (!a.werkzeuge.empty()) r += ",\"tools\":" + a.werkzeuge;
    r += ",\"system\":[{\"type\":\"text\",\"text\":" + escape(a.system) + ",\"cache_control\":{\"type\":\"ephemeral\"}}]";
    if (a.denken) r += ",\"thinking\":{\"type\":\"adaptive\",\"display\":\"summarized\"}";
    if (mit_ersatzmodell(a.modell)) r += ",\"fallbacks\":\"default\"";
    if (a.aufwand || !a.format.empty()) {
        r += ",\"output_config\":{";
        if (a.aufwand) r += "\"effort\":\"" + std::string(a.aufwand) + "\"";
        if (!a.format.empty()) r += std::string(a.aufwand ? "," : "") + "\"format\":{\"type\":\"json_schema\",\"schema\":" + a.format + "}";
        r += "}";
    }
    r += ",\"messages\":[";
    for (size_t i = 0; i < nachrichten.size(); i++) r += (i ? "," : "") + nachrichten[i];
    r += "]}";
    return r;
}

std::string nutzer(const std::string &text)
{
    return "{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":" + escape(text) + "}]}";
}

std::string assistent(const Antwort &a)
{
    std::string r = "{\"role\":\"assistant\",\"content\":[";
    bool erste = true;
    for (const auto &b : a.bloecke) {
        std::string x;
        if (b.typ == "text" && !b.text.empty()) x = "{\"type\":\"text\",\"text\":" + escape(b.text) + "}";
        else if (b.typ == "thinking" && !b.signatur.empty())
            x = "{\"type\":\"thinking\",\"thinking\":" + escape(b.text) + ",\"signature\":" + escape(b.signatur) + "}";
        else if (b.typ == "redacted_thinking" && !b.daten.empty()) x = "{\"type\":\"redacted_thinking\",\"data\":" + escape(b.daten) + "}";
        else if (b.typ == "tool_use" && b.fertig) {
            json::Baum in = json::lesen(b.eingabe.empty() ? "{}" : b.eingabe);
            x = "{\"type\":\"tool_use\",\"id\":" + escape(b.id) + ",\"name\":" + escape(b.name) +
                ",\"input\":" + (in && cJSON_IsObject(in.get()) ? json::schreiben(in.get()) : std::string("{}")) + "}";
        }
        if (x.empty()) continue;
        r += (erste ? "" : ",") + x;
        erste = false;
    }
    if (erste) r += "{\"type\":\"text\",\"text\":\"(leer)\"}";
    return r + "]}";
}

std::string ergebnisse(const std::vector<std::pair<std::string, std::string>> &e)
{
    std::string r = "{\"role\":\"user\",\"content\":[";
    for (size_t i = 0; i < e.size(); i++)
        r += (i ? "," : "") + std::string("{\"type\":\"tool_result\",\"tool_use_id\":") + escape(e[i].first) +
             ",\"content\":" + escape(e[i].second.empty() ? "(leer)" : e[i].second) + "}";
    return r + "]}";
}

std::string fehlertext(int status, const std::string &body)
{
    json::Baum j = json::lesen(body);
    std::string msg = json::text(j.get(), "error.message");
    if (status == 0) return "Anthropic ist nicht erreichbar. Besteht eine Verbindung zum Internet?";
    if (status == 401) return "Der API-Schlüssel von Anthropic wurde abgelehnt. Bitte prüfen Sie ihn in den Einstellungen der Tafel.";
    if (status == 403) return "Der API-Schlüssel hat keine Berechtigung für dieses Modell.";
    if (status == 429) return "Anthropic begrenzt gerade die Anfragen. Bitte versuchen Sie es in einer Minute erneut.";
    if (status == 400 && msg.find("prompt is too long") != std::string::npos)
        return "Das Gespräch ist für das Modell zu lang geworden. Beginnen Sie ein neues Gespräch.";
    if (status >= 500) return "Anthropic ist gerade überlastet. Bitte versuchen Sie es gleich noch einmal.";
    return "Anthropic meldet Fehler " + std::to_string(status) + (msg.empty() ? "." : ": " + msg);
}

bool wiederholen(int status, const std::string &t) { return status >= 500 || status == 529 || t == "overloaded_error" || t == "api_error"; }

namespace {

// Fassung aus der Kennung: claude-opus-5-5 → 5.5, claude-sonnet-5 → 5, sonst 0
double fassung(const std::string &id, const std::string &reihe)
{
    std::string p = "claude-" + reihe + "-";
    if (id.rfind(p, 0) != 0 || id.size() <= p.size() || !isdigit((unsigned char)id[p.size()])) return 0;
    int a = 0, b = 0;
    sscanf(id.c_str() + p.size(), "%d-%d", &a, &b);
    if (b > 99) b = 0;   // Datum statt Unterfassung
    return a + b / 10.0;
}

}  // namespace

Modell modell_vorgabe(const std::string &reihe)
{
    if (reihe == "haiku") return {"claude-haiku-5-5", true, true};
    if (reihe == "sonnet") return {"claude-sonnet-5-5", true, true};
    return {"claude-opus-5-5", true, true};
}

Modell modell_waehlen(const std::string &antwort, const std::string &reihe)
{
    Modell m = modell_vorgabe(reihe);
    json::Baum j = json::lesen(antwort);
    std::string juengst;
    for (const cJSON *e : json::liste(j.get(), "data")) {
        std::string id = json::text(e, "id"), zeit = json::text(e, "created_at");
        if (fassung(id, reihe) == 0 || zeit <= juengst) continue;
        juengst = zeit;
        m.id = id;
        // Ohne Angabe der API: Haiku vor 5 kann weder Denken noch Aufwand, alle übrigen beides
        bool alt = reihe == "haiku" && fassung(id, reihe) < 5;
        m.denken = json::hat(e, "capabilities.thinking.types.adaptive.supported")
                       ? json::wahr(e, "capabilities.thinking.types.adaptive.supported") : !alt;
        m.aufwand = json::hat(e, "capabilities.effort.supported") ? json::wahr(e, "capabilities.effort.supported") : !alt;
    }
    return m;
}

bool mit_ersatzmodell(const std::string &id) { return fassung(id, "opus") >= 5 || fassung(id, "sonnet") >= 5.5; }

std::string modellname(const std::string &id)
{
    // claude-opus-5-5 → Claude Opus 5.5
    std::string r = "Claude";
    size_t p = id.find('-');
    if (p == std::string::npos) return id;
    std::string rest = id.substr(p + 1);
    size_t q = rest.find('-');
    std::string reihe = rest.substr(0, q);
    if (!reihe.empty()) reihe[0] = (char)toupper((unsigned char)reihe[0]);
    r += " " + reihe;
    if (q != std::string::npos) {
        std::string v = rest.substr(q + 1);
        for (char &c : v)
            if (c == '-') c = '.';
        r += " " + v;
    }
    return r;
}

}  // namespace claude
