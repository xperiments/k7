#pragma once
/*
 * cassette_eq - the DJ block on the right of the Cassette screen.
 *
 * Six 96 px cells in two touching columns, the same carcase as the transport's P/R/W:
 * the first column holds GAIN above the < and > transport arrows, the second the HI,
 * MED and LOW knobs. The arrows keep the buttons' 64 px circle. Each knob is a 44 px
 * circle (30% smaller), a 24-dot crown outside it at r=32, and a 6x22 line from the centre
 * to the rim. 0 dB is at twelve o'clock, the right side cuts and the left side boosts. The
 * two arrows are the only next/previous controls on the screen.
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
 * screen intent, because the touch adapter's two-finger menu arrives directly.
 *
 * The block is a screen child, created after the Level sheets, so it is chrome like the
 * transport drawer: it stays above the sheets and the view raises it after a vertical
 * swipe. It is hidden at rest, so the entry view is the untouched full-scale cassette; the
 * view shows it together with the transport drawer, when the cassette is already at the
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

/* True while a knob is in edit mode. The overlay owns the touch; the view must drop every
 * screen intent, including the two-finger menu the touch adapter reports directly. */
bool cassette_eq_is_editing(void);

#ifdef __cplusplus
}
#endif
