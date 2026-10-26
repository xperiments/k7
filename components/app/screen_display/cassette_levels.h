#pragma once
/*
 * cassette_levels - the peek and the white page's cover, hosted on the Cassette screen.
 *
 * The Label cover and Artwork screens, and the down/up Level gestures that reached them,
 * are removed: Bare is the only screen the cassette has. The up swipe at Bare has one
 * meaning left - raise or lower the peek, cassette_peek's timeline bar and two tags - and
 * the down swipe shows the controls, then turns the white page's cover on (the up swipe
 * turns it off before it closes the controls). There
 * is no Level machine and no sheet; the peek asks the cassette for room through the
 * `room` callback, as it always did.
 *
 * The peek's group and song tags and the white page's picture both come from a Track's
 * container through artwork_loader (the old Artwork loader, minus its Case). A Track
 * without a picture shows no cover; a Track without tags falls back to the path and
 * filename, as before.
 *
 * Private to screen_display.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette.h"
#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The room the peek needs from the cassette. `shown` is whether the cassette should
 * give the overlay its room (true) or go back to its resting pose (false). `instant`
 * asks for that in one frame (ms = 0) rather than on the overlay's own slide. The view
 * owns the pose - the scale, the offset and the duration - because the cassette is its. */
typedef void (*cassette_levels_room_cb_t)(bool shown, bool instant, void *ctx);

/* Build the hidden peek on `screen`. On a board without the touch adapter there is no
 * peek, and this builds nothing. */
void cassette_levels_create(lv_obj_t *screen, cassette_levels_room_cb_t room, void *ctx);

/* Apply a cassette frame to the peek: the clock, the fill and the filename fallback. */
void cassette_levels_apply(const cassette_frame_t *frame);

/* Keep the status, and request a new Track's tags when the path/format changes. The
 * request is deduped here, so a 25 Hz update is a string compare. */
void cassette_levels_update(const mini_icecast_status_t *status);

/* The peek toggle. The view calls it to raise the peek from the entry 100% pose and to
 * put it away on the down swipe; an up swipe while it is up is ignored. */
void cassette_levels_toggle_peek(void);

/* The Track's text tags, UTF-8. Empty strings mean the Track carries none. */
void cassette_levels_set_tags(const char *group, const char *song);

/* The two hub anchors in screen pixels, for the peek's tethers. */
void cassette_levels_set_hubs(int x0, int y0, int x1, int y1);

#ifdef __cplusplus
}
#endif
