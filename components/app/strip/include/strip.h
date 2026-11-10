#pragma once
/*
 * strip - the addressable light: eight WS2812B pixels on GPIO 17, refreshed at 30 Hz.
 *
 * The state and the effects are not here. `components/portable/rgb` owns them and `rgb_frame()`
 * draws one into a pixel buffer from (state, now_ms, level, n); this module is the board half -
 * the RMT channel, the pixel order, the clock and the one task that runs it. The screen hands the
 * **RGB group**'s state over through `screen_display_rgb_publish()`, so a knob move is a state
 * copy and the next tick shows it.
 *
 * Nothing is on the host tier and nothing needs to be: the arithmetic worth pinning is rgb's,
 * pinned by `t25_rgb`, and what is left is an RMT transaction. The spec is
 * `docs/specs/led-strip.md`; the decision is ADR 0074.
 *
 * `strip_start()` is called once by the appliance, after the screen has built the panel (the
 * cassette's create clears the publish hook, so an earlier install would be wiped). A failure to
 * take an RMT channel is logged and reported, not fatal: the rest of the device serves.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the light up: the RMT channel, the panel's publish hook, and the 33 ms task. True when
 * the strip is running. A second call repeats the first answer and does nothing. */
bool strip_start(void);

#ifdef __cplusplus
}
#endif
