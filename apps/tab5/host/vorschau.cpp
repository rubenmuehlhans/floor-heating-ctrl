// Vorschau der Bedientafel auf dem Mac: zeichnet Seiten mit LVGL in einen Speicher und
// legt sie als PNG ab — ohne Fenster, ohne Gerät.
//
//   host/vorschau.sh                       alle Seiten, hell und dunkel → host/shots/
//   host/build/vorschau <beispiel.json> <ziel> [hell] seite…
//
// Die Seiten laufen mit der Beispielanlage (host/plattform.cpp).
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <string>
#include <vector>
#include "lvgl.h"
#include "ui.h"

void plattform_start(const char *beispiel);
void plattform_hell(bool hell);

static constexpr int B = 1280, H = 720;
static std::vector<uint16_t> s_fb(B * H);

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *p = (const uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) s_fb[y * B + x] = *p++;
    lv_display_flush_ready(d);
}

static void png_chunk(FILE *f, const char *typ, const std::vector<uint8_t> &d)
{
    uint8_t l[4] = {(uint8_t)(d.size() >> 24), (uint8_t)(d.size() >> 16), (uint8_t)(d.size() >> 8), (uint8_t)d.size()};
    fwrite(l, 1, 4, f);
    fwrite(typ, 1, 4, f);
    if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    uLong crc = crc32(0, (const Bytef *)typ, 4);
    crc = crc32(crc, d.data(), (uInt)d.size());
    uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    fwrite(c, 1, 4, f);
}

static void png_schreiben(const std::string &pfad)
{
    std::vector<uint8_t> roh;
    roh.reserve((B * 3 + 1) * H);
    for (int y = 0; y < H; y++) {
        roh.push_back(0);
        for (int x = 0; x < B; x++) {
            uint16_t v = s_fb[y * B + x];
            roh.push_back((uint8_t)(((v >> 11) & 31) * 255 / 31));
            roh.push_back((uint8_t)(((v >> 5) & 63) * 255 / 63));
            roh.push_back((uint8_t)((v & 31) * 255 / 31));
        }
    }
    uLongf n = compressBound((uLong)roh.size());
    std::vector<uint8_t> z(n);
    compress2(z.data(), &n, roh.data(), (uLong)roh.size(), 6);
    z.resize(n);
    FILE *f = fopen(pfad.c_str(), "wb");
    if (!f) return;
    static const uint8_t SIG[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(SIG, 1, 8, f);
    std::vector<uint8_t> ihdr = {0, 0, (uint8_t)(B >> 8), (uint8_t)B, 0, 0, (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
    png_chunk(f, "IHDR", ihdr);
    png_chunk(f, "IDAT", z);
    png_chunk(f, "IEND", {});
    fclose(f);
}

static void laufen(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Aufruf: vorschau <beispiel.json> <zielverzeichnis> [hell] seite…\n");
        return 2;
    }
    int i = 3;
    bool hell = false;
    if (!strcmp(argv[i], "hell")) {
        hell = true;
        i++;
    }
    plattform_start(argv[1]);
    plattform_hell(hell);
    lv_init();
    lv_display_t *d = lv_display_create(B, H);
    static std::vector<uint16_t> puffer(B * 120);
    lv_display_set_buffers(d, puffer.data(), nullptr, puffer.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    ui::start(d);
    laufen(100);
    for (; i < argc; i++) {
        std::string name = argv[i];
        if (!ui::seite_nach_name(name)) {
            fprintf(stderr, "unbekannte Seite: %s\n", name.c_str());
            continue;
        }
        laufen(400);
        lv_refr_now(d);
        std::string datei = name;
        for (char &c : datei)
            if (c == ':' || c == '/') c = '_';
        std::string pfad = std::string(argv[2]) + "/" + datei + (hell ? "-hell" : "") + ".png";
        png_schreiben(pfad);
        printf("  %s\n", pfad.c_str());
    }
    return 0;
}
