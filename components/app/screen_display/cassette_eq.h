#pragma once
/*
 * cassette_eq - the DJ block: the EQ panel of the screen's right bar.
 *
 * The panel fills five of the right **bar**'s six cells: the four knobs GAIN, HI, MED and LOW
 * and R. The first column's middle cell is M, which is the bar's chrome and never the
 * panel's - it is the Menu's door whatever panel is up (cassette_bars). The bar builds the
 * carcases, slides them in and out over 260 ms and paints the page into them; this module
 * builds the knobs and R inside them and owns its edit layer: the 260 ms timer, the overlay,
 * the blink and the dim. The edit's ordering - which knob is pending, which is open, the reset
 * gesture and its owed click, the close - is `knob_edit_t`'s in knob_model, which is
 * clock-free; this module feeds it the events and applies the effects it returns. The six
 * carcases are the same 96x96 box as the Transport panel's, two touching columns three rows
 * deep.
 *
 * Each knob is a 44 px circle (30% smaller than a button's), a 24-dot crown outside it at
 * r=32, and a line from the centre towards the rim (the owner's tuned 4 px width). 0 dB is at
 * twelve o'clock, the right side cuts and the left side boosts. R is the drawer's button moved
 * here - the shared cassette_button on the bar's cell: the same 64 px circle, Jura 36 letter,
 * one pressed state and click pulse - still a placeholder, with its own red cap on the white
 * page. The next/previous controls are the Transport panel's < and >.
 *
 * A tap on a knob opens edit mode after a 260 ms delay; the second tap of a double tap lands
 * on the 3x knob and resets it there, so the reset gesture survives the short delay. A 3x
 * virtual knob (256 px at x=220, 90% of the 96+94+94 length, its top at y=90) takes the
 * cassette's centre, with no outer square - just the circle, the crown and the line; under
 * the dial, a rounded primary pillow carries the knob's name (GAIN, HI, MED or LOW). The
 * cassette cuts out and the knob appears; on close the knob disappears and the cassette cuts
 * back in. Only the knob's centre circle closes. A click on
 * the ring - the 256 px area around that circle - is the target point: the knob jumps to that
 * angle, 0 dB at twelve, clockwise cutting and counter-clockwise boosting. A click outside
 * does nothing; there is no drag and no relative rotation. A double tap on any small knob
 * resets it to 0 dB and cancels the pending open, like a mixer's centre detent, so the virtual
 * knob never appears. While edit mode is on, the view ignores every screen intent, because the
 * touch adapter's two-finger VU arrives directly.
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

/* True while a knob is in edit mode. The overlay owns the touch; the view must drop every
 * screen intent, including the two-finger VU the touch adapter reports directly. */
bool cassette_eq_is_editing(void);

/* The panel's value, for the Menu to install again after its Pad. Handing it back does not
 * rebuild anything: the bar rebuilds the cells and the panel's own edit layer, its knob
 * values and its audio calls are the ones it already had. */
const cassette_panel_t *cassette_eq_panel(void);

#ifdef __cplusplus
}
#endif
