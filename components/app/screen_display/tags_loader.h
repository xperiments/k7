#pragma once
/*
 * tags_loader - the Track's group and song tags for the peek, on their own task.
 *
 * This is the tag half of the old artwork loader. The cover half - the Artwork screen's
 * JPEG, the Case backdrop and its buffers - is parked in artwork_loader.c and is not
 * built; the Label cover and the Artwork screen are gone. The peek keeps its two text
 * labels, and falls back to the path/filename when a Track has no tags.
 *
 * Reads the container through components/portable/artwork's tag parser (ID3 TPE1/TIT2,
 * FLAC ARTIST/TITLE) on a worker task, behind a one-slot request, and hands the result to
 * the view under the LVGL lock. The card is shared with the broadcast, so the read backs
 * off while the stream is behind.
 *
 * Private to screen_display. The panel board is the only build that starts it.
 */
#include "audio_format.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the request channel and start the task. `music_dir` is the card directory the
 * Track paths are relative to. A failure leaves the peek on its filename fallback and is
 * logged by the caller. */
esp_err_t tags_loader_start(const char *music_dir);

/* Ask for a Track's tags. A newer request replaces the pending one; the view receives
 * only the newest, and an empty result is a real answer (the Track has no tags). */
void tags_loader_request(const char *track_path, mi_fmt_t format);

#ifdef __cplusplus
}
#endif
