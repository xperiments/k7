#pragma once
/*
 * fx_ui - the tape effect's control surface: one page, five values, saved.
 *
 * Its own component because the broadcast must not own markup (host/tests/
 * layering.sh, rule S1), and because this is the piece a different surface - the
 * touchscreen, a JSON API - would replace without touching the audio path.
 */
#include <stdbool.h>

#include "esp_err.h"
#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register GET /fx with the HTTP server. Call after mini_icecast_start() and
 * before http_server_start(). Does nothing when the station was built without
 * the effect, so the caller does not have to know. */
esp_err_t fx_ui_start(void);

/* Apply a tuning to the running broadcast and save it to flash. This is the call
 * a screen or an API uses; the page does the same thing through the query string.
 * ESP_ERR_INVALID_STATE when the station has no effect. */
esp_err_t fx_ui_apply(const mini_icecast_fx_t *tune);

/* Turn the effect on or off and remember it. */
esp_err_t fx_ui_set_enabled(bool on);

#ifdef __cplusplus
}
#endif
