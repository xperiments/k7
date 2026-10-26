#pragma once
/*
 * cassette_eq - the DJ block on the right of the Cassette screen.
 *
 * Six 96 px cells in two touching columns, the same carcase as the left drawer's
 * buttons: the first column holds GAIN above M and R, the second the HI, MED and LOW
 * knobs. M and R are the drawer's buttons moved here - the same 64 px circle, Jura 36
 * letter, two pressed states and click pulse - with no action yet; each carries its own cap
 * too, and R is the one button whose white-page disc is not a grey (`0xAA2200`). Each knob is
 * a 44 px
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

#include "cassette_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The block's two text buttons, in the order they sit: M above R. */
typedef enum {
    CASSETTE_EQ_BUTTON_M = 0,
    CASSETTE_EQ_BUTTON_R,
    CASSETTE_EQ_BUTTON_COUNT
} cassette_eq_button_t;

/* Build the block on `parent` (the screen), hidden. Call once, after the Levels and the
 * drawer. On the dark page all of them - the four knobs and M and R - are the palette's playing
 * red line art. On the white page M's cap is k_col_bg (black) and R's k_col_button_r
 * (0xAA2200), and both letters on those caps are k_col_button_white. */
void cassette_eq_create(lv_obj_t *parent);

/* Show or hide the block with the transport drawer, sliding from the right edge. Hiding
 * it also leaves edit mode: a hidden block must not own the overlay. */
void cassette_eq_set_shown(bool shown);

/* Keep the block above the Level sheets after one of them raises itself. A hidden block
 * does nothing. */
void cassette_eq_raise(void);

/* Paint the page's style into the block: the six cells' opaque fill (k_col_white or
 * k_col_bg, so the tape does not show through the block), and on the white page the four
 * knobs' and M's and R's circles as caps - a flat disc with the two shadows, by way of
 * cassette_circle_face(): the knobs in k_col_knob_cap (much darker, so a knob does not read as
 * a button, and with no rim), M in k_col_bg (black), R in its own red, with M's and R's 4 px
 * rim in k_col_dome's original grey (CASSETTE_CAP_RIM_W) so each reads as a disc let into it.
 * M's and R's letters on the white page are k_col_button_white, and the four knobs' cells are
 * the knob plate here: filled k_col_knob_rect, bordered k_col_bg. The dark page is the palette's
 * line art throughout - the playing red rings and letters, every cell filled k_col_bg and
 * bordered with the drawer's ink - whatever the caps say. */
void cassette_eq_set_page(bool white);

/* Give M or R - both of its states - its own cap, like a drawer button's: the white page's
 * disc and the letter on it. The dark page ignores it and stays the palette's line art. */
void cassette_eq_set_cap(cassette_eq_button_t id, cassette_cap_t cap);

/* True while a knob is in edit mode. The overlay owns the touch; the view must drop every
 * screen intent, including the two-finger VU the touch adapter reports directly. */
bool cassette_eq_is_editing(void);

#ifdef __cplusplus
}
#endif
