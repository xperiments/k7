#pragma once
/*
 * screen_input - the cassette screen's touch policy: what a touch means, not what the
 * screen does about it.
 *
 * The screen is the one object a finger can hit, so LVGL sends it a press, a click and a
 * one-finger gesture; on the panel board the touch adapter also reports a two-finger swipe
 * it detects from the contacts themselves. This module turns those events into one intent
 * and hands it to the view's sink. A tap, a swipe's direction and the reserved two-finger
 * gesture are decided here, so the view does not have to: nothing in this module calls
 * mini_icecast, moves a Level, touches the Label or slides the button drawer.
 *
 * A left/right swipe no longer skips a Track: it hides or shows the buttons at the left
 * edge. Down/up still moves one Level, a tap is still next track, and two fingers are
 * still the menu's.
 *
 * "One touch, one intent" is the point of the policy. LVGL sends the release of a swipe as
 * a click as well, and the two-finger swipe arrives while a one-finger gesture for the same
 * contact is still to come, so the module keeps a latch for the length of a touch: the
 * first thing a touch means is the only thing it means.
 *
 * The two-finger swipe is reserved for the menu. It is read as SCREEN_INTENT_MENU, so when
 * the menu lands it takes the intent and nothing here changes; until then the view's sink
 * ignores it.
 *
 * Contract:
 *  - attach() once, under the LVGL lock, on a screen the caller has already made clickable.
 *    The screen belongs to the caller: this module adds handlers to it and changes nothing
 *    else about it - not its clickability, its style or its geometry - and it creates and
 *    deletes no object.
 *  - The sink runs on the thread LVGL dispatches events on - the screen task, inside
 *    lv_timer_handler() - with the LVGL lock held, so it may call LVGL. It must not block.
 *  - No allocation, and no state beyond the latch, the screen and the sink.
 *
 * SCREEN_INTENT_NONE is the zero value and is never reported.
 */
#include "lvgl.h"

typedef enum {
    SCREEN_INTENT_NONE = 0,
    SCREEN_INTENT_BUTTONS_SHOW,
    SCREEN_INTENT_BUTTONS_HIDE,
    SCREEN_INTENT_LEVEL_DOWN,
    SCREEN_INTENT_LEVEL_UP,
    SCREEN_INTENT_TAP,
    SCREEN_INTENT_MENU,
} screen_intent_t;

/* One decision, already filtered: a swipe's release is not also a tap, and a two-finger
 * swipe is not also a one-finger gesture. `ctx` is the value given to attach(). */
typedef void (*screen_intent_cb_t)(screen_intent_t intent, void *ctx);

/* Register the screen's press, click and gesture handlers and, on the panel board, point
 * the touch adapter's two-finger callback at this module. Calling it again on the same
 * screen takes the new sink without registering the handlers a second time, which would
 * report every intent twice. */
void screen_input_attach(lv_obj_t *screen, screen_intent_cb_t on_intent, void *ctx);
