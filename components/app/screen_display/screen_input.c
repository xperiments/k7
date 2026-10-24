/*
 * The cassette screen's touch policy (see screen_input.h).
 *
 * This is the whole of the screen's input: the press, the release's click, the one-finger
 * gesture, and the touch adapter's two-finger swipe. It decides which of those a touch is
 * and keeps one touch from being two of them, and hands the answer to the view's sink. What
 * a swipe means to the picture - which Level, whether the buttons slide out, whether the
 * small label peeks or hides - is not decided here: the sink owns that, and this file names
 * no Level and calls nothing above itself. A left/right swipe no longer skips a Track; it
 * hides or shows the button drawer instead, which is why the transport is no longer in the
 * gesture map.
 *
 * The log lines that are about the input itself - the swipe directions and the reserved
 * two-finger gesture - are kept here, with the view's tag, so the boot log reads as it did.
 * The log for the action taken is the sink's, which is why the tap's "next track" line lives
 * there.
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
 * one-finger gesture - LVGL's recognizer still has the first contact - or as the click its
 * release produces. The adapter reports the swipe before either of those arrives, so the
 * latch is already set when they do.
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

/* A new touch clears the two-finger latch from the previous one. The drawer and its
 * buttons bubble their presses here, so a touch that starts on the transport is a new
 * touch too. */
static void screen_pressed_cb(lv_event_t *e)
{
    (void)e;
    s_two_finger_seen = false;
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The two-finger swipe is reserved for the menu (on/off), which is not designed yet. The
 * port still reports it, and this callback keeps the one-finger handler and the click from
 * acting on the same touch: a two-finger gesture must not change a Level, skip a Track or
 * open the sticker by accident. When the menu lands, it takes this intent and nothing in
 * touch_gt911.c has to change. */
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
 * handler's knowledge; the Levels, the ends of the order, the Bare label's peek and the
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
 * A swipe is a release too, and LVGL sends CLICKED after the gesture. The gesture has
 * already reported its intent, so the click must not act a second time. Two things tell the
 * two apart: a two-finger swipe has set the latch, and a one-finger swipe leaves its
 * direction on the input device, where it still is on the release. Only a click that is
 * neither is a tap.
 *
 * The drawer and its buttons bubble their clicks here. They are not the screen's tap:
 * the target check drops every click that started on a child, so a button press cannot
 * skip a Track and a tap on the drawer's margin is absorbed there.
 */
static void screen_clicked_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_screen) return;
    if (s_two_finger_seen) return;
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;
    report(SCREEN_INTENT_TAP);
}

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
    lv_obj_add_event_cb(screen, screen_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(screen, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The touch adapter's two-finger swipe; it is called before the one-finger gesture for
     * the same touch, so the latch is set when that one arrives. */
    touch_gt911_set_two_finger_cb(screen_two_finger_cb);
#endif
}
