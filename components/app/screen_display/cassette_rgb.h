#pragma once
/*
 * cassette_rgb - the RGB group: the right bar's second panel, for the addressable strip.
 *
 * Six cells, the same 2x3 as the EQ group:
 *
 *     [Back  ] [Red  ]
 *     [Effect] [Green]
 *     [Bright] [Blue ]
 *
 * the inside column the group's chrome and the strip's two global controls, the outer column the
 * three channels. The five knobs are knob_cell's small dials on the shared 3x editor
 * (knob_editor); the Back cell is the shared cassette_button with the icon font's left chevron.
 *
 * The three channels and the brightness are **level** knobs - the unipolar 270 degree sweep, 0 at
 * 7:30 and full at 4:30, neutral at full - and the effect is a **stepped** knob, one slot per
 * effect in rgb.h's list (OFF, SOLID, BREATHE, RAINBOW, CHASE, LEVEL), neutral at OFF. Its 3x
 * pillow shows the effect's name instead of a number; the small cell shows only the crown's lit
 * run, like every other knob.
 *
 * The knobs' values are this panel's and survive a swap; what they *publish* is rgb.h's state.
 * There is no strip in the firmware yet, so the seam is `cassette_rgb_set_publish()`: the board
 * installs a hook (or does not), and every move hands it the whole state. A hook installed late is
 * called once with the values that are already there.
 *
 * Private to screen_display. create() installs the panel value with the right bar, after
 * cassette_bars_create() and knob_editor_create().
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_bars.h"
#include "rgb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the panel's values and install it with the right bar. A second call is a no-op. */
void cassette_rgb_create(void);

/* The view's Back action, run by the Back cell's click after the tap guard and the pulse. The
 * panel does not know what Back means (Empty, in the groups model). */
void cassette_rgb_set_back_cb(void (*cb)(void *ctx), void *ctx);

/* Where the strip's state goes. `cb` is called with the whole state after every move, and once
 * when it is installed; NULL installs nothing (the default: there is no strip driver yet). */
typedef void (*cassette_rgb_publish_t)(const rgb_state_t *state, void *ctx);
void cassette_rgb_set_publish(cassette_rgb_publish_t cb, void *ctx);

/* The panel's value, for the view to install when the RGB group is selected. */
const cassette_panel_t *cassette_rgb_panel(void);

#ifdef __cplusplus
}
#endif
