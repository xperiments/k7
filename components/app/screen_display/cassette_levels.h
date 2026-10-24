#pragma once
/*
 * cassette_levels - the Cassette screen's Levels, their sheets, and the Label.
 *
 * The screen has three Levels, ordered by how much of it the sheet covers: Bare, the
 * Label cover and Artwork. A one-finger down swipe moves one Level towards Artwork, an
 * up swipe one towards Bare, and the ends do not wrap. The Level changes in one frame,
 * before LVGL draws, and exactly one sheet is shown at a time. The Label is not a
 * Level: it is the cream bar that carries the Now playing title, an overlay on Bare
 * alone, peeked by the up swipe that has no Level to move to.
 *
 * This module owns all of that - the Level, the Label, the sheets' objects and the
 * screen backdrop that follows the Label cover - and nothing of the cassette. The body,
 * the tapes and the meter are the view's. The two meet at one seam: the Label needs room
 * from the cassette (the view shrinks and lifts it so the bar clears the tape), and it
 * asks for it through the `room` callback rather than touching the pose itself. The view
 * turns that request into the exact scale, offset and duration, so this module has no
 * cassette_layout.h and no opinion about where the cassette sits.
 *
 * Private to screen_display. The titles come from components/portable/cassette, the
 * Track's picture from artwork_loader; nothing here reads the card or the model.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette.h"
#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The room the Label needs from the cassette. `shown` is whether the cassette should
 * give the bar its room (true) or go back to its resting pose (false). `instant` asks
 * for that in one frame (ms = 0) rather than on the bar's own slide: a Level change
 * hides the Label and restores the cassette before the target sheet is placed, while a
 * swipe runs the pose over the same 260 ms as the bar so the two arrive as one picture.
 *
 * Showing is always animated; only hiding by a Level change is instant. The view owns
 * the pose - the scale, the offset and the duration - because the cassette is its; the
 * slide's duration here and the view's pose duration must stay equal, which is the one
 * value the seam shares.
 */
typedef void (*cassette_levels_room_cb_t)(bool shown, bool instant, void *ctx);

/* Build the Label, the Label cover, the Artwork sheet, the Case window picture and the
 * three titles. `screen` is the screen the sheets sit on; the sheets are its children,
 * above whatever was created before this call, so it must run after the cassette's own
 * container. `room` is how the Label asks the cassette for room; `ctx` is passed back to
 * it. On a board without the touch adapter there are no sheets beyond Bare, and this
 * builds nothing. */
void cassette_levels_create(lv_obj_t *screen, cassette_levels_room_cb_t room, void *ctx);

/* Write the frame's title to the Label, the Label cover and the Artwork band, when the
 * frame says it changed. The text is the model's cleaned title; the fallback for an
 * empty one is this module's, the same string the screen has always shown. */
void cassette_levels_apply(const cassette_frame_t *frame);

/* Keep the status, and while the Artwork Level is up ask the loader for the Track's
 * picture. The request is deduped on path and format, so a 25 Hz update is a string
 * compare; a new Track hides the window's picture until its own lands. */
void cassette_levels_update(const mini_icecast_status_t *status);

/* The one-finger vertical swipes, as the view's gesture handler reads them. A down
 * swipe at Bare with the Label up dismisses the Label and moves no Level; otherwise it
 * moves one Level towards Artwork. An up swipe at Bare toggles the Label (the peek);
 * at any higher Level it moves one Level towards Bare. While the Label is up, either
 * swipe puts it away first. A Level change puts it away at once. */
void cassette_levels_swipe_down(void);
void cassette_levels_swipe_up(void);

/* The Track's picture, fitted to the Case's window, or NULL while it is decoding or
 * when the Track carries none; and the Case backdrop itself, decoded once at start.
 * The pixels belong to the loader and stay valid until the next call. */
void cassette_levels_set_artwork(const lv_image_dsc_t *dsc);
void cassette_levels_set_case(const lv_image_dsc_t *dsc);

#ifdef __cplusplus
}
#endif
