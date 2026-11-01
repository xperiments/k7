#pragma once
/*
 * front_door - the page at `/`, and the one fact it is built around.
 *
 * The device serves the analysis app out of one packed image: `web/dist-shell` on the host,
 * the `frontdoor` image the board copies into PSRAM at boot (shell_image.h). Everything here
 * is a static file with a cache-header rule, plus `/api/status`, which answers the question
 * the page's two views hang from - is a card in the player right now.
 *
 * Nothing here touches flash. The bytes are resident - PSRAM on the board, a buffer on the
 * host - because any flash access takes the bus the RGB panel's scan-out draws from, which is
 * what tore the picture on every page load (ADR 0055).
 *
 * The index is not here: it moved to `/library` when the app took `/`.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* The Shell's image, as `tools/pack_shell.py` wrote it - the mapped partition on the
     * board, a buffer on the host. NULL, or bytes that are not a valid image, mean this
     * firmware carries no page: `/` then answers 503 and says so, while `/library` and the
     * API keep working. */
    const void *shell_image;
    size_t      shell_image_size;
    /* The Card state, as the page asks for it. NULL reads as "no card". */
    bool (*card_present)(void);
} front_door_cfg_t;

/* Register `/` and `/api/status`. Call before http_server_start(). */
esp_err_t front_door_start(const front_door_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
