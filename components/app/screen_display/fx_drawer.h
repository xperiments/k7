#pragma once
/*
 * The effects drawer: a panel that slides in from the right edge and lets the
 * effect be switched, and its flavour chosen, while the stream runs.
 *
 * Private to screen_display. The controls do not know how the effect works: they
 * call fx_ui_apply()/fx_ui_set_enabled(), which are the same two calls the web
 * page uses, so the screen and the browser cannot disagree about the state.
 */
#include <stdbool.h>

#include "mini_icecast.h"

/* Build the hidden drawer, its backdrop, and its controls. Call once, after the
 * cassette view exists (it is a sibling of it on the same screen). */
void fx_drawer_create(void);

/* Reflect the broadcast's state on the controls: the on/off label and which tape
 * type is highlighted. Call from the screen task, under the LVGL lock. */
void fx_drawer_update(const mini_icecast_status_t *st);

/* The two-finger swipe handler. Closed: a swipe to the left (dx < 0) opens it and
 * returns true; a swipe to the right is not ours and returns false, so the caller
 * can give it to the cover. Open: any swipe closes it. A tap on the dimmed
 * backdrop, or the close cross, closes it too. Safe from the LVGL task. */
bool fx_drawer_swipe(int dx, int dy);

bool fx_drawer_is_open(void);
