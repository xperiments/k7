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
    /* The card's `k7.card` id, when it has one. Writes a NUL-terminated copy; false means
     * no mark. The callback owns the read: the status route must not touch the card. */
    bool (*card_mark)(char *out, size_t cap);
    /* Whether a library rescan is running. The App's card shows its progress from this; the
     * panel's Menu already says "Rescanning..." from the same function. */
    bool (*rescan_running)(void);
    /* Which deck this is, for a home automation that must tell two units apart: the station
     * MAC as twelve lowercase hex characters, the running firmware's version, and the
     * board's short name. Copied at start, so the strings need not outlive the call; an
     * empty one leaves its field out of the document. */
    const char *id;
    const char *fw;
    const char *board;
} front_door_cfg_t;

/* Register `/api/status`. Call before http_server_start(). */
esp_err_t front_door_start(const front_door_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
