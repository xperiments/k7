/*
 * The cassette screen's touch policy (see screen_input.h).
 *
 * This is the whole of the screen's input: the one-finger gesture. It decides which swipe
 * the screen acts on and keeps a drag that belongs to a part - the Pad's ratchet, a knob's,
 * the bars' cells - from also meaning a screen intent, and hands the answer to the view's
 * sink. What a swipe means to the picture - which rung of the ladder it takes - is not
 * decided here: the sink owns that, and this file calls nothing above itself.
 *
 * The vertical pair is the ladder: an up swipe is UP and a down swipe is DOWN (ADR 0025).
 * The horizontal pair is nothing: the drawer and the block are reached by the ladder now,
 * and the only left swipe left in the build is the Pad's Back, which the bar keeps local.
 * A two-finger swipe is nothing either; the VU meter's gesture is gone and the adapter no
 * longer reports one. Next and previous belong to the DJ block's arrows, whose clicks are
 * their own objects' and never reach this module.
 *
 * The log lines that are about the input itself - the swipe directions - are kept here, with
 * the view's tag, so the boot log reads as it does for the gestures that remain.
 */
#include "screen_input.h"

#include "esp_log.h"

/* The same tag the screen has always logged under: these messages sit in the view's log
 * stream, and moving them to a module must not change where they appear. */
static const char *TAG = "screen";

/* The sink and its context: the view's, not this module's. */
static screen_intent_cb_t s_on_intent;
static void *s_ctx;

/* The screen the handler is registered on, so a second attach() does not register it again
 * and report every intent twice. */
static lv_obj_t *s_screen;

static void report(screen_intent_t intent)
{
    if (s_on_intent) s_on_intent(intent, s_ctx);
}

/*
 * The one-finger gesture: the vertical pair is reported as the direction - DOWN and UP - for
 * the sink to take the rung. The direction is the whole of this handler's knowledge. A
 * gesture on a part is not the screen's: the Pad's drag is the Pad's ratchet, a knob's is the
 * knob's, and the bar's cells keep their gestures local, so this handler only runs for a
 * gesture whose target is the screen itself.
 */
static void screen_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_screen) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    switch (lv_indev_get_gesture_dir(indev)) {
        case LV_DIR_TOP:
            report(SCREEN_INTENT_UP);
            break;
        case LV_DIR_BOTTOM:
            report(SCREEN_INTENT_DOWN);
            break;
        default:
            break;
    }
}

/*
 * A swipe is a release too, and LVGL sends CLICKED after the gesture. The screen gives a
 * click no meaning: next and previous are the DJ block's own buttons, whose clicks belong to
 * their cells, and a tap on the cassette is not an action. There is therefore no click
 * handler; the gesture above is the whole screen policy.
 */

void screen_input_attach(lv_obj_t *screen, screen_intent_cb_t on_intent, void *ctx)
{
    if (!screen) return;

    /* The sink is a value: a second attach on the same screen takes the new one, but the
     * handler stays registered once. */
    s_on_intent = on_intent;
    s_ctx = ctx;
    if (screen == s_screen) return;
    s_screen = screen;

    lv_obj_add_event_cb(screen, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
}
