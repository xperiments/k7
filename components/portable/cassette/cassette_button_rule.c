/*
 * The button's pure rule: the tap guard, the ring table and the pulse's triangle. See
 * cassette_button_rule.h. No LVGL, no allocation, no state: this is the arithmetic the two
 * copies had begun to disagree about, in one place the host can test.
 */
#include "cassette_button_rule.h"

bool cassette_button_is_tap(bool moved, bool gesturing)
{
    return !moved && !gesturing;
}

int cassette_button_ring_w(cassette_button_ring_t ring, bool pressed)
{
    return pressed ? ring.pressed : ring.released;
}

int cassette_button_pulse_scale(int32_t step)
{
    /* Total, not asserted: an animation's step is never out of range, but a caller that
     * starts mid-run should not draw a scale above full size. */
    if (step < 0) step = 0;
    else if (step > CASSETTE_BUTTON_PULSE_STEPS) step = CASSETTE_BUTTON_PULSE_STEPS;

    /* The distance from the start along the triangle: up to the half, then back down. */
    const int32_t half = CASSETTE_BUTTON_PULSE_STEPS / 2;
    const int32_t peak = step < half ? step : CASSETTE_BUTTON_PULSE_STEPS - step;
    const int32_t depth = CASSETTE_BUTTON_PULSE_SCALE - CASSETTE_BUTTON_PULSE_PEAK;

    return CASSETTE_BUTTON_PULSE_SCALE - (int)(depth * peak / half);
}
