/* The panel, when the board has one. */
#pragma once

#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/* Bring the RGB panel up from the board profile and hand back its handle, or NULL if
 * it could not be created. Two screen-sized framebuffers are allocated in PSRAM by the
 * driver, which is where the interface is drawn. */
esp_lcd_panel_handle_t panel_rgb_start(void);

/* LVGL's flush hook for a panel that scans its own framebuffer out: it points the panel
 * at the buffer LVGL has just finished and waits for the scan-out to reach the end of
 * the frame before releasing it back to LVGL. */
void panel_rgb_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px);
