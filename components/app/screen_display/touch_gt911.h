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
 *  - A second contact is not the pointer: the module reports the first point to LVGL, and
 *    reads the two contacts itself for a two-finger swipe. That detector used to toggle the
 *    VU meter (ADR 0025 removed it with the meter); it now reports to screen_input, which
 *    turns a vertical pair into the full-screen picture's intents.
 */
#include "esp_err.h"
#include "lvgl.h"

/* The two-finger swipe, as the midpoint of the first two contacts: how far it travelled, in
 * panel pixels, negative dy up. Reported once per touch, from the pointer read (so under the
 * LVGL lock), as soon as the midpoint has moved TOUCH_TWO_FINGER_PX on either axis. */
typedef void (*touch_two_finger_cb_t)(int dx, int dy);
void touch_gt911_set_two_finger_cb(touch_two_finger_cb_t cb);

esp_err_t touch_gt911_start(lv_display_t *display);
