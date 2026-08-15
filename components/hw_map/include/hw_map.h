/*
 * Zentrale Hardwarebeschreibung der Steuerplatine.
 *
 * Einzige Stelle im Projekt, die Pinnummern und die Verdrahtung der
 * Ventilkanaele auf die BEMF-Messgruppen kennt. Alles andere arbeitet nur noch
 * mit Kanalnummern (1..11) und Gruppennummern (0..5).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ventilkanaele. CH1..CH11, intern durchgehend 0..10 indiziert. */
#define HW_CHANNEL_COUNT 11

/* BEMF-Messgruppen. Je Gruppe ein ADC-Eingang, daran haengen ein oder zwei
 * Kanaele. Innerhalb einer Gruppe darf immer nur ein Kanal fahren, sonst laesst
 * sich die gemessene Gegen-EMK keinem Motor mehr zuordnen. */
#define HW_BEMF_GROUP_COUNT 6

/* Schieberegister: 3 x SN74HC595 = 24 Ausgaenge, davon Q0..Q21 belegt. */
#define HW_SR_CHIP_COUNT 3
#define HW_SR_BIT_COUNT  (HW_SR_CHIP_COUNT * 8)

/* Pins des Schieberegisters (SN74HC595). */
#define HW_PIN_SR_DATA  16 /* SER   */
#define HW_PIN_SR_CLOCK 5  /* SRCLK */
#define HW_PIN_SR_LATCH 17 /* RCLK  */
#define HW_PIN_SR_OE    18 /* OE, aktiv low - auf der Platine fest auf Masse */

/* I2C-Bus: SSD1327-Anzeige (0x3C) und HDC1080 (0x40). */
#define HW_PIN_I2C_SDA 21
#define HW_PIN_I2C_SCL 22
#define HW_I2C_FREQ_HZ 400000
#define HW_ADDR_SSD1327 0x3C
#define HW_ADDR_HDC1080 0x40

/* 1-Wire-Bus der DS18B20-Vorlauffuehler. */
#define HW_PIN_ONEWIRE 23

/* Kapazitive Tasten. Reihenfolge: links, rechts, Auswahl. */
#define HW_TOUCH_COUNT 3
#define HW_PIN_TOUCH_DOWN   4  /* TOUCH0 */
#define HW_PIN_TOUCH_UP     2  /* TOUCH2 */
#define HW_PIN_TOUCH_SELECT 15 /* TOUCH3 */

/* Ansteuerung eines Kanals. Die beiden Eingaenge des L9110s duerfen niemals
 * gleichzeitig aktiv sein - das erzwingt die Ausgabestufe. */
typedef enum {
    HW_DRIVE_OFF = 0,
    HW_DRIVE_OPEN,  /* IB aktiv */
    HW_DRIVE_CLOSE, /* IA aktiv */
} hw_drive_t;

/* Beschreibung eines Ventilkanals. */
typedef struct {
    uint8_t channel;  /* 1..11, wie auf der Platine beschriftet */
    uint8_t sr_bit_ia; /* Bitposition im Schieberegister, Richtung schliessen */
    uint8_t sr_bit_ib; /* Bitposition im Schieberegister, Richtung oeffnen   */
    uint8_t bemf_group; /* 0..5 */
} hw_channel_t;

/* Beschreibung einer BEMF-Messgruppe. */
typedef struct {
    uint8_t group;      /* 0..5 */
    uint8_t gpio;       /* ADC1-faehiger Eingang */
    int adc_channel;    /* adc_channel_t, hier als int um den Header treiberfrei zu halten */
    uint8_t channels[2]; /* zugehoerige Kanalnummern, 0 = nicht belegt */
} hw_bemf_group_t;

const hw_channel_t *hw_channel(uint8_t channel);
const hw_bemf_group_t *hw_bemf_group(uint8_t group);

/* Messgruppe eines Kanals. */
uint8_t hw_group_of_channel(uint8_t channel);

/* Liefert true, wenn die Kanalnummer gueltig ist (1..11). */
static inline bool hw_channel_valid(uint8_t channel)
{
    return channel >= 1 && channel <= HW_CHANNEL_COUNT;
}

#ifdef __cplusplus
}
#endif
