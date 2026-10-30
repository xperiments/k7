#pragma once
/*
 * cassette_menu - the Menu's view: the five-row page, the header's location and the footer's
 * now playing. Panel board only.
 *
 * The view owns the drawing and the lifecycle of the mode: `open()` builds the rows for
 * the page that is up, hides the cassette container, aims the Pad (`cassette_pad`) at this
 * menu's ratchet and makes it the right bar's panel, and fills the window with the page's
 * colour. `close()` puts all of it back. The model is portable and lives in
 * components/portable/menu; this module only executes its actions.
 *
 * **It never closes itself.** The two ways its own input asks for a close - the Close row's
 * action and the Pad's Back at the root - call the close hook its owner installs, and the
 * owner (cassette_view) sends the Controls model's CONTROLS_INTENT_MENU_CLOSE and applies the
 * MENU bit that comes back. That bit is what calls close(). One owner for the open state, one
 * path for the latch on the mode bar and the group on the right bar.
 *
 * `cassette_view` calls open() on the MENU bit, close() on it, and update() with the
 * broadcast's status while the Menu is up. All calls are under the LVGL lock.
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

/* The owner's close request: the Menu's own input calls this instead of closing itself. The
 * owner sends CONTROLS_INTENT_MENU_CLOSE to the Controls model and applies the MENU bit, which
 * is what calls cassette_menu_close(). A NULL hook makes a Close a no-op, which is a build
 * error the module logs: the Menu would be a trap. Called under the LVGL lock, from a Pad or
 * page callback. */
typedef void (*cassette_menu_close_t)(void *ctx);
void cassette_menu_set_close_cb(cassette_menu_close_t cb, void *ctx);

/* Open on the remembered list, for the page that is up (white: the grey page). No-op when
 * already open. Called by the view on the Controls model's MENU bit. */
void cassette_menu_open(bool white);

/* Close and restore the controls: the cassette, the DJ block, the drawer, the window, the
 * bands' texts. No-op when closed. The view calls this on the MENU bit; the Menu's own input
 * asks for it through the close hook instead of calling it. */
void cassette_menu_close(void);

bool cassette_menu_is_open(void);

/* Feed the broadcast's status (footer, the playing row's marker). Called from the view's
 * 25 Hz update while the Menu is open; cheap when nothing changed. */
void cassette_menu_update(const mini_icecast_status_t *status);

#ifdef __cplusplus
}
#endif
