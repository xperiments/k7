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
 * A tap on a knob enters edit mode, which is modal: a transparent overlay covers the
 * whole screen, a horizontal drag moves the knob (right cuts, left boosts) and everything
 * else - the arrows included - is blocked until the active knob is tapped again. Tapping
 * another knob switches the edit to it. While edit mode is on, the view ignores every
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
