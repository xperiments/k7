#pragma once
/*
 * cassette_eq - the DJ block on the right of the Cassette screen.
 *
 * Six 96 px cells in two touching columns, the same carcase as the left drawer's
 * buttons: the first column holds GAIN above M and R, the second the HI, MED and LOW
 * knobs. M and R are the drawer's buttons moved here - the same 64 px circle, Jura 36
 * letter, two pressed states and click pulse - with no action yet. Each knob is a 44 px
 * circle (30% smaller), a 24-dot crown outside it at r=32, and a line from the centre
 * towards the rim (the owner's tuned 4 px width). 0 dB is at twelve o'clock, the right
 * side cuts and the left side boosts. The next/previous controls are the left drawer's
 * < and >.
 *
 * A tap on a knob opens edit mode after a 550 ms delay, so a second tap is read as the
 * reset double click instead: a 3x virtual knob (284 px at x=206, the 96+94+94 length, its
 * top on the GAIN button's top) takes the cassette's centre, with no outer square - just
 * the circle, the crown and the line. The cassette cuts out and the knob appears; on close
 * the knob disappears and the cassette cuts back in. Only the knob's centre circle closes.
 * A click on the ring - the 284 px area around that circle - is the target point: the knob
 * jumps to that angle, 0 dB at twelve, clockwise cutting and counter-clockwise boosting. A
 * click outside does nothing; there is no drag and no relative rotation. A double tap on
 * any small knob resets it to 0 dB and cancels the pending open, like a mixer's centre
 * detent, so the virtual knob never appears. While edit mode is on, the view ignores every
 * screen intent, because the touch adapter's two-finger VU arrives directly.
 *
 * The block is a screen child, created after the peek, so it is chrome like the
 * transport drawer. It is hidden at rest, so the entry view is the untouched full-scale
 * cassette; the view shows it together with the transport drawer, when the cassette is
 * already at the
 * 134/256 control pose that leaves the right column room. The block slides in from the
 * right edge over the drawer's 260 ms and slides back out when the drawer leaves.
 */
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the block on `parent` (the screen), hidden. Call once, after the Levels and the
 * drawer. */
void cassette_eq_create(lv_obj_t *parent);

/* Show or hide the block with the transport drawer, sliding from the right edge. Hiding
 * it also leaves edit mode: a hidden block must not own the overlay. */
void cassette_eq_set_shown(bool shown);

/* Keep the block above the Level sheets after one of them raises itself. A hidden block
 * does nothing. */
void cassette_eq_raise(void);

/* Paint the page's style into the block: the six cells' opaque fill (k_col_white or
 * k_col_bg, so the tape does not show through the block), and on the white page the four
 * knobs' and M's and R's circles as domes - a flat disc with the two shadows, no ring - by
 * way of cassette_circle_face(); their letters go k_col_bg (black). The dark page keeps the
 * line-art circles and the playing red. */
void cassette_eq_set_page(bool white);

/* True while a knob is in edit mode. The overlay owns the touch; the view must drop every
 * screen intent, including the two-finger VU the touch adapter reports directly. */
bool cassette_eq_is_editing(void);

#ifdef __cplusplus
}
#endif
