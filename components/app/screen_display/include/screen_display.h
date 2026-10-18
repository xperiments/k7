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

/* Bring the screen up and start updating it. Call once, after the services are up.
 * Returns ESP_OK, or the reason the screen stayed dark; the device still serves. */
esp_err_t screen_display_start(void);

#ifdef __cplusplus
}
#endif
