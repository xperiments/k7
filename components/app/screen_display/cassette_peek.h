#pragma once
/*
 * cassette_peek - what the up swipe at Initial shows: the progress bar and the two
 * floating tags over the cassette.
 *
 * The overlay has two parts that move for different reasons:
 *
 *  - The bar at the bottom is the timeline. It carries the Track's elapsed time, a
 *    fill that grows with its position, and its total. The old bar carried the title;
 *    the title is now on the tags.
 *  - Two rounded tags are centred on the cassette, above and below the left hub: the
 *    song above it and the group below it, their centres the same distance from the
 *    hub's centre. Both hang from that one hub by a tether line, the group on its top
 *    rim and the song on its bottom rim. Both tethers leave on the hub's column (x=251),
 *    so they run vertically; at 504 px that is 20.4 % of the centred pill's width rather
 *    than a round quarter, which is the trade that keeps the line on the hub. They bob a
 *    few pixels, out of phase, on one animation; their tethers stay fixed and the tag's
 *    own fill hides the end.
 *
 * The tag texts are the Track's ID3/FLAC tags, read by artwork_loader on its own task
 * and handed in here; until they arrive (or when a Track has none) the group falls
 * back to the first directory of the Track's path and the song to the cleaned
 * filename. The bar's clock comes from the cassette frame.
 *
 * The peek is the ladder's bottom rung, not a screen of its own: this module is born and
 * hidden by cassette_levels. The Controls model owns the room, so the peek asks nobody
 * for it: the view applies the ROOM bit itself. The view owns the pose, so the hub
 * anchors are handed in on every pose change and only the tether's lower endpoint moves
 * with them.
 *
 * Private to screen_display, and built only on the touch board: the gestures that
 * reach Initial's peek are the panel's.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the hidden bar and tags on `screen`, above whatever exists at call time. */
void cassette_peek_create(lv_obj_t *screen);

/* Apply a cassette frame: the clock, the fill and the filename fallback. Only what the
 * frame reports changed is rewritten. A NULL frame is a no-op. */
void cassette_peek_apply(const cassette_frame_t *frame);

/* A new Track: its path supplies the group fallback, and the previous Track's tags are
 * dropped at once so they cannot be shown against the new song while the new ones are
 * still being read. */
void cassette_peek_set_path(const char *track_path);

/* The Track's tags, UTF-8, or empty strings when it has none. */
void cassette_peek_set_tags(const char *group, const char *song);

/* The two hub anchors in screen pixels: where each tether meets its hub. The view
 * calls this on creation and on every pose step, so the tethers follow the cassette
 * while its scale animates. */
void cassette_peek_set_hubs(int x0, int y0, int x1, int y1);

/* Show the overlay, sliding the bar up, or put it away with the same slide. */
void cassette_peek_show(void);
void cassette_peek_hide(void);

/* Whether the overlay is meant to be up, true while it is still sliding. */
bool cassette_peek_is_shown(void);

#ifdef __cplusplus
}
#endif
