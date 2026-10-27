#pragma once
/*
 * cassette_menu - the Menu's view: the five-row page, the Pad, the header's location and
 * the footer's now playing. Panel board only.
 *
 * The view owns the drawing and the lifecycle of the mode: `open()` builds the rows for
 * the page that is up, hides the cassette container, makes the Pad the right bar's panel,
 * turns the drawer into Back/Enter/Close and fills the window with the page's colour.
 * `close()` puts all of it back, the DJ block included. The model is portable and lives in
 * components/portable/menu; this module only executes its actions.
 *
 * `cassette_view` calls open() on the M cell's tap, close() on the up swipe, and update()
 * with the broadcast's status while the Menu is up. All calls are under the LVGL lock.
 */
#include <stdbool.h>

#include "lvgl.h"
#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the Menu's objects, hidden, under `screen`. `cassette` is the container the view
 * hides while the Menu is up; it must outlive this module. */
void cassette_menu_create(lv_obj_t *screen, lv_obj_t *cassette);

/* Open on the remembered list, for the page that is up (white: the grey page). No-op when
 * already open. */
void cassette_menu_open(bool white);

/* Close and restore the controls: the cassette, the DJ block, the drawer, the window, the
 * bands' texts. No-op when closed. */
void cassette_menu_close(void);

bool cassette_menu_is_open(void);

/* Feed the broadcast's status (footer, the playing row's marker). Called from the view's
 * 25 Hz update while the Menu is open; cheap when nothing changed. */
void cassette_menu_update(const mini_icecast_status_t *status);

/* The same group/song pair the Peek's tags carry, for the footer. Empty strings fall back
 * to the path's first directory and the cleaned title. */
void cassette_menu_set_tags(const char *group, const char *song);

#ifdef __cplusplus
}
#endif
