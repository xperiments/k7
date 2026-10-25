#pragma once
/*
 * cassette_buttons - the transport drawer at the screen's left edge.
 *
 * The drawer is a 120 px panel that starts off-screen to the left and hidden. Either
 * horizontal swipe brings it in from the entry, and while it is out either puts it
 * away. The view turns those intents into cassette_buttons_set_shown(). While the drawer is out the
 * view scales the cassette to 134/256 and pulls it left to x=148, so the tape clears
 * the panel and the DJ block's column on the right.
 *
 * The drawer holds three buttons down the left edge. Each is line art: a 96x96
 * rectangle rounded by 8 px, a 64 px circle at its centre, and the character P, < or >
 * centred in that circle in Jura 36. The three start 76 px from the screen's top; the
 * middle button (<) is therefore on y=218. Each button carries two prebuilt state
 * objects - unpressed and pressed - and swaps which one is visible on press and release,
 * so the pressed look costs no style change at touch time. A click is a short scale pulse
 * plus the button's action: < calls mini_icecast_prev(), > calls mini_icecast_skip(); P
 * is still a placeholder. These two are the transport's next/previous now.
 *
 * Private to screen_display, created above the peek in the child order. Every object
 * is built under the LVGL lock. The drawer and each button bubble gestures so a swipe
 * that starts on them still reaches screen_input's handler on the screen, and bubble
 * events so a press clears the two-finger latch; a click on the drawer's margin is
 * absorbed by the drawer, not read as a screen gesture.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How long the drawer takes to slide in or out. The view runs the cassette's pose over
 * the same time, so the panel, the scaled tape and the DJ block arrive as one picture. */
#define CASSETTE_BUTTONS_SLIDE_MS 260

/* The drawer reports a visibility change as it starts the slide. The view uses it to
 * put the cassette at its control pose (134/256) and to show the DJ block with the
 * drawer, or to give the cassette back to the peek. `ctx` is the value given to create(). */
typedef void (*cassette_buttons_room_cb_t)(bool shown, void *ctx);

/* Build the hidden drawer and its three buttons as children of `screen`, above whatever
 * exists when this is called. `room` is optional. */
void cassette_buttons_create(lv_obj_t *screen, cassette_buttons_room_cb_t room, void *ctx);

/* Slide the drawer in (true) or out (false). A call while the other slide is running
 * reverses from the current position; a call that names the state already held does
 * nothing. The drawer is hidden once its slide out completes. */
void cassette_buttons_set_shown(bool shown);

/* Whether the drawer is meant to be shown, true even while it is still sliding in. */
bool cassette_buttons_is_shown(void);

/* Put the drawer back on top of the screen's child order. The peek is the only sibling
 * it competes with; the view still calls this where it used to re-raise the drawer. */
void cassette_buttons_raise(void);

#ifdef __cplusplus
}
#endif
