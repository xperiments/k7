#pragma once
/*
 * cassette_eq - the EQ group: the screen's right bar, one of its three panels.
 *
 * The panel fills the right **bar**'s six cells, two columns of three:
 *
 *     [VU  ] [HI ]
 *     [VU  ] [MED]
 *     [Gain] [LOW]
 *
 * the inside column the meter - one 96x190 plate over its top two cells - and the gain, and the
 * outer column its three bands. There is no Back cell: the mode bar's sliders button is the
 * toggle, so a tap on it puts the group away. The bar builds the
 * carcases, slides them in and out over 260 ms and paints the page into them; this module builds
 * the knobs and the meter inside them, and owns the knobs' values and the
 * audio. The edit's ordering - which knob is pending, which is open, the reset gesture and its
 * owed click, the close - is knob_model's `knob_edit_t`; the modal surface itself is
 * knob_editor's. The six carcases are the same 96x96 box as the mode bar's, two touching columns
 * three rows deep.
 *
 * Each knob is a 44 px cap (30 % smaller than a button's) with a ring of short lines outside it
 * at r=32 - the two ends and 0 dB's twelve o'clock - and the 4 px line from the centre to the
 * rim (the owner's tuned width). The value marker is the brighter, longer line that points at
 * the value. The arc is 270 degrees: -26 dB at 7:30 on the left, +6 dB at 4:30 on the right, 0 dB
 * at twelve, so a boost turns clockwise (ADR 0027, which reverses ADR 0018's "the right side
 * cuts"). The meter's plate is cassette_meter's, two 84 px rows fed by the cassette frames.
 *
 * A knob is dragged, not aimed: a press and a vertical drag on the cell moves the value (2 px a
 * step, a 6 px dead zone, a magnet at 0 dB), and a tap opens the 3x dial after a 260 ms delay.
 * The dial only reads: its pillow carries the knob's name at rest and the live dB while it is
 * being moved, and the two ends are printed as plaques at their own angles. The moving is the
 * Pad's - it takes this panel's cells while the dial is up, and its ratchet, tap-to-cycle and
 * left-swipe-to-close are the whole interaction (ADR 0028). A double tap on a small knob still
 * resets it to 0 dB in place, so the reset gesture survives the short delay. This file supplies
 * the bank - the four knobs, the cell at a point, the repaint, the audio call and the shown gate
 * - and nothing of the Pad.
 *
 * The bar shows the panel with the mode bar, when the cassette is already at the 134/256
 * control pose that leaves the right column room. The bar tells the panel when it becomes
 * visible and when it is taken away: leaving ends edit mode and cancels a pending open, so
 * neither the Pad nor the 3x dial can outlive the hidden cells. A swap the *editor* makes keeps
 * the bank and leaves these cells absent for a while, which is why every hook checks s_shown. The
 * four knob values are
 * the panel's and survive both; the panel is also where the audio is set.
 *
 * The Menu's Pad is the right bar's other panel: open() installs it with
 * cassette_bars_set_panel() and close() restores this one through cassette_eq_panel(). The
 * bar deletes one panel's cells and builds the other's; this module's values and its edit
 * layer are its own and outlive the swap. The Menu is the only caller of the swap.
 */
#include "lvgl.h"

#include "cassette_bars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install the EQ panel in the right bar and build the edit layer - the 3x knob and its lazy
 * dial and the Pad's seam - on `parent` (the screen). Call once, after cassette_bars_create(). On the dark
 * page the group is the palette's playing red line art; on the white page the four knobs'
 * knobs' cells are the knob plate. */
void cassette_eq_create(lv_obj_t *parent);

/* The panel's value, for the view to install again when the EQ group is selected. Handing it
 * back does not rebuild anything: the bar rebuilds the cells and the panel's knob values, its
 * meter and its audio calls are the ones it already had. */
const cassette_panel_t *cassette_eq_panel(void);

#ifdef __cplusplus
}
#endif
