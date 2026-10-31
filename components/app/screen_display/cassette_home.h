#pragma once
/*
 * cassette_home - the Home group: the right bar's default panel, four keys.
 *
 *   [      1      ]
 *   [  2  ] [  3  ]
 *   [      4      ]
 *
 * Four cells of the bar's 2x3 lattice, and the ends are two of its columns merged: the top and
 * the bottom key are one 190x96 carcase, the middle two are the bar's ordinary 96x96 ones. A
 * merged cell is not a bigger button - it is one key and a spare - and the owner's call is that
 * the wide ones carry no dome at all: a wide key is its plate with the glyph in the outer
 * column, exactly where a domed key's glyph would sit, and the spare 94 px on the left is empty.
 * The middle pair are the ordinary 64 px dome, ring and all, in their cells.
 *
 * The merge is the panel's business alone, the trick knob_panel plays on the other axis turned a
 * quarter: cell_rect() returns the merged rect for the inside column and an empty rect for the
 * column under it, and the bar builds no carcase where the rect is empty. cassette_bars needs
 * no change.
 *
 * The keys are not modes: no latch, no toggle, no group of their own. They take the whole
 * carcase's touch, and a swipe that starts on one stops there - this is a control surface, not
 * the picture it replaced - so the panel does not bubble gestures.
 *
 * The actions are not here yet: every spec's on_click is NULL, so a click is the pulse alone,
 * and the four handles are the anchors those actions will hang from. The pages turn as each
 * key's ink allows: a bare one has only its glyph to re-ink, a domed one its ring, its cap and
 * its glyph.
 *
 * Private to screen_display. The module builds no carcase of its own: the view installs the
 * panel value with the right bar, and the bar builds the cells and hands each to build().
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_bars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the group's values: called once, with the other panels' create(), before the composition
 * installs a panel. A second call is a no-op. The cells themselves are the bar's and are built
 * whenever the panel is installed. */
void cassette_home_create(void);

/* The panel's value, for the view to install when the Home group is up: the default at boot, and
 * what a second tap on a group's button returns to. */
const cassette_panel_t *cassette_home_panel(void);

#ifdef __cplusplus
}
#endif
