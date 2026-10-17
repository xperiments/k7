/* The panel, when the board has one. */
#pragma once

#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/* Bring the panel up through esp_lvgl_port and hand back the display it created, or NULL
 * if the panel or the port refused. The port owns the framebuffer and vsync handshake
 * from there on; LVGL calls made from other tasks must take lvgl_port_lock(). */
lv_display_t *panel_rgb_start(void);
