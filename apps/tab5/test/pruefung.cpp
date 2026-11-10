// Prüfungen der Datenschicht ohne Gerät: test/run_tests.sh
#include <stdio.h>
#include <fstream>
#include <sstream>
#include "anlage.h"
#include "befunde.h"
#include "fmt.h"
#include "json.h"
#include "verlauf.h"
#include "claude.h"
#include "anweisungen.h"
#include "daten.h"
#include "werkzeuge.h"

void plattform_start(const char *beispiel);

static int s_fehler;
#define PRUEFE(b) do { if (!(b)) { printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #b); s_fehler++; } } while (0)

static std::string datei(const char *p)
{
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char **argv)
{
    std::string bsp = datei(argc > 1 ? argv[1] : "main/beispiel/beispiel.json");
    json::Baum j = json::lesen(bsp);
    PRUEFE(j != nullptr);
    std::vector<anlage::Geraetestand> st;
    for (const cJSON *g : json::liste(j.get(), "geraete")) {
        anlage::Geraetestand s;
        s.id = json::text(g, "device.id");
        s.art = anlage::art_aus(s.id, json::text(g, "device.role"));
        s.ort = json::text(g, "device.site");
        s.ip = json::text(g, "net.ip");
        s.erreichbar = s.gesehen = true;
        s.kontakt = 1787933045;
        s.state = json::schreiben(g);
        st.push_back(s);
    }
    auto b = anlage::zusammenfuehren(st, 1787933045);
    PRUEFE(b->etagen.size() == 3);
    PRUEFE(b->etagen[0].name == "Keller" && b->etagen[1].name == "Erdgeschoss");
    PRUEFE(b->raeume() == 11);
    PRUEFE(b->kessel.vorhanden && !b->kessel.laeuft);
    PRUEFE(b->speicher.vorhanden && fabs(b->speicher.temperatur - 53.04f) < 0.01f);
    PRUEFE(b->heizkreise.size() == 2);
    PRUEFE(!isnan(b->aussen.temperatur));
    PRUEFE(b->geraete.size() == 5 && b->geraete[0].rolle == anlage::Rolle::kessel && b->geraete[1].rolle == anlage::Rolle::speicher);
    const anlage::Raum *bad = b->raum("fbh_7b44f1/2");
    PRUEFE(bad && bad->name == "Bad" && bad->zustand() == anlage::Raumzustand::heizt);
    auto bef = befunde::pruefen(st, *b);
    befunde::Gedaechtnis g;
    auto sicht = g.fortschreiben(bef, 1787933045, true);
    printf("Etagen %zu, Räume %d (heizen %d), Befunde %zu (sichtbar %zu)\n", b->etagen.size(), b->raeume(), b->raeume_heizen(), bef.size(), sicht.size());
    for (auto &x : sicht) printf("  [%s] %s — %s\n", anlage::schwere_text(x.schwere), x.titel.c_str(), x.ort.c_str());

    PRUEFE(fmt::zahl(21.45, 1) == "21,4" || fmt::zahl(21.45, 1) == "21,5");
    PRUEFE(fmt::zahl(-0.04, 1) == "0,0");
    PRUEFE(fmt::temperatur(21.5) == "21,5\xC2\xA0°C");
    PRUEFE(fmt::dauer(4320) == "1\xC2\xA0h 12\xC2\xA0min");
    PRUEFE(fmt::uhrzeit(1787933045) == "18:04");   // 28.08.2026, MESZ
    PRUEFE(fmt::uhrzeit(1798761600) == "01:00");   // 01.01.2027 00:00 UTC, MEZ
    PRUEFE(fmt::versal("Räume") == "RÄUME");
    PRUEFE(fmt::einheiten_fest("bei 21 °C und 5 Starts") == "bei 21\xC2\xA0°C und 5 Starts");

    verlauf::Speicher v;
    for (int i = 0; i < 24; i++) v.abtasten(1787933045 + i * 60, verlauf::abtastung(*b));
    auto r = v.reihe("fuehler.puffer", 1787933045 - 3600, 1787933045 + 3600);
    PRUEFE(r.size() >= 5);
    verlauf::Speicher v2;
    PRUEFE(v2.laden(v.speichern()) && v2.reihe("fuehler.puffer", 1787933045 - 3600, 1787933045 + 3600).size() == r.size());
    // ---- Claude: Strom zerlegen
    {
        const char *sse =
            "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-5-5\",\"usage\":{\"input_tokens\":12}}}\n\n"
            "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}\n"
            "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"Ich lese\"}}\n"
            "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"abc\"}}\n"
            "data: {\"type\":\"content_block_stop\",\"index\":0}\n"
            "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"tu_1\",\"name\":\"befunde\",\"input\":{}}}\n"
            "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"erledigte\\\":\"}}\n"
            "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\" false}\"}}\n"
            "data: {\"type\":\"content_block_stop\",\"index\":1}\n"
            "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},\"usage\":{\"output_tokens\":40}}\n"
            "data: {\"type\":\"message_stop\"}\n";
        claude::Strom st;
        std::string s2 = sse;
        for (size_t i = 0; i < s2.size(); i += 7) st.futter(s2.data() + i, std::min<size_t>(7, s2.size() - i));   // in Stücken
        const auto &a = st.antwort();
        PRUEFE(a.beendet && a.stop_reason == "tool_use" && a.bloecke.size() == 2);
        PRUEFE(a.bloecke[0].typ == "thinking" && a.bloecke[0].signatur == "abc");
        PRUEFE(a.bloecke[1].name == "befunde" && a.bloecke[1].eingabe == "{\"erledigte\": false}");
        std::string m = claude::assistent(a);
        PRUEFE(json::lesen(m) != nullptr && m.find("\"signature\":\"abc\"") != std::string::npos);
        claude::Anfrage an;
        an.modell = "claude-opus-5-5";
        an.system = "Test";
        an.werkzeuge = werkzeuge::definitionen();
        std::string r = claude::rumpf(an, {claude::nutzer("Wie läuft die Anlage?"), m, claude::ergebnisse({{"tu_1", "{}"}})});
        PRUEFE(json::lesen(r) != nullptr);
        PRUEFE(json::lesen(werkzeuge::definitionen()) != nullptr && json::lesen(werkzeuge::definitionen(true)) != nullptr);
        PRUEFE(claude::modellname("claude-opus-5-5") == "Claude Opus 5.5");
        PRUEFE(json::lesen(anweisungen::lagebericht_schema()) != nullptr);
        printf("Systemanweisungen %zu Zeichen, Werkzeuge %zu Zeichen\n", anweisungen::system().size(), werkzeuge::definitionen().size());
    }
    // ---- Werkzeuge gegen die Beispielanlage
    {
        plattform_start(argc > 1 ? argv[1] : "main/beispiel/beispiel.json");
        werkzeuge::Kontext k;
        int angelegt = 0;
        k.vorschlag_anlegen = [&](ki::Vorschlag v) { angelegt++; return std::string("v-test") + (v.aenderungen.empty() ? "" : ""); };
        struct { const char *n, *in; bool json; } F[] = {
            {"anlage_status", "{\"bereich\":\"alle\"}", true},
            {"anlage_status", "{\"bereich\":\"verteiler\",\"geraet\":\"Obergeschoss\",\"rohdaten\":true}", true},
            {"befunde", "{\"erledigte\":true}", true},
            {"verlauf", "{\"reihen\":[\"waermeerzeugung\",\"raum.2.ist\"],\"stunden\":24,\"geraet\":\"Obergeschoss\"}", false},
            {"verlauf", "{\"reihen\":[],\"stunden\":24}", false},
            {"kennzahlen", "{\"tage\":7}", true},
            {"einstellungen_lesen", "{}", true},
            {"aenderung_vorschlagen", "{\"titel\":\"Bad wärmer\",\"aenderungen\":[{\"parameter\":\"raum.target_c\",\"eintrag\":\"Bad\",\"wert\":\"24,5\"}],\"begruendung\":\"b\",\"wirkung\":\"w\"}", true},
            {"aenderung_vorschlagen", "{\"titel\":\"x\",\"aenderungen\":[{\"parameter\":\"raum.target_c\",\"eintrag\":\"Bad\",\"wert\":\"99\"}],\"begruendung\":\"b\",\"wirkung\":\"w\"}", true},
            {"aenderung_vorschlagen", "{\"titel\":\"HK\",\"aenderungen\":[{\"parameter\":\"heizkreis.betriebsart\",\"eintrag\":\"1\",\"wert\":\"aus\"}],\"begruendung\":\"b\",\"wirkung\":\"w\"}", true},
            {"aktion_vorschlagen", "{\"aktion\":\"neustart\",\"geraet\":\"Kessel\",\"titel\":\"t\",\"begruendung\":\"b\",\"wirkung\":\"w\"}", true},
        };
        for (auto &f : F) {
            werkzeuge::Ergebnis e = werkzeuge::ausfuehren(f.n, f.in, k);
            bool gut = !e.text.empty() && (!f.json || json::lesen(e.text) != nullptr);
            PRUEFE(gut);
            printf("  %-22s %6zu Zeichen  %s\n", f.n, e.text.size(), e.beschreibung.c_str());
            if (!gut || getenv("ZEIGEN")) printf("%s\n", e.text.substr(0, 1500).c_str());
        }
        PRUEFE(angelegt == 3);
        PRUEFE(werkzeuge::ausfuehren("anlage_status", "{\"bereich\":\"alle\"}", k).text.find("\"pass\"") == std::string::npos);
    }
    printf(s_fehler ? "%d Fehler\n" : "alle Prüfungen bestanden\n", s_fehler);
    return s_fehler ? 1 : 0;
}
