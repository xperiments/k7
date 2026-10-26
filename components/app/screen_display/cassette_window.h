#pragma once
/*
 * cassette_window - the frame around the cassette on the controls screen.
 *
 * The header and the footer close the controls screen across the top and the bottom; this
 * is the window between the drawer and the DJ block: 470x284 at (110,76), its top and
 * bottom edges on the same lines as the bands and its sides in the gaps either side of the
 * scaled tape, so the three rectangles make one frame around the picture.
 *
 * It is line art like the carcases it joins: a 2 px border rounded by 8 px - the drawer
 * buttons' corner - in the drawer's carcase ink. It does not slide: it has no edge to come
 * in from, being a window rather than a band, and sweeping it across the tape would read as
 * a mistake. It fades in and out over the drawer's 260 ms instead, on the white page the
 * down swipe paints and on the dark page the horizontal swipes keep.
 *
 * It is the screen's first child, built before the cassette's own container, so the tape is
 * drawn over it: while the pose shrinks the cassette into the window the tape covers the
 * border and reveals it as it goes, and nothing in the window can paint over the picture.
 *
 * The Menu borrows the window as its page: while it is up the window's own fill is the
 * page's colour, because the Menu's Rows are drawn inside it, and set_menu_page(false)
 * puts the cassette's ground back. The border, the corner and the fade state are the
 * cassette's and are never written by the Menu. The rect is exposed so the Menu can place
 * its page without a second copy of the geometry.
 *
 * Private to screen_display; the view decides when the controls are out and calls
 * set_shown(). Built and written under the LVGL lock, like everything else on the screen.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the window as the screen's first child - under the cassette - transparent and
 * hidden. */
void cassette_window_create(lv_obj_t *screen);

/* Fade it in (true) or out (false) with the controls. A call while the other fade is
 * running reverses from the current opacity; a call that names the state already held does
 * nothing. The window is hidden once its fade out completes. */
void cassette_window_set_shown(bool shown);

/* The Menu's page: while it is up the window's fill is the page's colour - k_col_white on
 * the white page, k_col_bg on the dark one - because the Menu's Rows are drawn inside it.
 * `on` false restores k_col_bg, the cassette's own ground. Only the fill is written: the
 * 2 px border, the 8 px corner and the fade/shown state are untouched. */
void cassette_window_set_menu_page(bool on, bool white);

/* The window's geometry in screen coordinates - (110,76) and 470x284 - so the Menu does
 * not keep a second copy of it. Any pointer may be NULL. */
void cassette_window_rect(int *x, int *y, int *w, int *h);

#ifdef __cplusplus
}
#endif
