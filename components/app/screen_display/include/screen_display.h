#pragma once
/*
 * screen_display - the screen: the panel (or the headless framebuffer), LVGL's memory
 * and the cassette view.
 *
 * One call brings the screen up and starts the task that keeps it current. The status
 * comes from the live stream inside the module; callers do not push frames. The panel
 * and the headless path are two adapters behind this same interface, selected at build
 * time by CONFIG_APP_BOARD_SUNTON_8048S050C.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring the screen up and start updating it. `music_dir` is the card directory the
 * broadcast's Track paths are relative to: the Artwork level reads a Track's own
 * picture from it. Pass NULL on a build with no card reader; the screen still runs and
 * the Artwork level stays empty. Call once, after the services are up. Returns ESP_OK,
 * or the reason the screen stayed dark; the device still serves. */
esp_err_t screen_display_start(const char *music_dir);

#if CONFIG_APP_UI_SCREEN_HTTP
/* Register the screen's HTTP diagnostics with the front door: GET /api/screen.bmp, the
 * rendered 800x480 frame as a 24-bit BMP, and POST /api/scene?n=0..7, which re-aims the
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
