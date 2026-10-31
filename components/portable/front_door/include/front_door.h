#pragma once
/*
 * front_door - the page at `/`, and the one fact it is built around.
 *
 * The device serves the analysis app from a directory: the SPIFFS partition on the board,
 * `web/dist` on the host. Everything here is a static file with a cache-header rule, plus
 * `/api/status`, which answers the question the page's two views hang from - is a card in
 * the player right now.
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
    /* Where the page's files live. NULL means this firmware carries no page: `/` then
     * answers 503 and says so, while `/library` and the API keep working. */
    const char *root;
    /* The card state, as the page asks for it. May be NULL, which reads as "unknown". */
    bool (*card_present)(void *ctx);
    void *ctx;
} front_door_cfg_t;

/* Register `/` and `/api/status`. Call before http_server_start(). */
esp_err_t front_door_start(const front_door_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
