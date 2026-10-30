/*
 * The backlight on a board that has none: the same interface, doing nothing.
 *
 * The Menu offers a Backlight row and the screen task runs an idle ladder, and both of those
 * are in modules that compile on every board. The panel's backlight is an adapter, so the
 * board without one gets an adapter too - rather than a preprocessor branch inside the Menu,
 * which would be the composition knowing which board it is on for a feature nobody can see.
 *
 * Everything answers "nothing is dimmed, nothing is held, nothing is fading", which is exactly
 * true: there is no light to dim.
 */
#include "backlight.h"

void backlight_hold_off(void)
{
    /* There is no light to hold off. */
}

esp_err_t backlight_start(void)
{
    return ESP_OK;
}

bool backlight_is_dimmed(void)
{
    return false;
}

void backlight_set_percent(int percent)
{
    (void)percent;
}

void backlight_set_percent_immediate(int percent)
{
    (void)percent;
}

void backlight_set_hold(int percent)
{
    (void)percent;
}

int backlight_hold(void)
{
    return 0;   /* the idle ladder owns a level there is none of */
}

void backlight_tick(void)
{
}

bool backlight_is_fading(void)
{
    return false;
}
