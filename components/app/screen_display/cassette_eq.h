#pragma once
/*
 * cassette_eq - the DJ block on the right of the Cassette screen.
 *
 * Six 96 px cells in two touching columns, the same carcase as the transport's P/R/W:
 * the first column holds GAIN above the < and > transport arrows, the second the HI,
 * MED and LOW knobs. The four knobs are a 24-dot crown with a stick; 0 dB is at twelve
 * o'clock, the right side cuts and the left side boosts. The two arrows are the only
 * next/previous controls on the screen.
 *
 * A tap on a knob enters edit mode, which is modal: a transparent overlay covers the
 * whole screen, a horizontal drag moves the knob (right cuts, left boosts) and everything
 * else - the arrows included - is blocked until the active knob is tapped again. Tapping
 * another knob switches the edit to it. While edit mode is on, the view ignores every
 * screen intent, because the touch adapter's two-finger menu arrives directly.
 *
 * The block is a screen child, created after the Level sheets, so it is chrome like the
 * transport drawer: it stays above the sheets and the view raises it after a vertical
 * swipe. It is always visible; the cassette rests at the smaller pose that leaves it room.
 */
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the block on `parent` (the screen). Call once, after the Levels and the drawer. */
void cassette_eq_create(lv_obj_t *parent);

/* Keep the block above the Level sheets after one of them raises itself. */
void cassette_eq_raise(void);

/* True while a knob is in edit mode. The overlay owns the touch; the view must drop every
 * screen intent, including the two-finger menu the touch adapter reports directly. */
bool cassette_eq_is_editing(void);

#ifdef __cplusplus
}
#endif
