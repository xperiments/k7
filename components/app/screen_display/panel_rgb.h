/* The panel, when the board has one. */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

/* Bring the panel's peripheral, its framebuffers and the LVGL port up, and hand back the
 * display the port created, or NULL if either refused. The port owns the framebuffer and
 * vsync handshake from there on; LVGL calls made from other tasks must take
 * lvgl_port_lock().
 *
 * **No clock, no scan, no light.** Three things are deliberately left for later:
 *
 *  - `esp_lcd_panel_init()` is not called. It is the call that applies the timings, starts
 *    the pixel clock and starts the scan, and the panel must not be scanning while the boot
 *    touches flash: a flash access takes the PSRAM window from the scan-out and displaces
 *    the picture permanently (ADR 0055). `esp_lcd_new_rgb_panel()` and `lvgl_port_init()`
 *    stay here because the port's 16 KB internal stack must be taken before Wi-Fi asks for
 *    its contiguous DMA buffers (ADR 0004). The first clock is `panel_rgb_scan_start()`.
 *  - The display's refresh timer is paused, because an RGB flush in direct mode waits for
 *    the panel's vsync, and there is none until the clock starts.
 *  - The backlight is left off. The peripheral will scan two PSRAM framebuffers that read
 *    as white until something paints them, so lighting the panel here is the white flash at
 *    boot. The caller clears and paints them after the first clock, then calls
 *    backlight_start(). */
lv_display_t *panel_rgb_start(void);

/* Start the pixel clock and the scan - the driver's own init - now that the boot's flash work
 * is behind it. Blacks nothing itself: the caller fills both framebuffers first, so the first
 * frame the engine scans is the colour the caller chose. Resumes the display's refresh timer.
 * Takes and releases the port lock, so it may be called with it already held. Idempotent. */
esp_err_t panel_rgb_scan_start(void);

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
bool panel_rgb_frame(const uint16_t **px, int *w, int *h);

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
 * one. Call it with both framebuffers holding the picture that should be on screen. It also
 * resumes the display's refresh timer, in case the first clock never started. */
esp_err_t panel_rgb_reinit(void);
