#pragma once
/*
 * screen_input - the cassette screen's touch policy: what a touch means, not what the
 * screen does about it.
 *
 * The screen's surface and the parts that bubble their presses to it are what this module
 * sees; a part's own click (P/R/W, the DJ block's cells) belongs to the part. The screen
 * receives a press and a one-finger gesture; on the panel board the touch adapter also
 * reports a two-finger swipe it detects from the contacts themselves. This module turns
 * those events into one intent and hands it to the view's sink. A swipe's direction and the
 * reserved two-finger gesture are decided here, so the view does not have to: nothing in
 * this module calls mini_icecast, touches the peek or slides the button drawer.
 *
 * Either horizontal swipe toggles the transport drawer at the left edge (with the DJ
 * block): from the entry 100% pose either brings them in, and while they are out either
 * takes them out. An up swipe at
 * Bare is reported as PEEK_SHOW and a down swipe as PEEK_HIDE: the view raises the peek
 * only from the entry 100% pose and closes it on the down swipe. A two-finger left is
 * VU_SHOW and a two-finger right VU_HIDE: the VU meter starts hidden and fades in/out. A
 * tap is not an intent: next and previous are the left drawer's arrows, whose
 * clicks are their own cells' and never reach the screen.
 *
 * "One touch, one intent" is the point of the policy. LVGL sends the release of a swipe as
 * a click as well, and the two-finger swipe arrives while a one-finger gesture for the same
 * contact is still to come, so the module keeps a latch for the length of a touch: the
 * first thing a touch means is the only thing it means.
 *
 * The two-finger swipe is the VU meter's toggle: a left is SCREEN_INTENT_VU_SHOW and a
 * right SCREEN_INTENT_VU_HIDE. It is reported through the touch adapter, not through the
 * one-finger gesture, and the latch keeps the same touch from doing both.
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
    SCREEN_INTENT_PEEK_SHOW,
    SCREEN_INTENT_PEEK_HIDE,
    SCREEN_INTENT_VU_SHOW,
    SCREEN_INTENT_VU_HIDE,
} screen_intent_t;

/* One decision, already filtered: a two-finger swipe is not also a one-finger gesture. `ctx`
 * is the value given to attach(). */
typedef void (*screen_intent_cb_t)(screen_intent_t intent, void *ctx);

/* Register the screen's press and gesture handlers and, on the panel board, point the touch
 * adapter's two-finger callback at this module. Calling it again on the same screen takes
 * the new sink without registering the handlers a second time, which would report every
 * intent twice. */
void screen_input_attach(lv_obj_t *screen, screen_intent_cb_t on_intent, void *ctx);

/* True for the rest of a touch the adapter reported as a two-finger swipe. The DJ block's
 * cells consult it so a two-finger VU gesture that starts on one cannot skip a track or
 * enter edit mode. The latch is cleared by the next press, which the cells bubble here. */
bool screen_input_two_finger_seen(void);
