#pragma once
/*
 * cassette_eq - the DJ block: the EQ panel of the screen's right bar.
 *
 * The panel fills the right **bar**'s six cells, two columns of three:
 *
 *     [Back] [HI ]
 *     [VU  ] [MED]
 *     [Gain] [LOW]
 *
 * the inside column the group's chrome and the outer column its three bands. The bar builds the
 * carcases, slides them in and out over 260 ms and paints the page into them; this module builds
 * the knobs, the Back button and the VU cell inside them, and owns the knobs' values and the
 * audio. The edit's ordering - which knob is pending, which is open, the reset gesture and its
 * owed click, the close - is knob_model's `knob_edit_t`; the modal surface itself is
 * knob_editor's. The six carcases are the same 96x96 box as the mode bar's, two touching columns
 * three rows deep.
 *
 * Each knob is a 44 px circle (30% smaller than a button's), a 24-dot crown outside it at
 * r=32, and a line from the centre towards the rim (the owner's tuned 4 px width). 0 dB is at
 * twelve o'clock, the right side cuts and the left side boosts. The Back cell is the shared
 * cassette_button with the icon font's left chevron, and its click is the view's: the group goes
 * back to Empty. The VU cell is the meter, two 84 px rows, fed by the cassette frames.
 *
 * The modal half is knob_editor's, shared with the RGB panel: a tap on a knob opens the 3x
 * dial after a 260 ms delay, the second tap of a double tap resets the knob to 0 dB in place
 * (so the reset gesture survives the short delay), a click on the dial's centre circle closes,
 * and a click on its ring sets the angle. This file supplies the bank - the four knobs, the
 * cell at a point, the pillow's text, the repaint, the audio call and the shown gate - and
 * nothing of the overlay.
 *
 * The bar shows the panel with the Transport one, when the cassette is already at the 134/256
 * control pose that leaves the right column room. The bar tells the panel when it becomes
 * visible and when it is taken away: leaving ends edit mode and cancels a pending open, so
 * neither the overlay nor the 3x knob can outlive the hidden cells. The four knob values are
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
 * overlay - on `parent` (the screen). Call once, after cassette_bars_create(). On the dark
 * page all five cells are the palette's playing red line art; on the white page the four
 * knobs' cells are the knob plate and R's cap is k_col_button_r with a light letter. */
void cassette_eq_create(lv_obj_t *parent);

/* The view's Back action, run by the Back cell's click after the tap guard and the pulse. The
 * panel does not know what Back means (Empty, in the groups model); a NULL callback makes the
 * cell a pulse. */
void cassette_eq_set_back_cb(void (*cb)(void *ctx), void *ctx);

/* The panel's value, for the view to install again when the EQ group is selected. Handing it
 * back does not rebuild anything: the bar rebuilds the cells and the panel's knob values, its
 * meter and its audio calls are the ones it already had. */
const cassette_panel_t *cassette_eq_panel(void);

#ifdef __cplusplus
}
#endif
