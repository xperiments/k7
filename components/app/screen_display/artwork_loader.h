#pragma once
/*
 * artwork_loader - the card side of the white page's cover and the peek's tags.
 *
 * Locates a Track's own picture (components/portable/artwork), decodes it with TJpgDec
 * as the decoder reads the JPEG from the card on demand, and fits it, cover-style, to
 * one 603x385 RGB565 buffer. The white controls page scales that buffer to the
 * cassette's outer frame; it is the old Artwork screen's window size, kept because the
 * decode and the fit were already tested at it. It also reads the Track's text tags -
 * TPE1/TIT2 in an MP3, ARTIST/TITLE in a FLAC Vorbis comment - for the peek's two
 * labels. Work happens on its own task, off the audio core and behind a one-slot
 * request, so the view never touches the card or a decoder.
 *
 * Only JPEG is decoded. A PNG picture is located but refused with a log line: it is
 * rare in an MP3, and the cover path does not need LodePNG's full-size buffer.
 *
 * Private to screen_display. The panel board is the only build that starts it.
 */
#include "audio_format.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate the cover buffer and start the decoder task. `music_dir` is the card
 * directory the Track paths are relative to. A failure leaves the screen working: the
 * white page shows no cover and the peek falls back to its filename; it is logged by the
 * caller. */
esp_err_t artwork_loader_start(const char *music_dir);

/* Ask for a Track's picture. The request replaces any pending one. An empty path, NULL
 * or MI_FMT_NONE clears the cover. The view hides the old picture when the Track
 * changes, so the decode never writes the buffer while it is drawn. */
void artwork_loader_request(const char *track_path, mi_fmt_t format);

/* Ask for a Track's text tags. A request for the same Track as a pending picture is
 * coalesced with it (one open, one wake), and a new Track replaces both. The result is
 * handed to the view under the LVGL lock; empty tags mean the Track carries none. */
void artwork_loader_request_tags(const char *track_path, mi_fmt_t format);

#ifdef __cplusplus
}
#endif
