#include "roomctrl.h"

#include <math.h>

float roomctrl_target_position(float setpoint_c, float current_c, float p_band_k, float step)
{
    if (p_band_k <= 0.0f) {
        p_band_k = 1.0f;
    }
    if (step <= 0.0f) {
        step = 0.1f;
    }

    float diff = setpoint_c - current_c;
    float pos = ((diff / p_band_k) + 1.0f) / 2.0f;

    pos = roundf(pos / step) * step;

    if (pos > 1.0f) {
        pos = 1.0f;
    }
    if (pos < 0.0f) {
        pos = 0.0f;
    }
    return pos;
}

bool roomctrl_needs_move(float current_position, float target_position, float min_delta)
{
    return fabsf(current_position - target_position) > min_delta;
}
