#pragma once
/*
 * The cassette screen's LVGL view: objects, layout, fonts and the Level machine.
 * Private to screen_display; the tape arithmetic is components/portable/cassette.
 *
 * The three Levels (Bare, Label cover, Artwork) live here. A one-finger down/up swipe
 * moves between them; the two-finger swipe is reserved for the menu. The panel board's
 * artwork loader locates and decodes a Track's picture and hands it in under the LVGL
 * lock, so the view never touches the card or a decoder.
 */
#include "lvgl.h"

#include "mini_icecast.h"

void cassette_view_create(void);
void cassette_view_update(const mini_icecast_status_t *st);

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The Track picture, fitted to the Case's window, or NULL while it is being decoded or
 * when the Track carries none. The pixels belong to the loader and stay valid until the
 * next set_artwork(). */
void cassette_view_set_artwork(const lv_image_dsc_t *dsc);

/* The Case backdrop, decoded once at start; NULL leaves the Artwork screen's own
 * background in place. The pixels belong to the loader and live for the process. */
void cassette_view_set_case(const lv_image_dsc_t *dsc);
#endif
