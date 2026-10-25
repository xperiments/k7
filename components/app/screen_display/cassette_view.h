#pragma once
/*
 * The cassette screen: its composition, its pose and the model that drives it.
 *
 * Private to screen_display; the tape arithmetic is components/portable/cassette.
 * The screen's parts are private modules of the same component:
 *
 *   cassette_tapes    the packs, strands, rollers, run, hubs and the panel's frames
 *   cassette_meter    the VU meter
 *   cassette_levels   the Level machine, the Label cover and the Artwork sheet
 *   cassette_peek     the up swipe's timeline bar and its two floating tags
 *   cassette_buttons  the transport drawer at the left edge
 *   cassette_eq       the DJ block at the right edge: GAIN, HI/MED/LOW, < and >
 *   screen_input      what a touch means, as intents
 *   cassette_layout   the design grid (portable), cassette_style the palette,
 *                     cassette_widgets the object helpers
 *
 * The panel board's artwork loader locates and decodes a Track's picture and reads its
 * text tags, and hands both in under the LVGL lock, so no part of the screen touches the
 * card or a decoder.
 */
#include "lvgl.h"

#include "mini_icecast.h"

void cassette_view_create(void);
void cassette_view_update(const mini_icecast_status_t *st);

/* Where the cassette sits: 256 (CASSETTE_SCALE_NONE) is its design size, 200 is 0.78x
 * (the peek), and 134 is 0.52x (the rest pose, which leaves the left drawer and the right
 * DJ block room). The design box is centred in the panel and then moved by
 * (offset_x, offset_y) screen pixels - right and down positive. The whole layout is
 * re-placed at the new pose: at once when ms is 0, or eased over ms milli-seconds, with
 * the size and the offset moving together. The scale is clamped to 32..1024; the offset is
 * not.
 *
 * Called under the LVGL lock, like everything else on the screen. */
void cassette_view_set_scale(int scale, int offset_x, int offset_y, uint32_t ms);

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The Track picture, fitted to the Case's window, or NULL while it is being decoded or
 * when the Track carries none. The pixels belong to the loader and stay valid until the
 * next set_artwork(). */
void cassette_view_set_artwork(const lv_image_dsc_t *dsc);

/* The Case backdrop, decoded once at start; NULL leaves the Artwork screen's own
 * background in place. The pixels belong to the loader and live for the process. */
void cassette_view_set_case(const lv_image_dsc_t *dsc);

/* The Track's text tags for the peek's two labels, UTF-8. Empty strings mean the Track
 * carries none (or they are still being read), and the peek falls back to the path and
 * filename it already knows. */
void cassette_view_set_tags(const char *group, const char *song);
#endif
