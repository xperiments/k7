#pragma once
/*
 * screen_input - the cassette screen's touch policy: what a touch means, not what the
 * screen does about it.
 *
 * The screen's surface and the parts that bubble their presses to it are what this module
 * sees; a part's own click (P/M/R, the DJ block's cells, the Pad) belongs to the part. This
 * module turns the screen's one-finger gesture into one intent and hands it to the view's
 * sink. The direction is decided here, so the view does not have to: nothing in this module
 * calls mini_icecast, raises the peek or slides a bar.
 *
 * The vertical pair is the ladder: a down swipe is DOWN and takes the screen one rung
 * towards Real, an up swipe is UP and takes it one rung towards the Peek (ADR 0025). Which
 * rung a direction takes belongs to the view, because only it knows what is on screen; this
 * module reports the direction.
 *
 * The horizontal swipes are not intents. The drawer and the DJ block are the ladder's now,
 * so either side swipe on the screen does nothing (ADR 0025). The only left swipe left in
 * the build is the Menu's Back, and it is the Pad's - the bar keeps that gesture local, so
 * it never arrives here.
 *
 * A two-finger swipe is not an intent either: the VU meter's gesture is gone with the
 * meter's old home (ADR 0025). There is no latch any more, because there is no second
 * gesture for a touch to be taken from.
 *
 * A tap is not an intent: next and previous are the left drawer's arrows, whose
 * clicks are their own cells' and never reach the screen.
 *
 * "One touch, one intent" is still the point of the policy, and it is now the gesture
 * recognition's: LVGL sends the release of a swipe as a click as well, and a click on the
 * screen has no meaning, so the one gesture is the one intent.
 *
 * Contract:
 *  - attach() once, under the LVGL lock, on a screen the caller has already made clickable.
 *    The screen belongs to the caller: this module adds a handler to it and changes nothing
 *    else about it - not its clickability, its style or its geometry - and it creates and
 *    deletes no object.
 *  - The sink runs on the thread LVGL dispatches events on - the screen task, inside
 *    lv_timer_handler() - with the LVGL lock held, so it may call LVGL. It must not block.
 *  - No allocation, and no state beyond the screen and the sink.
 *
 * SCREEN_INTENT_NONE is the zero value and is never reported.
 */
#include "lvgl.h"

typedef enum {
    SCREEN_INTENT_NONE = 0,
    /* The vertical swipes, reported as the direction: the view knows which rung of the
     * ladder each one takes. */
    SCREEN_INTENT_DOWN,
    SCREEN_INTENT_UP,
    /* The two-finger vertical pair: Real's picture full screen, and back. Only the touch
     * adapter can report these - it is the one place that sees two contacts. */
    SCREEN_INTENT_FULL_ON,
    SCREEN_INTENT_FULL_OFF,
} screen_intent_t;

/* One decision: a gesture on the screen itself, never on a part that owns its drags. `ctx`
 * is the value given to attach(). */
typedef void (*screen_intent_cb_t)(screen_intent_t intent, void *ctx);

/* Register the screen's gesture handler. Calling it again on the same screen takes the new
 * sink without registering the handler a second time, which would report every intent
 * twice. */
void screen_input_attach(lv_obj_t *screen, screen_intent_cb_t on_intent, void *ctx);

/* The touch adapter's two-finger swipe: the midpoint of the two contacts travelled by dx,dy
 * panel pixels, negative dy up. It sets the latch for the rest of the touch - so the same
 * touch cannot also take a rung - and reports FULL_ON (up) or FULL_OFF (down) when the travel
 * is mostly vertical. A horizontal two-finger swipe is latched and reports nothing.
 *
 * The adapter is the only caller: screen_input never names the board (layering A2). */
void screen_input_two_finger(int dx, int dy);

/* Whether the touch in progress began as a two-finger gesture. Parts that act on a click ask
 * this so the release of that touch is not also a tap. */
bool screen_input_two_finger_seen(void);
