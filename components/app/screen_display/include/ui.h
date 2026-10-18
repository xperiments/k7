/* The interface: what it shows comes from the broadcast, where its memory comes from
 * is this project's decision, and how it reaches the panel is the board's business. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mini_icecast.h"

/* The draw buffer when there is no panel: one strip, not a whole screen. LVGL renders
 * only the invalidated area into it, and a smaller buffer means less of the screen is
 * touched per refresh - the arrangement the verified reference integration uses
 * (SolidGL firmware, components/solidgl_platform/src/solidgl_platform.c). */
#define UI_DRAW_LINES 40

/* How long the interface task rests between status updates. The reels are not on this
 * clock - ui_cassette.c runs them from their own 40 ms LVGL timer - so this is the
 * cadence for the text and the host-side redraw alone, and labels do not need 50
 * updates a second. That leaves the core for the network stack that shares it. */
#define UI_FRAME_MS 100

/* And never rest longer than this, however expensive a frame was. The verified
 * reference integration caps its own wait at 500 ms (`task_max_sleep_ms`), and the
 * reason shows up at startup: the first frame draws the whole screen and costs seconds,
 * so an uncapped "three times the frame" would freeze the interface for thirteen of
 * them before it drew anything again. */
#define UI_REST_MAX_MS 500

/* Build the screen and start updating it. Called once, after the services are up. */
void ui_start(void);

/* The cassette screen: created once, then updated from a status snapshot. */
void ui_cassette_create(void);
void ui_cassette_update(const mini_icecast_status_t *st);

/* What LVGL is holding, counted by the allocator. */
size_t ui_lvgl_held_bytes(void);
size_t ui_lvgl_peak_bytes(void);
