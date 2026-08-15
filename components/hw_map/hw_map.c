#include "hw_map.h"

#include <stddef.h>

/*
 * Belegung der Schieberegister: Kanal n benutzt die Bits 2*(n-1) und
 * 2*(n-1)+1. IA schliesst, IB oeffnet - so ist es im ESPHome-Aufbau
 * verdrahtet (open_action schaltet IB ein und IA aus).
 */
static const hw_channel_t s_channels[HW_CHANNEL_COUNT] = {
    {.channel = 1,  .sr_bit_ia = 0,  .sr_bit_ib = 1,  .bemf_group = 0},
    {.channel = 2,  .sr_bit_ia = 2,  .sr_bit_ib = 3,  .bemf_group = 0},
    {.channel = 3,  .sr_bit_ia = 4,  .sr_bit_ib = 5,  .bemf_group = 1},
    {.channel = 4,  .sr_bit_ia = 6,  .sr_bit_ib = 7,  .bemf_group = 1},
    {.channel = 5,  .sr_bit_ia = 8,  .sr_bit_ib = 9,  .bemf_group = 2},
    {.channel = 6,  .sr_bit_ia = 10, .sr_bit_ib = 11, .bemf_group = 2},
    {.channel = 7,  .sr_bit_ia = 12, .sr_bit_ib = 13, .bemf_group = 3},
    {.channel = 8,  .sr_bit_ia = 14, .sr_bit_ib = 15, .bemf_group = 3},
    {.channel = 9,  .sr_bit_ia = 16, .sr_bit_ib = 17, .bemf_group = 4},
    {.channel = 10, .sr_bit_ia = 18, .sr_bit_ib = 19, .bemf_group = 4},
    {.channel = 11, .sr_bit_ia = 20, .sr_bit_ib = 21, .bemf_group = 5},
};

/*
 * ADC1-Kanaele des ESP32 zu den verwendeten Pins:
 *   GPIO36 = CH0, GPIO39 = CH3, GPIO34 = CH6,
 *   GPIO35 = CH7, GPIO32 = CH4, GPIO33 = CH5
 * ADC2 ist bei aktivem WLAN nicht nutzbar, deshalb liegen alle sechs
 * Messeingaenge auf ADC1.
 */
static const hw_bemf_group_t s_groups[HW_BEMF_GROUP_COUNT] = {
    {.group = 0, .gpio = 36, .adc_channel = 0, .channels = {1, 2}},
    {.group = 1, .gpio = 39, .adc_channel = 3, .channels = {3, 4}},
    {.group = 2, .gpio = 34, .adc_channel = 6, .channels = {5, 6}},
    {.group = 3, .gpio = 35, .adc_channel = 7, .channels = {7, 8}},
    {.group = 4, .gpio = 32, .adc_channel = 4, .channels = {9, 10}},
    {.group = 5, .gpio = 33, .adc_channel = 5, .channels = {11, 0}},
};

const hw_channel_t *hw_channel(uint8_t channel)
{
    if (!hw_channel_valid(channel)) {
        return NULL;
    }
    return &s_channels[channel - 1];
}

const hw_bemf_group_t *hw_bemf_group(uint8_t group)
{
    if (group >= HW_BEMF_GROUP_COUNT) {
        return NULL;
    }
    return &s_groups[group];
}

uint8_t hw_group_of_channel(uint8_t channel)
{
    const hw_channel_t *ch = hw_channel(channel);
    return ch ? ch->bemf_group : 0xFF;
}
