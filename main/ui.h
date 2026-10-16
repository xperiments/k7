/* The interface: what it shows comes from the broadcast, where its memory comes from
 * is this project's decision, and how it reaches the panel is the board's business. */
#pragma once

#include <stdint.h>

#include "lvgl.h"

#include "mini_icecast.h"

/* Build the screen and start updating it. Called once, after the services are up. */
void ui_start(void);

/* LVGL's flush hook. With an RGB panel the framebuffer *is* the display, so there is
 * nothing to copy out; with no panel this marks the buffer ready and nothing more. */
void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px);

/* The cassette screen: created once, then updated from a status snapshot. */
void ui_cassette_create(void);
void ui_cassette_update(const mini_icecast_status_t *st);

/* What LVGL is holding, counted by the allocator. */
size_t ui_lvgl_held_bytes(void);
size_t ui_lvgl_peak_bytes(void);
