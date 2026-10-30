/*
 * What this build's screen has; see screen_caps.h.
 *
 * The one file in this tier where the board symbol is allowed to decide behaviour rather than
 * select an adapter. Everything else asks.
 */
#include "screen_caps.h"

/* Explicit, and load-bearing: this file tests a CONFIG_ symbol, and a translation unit that
 * relies on stdbool alone reads it as 0. That is exactly how this shipped once - both
 * capabilities read false on the panel, so the Card never mounted, the border frames were
 * never built and the artwork loader was never started. */
#include "sdkconfig.h"

static const screen_caps_t k_caps = {
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    .cover = true,
    .panel_borders = true,
#else
    .cover = false,
    .panel_borders = false,
#endif
};

const screen_caps_t *screen_caps(void)
{
    return &k_caps;
}
