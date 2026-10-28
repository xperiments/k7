#pragma once
/*
 * cassette_empty - the Empty group: the right bar's default panel, a speaker grille.
 *
 * One plate at the Menu's Pad footprint (186x284 at 596,76), the shared 8 px corner and 2 px
 * stroke, filled with a staggered grid of 4 px dots on a 14 px pitch - alternate rows offset by
 * half a pitch, at least 12 px clear of the stroke - so it reads as a speaker's front and not
 * as a lattice. The dots are one object's draw callback, not one object each, for the reason the
 * tape's packs are: the count follows the panel, and ~200 extra LVGL objects would double the
 * screen's object count for pure decoration.
 *
 * The panel is inert. It is a picture with no controls, so the bar leaves its carcase out of the
 * hit test and bubbles gestures: a swipe that starts on the grille still moves the ladder, which
 * matters because the Empty group is the default and covers a third of the screen at Art/Real.
 *
 * The ink turns with the Page: `k_col_button_rect` dots on the dark page's `k_col_bg` plate,
 * `k_col_bg` dots on the white page's plate. The plate's fill and stroke are the bar's; this
 * module only paints the dots.
 *
 * Private to screen_display. create() installs the panel value with the right bar, so it runs
 * after cassette_bars_create(); the view installs it as the default group.
 */
#include "lvgl.h"

#include "cassette_bars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the grille's objects (none until the panel is installed) and install this panel with
 * the right bar. A second call is a no-op. */
void cassette_empty_create(void);

/* The panel's value, for the view to install again when the Empty group is selected after
 * another group or the Menu. Installing it does not rebuild the grille's values - it has none. */
const cassette_panel_t *cassette_empty_panel(void);

#ifdef __cplusplus
}
#endif
