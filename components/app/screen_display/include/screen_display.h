#pragma once
/*
 * screen_display - the screen: the panel (or the headless framebuffer), LVGL's memory
 * and the cassette view.
 *
 * The screen comes up in three calls, because the panel must not scan while the boot
 * touches flash, and the boot mark must be on the glass for its dwell before the cassette.
 * `screen_display_boot()` brings the hardware and LVGL up with the clock stopped and the
 * light off; `screen_display_mark()` - after the services have finished the boot's flash
 * work - starts the first clock, paints the K7 mark on the black page, lights the backlight
 * and holds the mark; `screen_display_start()` replaces it with the cassette and starts the
 * task that keeps that current. The status comes from the live stream inside the module;
 * callers do not push frames. The panel and the headless path are two adapters behind this
 * same interface, selected at build time by CONFIG_APP_BOARD_SUNTON_8048S050C.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the screen's hardware and LVGL up, with nothing on the glass and the backlight off.
 * Returns ESP_OK, or the reason the screen stayed dark; the device still serves.
 *
 * On the panel board this allocates the two PSRAM framebuffers, takes the LVGL port's 16 KB
 * internal stack and pauses the display's refresh timer, but deliberately does NOT call
 * `esp_lcd_panel_init()`: that call starts the pixel clock and the scan, and a flash access
 * during a scan displaces the picture permanently (ADR 0055). The clock is started by
 * `screen_display_mark()`, after the services. Call this **before** the services, both for
 * the internal-RAM order (ADR 0004) and so the flash work afterwards happens on a dark
 * panel. */
esp_err_t screen_display_boot(void);

/* Start the first clock (panel board), paint the boot mark, light the backlight and hold for
 * CONFIG_APP_UI_MARK_HOLD_MS before returning. Call once, **after** the services: by then
 * app_services_start() has mapped and validated the Snapshot, and Wi-Fi has read its NVS
 * out of flash, so the scan starts on a quiet bus and nothing displaces the
 * mark. Returns ESP_OK, or the reason the mark did not go up; the device still serves and
 * the screen stays dark. */
esp_err_t screen_display_mark(void);

/* Replace the boot mark with the cassette and start updating it. `music_dir` is the card
 * directory the broadcast's Track paths are relative to: the Artwork level reads a Track's
 * own picture from it. Pass NULL on a build with no card reader; the screen still runs and
 * the Artwork level stays empty. Call once, after `screen_display_mark()` has returned.
 * Returns ESP_OK, or the reason the cassette could not be built; the mark stays up. */
esp_err_t screen_display_start(const char *music_dir);

/* Flash is about to be read: take the picture off the glass until a moment after the last
 * call.
 *
 * A mapped-flash read (the Snapshot) takes the PSRAM bus from the panel's scan-out for a
 * couple of microseconds - enough for the LCD engine to consume pixels that are not there,
 * after which the DMA and the engine are permanently out of step and the whole picture sits
 * a band to one side (ADR 0055). So the screen goes black for the length of the read and is
 * re-established afterwards: the slip happens behind a black frame, where it cannot be seen,
 * and panel_rgb_reinit() puts the scan back before the picture returns.
 *
 * Cheap to call per read: the first call of a burst does the blacking and waits a frame for
 * it to land, the rest only extend the hold. The screen comes back by itself a second after
 * the last call. No-op on a build with no panel, and before the mark has started the first
 * clock. */
void screen_display_page_service(void);

#if CONFIG_APP_UI_SCREEN_HTTP
/* Register the screen's HTTP diagnostics with the server: GET /api/screen.bmp, the
 * rendered 800x480 frame as a 24-bit BMP, and POST /api/scene?n=0..9, which re-aims the
 * scene driver the console dump uses (CONFIG_APP_UI_DUMP_SCENE's scenes).
 *
 * Call BEFORE http_server_start(): the routes are the server's to serve. The screen
 * itself comes up later, so the handlers answer 503 until it has - registering early
 * costs nothing and a route registered after the listener would never be reachable.
 * Diagnostic builds only; normal builds do not compile it. */
esp_err_t screen_display_http_start(void);
#endif

#ifdef __cplusplus
}
#endif
