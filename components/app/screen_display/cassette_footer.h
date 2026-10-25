#pragma once
/*
 * cassette_footer - the band that closes the bottom of the controls screen.
 *
 * The header closes the top; this is its mirror: 768 px wide at (16,359), 6 px short of
 * the panel's bottom, so its top edge lands on the last drawer button's and the last DJ
 * cell's bottom line and its sides sit on the cassette frame's 16 px inset, like the
 * header's.
 *
 * It is line art like the carcases it joins: a 2 px border rounded by 8 px - the drawer
 * buttons' corner - in the drawer's carcase ink, over an opaque fill in the page's colour
 * (k_col_white on the white page, k_col_bg on the dark one), so the band covers what it
 * slides over instead of letting the picture show through it. It moves with the rest of the
 * controls chrome: a 260 ms ease-out slide up from below the panel when the controls come
 * out, and back down, hidden at the end, when they go away. It is on the white page the down
 * swipe paints and on the dark page the horizontal swipes keep.
 *
 * Private to screen_display; the view decides when the controls are out and calls
 * set_shown(). Built and written under the LVGL lock, like everything else on the screen.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the band as a child of the screen, at its hidden position below the panel. */
void cassette_footer_create(lv_obj_t *screen);

/* Slide it in (true) or out (false) with the controls. A call while the other slide is
 * running reverses from the current position; a call that names the state already held does
 * nothing. The band is hidden once its slide out completes. */
void cassette_footer_set_shown(bool shown);

/* Paint the opaque fill for the page behind the band: k_col_white while the controls hold
 * the white page, k_col_bg on the dark screen. The view calls it from its page flip. */
void cassette_footer_set_page(bool white);

#ifdef __cplusplus
}
#endif
