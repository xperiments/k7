#pragma once
/*
 * touch_gt911 - the panel's touch controller, as LVGL's pointer.
 *
 * One call, and the screen accepts touches. The bus, the address, the reset and
 * interrupt pins, the read cadence and the coordinate latch are this module's; the
 * view sees only a pointer. That is the whole interface, and it is deliberately the
 * same shape as panel_rgb_start(): a board adapter that hands back a working device
 * and names nothing above itself.
 *
 * Contract:
 *  - Call once, after the LVGL port owns `display` (panel_rgb_start()'s display).
 *  - The call takes the port lock itself; a caller that already holds it is fine.
 *  - ESP_OK means the controller answered and LVGL reads a point from it every
 *    10 ms. A failure leaves `display` working and is only a missing pointer.
 *  - Coordinates are the panel's native ones: this panel is not rotated, and the
 *    controller's axes are not swapped.
 *  - A two-finger swipe is reported through the callback, once per swipe; the view
 *    decides what it means. It is detected from the two contacts themselves, so it
 *    does not depend on the controller's track ids.
 */
#include "esp_err.h"
#include "lvgl.h"

/* One two-finger swipe: the midpoint of the first two contacts travelled past the
 * threshold. `dx`/`dy` are that travel in panel pixels, so the view can decide
 * which direction it wants. Called from the port's task, once per swipe, with the
 * LVGL lock held. */
typedef void (*touch_two_finger_cb_t)(int dx, int dy);

esp_err_t touch_gt911_start(lv_display_t *display);

/* Report two-finger swipes to `cb`; NULL turns them off. May be called before or
 * after touch_gt911_start(). */
void touch_gt911_set_two_finger_cb(touch_two_finger_cb_t cb);
