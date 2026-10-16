#pragma once
/*
 * mini_files - the card's library, over HTTP.
 *
 * Two routes: the index at "/" (a flat listing of the music directory, with the
 * live stream's current title and a link to the stream) and the download at
 * "/music/<name>" (the track's original, untranscoded bytes).
 *
 * The index doubles as the device's diagnostic surface: when no card is mounted
 * it answers 200 with a page saying so and quoting the mount error, because a
 * card that will not mount is the most likely fault this hardware has and the
 * network should be a better place to find that out than a serial console.
 *
 * Downloads are deliberately single-file-at-a-time: the broadcast reads the same
 * card, so a second concurrent download is declined rather than queued.
 */
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *station_name; /* shown on the index; default "ESP32 Radio" */
    const char *music_dir;    /* the mounted music directory, or NULL/"" when no card is mounted */
    const char *mount_error;  /* text of the mount failure, or NULL when the card is present */
} mini_files_cfg_t;

/* Register the "/" and "/music/" routes. Call http_server_start() afterwards. */
esp_err_t mini_files_start(const mini_files_cfg_t *cfg);

/* Update the card state, e.g. after a background mount retry succeeds. Passing a
 * NULL or empty music_dir means "no card"; `mount_error` is then shown on the
 * index page. */
void mini_files_set_card(const char *music_dir, const char *mount_error);

#ifdef __cplusplus
}
#endif
