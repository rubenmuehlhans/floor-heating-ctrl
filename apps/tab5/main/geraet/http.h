// HTTP zu den Geräten im Heimnetz (ohne TLS, wie NSAllowsLocalNetworking der App).
//
// ⚠️ Blockiert bis zur Antwort oder Zeitgrenze; nur aus eigenen Tasks, nie aus LVGL.
// ⚠️ Weiterleitungen werden nicht verfolgt: 3xx heißt, das Gerät steht im
// Zugangspunktbetrieb (Geraeteverbindung.swift).
#pragma once
#include <string>

namespace http {

struct Antwort {
    int status = 0;          // 0 = kein Austausch (Netz, Zeitgrenze)
    std::string body, etag;
    bool ok() const { return status >= 200 && status < 300; }
};

// `etag`: If-None-Match senden; 304 kommt als status 304 mit leerem Rumpf.
Antwort anfrage(const std::string &ip, const char *methode, const std::string &pfad, const std::string &rumpf = "",
                const std::string &etag = "", int zeit_ms = 6000);

// Fehlermeldung eines Befehls: {"ok":false,"error":"…"} auch mit Status 200.
// Leer = Erfolg.
std::string befehlsfehler(const Antwort &a);

}  // namespace http
