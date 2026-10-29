/* The panel, when the board has one. */
#pragma once

#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/* Bring the panel up through esp_lvgl_port and hand back the display it created, or NULL
 * if the panel or the port refused. The port owns the framebuffer and vsync handshake
 * from there on; LVGL calls made from other tasks must take lvgl_port_lock(). */
lv_display_t *panel_rgb_start(void);

/* The frame the panel is scanning, as the RGB565 buffer LVGL draws into - the panel board
 * runs the port in direct mode, so this is not a copy. False when the panel is not up.
 *
 * The port owns two of these buffers and LVGL alternates between them, so a caller that
 * wants a complete picture must force full refreshes first (screen_frame.c does); reading
 * this without one can catch the buffer that is not in front. Call under
 * lvgl_port_lock(), and copy before unlocking if the caller means to touch the pixels
 * while the screen keeps drawing. */
bool panel_rgb_frame(const uint16_t **px, int *w, int *h);
