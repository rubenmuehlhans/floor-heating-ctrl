// Bedientafel der Fußbodenheizung auf dem M5Stack Tab5 — Start der Firmware.
//
// Reihenfolge ist Vertrag (wie camperSense, satellite-display/tab5/main/main.cpp):
//   1. NVS, dann alles, was Einstellungen liest.
//   2. Konsole — hängt danach etwas, ist das Gerät trotzdem befragbar.
//   3. Display: LCD und Touch einschalten und 500 ms warten, sonst erkennt die
//      Board-Unterstützung die Bestückung nicht.
//   4. ⚠️⚠️ Funkmodul GLEICH nach dem Display, vor allen übrigen Tasks: ESP-Hosted legt
//      seine SDIO-Deskriptoren im internen DMA-RAM an, und jeder Task davor nimmt sich
//      seinen Stapel aus demselben Speicher („no mem for dma descriptors“, Neustartschleife).
//   5. Abfrage (zeigt sofort die Beispielanlage bzw. die bekannten Geräte), Assistent,
//      Oberfläche, zuletzt das WLAN.
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "eins.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bootmenu.h"
#include "geraet.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "tafel";

extern "C" void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    eins::init();
    bootmenu::beim_start();   // nur mit Bootmenü (bootmenu.h)
    konsole::start();

    bsp_i2c_init();
    bsp_feature_enable(BSP_FEATURE_LCD, true);
    bsp_feature_enable(BSP_FEATURE_TOUCH, true);
    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_display_cfg_t cfg = {};
    cfg.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // Tiefe Objektbäume und std::string in den Ereignissen: 7 KB reichen nicht.
    cfg.lvgl_port_cfg.task_stack = 24576;
    cfg.buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES;
    cfg.double_buffer = true;
    cfg.flags.buff_dma = true;
    cfg.flags.buff_spiram = true;
    cfg.flags.sw_rotate = true;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) ESP_LOGE(TAG, "Display nicht gestartet (Bestückung nicht erkannt?)");
    bsp_display_brightness_set((int)eins::zahl(eins::k::HELL, 80));

    bool c6 = daten::funk_starten();

    daten::start();
    ki::start();
    if (disp) {
        bsp_display_lock(0);
        // Querformat: das Panel ist nativ 720 × 1280, gedreht über die PPA
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
        ui::start(disp);
        lv_timer_create([](lv_timer_t *) { anzeige::takt(); }, 1000, nullptr);
        bsp_display_unlock();
    }
    if (c6) daten::wlan_start();
    else ESP_LOGE(TAG, "ohne Funkmodul: kein WLAN, die Tafel zeigt die Beispielanlage");
    ESP_LOGI(TAG, "Start abgeschlossen");
}
