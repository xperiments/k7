#pragma once
/*
 * cassette_rgb - the RGB group: the right bar's second panel, for the addressable strip.
 *
 * Six cells, the same 2x3 as the EQ group:
 *
 *     [Effect] [Red  ]
 *     [Effect] [Green]
 *     [Bright] [Blue ]
 *
 * the inside column the effect - one 96x190 plate, its dial above and its name below - and the
 * brightness, the outer column the three channels. The five knobs are knob_cell's dials on the
 * shared 3x editor (knob_editor). There is no Back cell: the mode bar's led button is the toggle.
 *
 * The three channels and the brightness are **level** knobs - the unipolar 270 degree sweep, 0 at
 * 7:30 and full at 4:30, neutral at full - and the effect is a **stepped** knob, one slot per
 * effect in rgb.h's list (OFF, SOLID, BREATHE, RAINBOW, CHASE, LEVEL), neutral at OFF. Each slot is
 * a marked position carrying the effect's own name, so the 3x dial's pillow says RAINBOW instead of
 * EFFECT, and the 1x plate shows that name in a bottom pillow under its dial. None of those marks
 * is plaqued round the dial: the pillow is the selector's text. The group's own look is still owed
 * a rework; this panel only carries the new knob vocabulary (ADR 0027).
 *
 * The knobs' values are this panel's and survive a swap; what they *publish* is rgb.h's state.
 * There is no strip in the firmware yet, so the seam is `cassette_rgb_set_publish()`: the board
 * installs a hook (or does not), and every move hands it the whole state. A hook installed late is
 * called once with the values that are already there.
 *
 * Private to screen_display. The grid, the dials, the handlers and the editor's bank are
 * knob_panel's, shared with the EQ group; what is here is this group's table - its cell map, its
 * five names, the effect's six marked slots - and its publish seam. create() builds the knobs'
 * values and nothing else; the panel value is installed by the composition, which is also what
 * decides when the group is up.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_bars.h"
#include "rgb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the panel's values - the five knobs, the effect's slots and names, the state they read
 * from. A second call is a no-op. It creates no object: the cells are the bar's. */
void cassette_rgb_create(void);

/* Where the strip's state goes. `cb` is called with the whole state after every move, and once
 * when it is installed; NULL installs nothing (the default: there is no strip driver yet). */
typedef void (*cassette_rgb_publish_t)(const rgb_state_t *state, void *ctx);
void cassette_rgb_set_publish(cassette_rgb_publish_t cb, void *ctx);

/* The panel's value, for the view to install when the RGB group is selected. */
const cassette_panel_t *cassette_rgb_panel(void);

#ifdef __cplusplus
}
#endif
