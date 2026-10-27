#pragma once
/*
 * The cassette screen: its composition, its pose and the model that drives it.
 *
 * Private to screen_display; the tape arithmetic is components/portable/cassette.
 * The screen's parts are private modules of the same component:
 *
 *   cassette_tapes    the packs, strands, rollers, run, hubs and the panel's frames
 *   cassette_meter    the VU meter, parked: built, placed and fed, never shown
 *   cassette_cover    Real's Track picture, masked to the frames
 *   cassette_levels   the peek's host (the Label cover and Artwork screens are gone)
 *   cassette_peek     the up swipe's timeline bar and its two floating tags
 *   cassette_bars     the two edge strips: their 260 ms entrance, page and raise
 *   cassette_buttons  the transport drawer at the left edge
 *   cassette_eq       the DJ block at the right edge: GAIN, HI/MED/LOW and R (M is the bar's chrome)
 *   controls          the portable Controls model (the four-rung ladder and the Menu)
 *   screen_input      what a touch means, as intents
 *   cassette_layout   the design grid (portable), cassette_style the palette,
 *                     cassette_widgets the object helpers
 *
 * The panel board's artwork loader reads a Track's picture and text tags and hands them
 * in under the LVGL lock, so no part of the screen touches the card or a decoder. The
 * cover is Real's; the peek's tags are its labels.
 */
#include "lvgl.h"

#include "mini_icecast.h"

void cassette_view_create(void);
void cassette_view_update(const mini_icecast_status_t *st);

/* Where the cassette sits: 256 (CASSETTE_SCALE_ENTRY) is the full entry view, 200
 * (CASSETTE_SCALE_PEEK) is 0.78x, and 134 (CASSETTE_SCALE_CONTROLS) is 0.52x, the control
 * view with the two bars out. The design box is centred in the panel and then moved by
 * (offset_x, offset_y) screen pixels - right and down positive. The whole layout is
 * re-placed at the new pose: at once when ms is 0, or eased over ms milli-seconds, with the
 * size and the offset moving together. The scale is clamped to 32..1024; the offset is not.
 * The named poses live in cassette_layout.h; the Controls model asks for the room.
 *
 * Called under the LVGL lock, like everything else on the screen. */
void cassette_view_set_scale(int scale, int offset_x, int offset_y, uint32_t ms);

/* Cut the cassette's own picture out (dim true) or back in (false), in one frame. The
 * peek, the transport drawer and the DJ block are not in the cassette's container and
 * stay. The EQ's edit mode uses it: the cassette goes and the virtual knob appears. */
void cassette_view_dim(bool dim);

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The Track's picture for the white page's cover, or NULL when it has none (or while a
 * new one is being decoded). The descriptor is the loader's one cover buffer and stays
 * alive; the cover module copies nothing. */
void cassette_view_set_cover(const lv_image_dsc_t *dsc);

/* The Track's text tags for the peek's two labels, UTF-8. Empty strings mean the Track
 * carries none (or they are still being read), and the peek falls back to the path and
 * filename it already knows. */
void cassette_view_set_tags(const char *group, const char *song);
#endif
