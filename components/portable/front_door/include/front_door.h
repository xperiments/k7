#pragma once
/*
 * front_door - the card-state route, and nothing else.
 *
 * The product is Chrome-only and the App is hosted, so the device carries no page: it
 * serves the Library at `/library`, the broadcast at `/stream`, the API and `/mcp`, and
 * every other path - `/` and `/bridge` included - is a 404. The one route here is
 * `/api/status`, which answers the question the App's two views hang from: is a card in
 * the player right now.
 *
 * Nothing here touches flash.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* The Card state, as the App asks for it. NULL reads as "no card". */
    bool (*card_present)(void);
} front_door_cfg_t;

/* Register `/api/status`. Call before http_server_start(). */
esp_err_t front_door_start(const front_door_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
