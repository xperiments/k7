/* The panel, when the board has one. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/* Bring the panel up through esp_lvgl_port and hand back the display it created, or NULL
 * if the panel or the port refused. The port owns the framebuffer and vsync handshake
 * from there on; LVGL calls made from other tasks must take lvgl_port_lock().
 *
 * **The backlight is left off.** The peripheral starts scanning two PSRAM framebuffers the
 * moment it is initialised, and until something paints them they hold whatever PSRAM
 * powered up with - white - so lighting the panel here is the white flash at boot. The
 * caller paints its first frame and then calls backlight_start(). */
lv_display_t *panel_rgb_start(void);

/* Fill both framebuffers with one RGB565 colour - panel_rgb_clear() fills them black.
 * Call under lvgl_port_lock() and before the backlight is lit; the first frame LVGL
 * renders covers only the area it invalidates, so a buffer nobody filled can still show
 * through. False when the panel is not up or the buffers cannot be read. */
esp_err_t panel_rgb_fill(uint16_t color);
esp_err_t panel_rgb_clear(void);

/* The frame the panel is scanning, as the RGB565 buffer LVGL draws into - the panel board
 * runs the port in direct mode, so this is not a copy. False when the panel is not up.
 *
 * The port owns two of these buffers and LVGL alternates between them, so a caller that
 * wants a complete picture must force full refreshes first (screen_frame.c does); reading
 * this without one can catch the buffer that is not in front. Call under
 * lvgl_port_lock(), and copy before unlocking if the caller means to touch the pixels
 * while the screen keeps drawing. */
/* Stop the scan and start it again, from the frame in the framebuffers, cleanly.
 *
 * The scan-out is only as aligned as the moment it started: the driver primes the LCD FIFO
 * from PSRAM with a one-microsecond head start, and any flash access after that takes the
 * PSRAM window away for a few microseconds - which is a few dozen pixels of scan-out, after
 * which the DMA and the engine are permanently out of step and the whole picture sits that far
 * to one side. Boot is full of such accesses (Wi-Fi reads NVS), so the picture is painted
 * correctly and then slips a moment later.
 *
 * This re-runs the driver's own init, which stops the peripheral, resets the FIFO and the
 * channel and starts again - so the frame is established from a quiet bus instead of a busy
 * one. Call it with both framebuffers holding the picture that should be on screen. */
esp_err_t panel_rgb_reinit(void);

bool panel_rgb_frame(const uint16_t **px, int *w, int *h);
