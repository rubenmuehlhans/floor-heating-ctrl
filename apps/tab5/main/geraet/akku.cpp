#include "akku.h"

// Übernommen vom Tab5 von camperSense (main/akku.cpp); Vorbild dort die Werksfirmware
// (M5Tab5-UserDemo): INA226 an 0x41 auf dem internen I²C-Bus, Shunt 5 mΩ, 16-fach gemittelt.

#include <math.h>
#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "akkustand.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "akku";

namespace akku {

namespace {

constexpr uint8_t ADRESSE = 0x41;
constexpr uint8_t REG_KONFIG = 0x00, REG_SHUNT = 0x01, REG_BUS = 0x02, REG_HERSTELLER = 0xFE, REG_CHIP = 0xFF;
// Mittelung 16, Wandelzeit Bus und Shunt je 1,1 ms, Dauerbetrieb Shunt + Bus —
// dieselbe Einstellung wie die Werksfirmware (INA226_AVERAGES_16, …_1100US,
// INA226_MODE_SHUNT_BUS_CONT).
constexpr uint16_t KONFIG = 0x4000 | (2 << 9) | (4 << 6) | (4 << 3) | 7;
constexpr float SHUNT_OHM = 0.005f;
constexpr float SHUNT_LSB_V = 2.5e-6f, BUS_LSB_V = 1.25e-3f;

i2c_master_dev_handle_t s_dev;
SemaphoreHandle_t s_mx;
Stand s_stand;
bool s_da;
bool s_lade_an;
akkustand::Rechner s_rechner;

struct Sperre {
    Sperre() { xSemaphoreTake(s_mx, portMAX_DELAY); }
    ~Sperre() { xSemaphoreGive(s_mx); }
};

bool lesen16(uint8_t reg, uint16_t &v)
{
    uint8_t b[2];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, 100) != ESP_OK) return false;
    v = (uint16_t)(b[0] << 8 | b[1]);   // INA226: höherwertiges Byte zuerst
    return true;
}

bool schreiben16(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = {reg, (uint8_t)(v >> 8), (uint8_t)(v & 0xFF)};
    return i2c_master_transmit(s_dev, b, 3, 100) == ESP_OK;
}

esp_io_expander_handle_t expander() { return bsp_io_expander1_init(); }


bool einrichten()
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) return false;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = ADRESSE;
    cfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) return false;
    uint16_t hersteller = 0, chip = 0;
    if (!lesen16(REG_HERSTELLER, hersteller) || !lesen16(REG_CHIP, chip) || hersteller != 0x5449) {
        ESP_LOGW(TAG, "INA226 an 0x%02x antwortet nicht passend (0x%04x/0x%04x)", ADRESSE, hersteller, chip);
        i2c_master_bus_rm_device(s_dev);
        s_dev = nullptr;
        return false;
    }
    return schreiben16(REG_KONFIG, KONFIG);
}

void messen();
void anstossen(float spannung, float strom);

void task(void *)
{
    for (int i = 0; i < 5 && !s_dev; i++) {
        if (einrichten()) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_da = s_dev != nullptr;
    ESP_LOGI(TAG, "INA226 %s", s_da ? "bereit" : "nicht gefunden");
    if (!s_da) vTaskDelete(nullptr);
    while (true) {
        messen();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void messen()
{
    {
        uint16_t sh = 0, bu = 0;
        if (lesen16(REG_SHUNT, sh) && lesen16(REG_BUS, bu)) {
            float strom = (int16_t)sh * SHUNT_LSB_V / SHUNT_OHM;
            float spannung = bu * BUS_LSB_V;
            s_rechner.schritt(spannung, strom, 1.0f);
            anstossen(spannung, strom);
            const auto &r = s_rechner;
            Sperre s;
            s_stand.spannung = spannung;
            s_stand.strom = fabsf(r.strom_mittel);
            s_stand.lade_an = s_lade_an;
            s_stand.strom_roh = strom;
            s_stand.strom_mittel = r.strom_mittel;
            s_stand.zustand = r.strom_mittel < -akkustand::RUHE_A ? Zustand::laedt
                              : r.strom_mittel > akkustand::RUHE_A ? Zustand::entlaedt
                              : r.voll                             ? Zustand::voll
                                                                   : Zustand::netz;
            s_stand.prozent = r.soc < 0 ? -1 : (int)lroundf(r.soc);
            s_stand.restzeit_min = r.restzeit_min();
        }
    }
}

// ⚠️⚠️ Das Ladegerät springt beim Einschalten von CHG_EN nur manchmal an: am Mac-Port je
// Umschaltung etwa jedes dritte Mal, sonst bleibt der Strom bei 0, stabil, bis CHG_EN erneut
// wechselt — gleich mit welcher Stromstufe und wie lange CHG_EN vorher aus war (am Gerät,
// 11.11.2026; die Werksfirmware schaltet es auch nur einmal ein). Deshalb: Laden ein, am Netz
// (kein Entladestrom), 10 s kein Ladestrom und der Akku nicht oben → CHG_EN kurz aus und
// wieder ein. Höchstens alle 10 s; über 8,15 V nie, dort darf das Ladegerät abgeregelt haben.
void anstossen(float spannung, float strom)
{
    static int still_s, versuche;
    bool haengt = s_lade_an && fabsf(strom) < akkustand::RUHE_A && spannung > 6.0f && spannung < 8.15f && !s_rechner.voll;
    if (!haengt) {
        if (versuche && strom < -akkustand::RUHE_A) {
            ESP_LOGI(TAG, "Ladegerät lädt nach %d Neustart(s) von CHG_EN", versuche);
            versuche = 0;
        }
        still_s = 0;
        return;
    }
    if (++still_s < 10) return;
    still_s = 0;
    versuche++;
    ESP_LOGW(TAG, "Laden ein, aber kein Ladestrom bei %.2f V — CHG_EN neu (%d)", (double)spannung, versuche);
    laden(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    laden(true);
}

}  // namespace

void start()
{
    s_mx = xSemaphoreCreateMutex();
    // ⚠️ Eine Messung SOFORT, vor `laden(true)` im Hauptablauf: Nach jedem Neustart
    // steht CHG_EN zunächst auf aus, der Akku liegt also kurz in Ruhe — die beste
    // Gelegenheit für einen Ladestand aus der Spannung. Beim Laden ginge das nicht.
    if (einrichten()) {
        s_da = true;
        vTaskDelay(pdMS_TO_TICKS(40));   // erste Wandlung (16 × 2 × 1,1 ms)
        messen();
        ESP_LOGI(TAG, "Start: %.2f V, %.3f A, Ladestand %d %%", (double)s_stand.spannung,
                 (double)s_rechner.strom_mittel, s_stand.prozent);
    }
    xTaskCreate(task, "akku", 3072, nullptr, 2, nullptr);
}

bool vorhanden() { return s_da; }

Stand stand()
{
    Sperre s;
    return s_stand;
}

void laden(bool an)
{
    esp_io_expander_handle_t e = expander();
    if (!e) return;
    // ⚠️⚠️ Der Treiber setzt den Expander beim Anlegen zurück: alle Pins Ausgang, HOCHOHMIG
    // (OUT_H_IM = 0xFF) und mit Pull-down. Ein gesetzter Pegel wirkt erst im Gegentakt.
    // ⚠️⚠️ Dazu gehört nCHG_QC_EN (P5): hochohmig an Pull-down ist die Stromstufe des Ladegeräts
    // unbestimmt, und es lud nach dem Einschalten von CHG_EN mal mit 0,49 A, mal gar nicht —
    // zufällig je Umschaltung, und dazwischen stabil (am Gerät, 11.11.2026). Deshalb wie M5Unified
    // (Power_Class.inl, board_M5Tab5): P5 getrieben, CHG_STAT (P6) Eingang, ohne Pulls.
    // P5 hoch = 500 mA (tief = 1000 mA): genug für den Akku, schont einen schwachen USB-Port.
    constexpr uint32_t QC = IO_EXPANDER_PIN_NUM_5, STAT = IO_EXPANDER_PIN_NUM_6, EN = IO_EXPANDER_PIN_NUM_7;
    esp_io_expander_set_pullupdown(e, QC | STAT | EN, IO_EXPANDER_PULL_NONE);
    esp_io_expander_set_dir(e, STAT, IO_EXPANDER_INPUT);
    esp_io_expander_set_level(e, QC, 1);
    esp_io_expander_set_dir(e, QC, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_output_mode(e, QC, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    esp_io_expander_set_level(e, EN, an ? 1 : 0);
    esp_io_expander_set_dir(e, EN, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_output_mode(e, EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    // ⚠️ Gemerkt statt zurückgelesen: das Eingangsregister des Expanders meldet für
    // Ausgänge 0, auch für den sichtbar treibenden WLAN-Pin.
    s_lade_an = an;
    ESP_LOGI(TAG, "Laden %s", an ? "ein" : "aus");
}

std::string register_text()
{
    // Eigener Zugang zum Chip: der Treiber des Expanders liest Ausgänge aus seinem Zwischenspeicher.
    static i2c_master_dev_handle_t dev;
    if (!dev) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = 0x44;
        cfg.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &dev) != ESP_OK) return "0x44 nicht erreichbar";
    }
    struct {
        uint8_t reg;
        const char *name;
    } const R[] = {{0x03, "Richtung"}, {0x05, "Ausgang"}, {0x07, "hochohmig"}, {0x0B, "Pull an"}, {0x0D, "Pull hoch"}, {0x0F, "Eingang"}};
    std::string t;
    for (const auto &r : R) {
        uint8_t v = 0;
        char z[48];
        if (i2c_master_transmit_receive(dev, &r.reg, 1, &v, 1, 100) != ESP_OK) snprintf(z, sizeof z, "%s ?, ", r.name);
        else snprintf(z, sizeof z, "%s %02x, ", r.name, v);
        t += z;
    }
    return t;
}

}  // namespace akku
