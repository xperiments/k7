#pragma once
/*
 * image_http - GET /api/k7db, the card's Snapshot image.
 *
 * The image is the card's own `k7.db`: the rows the App wrote when it measured the card,
 * carrying each Track's title, artist, album and genre beside its measurements
 * (snapshot-format.md). Serving it verbatim is what lets a browser that has never met the
 * card - a cleared store, another machine - remember its albums from the card in the player
 * instead of asking for the card to be put in a computer.
 *
 * It is the App's file, served as it stands: the device never parses it. Reading it touches
 * the card, not flash.
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the route. The image's path comes from card_path() when the request arrives. */
esp_err_t image_http_start(void);

#ifdef __cplusplus
}
#endif
