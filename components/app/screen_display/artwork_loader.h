#pragma once
/*
 * artwork_loader - the card side of the Artwork screen.
 *
 * Locates a Track's own picture (components/portable/artwork), decodes it with TJpgDec
 * as the decoder reads the JPEG from the card on demand, and fits it to the Case's
 * window, 603x385 at (101,23). The Case itself (assets/labels/cover.jpg) is embedded as
 * a JPEG and decoded once at start. Work happens on its own task, off the audio core and
 * behind a one-slot request, so the view never touches the card or a decoder.
 *
 * Only JPEG is decoded. A PNG picture is located but refused with a log line: it is
 * rare in an MP3, and the case window does not need LodePNG's full-size buffer.
 *
 * Private to screen_display. The panel board is the only build that starts it.
 */
#include "audio_format.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate the cover buffer and start the decoder task. `music_dir` is the card
 * directory the Track paths are relative to. A failure leaves the screen working and
 * the Artwork screen empty; it is logged by the caller. */
esp_err_t artwork_loader_start(const char *music_dir);

/* Ask for a Track's picture. The request replaces any pending one; a later request
 * for the same path and format is still allowed to re-render after a failure, but the
 * view dedupes while a Track does not change. An empty path, NULL or MI_FMT_NONE
 * clears the cover. */
void artwork_loader_request(const char *track_path, mi_fmt_t format);

#ifdef __cplusplus
}
#endif
