#pragma once
/*
 * playlist_http - the Playlists the Snapshot holds, as the App reads them.
 *
 * Three routes: the list of lists, one list's rows, and the one command that selects one.
 * None of them writes flash: a new list arrives as a new image and a choice is a command
 * (ADR 0051).
 *
 * They lived in `app_services`, which is the composition - the thing that decides the order
 * the device boots in - so a change to how a Playlist is answered edited the same file as the
 * boot sequence. They are beside the module that owns the selection now.
 */
#include "esp_err.h"
#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register the three routes. `snapshot` is asked per request: an update replaces the image,
 * so a route must never hold the one that was current when it was registered. It may return
 * NULL, and the answer is then "no lists", which is true. */
esp_err_t playlist_http_start(const snapshot_t *(*snapshot)(void));

#ifdef __cplusplus
}
#endif
