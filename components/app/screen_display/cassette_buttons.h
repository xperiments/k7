#pragma once
/*
 * cassette_buttons - the Transport panel: the three buttons at the screen's left edge.
 *
 * The panel is the left **bar**'s content (cassette_bars). Three 96x96 carcases down the
 * left edge on the 94 px pitch that the right bar's cells share, at x=16 and y=76/170/264:
 * the bar builds them, slides them in and out over 260 ms and paints the page into them;
 * this module builds one button inside each and owns nothing of the strip. There is no 120 px
 * panel object: the three cells are the panel, so the margin the old panel absorbed is gone
 * and the cells are the clickable roots.
 *
 * Each button is line art: a 64 px circle at the carcase's centre and the character P, < or >
 * centred in that circle in Jura 36. The three are the shared button module, cassette_button:
 * this file is their table - each button's label, its cap and the action its click runs - and
 * the panel value the bar fills. Either horizontal swipe brings the controls in from the
 * entry, and while they are out either puts them away; a down swipe brings them in as well
 * and an up swipe puts them away - the vertical ladder, which is the Controls model's. This
 * module knows none of that.
 *
 * One pressed state, restyled: a press writes the carcase's ring (and, on the dark page, the
 * disc's), RELEASED or PRESS_LOST writes it back, and a click is a 160 ms scale pulse plus the
 * button's action. < calls mini_icecast_prev(), > calls mini_icecast_skip(); P is still a
 * placeholder. The tap guard is the shared one: a touch with a gesture direction, one that
 * moved past the press threshold, or the adapter's two-finger VU swipe is not a tap and does
 * nothing. The carcases bubble gestures, so a swipe that starts on a button still reaches
 * screen_input, and they bubble events, so a press clears the two-finger latch.
 *
 * While the Menu is up the three buttons change meaning: P closes it, < is Back and > is
 * Enter, and their Jura characters become the icon font's cassette-tape and circled left and
 * right. This file keeps the Menu's three callbacks and applies the mode as a table: label,
 * font and action are rewritten; the press look, the pulse, the caps and the page ink are the
 * shared button's and do not change. The transport clicks are suspended for the Menu's
 * lifetime; set_menu_mode(false) gives the characters and the transport back.
 *
 * Every button carries its own cap - the face and the letter of the disc on the white page -
 * so the three can differ from each other, and the panel starts all three in the palette's
 * neutral grey and black. The cap is the button's spec data; there is no runtime set_cap
 * here. The dark page is not the caps': it is the palette's line art, the playing red ring
 * and letter, the same for every button, and the rectangle keeps the palette's carcase ink on
 * both pages.
 *
 * Private to screen_display. create() installs the panel value with the left bar, so it runs
 * after cassette_bars_create() and before the first set_shown(). Every object is built under
 * the LVGL lock.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Menu's three actions: `close` (P), `back` (<) and `enter` (>). Called from the
 * button's click handler after the tap guard has passed and the pulse has been started, in
 * place of the transport action, under the LVGL lock. `ctx` is the value given to
 * set_menu_mode(). */
typedef void (*cassette_buttons_cb_t)(void *ctx);

/* The three buttons, in the order they are stacked in the panel - the character each one
 * shows, which is also the action it runs. */
typedef enum {
    CASSETTE_BUTTON_P = 0,
    CASSETTE_BUTTON_PREV,
    CASSETTE_BUTTON_SKIP,
    CASSETTE_BUTTON_COUNT
} cassette_button_id_t;

/* Give the left bar its Transport panel: three cells, one button each, built into the
 * carcases the bar makes. Every button starts with the palette's default cap: k_col_dome for
 * the face and k_col_bg for the letter on it. A second call is a no-op. */
void cassette_buttons_create(void);

/* The Menu's three meanings. While `on` is true, P shows CASSETTE_ICON_TAPE and calls
 * `close`, < shows CASSETTE_ICON_LEFT and calls `back`, and > shows CASSETTE_ICON_RIGHT and
 * calls `enter`, all in &xtape_36; the transport clicks are suspended. `on` false restores
 * the Jura 36 "P", "<" and ">" and the transport actions. The label and the action change
 * together; the press look, the pulse and the page ink are the shared button's and keep
 * working in both modes. A NULL callback does nothing. The mode is remembered if this is
 * called before create(); create() applies it to the cells it fills. */
void cassette_buttons_set_menu_mode(bool on, cassette_buttons_cb_t back,
                                    cassette_buttons_cb_t enter,
                                    cassette_buttons_cb_t close, void *ctx);

#ifdef __cplusplus
}
#endif
