#include "i2cbus.h"

#include "esp_log.h"
#include "hw_map.h"

static const char *TAG = "i2c";

static i2c_master_bus_handle_t s_bus;
static bool s_tried;

i2c_master_bus_handle_t i2cbus_get(void)
{
    if (s_bus != NULL || s_tried) {
        return s_bus;
    }
    s_tried = true;

    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = HW_PIN_I2C_SDA,
        .scl_io_num = HW_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C-Bus liess sich nicht einrichten: %s", esp_err_to_name(err));
        s_bus = NULL;
    }
    return s_bus;
}

void i2cbus_scan(void)
{
    i2c_master_bus_handle_t bus = i2cbus_get();
    if (bus == NULL) {
        return;
    }

    char found[64] = {0};
    size_t len = 0;
    int count = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
            continue;
        }
        count++;
        if (len < sizeof(found) - 8) {
            len += snprintf(found + len, sizeof(found) - len, "0x%02X ", addr);
        }
    }

    if (count == 0) {
        ESP_LOGW(TAG, "Kein Baustein am I2C-Bus (SDA %d, SCL %d)", HW_PIN_I2C_SDA,
                 HW_PIN_I2C_SCL);
    } else {
        ESP_LOGI(TAG, "I2C-Bus: %d Baustein(e) auf %s", count, found);
    }
}

bool i2cbus_probe(uint8_t address)
{
    i2c_master_bus_handle_t bus = i2cbus_get();
    if (bus == NULL) {
        return false;
    }
    return i2c_master_probe(bus, address, 100) == ESP_OK;
}
