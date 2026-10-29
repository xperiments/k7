#pragma once
/*
 * cassette_footer - the band that closes the bottom of the controls screen.
 *
 * The header closes the top; this is its mirror: 768 px wide at (16,358), 6 px short of
 * the panel's bottom, so its 2 px border lands on the last mode bar button's and the last
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
 * The footer shows the broadcast's **timeline**, and it is the Peek's, not a likeness of it: the
 * same 568x52 bar at the same (116,424), the same 20 px insets and time boxes, the same 6 px
 * track - all of it read from cassette_layout, so the two cannot drift - and it slides in the way
 * the Peek's bar slides. It is up whenever the chrome is, on both pages, not only in the Menu.
 *
 * The bar has three faces and turns them over every four seconds: where the broadcast is (the
 * times and the track), who it is (the artist - the Track's first directory), and what it is (the
 * song - the title, cleaned). A face with no words is skipped; with nothing playing the bar stays
 * on the times and prints --:-- (ADR 0033).
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
 * the white page, k_col_bg on the dark screen. The view calls it from its page flip. The
 * Menu's now playing, if it has one, is repainted with the page's ink in the same call. */
void cassette_footer_set_page(bool white);

/* What the timeline says: whether anything is playing, its elapsed and total milliseconds, and
 * the Track's title and path - the artist is the path's first directory and the song is the
 * cleaned title. `duration_ms` of 0 prints "--:--" and leaves the fill empty, which is what a
 * Track of unknown length looks like; the fill's fraction is computed here, so the caller does
 * not have to agree with this band about rounding. Call it every frame: it is a pair of
 * snprintfs until a value moves. A call before the band is built is ignored. */
void cassette_footer_set_playing(bool playing, uint32_t elapsed_ms, uint32_t duration_ms,
                                 const char *title, const char *path);

#ifdef __cplusplus
}
#endif
