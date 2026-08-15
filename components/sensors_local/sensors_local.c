#include "sensors_local.h"

#include <string.h>

#include "ds18b20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hw_map.h"
#include "i2cbus.h"
#include "onewire_bus.h"

static const char *TAG = "sens";

#define POLL_PERIOD_MS 30000
#define HDC1080_REG_TEMP 0x00
#define HDC1080_REG_HUM  0x01
#define HDC1080_REG_CFG  0x02

static SemaphoreHandle_t s_mtx;
static sensors_snapshot_t s_snap;

static onewire_bus_handle_t s_bus;
static ds18b20_device_handle_t s_ds[SENSORS_MAX_DS18B20];
static uint8_t s_ds_count;

static i2c_master_dev_handle_t s_hdc;

/* ------------------------------------------------------------------ */
/* DS18B20                                                             */
/* ------------------------------------------------------------------ */

static void ds_discover(void)
{
    onewire_bus_config_t bus_cfg = {.bus_gpio_num = HW_PIN_ONEWIRE};
    onewire_bus_rmt_config_t rmt_cfg = {.max_rx_bytes = 10};

    if (onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "1-Wire-Bus an GPIO%d liess sich nicht einrichten", HW_PIN_ONEWIRE);
        return;
    }

    onewire_device_iter_handle_t iter = NULL;
    if (onewire_new_device_iter(s_bus, &iter) != ESP_OK) {
        return;
    }

    onewire_device_t dev;
    while (onewire_device_iter_get_next(iter, &dev) == ESP_OK) {
        if (s_ds_count >= SENSORS_MAX_DS18B20) {
            ESP_LOGW(TAG, "Mehr als %d Fuehler am Bus, die weiteren werden ignoriert",
                     SENSORS_MAX_DS18B20);
            break;
        }
        ds18b20_config_t cfg = {};
        ds18b20_device_handle_t handle;
        if (ds18b20_new_device(&dev, &cfg, &handle) != ESP_OK) {
            continue; /* kein DS18B20, etwa ein anderer 1-Wire-Baustein */
        }
        s_ds[s_ds_count] = handle;
        s_snap.ds[s_ds_count].address = dev.address;
        s_ds_count++;
        ESP_LOGI(TAG, "Fuehler 0x%016llX gefunden", dev.address);
    }
    onewire_del_device_iter(iter);

    s_snap.ds_count = s_ds_count;
    ESP_LOGI(TAG, "%u DS18B20 am Bus", s_ds_count);
}

static void ds_read_all(void)
{
    for (uint8_t i = 0; i < s_ds_count; i++) {
        if (ds18b20_trigger_temperature_conversion(s_ds[i]) != ESP_OK) {
            continue;
        }
        float t = 0;
        if (ds18b20_get_temperature(s_ds[i], &t) != ESP_OK) {
            continue;
        }
        /* 85 Grad ist der Ruecksetzwert nach dem Einschalten und kein
         * Messwert - im ESPHome-Aufbau war dafuer filter_out gesetzt. */
        if (t > 84.9f && t < 85.1f) {
            continue;
        }
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_snap.ds[i].temp_c = t;
        s_snap.ds[i].valid = true;
        xSemaphoreGive(s_mtx);
    }
}

/* ------------------------------------------------------------------ */
/* HDC1080                                                             */
/* ------------------------------------------------------------------ */

static bool hdc_init(void)
{
    i2c_master_bus_handle_t bus = i2cbus_get();
    if (bus == NULL || !i2cbus_probe(HW_ADDR_HDC1080)) {
        ESP_LOGW(TAG, "Kein HDC1080 auf Adresse 0x%02X", HW_ADDR_HDC1080);
        return false;
    }

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HW_ADDR_HDC1080,
        .scl_speed_hz = HW_I2C_FREQ_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev, &s_hdc) != ESP_OK) {
        return false;
    }

    /* Getrennte Messung, 14 Bit fuer beide Groessen, Heizung aus. */
    uint8_t cfg[3] = {HDC1080_REG_CFG, 0x00, 0x00};
    if (i2c_master_transmit(s_hdc, cfg, sizeof(cfg), 100) != ESP_OK) {
        return false;
    }
    ESP_LOGI(TAG, "HDC1080 bereit");
    return true;
}

/* Der HDC1080 braucht nach dem Anstossen der Messung eine Wandlungszeit,
 * bevor gelesen werden darf. Deshalb Schreiben und Lesen getrennt. */
static bool hdc_read_reg(uint8_t reg, uint16_t *out)
{
    if (i2c_master_transmit(s_hdc, &reg, 1, 100) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t raw[2];
    if (i2c_master_receive(s_hdc, raw, sizeof(raw), 100) != ESP_OK) {
        return false;
    }
    *out = (uint16_t)((raw[0] << 8) | raw[1]);
    return true;
}

static void hdc_read(void)
{
    if (s_hdc == NULL) {
        return;
    }
    uint16_t t_raw, h_raw;
    if (!hdc_read_reg(HDC1080_REG_TEMP, &t_raw) || !hdc_read_reg(HDC1080_REG_HUM, &h_raw)) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_snap.hdc_valid = false;
        xSemaphoreGive(s_mtx);
        return;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_snap.hdc_temp_c = (t_raw / 65536.0f) * 165.0f - 40.0f;
    s_snap.hdc_humidity = (h_raw / 65536.0f) * 100.0f;
    s_snap.hdc_valid = true;
    xSemaphoreGive(s_mtx);
}

/* ------------------------------------------------------------------ */

static void sensors_task(void *arg)
{
    (void)arg;
    for (;;) {
        ds_read_all();
        hdc_read();
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

esp_err_t sensors_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ds_discover();
    hdc_init();

    if (s_ds_count == 0 && s_hdc == NULL) {
        ESP_LOGW(TAG, "Keine Zusatzsensoren gefunden, Abfrage wird nicht gestartet");
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(sensors_task, "sensors", 4096, NULL, 3, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void sensors_get(sensors_snapshot_t *out)
{
    if (s_mtx == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_mtx);
}
