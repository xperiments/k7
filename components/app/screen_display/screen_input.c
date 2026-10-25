/*
 * The cassette screen's touch policy (see screen_input.h).
 *
 * This is the whole of the screen's input: the press, the one-finger gesture, and the
 * touch adapter's two-finger swipe. It decides which of those a touch is and keeps one
 * touch from being two of them, and hands the answer to the view's sink. What a swipe
 * means to the picture - which Level, whether the drawer slides out, whether the peek
 * shows or hides - is not decided here: the sink owns that, and this file names no Level
 * and calls nothing above itself. A left/right swipe no longer skips a Track; it hides or
 * shows the transport drawer instead. Next and previous belong to the DJ block's arrows,
 * whose clicks are their own objects' and never reach this module.
 *
 * The log lines that are about the input itself - the swipe directions and the reserved
 * two-finger gesture - are kept here, with the view's tag, so the boot log reads as it did.
 */
#include "screen_input.h"

#include "esp_log.h"

#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "touch_gt911.h"
#endif

/* The same tag the screen has always logged under: these messages sit in the view's log
 * stream, and moving them to a module must not change where they appear. */
static const char *TAG = "screen";

/* The sink and its context: the view's, not this module's. */
static screen_intent_cb_t s_on_intent;
static void *s_ctx;

/* The screen the handlers are registered on, so a second attach() does not register them
 * again and report every intent twice. */
static lv_obj_t *s_screen;

/*
 * Set by the touch adapter's two-finger callback and cleared by the next press: one bit for
 * the length of one touch. It is what stops a two-finger swipe from also being read as a
 * one-finger gesture: LVGL's recognizer still has the first contact. The adapter reports
 * the swipe before the gesture arrives, so the latch is already set when it does.
 *
 * Only the panel board can set it: a board without a controller has no two-finger callback,
 * and hears about a touch as a pointer only. The test is unconditional anyway, so the policy
 * reads the same on both boards instead of growing a second copy of itself.
 */
static bool s_two_finger_seen;

static void report(screen_intent_t intent)
{
    if (s_on_intent) s_on_intent(intent, s_ctx);
}

/* A new touch clears the two-finger latch from the previous one. The drawer, its buttons
 * and the DJ block's cells bubble their presses here, so a touch that starts on a control
 * is a new touch too. */
static void screen_pressed_cb(lv_event_t *e)
{
    (void)e;
    s_two_finger_seen = false;
}

bool screen_input_two_finger_seen(void)
{
    return s_two_finger_seen;
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/*
 * The two-finger swipe is reserved for the menu (on/off), which is not designed yet. The
 * port still reports it, and this callback keeps the one-finger handler from acting on the
 * same touch: a two-finger gesture must not change a Level or open the drawer by accident.
 * When the menu lands, it takes this intent and nothing in touch_gt911.c has to change. */
static void screen_two_finger_cb(int dx, int dy)
{
    (void)dx;
    (void)dy;
    s_two_finger_seen = true;
    ESP_LOGI(TAG, "two-finger swipe: reserved for the menu");
    report(SCREEN_INTENT_MENU);
}
#endif

/*
 * The one-finger gestures: left/right show and hide the buttons at the left edge, down/
 * up move one Level towards Artwork and towards Bare. The direction is the whole of this
 * handler's knowledge; the Levels, the ends of the order, Bare's peek and the
 * drawer's slide are the sink's, which is where the old handler's `s_level` and
 * `s_paper_shown` tests went. A two-finger swipe does not come through here: the touch
 * adapter reports it, and the latch keeps this handler from acting on the same touch as
 * well.
 */
static void screen_gesture_cb(lv_event_t *e)
{
    (void)e;
    if (s_two_finger_seen) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    switch (lv_indev_get_gesture_dir(indev)) {
        case LV_DIR_LEFT:
            report(SCREEN_INTENT_BUTTONS_HIDE);
            ESP_LOGI(TAG, "swipe left: buttons hidden");
            break;
        case LV_DIR_RIGHT:
            report(SCREEN_INTENT_BUTTONS_SHOW);
            ESP_LOGI(TAG, "swipe right: buttons shown");
            break;
        case LV_DIR_BOTTOM:
            report(SCREEN_INTENT_LEVEL_DOWN);
            break;
        case LV_DIR_TOP:
            report(SCREEN_INTENT_LEVEL_UP);
            break;
        default:
            break;
    }
}

/*
 * A swipe is a release too, and LVGL sends CLICKED after the gesture. The screen no longer
 * gives a click a meaning: next and previous are the DJ block's own buttons, whose clicks
 * belong to their cells, and a tap on the cassette is not an action. The handler is
 * therefore gone; the gesture above and the latch are the whole screen policy.
 */

void screen_input_attach(lv_obj_t *screen, screen_intent_cb_t on_intent, void *ctx)
{
    if (!screen) return;

    /* The sink is a value: a second attach on the same screen takes the new one, but the
     * handlers stay registered once. */
    s_on_intent = on_intent;
    s_ctx = ctx;
    if (screen == s_screen) return;
    s_screen = screen;
    s_two_finger_seen = false;

    lv_obj_add_event_cb(screen, screen_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The touch adapter's two-finger swipe; it is called before the one-finger gesture for
     * the same touch, so the latch is set when that one arrives. */
    touch_gt911_set_two_finger_cb(screen_two_finger_cb);
#endif
}
