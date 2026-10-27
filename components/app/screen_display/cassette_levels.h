#pragma once
/*
 * cassette_levels - the peek and Real's cover, hosted on the Cassette screen.
 *
 * The peek is the bottom rung of the four (ADR 0025): an up swipe at Initial raises it and
 * a down swipe puts it away, cassette_peek's timeline bar and two tags. Real's picture is
 * requested here as well - the down from Art shows it, the up takes it off with the Page.
 * There is no screen machine and no sheet; the Controls model owns the room, so the peek
 * asks nobody for it.
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

/* Build the hidden peek on `screen`. On a board without the touch adapter there is no
 * peek, and this builds nothing. The room is the Controls model's now: the view animates the
 * cassette when its ROOM bit changes, so the peek asks nobody for it. */
void cassette_levels_create(lv_obj_t *screen);

/* Apply a cassette frame to the peek: the clock, the fill and the filename fallback. */
void cassette_levels_apply(const cassette_frame_t *frame);

/* Keep the status, and request a new Track's tags when the path/format changes. The
 * request is deduped here, so a 25 Hz update is a string compare. */
void cassette_levels_update(const mini_icecast_status_t *status);

/* Raise or put away the peek. The Controls model owns the ladder and the room; the view
 * applies its PEEK bit with this. A call for the state already held does nothing. */
void cassette_levels_set_peek(bool shown);

/* The Track's text tags, UTF-8. Empty strings mean the Track carries none. */
void cassette_levels_set_tags(const char *group, const char *song);

/* The two hub anchors in screen pixels, for the peek's tethers. */
void cassette_levels_set_hubs(int x0, int y0, int x1, int y1);

#ifdef __cplusplus
}
#endif
