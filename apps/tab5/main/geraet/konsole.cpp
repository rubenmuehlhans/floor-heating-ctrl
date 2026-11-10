// Konsole über USB-C (USB-Serial/JTAG): Befehle für Entwicklung und Bildschirmfotos.
//
//   info                    Firmware, WLAN, Geräte, freier Speicher
//   seite <name>            Seite öffnen (ui::seite_nach_name: uebersicht, raeume, raum:<id>, …)
//   wifi <ssid> [kennwort]  mit einem Netz verbinden
//   ki_schluessel <sk-ant-…> API-Schlüssel von Anthropic ins NVS (tools/schluessel.py)
//   shot                    Bildschirmfoto Zeile für Zeile (tools/aufnahme.py)
//   neustart
//
// ⚠️ Nie Kennwörter oder den API-Schlüssel ausgeben.
// ⚠️ LVGL nur unter bsp_display_lock(); `shot` zeichnet im Konsolen-Task, deshalb 16 KB Stapel
// (camperSense, Falle 19).
#include <stdio.h>
#include <string.h>
#include <string>
#include "bsp/esp-bsp.h"
#include "daten.h"
#include "eins.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "geraet.h"
#include "ki.h"
#include "ui.h"

namespace konsole {

namespace {

// Base64 eines Puffers in eine Zeichenkette
std::string base64(const uint8_t *d, size_t n)
{
    static const char Z[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o += Z[v >> 18 & 63];
        o += Z[v >> 12 & 63];
        o += i + 1 < n ? Z[v >> 6 & 63] : '=';
        o += i + 2 < n ? Z[v & 63] : '=';
    }
    return o;
}

// Bildschirmfoto Zeile für Zeile: „@@Z <nr> <base64>“. Das Werkzeug setzt die Zeilen nach
// Nummer zusammen und holt fehlende aus einer zweiten Aufnahme (wie camperSense, Falle 9).
// ⚠️ Das Protokoll schweigt währenddessen: eine Meldung eines anderen Tasks mitten in einer
// Zeile machte sie unbrauchbar. ⚠️ Nach jeder Zeile die CPU freigeben, sonst schlägt der
// Task-Watchdog an (am Gerät gesehen, 10.11.2026).
void shot()
{
    bsp_display_lock(0);
    lv_draw_buf_t *b = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();
    if (!b) {
        puts("@@FEHLER kein Speicher für das Bild");
        return;
    }
    esp_log_level_set("*", ESP_LOG_NONE);
    printf("@@BILD %d %d %d\n", (int)b->header.w, (int)b->header.h, (int)b->header.stride);
    for (int y = 0; y < (int)b->header.h; y++) {
        std::string z = base64(b->data + (size_t)y * b->header.stride, (size_t)b->header.w * 2);
        printf("@@Z %d %s\n", y, z.c_str());
        vTaskDelay(1);
    }
    puts("@@ENDE");
    esp_log_level_set("*", ESP_LOG_INFO);
    lv_draw_buf_destroy(b);
}

void befehl(const std::string &z)
{
    std::string cmd = z.substr(0, z.find(' ')), arg = z.find(' ') == std::string::npos ? "" : z.substr(z.find(' ') + 1);
    if (cmd == "info") {
        daten::Wlan w = daten::wlan();
        auto b = daten::bild();
        printf("Firmware %s\nWLAN %s %s %d dBm\nGeräte %u%s, Befunde %u\nfrei intern %u, PSRAM %u\n", esp_app_get_description()->version,
               w.ssid.c_str(), w.ip.c_str(), w.rssi, (unsigned)b->geraete.size(), b->beispiel ? " (Beispiel)" : "",
               (unsigned)b->befunde.size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        printf("Leitstand: %s\nVerlauf: %u Reihen\n", daten::leitstand_info().c_str(), (unsigned)daten::reihen().size());
    } else if (cmd == "seite") {
        bsp_display_lock(0);
        bool ok = ui::seite_nach_name(arg);
        bsp_display_unlock();
        puts(ok ? "ok" : "unbekannte Seite");
    } else if (cmd == "wifi") {
        std::string ssid = arg.substr(0, arg.find(' ')), pw = arg.find(' ') == std::string::npos ? "" : arg.substr(arg.find(' ') + 1);
        daten::wlan_verbinden(ssid, pw);
        puts("verbinde");
    } else if (cmd == "ki_schluessel") {
        // ⚠️ Nie zurückgeben oder protokollieren; nur die letzten vier Zeichen zur Kontrolle.
        std::string k = arg;
        while (!k.empty() && (k.back() == ' ' || k.back() == '\r')) k.pop_back();
        if (k.rfind("sk-ant-", 0) != 0 || k.size() < 40) puts("@@SCHLUESSEL abgelehnt: erwartet sk-ant-…");
        else {
            eins::text_setzen(eins::k::KI_SCHLUESSEL, k);
            printf("@@SCHLUESSEL gespeichert, endet auf …%s\n", k.substr(k.size() - 4).c_str());
        }
        k.assign(k.size(), '\0');
        linenoiseHistoryFree();   // die Zeile mit dem Schlüssel nicht im Verlauf der Konsole halten
    } else if (cmd == "ki_pruefen") {
        ki::verbindung_pruefen();
        puts("Verbindungstest eingereiht, Ergebnis im Protokoll");
    } else if (cmd == "shot") shot();
    else if (cmd == "neustart") esp_restart();
    else if (!cmd.empty()) puts("Befehle: info, seite <name>, wifi <ssid> [kennwort], ki_schluessel <sk-ant-…>, shot, neustart");
}

int ausfuehren(int argc, char **argv)
{
    std::string z;
    for (int i = 0; i < argc; i++) z += (i ? " " : "") + std::string(argv[i]);
    befehl(z);
    return 0;
}

}  // namespace

void start()
{
    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "tafel> ";
    rc.task_stack_size = 16384;   // `shot` zeichnet in diesem Task
    esp_console_dev_usb_serial_jtag_config_t dc = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if (esp_console_new_repl_usb_serial_jtag(&dc, &rc, &repl) != ESP_OK) return;
    // ⚠️⚠️ Immer der einfache Modus: antwortet das Terminal auf ESC[5n (idf.py monitor), schaltet
    // die Konsole sonst in den Vollmodus und verschluckt Eingaben (camperSense, Falle 10.16).
    linenoiseSetDumbMode(1);
    for (const char *n : {"info", "seite", "wifi", "ki_schluessel", "ki_pruefen", "shot", "neustart"}) {
        esp_console_cmd_t c = {};
        c.command = n;
        c.help = "";
        c.func = ausfuehren;
        esp_console_cmd_register(&c);
    }
    esp_console_start_repl(repl);
}

}  // namespace konsole
