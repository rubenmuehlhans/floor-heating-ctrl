#include "sr74hc595.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "sr595";

static spi_device_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static uint32_t s_bits;
static hw_drive_t s_drive[HW_CHANNEL_COUNT];

/*
 * Das zuerst gesendete Byte wandert am weitesten durch die Kette und landet
 * damit im letzten Baustein. Byte 0 (Q0..Q7) muss deshalb zuletzt raus.
 * Innerhalb eines Bytes gilt MSB zuerst, dadurch entspricht Bit n dem
 * Ausgang Qn.
 */
static esp_err_t sr595_flush_locked(void)
{
    uint8_t tx[HW_SR_CHIP_COUNT];
    for (int i = 0; i < HW_SR_CHIP_COUNT; i++) {
        tx[i] = (uint8_t)((s_bits >> (8 * (HW_SR_CHIP_COUNT - 1 - i))) & 0xFF);
    }

    spi_transaction_t t = {
        .length = HW_SR_CHIP_COUNT * 8,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(s_dev, &t);
}

esp_err_t sr595_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /* Ausgangstreiber freigeben. OE ist aktiv low und auf der Platine fest auf
     * Masse gelegt; wir treiben den Pin zusaetzlich auf low, damit der Zustand
     * auch ohne diese Bruecke definiert ist. */
    gpio_config_t oe = {
        .pin_bit_mask = 1ULL << HW_PIN_SR_OE,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t oe_err = gpio_config(&oe);
    if (oe_err != ESP_OK) {
        ESP_LOGE(TAG, "OE-Pin liess sich nicht einrichten: %s", esp_err_to_name(oe_err));
        return oe_err;
    }
    gpio_set_level(HW_PIN_SR_OE, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = HW_PIN_SR_DATA,
        .miso_io_num = -1,
        .sclk_io_num = HW_PIN_SR_CLOCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = HW_PIN_SR_LATCH,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    memset(s_drive, 0, sizeof(s_drive));
    s_bits = 0;
    return sr595_flush_locked();
}

esp_err_t sr595_set_drive(uint8_t channel, hw_drive_t drive)
{
    const hw_channel_t *ch = hw_channel(channel);
    if (ch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    uint32_t bits = s_bits;
    bits &= ~((1UL << ch->sr_bit_ia) | (1UL << ch->sr_bit_ib));
    if (drive == HW_DRIVE_OPEN) {
        bits |= 1UL << ch->sr_bit_ib;
    } else if (drive == HW_DRIVE_CLOSE) {
        bits |= 1UL << ch->sr_bit_ia;
    }

    esp_err_t err = ESP_OK;
    if (bits != s_bits) {
        s_bits = bits;
        err = sr595_flush_locked();
    }
    if (err == ESP_OK) {
        s_drive[channel - 1] = drive;
    }

    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH%u konnte nicht geschaltet werden: %s", channel, esp_err_to_name(err));
    }
    return err;
}

hw_drive_t sr595_get_drive(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return HW_DRIVE_OFF;
    }
    return s_drive[channel - 1];
}

esp_err_t sr595_all_off(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bits = 0;
    memset(s_drive, 0, sizeof(s_drive));
    esp_err_t err = sr595_flush_locked();
    xSemaphoreGive(s_lock);
    return err;
}

uint32_t sr595_raw_bits(void)
{
    return s_bits;
}

esp_err_t sr595_write_raw(uint32_t bits)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_bits = bits & 0x00FFFFFFUL;
    esp_err_t err = sr595_flush_locked();
    xSemaphoreGive(s_lock);
    return err;
}
