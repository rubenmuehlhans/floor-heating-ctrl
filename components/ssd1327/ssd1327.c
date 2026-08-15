#include "ssd1327.h"

#include <string.h>

#include "esp_log.h"
#include "font5x7.h"
#include "hw_map.h"
#include "i2cbus.h"

static const char *TAG = "oled";

#define BUF_SIZE (SSD1327_WIDTH * SSD1327_HEIGHT / 2)

static i2c_master_dev_handle_t s_dev;
static uint8_t s_buf[BUF_SIZE];
static bool s_present;

/* ------------------------------------------------------------------ */
/* Uebertragung                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t write_cmd(const uint8_t *cmds, size_t n)
{
    /* Steuerbyte 0x00 kennzeichnet Befehle, 0x40 Bilddaten. */
    uint8_t buf[16];
    if (n > sizeof(buf) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = 0x00;
    memcpy(buf + 1, cmds, n);
    return i2c_master_transmit(s_dev, buf, n + 1, 100);
}

static esp_err_t cmd1(uint8_t c)
{
    return write_cmd(&c, 1);
}

static esp_err_t cmd2(uint8_t c, uint8_t arg)
{
    uint8_t b[2] = {c, arg};
    return write_cmd(b, 2);
}

/* ------------------------------------------------------------------ */

esp_err_t ssd1327_init(void)
{
    i2c_master_bus_handle_t bus = i2cbus_get();
    if (bus == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!i2cbus_probe(HW_ADDR_SSD1327)) {
        ESP_LOGW(TAG, "Keine Anzeige auf Adresse 0x%02X gefunden", HW_ADDR_SSD1327);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HW_ADDR_SSD1327,
        .scl_speed_hz = HW_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    cmd1(0xAE); /* Anzeige aus, solange sie eingerichtet wird */

    static const uint8_t init_seq[][2] = {
        {0xA0, 0x51},    /* Adressierung: horizontal, Reihenfolge      */
        {0xA1, 0x00},    /* Startzeile                                 */
        {0xA2, 0x00},    /* Versatz                                    */
        {0xA8, 0x7F},    /* Multiplexverhaeltnis 128                   */
        {0xAB, 0x01},    /* interne Spannungsversorgung                */
        {0x81, 0x20},    /* Kontrast, wird spaeter gesetzt             */
        {0xB1, 0x51},    /* Phasenlaenge                               */
        {0xB3, 0x00},    /* Taktteiler                                 */
        {0xB6, 0x0F},    /* zweite Vorladephase                        */
        {0xBC, 0x08},    /* Vorladespannung                            */
        {0xBE, 0x07},    /* VCOMH                                      */
        {0xD5, 0x62},    /* Funktionsauswahl B                         */
        {0xFD, 0x12},    /* Befehlssperre aufheben                     */
    };
    for (size_t i = 0; i < sizeof(init_seq) / sizeof(init_seq[0]); i++) {
        err = cmd2(init_seq[i][0], init_seq[i][1]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Anzeige antwortet nicht: %s", esp_err_to_name(err));
            return err;
        }
    }
    cmd1(0xA4); /* normale Darstellung */

    memset(s_buf, 0, sizeof(s_buf));
    s_present = true;
    ssd1327_flush();
    cmd1(0xAF); /* Anzeige ein */

    ESP_LOGI(TAG, "Anzeige bereit (%dx%d, 16 Graustufen)", SSD1327_WIDTH, SSD1327_HEIGHT);
    return ESP_OK;
}

bool ssd1327_present(void)
{
    return s_present;
}

esp_err_t ssd1327_set_brightness(uint8_t percent)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percent > 100) {
        percent = 100;
    }
    return cmd2(0x81, (uint8_t)(percent * 255 / 100));
}

esp_err_t ssd1327_flush(void)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Spaltenbereich in Bytes - zwei Bildpunkte je Byte. */
    uint8_t col[3] = {0x15, 0x00, SSD1327_WIDTH / 2 - 1};
    esp_err_t err = write_cmd(col, 3);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t row[3] = {0x75, 0x00, SSD1327_HEIGHT - 1};
    err = write_cmd(row, 3);
    if (err != ESP_OK) {
        return err;
    }

    /* In Teilstuecken senden, damit der I2C-Puffer nicht ueberlaeuft. */
    enum { CHUNK = 256 };
    uint8_t tx[CHUNK + 1];
    tx[0] = 0x40;
    for (size_t off = 0; off < BUF_SIZE; off += CHUNK) {
        size_t n = BUF_SIZE - off < CHUNK ? BUF_SIZE - off : CHUNK;
        memcpy(tx + 1, s_buf + off, n);
        err = i2c_master_transmit(s_dev, tx, n + 1, 200);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Zeichnen                                                            */
/* ------------------------------------------------------------------ */

void ssd1327_clear(uint8_t gray)
{
    gray &= 0x0F;
    memset(s_buf, (uint8_t)((gray << 4) | gray), sizeof(s_buf));
}

void ssd1327_pixel(int x, int y, uint8_t gray)
{
    if (x < 0 || y < 0 || x >= SSD1327_WIDTH || y >= SSD1327_HEIGHT) {
        return;
    }
    size_t idx = (size_t)y * (SSD1327_WIDTH / 2) + (size_t)(x / 2);
    gray &= 0x0F;
    if (x % 2 == 0) {
        s_buf[idx] = (uint8_t)((s_buf[idx] & 0x0F) | (gray << 4));
    } else {
        s_buf[idx] = (uint8_t)((s_buf[idx] & 0xF0) | gray);
    }
}

void ssd1327_fill_rect(int x, int y, int w, int h, uint8_t gray)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            ssd1327_pixel(xx, yy, gray);
        }
    }
}

void ssd1327_rect(int x, int y, int w, int h, uint8_t gray)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int xx = x; xx < x + w; xx++) {
        ssd1327_pixel(xx, y, gray);
        ssd1327_pixel(xx, y + h - 1, gray);
    }
    for (int yy = y; yy < y + h; yy++) {
        ssd1327_pixel(x, yy, gray);
        ssd1327_pixel(x + w - 1, yy, gray);
    }
}

/*
 * Naechstes Zeichen aus einer UTF-8-Folge als Index in die Schrifttabelle.
 * Nicht darstellbare Zeichen werden zu einem Leerzeichen, die
 * Grossbuchstaben-Umlaute auf ihre Grundform abgebildet - fuer Punkte ueber
 * einem Grossbuchstaben fehlt in sieben Zeilen der Platz.
 */
static int next_glyph(const char **p)
{
    unsigned char c = (unsigned char)**p;
    if (c == 0) {
        return -1;
    }
    (*p)++;

    if (c < 0x80) {
        if (c < 0x20 || c > 0x7E) {
            return 0;
        }
        return c - 0x20;
    }

    unsigned char c2 = (unsigned char)**p;
    if (c2 == 0) {
        return 0;
    }
    (*p)++;

    if (c == 0xC3) {
        switch (c2) {
        case 0xA4:
            return GLYPH_ae;
        case 0xB6:
            return GLYPH_oe;
        case 0xBC:
            return GLYPH_ue;
        case 0x9F:
            return GLYPH_sz;
        case 0x84:
            return 'A' - 0x20;
        case 0x96:
            return 'O' - 0x20;
        case 0x9C:
            return 'U' - 0x20;
        default:
            return 0;
        }
    }
    if (c == 0xC2 && c2 == 0xB0) {
        return GLYPH_deg;
    }
    return 0;
}

int ssd1327_text(int x, int y, const char *utf8, uint8_t gray, uint8_t scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int start = x;
    const char *p = utf8;

    for (;;) {
        int g = next_glyph(&p);
        if (g < 0) {
            break;
        }
        if (g >= GLYPH_COUNT) {
            g = 0;
        }
        for (int col = 0; col < FONT_W; col++) {
            uint8_t bits = font5x7[g][col];
            for (int row = 0; row < 7; row++) {
                if (!(bits & (1 << row))) {
                    continue;
                }
                if (scale == 1) {
                    ssd1327_pixel(x + col, y + row, gray);
                } else {
                    ssd1327_fill_rect(x + col * scale, y + row * scale, scale, scale, gray);
                }
            }
        }
        x += FONT_ADVANCE * scale;
    }
    return x - start;
}

int ssd1327_text_width(const char *utf8, uint8_t scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    const char *p = utf8;
    while (next_glyph(&p) >= 0) {
        n++;
    }
    return n * FONT_ADVANCE * scale;
}
