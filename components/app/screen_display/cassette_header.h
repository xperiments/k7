#pragma once
/*
 * cassette_header - the band across the top of the controls screen.
 *
 * The controls screen is the transport drawer on the left and the DJ block on the right,
 * both of which start at y=76. This is the frame that closes the screen across the top:
 * 768x71 at (16,6), its bottom edge on the buttons' and the cells' top line and its 16 px
 * side margins on the cassette's outer frame inset.
 *
 * It is line art like the carcases it joins: a 2 px border rounded by 8 px - the drawer
 * buttons' corner - in the drawer's carcase ink, over an opaque fill in the page's colour
 * (k_col_white on the white page, k_col_bg on the dark one), so the band covers what it
 * slides over instead of letting the picture show through it. It moves with the rest of the
 * controls chrome: a 260 ms ease-out slide down from just above the panel when the controls
 * come out, and back up, hidden at the end, when they go away. It is on the white page the
 * down swipe paints and on the dark page the horizontal swipes keep.
 *
 * The band carries the K7 mark: assets/labels/k7b.png, a 75x40 A8 mask with its horizontal
 * centre on the drawer's button column (screen x=64) and its vertical centre on the band's,
 * so it sits at (27,21) above the P button. The mask is tinted with the page's ink -
 * k_col_primary on the dark page, k_col_bg on the white one - by the same set_page() call
 * that repaints the band, and it slides and hides with it.
 *
 * The Menu also puts the list's title here: Jura 36 at band x=104 - 18 px clear of the
 * mark - vertically centred and ellipsized at 648 px. It takes the page's label ink
 * (k_col_bg on the white page, k_col_primary on the dark one), is empty and hidden outside
 * the Menu, and its text is copied from the caller.
 *
 * Private to screen_display; the view decides when the controls are out and calls
 * set_shown(). Built and written under the LVGL lock, like everything else on the screen.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the band as a child of the screen, at its hidden position above the panel. */
void cassette_header_create(lv_obj_t *screen);

/* Slide it in (true) or out (false) with the controls. A call while the other slide is
 * running reverses from the current position; a call that names the state already held does
 * nothing. The band is hidden once its slide out completes. */
void cassette_header_set_shown(bool shown);

/* Paint the opaque fill for the page behind the band: k_col_white while the controls hold
 * the white page, k_col_bg on the dark screen. The view calls it from its page flip. The
 * Menu's title, if it has one, is repainted with the page's ink in the same call. */
void cassette_header_set_page(bool white);

/* The Menu's list title, or NULL to hide it. The text is copied - the caller's buffer may
 * be reused - into a fixed buffer and ellipsized at the band's 648 px. It is empty and
 * hidden outside the Menu. A call before the band is built is ignored. */
void cassette_header_set_title(const char *title);

#ifdef __cplusplus
}
#endif
