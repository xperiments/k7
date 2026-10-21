#pragma once
/*
 * The panel's backlight: GPIO 2 as a PWM duty cycle rather than an on/off pin.
 *
 * The pin is the board's brightness control, so a percentage of full duty is the
 * whole interface. The screen decides when to dim; this module owns the timer, the
 * channel and the percentage, and logs what it is showing.
 */
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the backlight up at CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT. On a board whose
 * LEDC will not take the pin, falls back to a plain output held high: the panel stays
 * lit at full brightness and only dimming is lost. */
esp_err_t backlight_start(void);

/* True when the backlight is at an idle level rather than the awake one. The touch
 * adapter uses it to make the first touch after sleep a wake-up only. */
bool backlight_is_dimmed(void);

/* Set brightness as a percentage of full duty, clamped to 0..100, then fade toward
 * it over APP_UI_BACKLIGHT_DIM_FADE_MS or APP_UI_BACKLIGHT_WAKE_FADE_MS depending on
 * the direction. Idempotent, and a no-op if backlight_start() did not configure the
 * PWM. A fade already running is reversed from the current light level, so a touch
 * during a dim does not wait for the dim to finish. */
void backlight_set_percent(int percent);

/* Set the duty immediately, with no fade. For the visual test screen, where the
 * point is to compare exact levels rather than the transition. */
void backlight_set_percent_immediate(int percent);

/* Advance a fade by one step. The screen task calls this each pass; backlight_set_
 * percent() also calls it once so the first step is immediate. */
void backlight_tick(void);

/* True while the written duty has not reached the target yet. The screen task uses it
 * to shorten its rest so the fade keeps a steady cadence. */
bool backlight_is_fading(void);

#ifdef __cplusplus
}
#endif
