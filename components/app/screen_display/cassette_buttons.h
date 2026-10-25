#pragma once
/*
 * cassette_buttons - the transport drawer at the screen's left edge.
 *
 * The drawer is a 120 px panel that starts off-screen to the left and hidden. A
 * one-finger right swipe brings it in and a left swipe puts it away; the view turns
 * those two intents into cassette_buttons_set_shown(). The cassette rests at 134/256
 * (148..550), so the drawer already has room and no longer asks the view for a pose.
 *
 * The drawer holds three buttons down the left edge. Each is line art: a 96x96
 * rectangle rounded by 8 px, a 64 px circle at its centre, and the letter P, R or W
 * centred in that circle in Jura 36. The three start 76 px from the screen's top; the
 * middle button (R) is therefore on y=218. Each button carries two prebuilt state
 * objects - unpressed and pressed - and swaps which one is visible on press and release,
 * so the pressed look costs no style change at touch time. A click is a short scale pulse
 * and nothing else: the transport is not wired, and next/previous belong to the DJ
 * block's arrows.
 *
 * Private to screen_display, created above the Levels in the child order. Every object
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

/* How long the drawer takes to slide in or out. */
#define CASSETTE_BUTTONS_SLIDE_MS 260

/* Build the hidden drawer and its three buttons as children of `screen`, above whatever
 * exists when this is called. The drawer no longer reports a room request: the resting
 * pose already leaves it clear. */
void cassette_buttons_create(lv_obj_t *screen);

/* Slide the drawer in (true) or out (false). A call while the other slide is running
 * reverses from the current position; a call that names the state already held does
 * nothing. The drawer is hidden once its slide out completes. */
void cassette_buttons_set_shown(bool shown);

/* Whether the drawer is meant to be shown, true even while it is still sliding in. */
bool cassette_buttons_is_shown(void);

/* Put the drawer back on top of the screen's child order. The Levels raise their
 * Artwork sheet when it settles; the view calls this after a vertical swipe so the
 * drawer stays above every Level. */
void cassette_buttons_raise(void);

#ifdef __cplusplus
}
#endif
