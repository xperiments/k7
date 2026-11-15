#pragma once
/*
 * playlist_http - the Playlists on the card, as the App reads and writes them.
 *
 * Five routes: the index with each list's fingerprint, one list's rows, a PUT that stores one
 * file, a DELETE that removes one, and the command that selects one. The first four read and
 * write `k7.playlists/` at the card root -- the only directory the device may create and the
 * only content it writes besides the selection record (ADR 0094). Nothing here touches flash:
 * a Playlist is a file now, and an image carries measurements (ADR 0095).
 *
 * They lived in `app_services`, which is the composition - the thing that decides the order
 * the device boots in - so a change to how a Playlist is answered edited the same file as the
 * boot sequence. They are beside the module that owns the selection now.
 */
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Body limit of a PUT: the format's own size for a list, with room for the header. */
#define PLAYLIST_HTTP_BODY_MAX (64u * 1024u)

/* Register the routes. `card_root` is asked per request -- a card is inserted and removed
 * while the device runs -- and fills in the card's root, or "" when there is none. */
esp_err_t playlist_http_start(void (*card_root)(char *out, size_t cap));

#ifdef __cplusplus
}
#endif
