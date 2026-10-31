#pragma once
/*
 * cassette_buttons - the mode bar: the screen's left edge, three buttons.
 *
 * The left **bar**'s content (cassette_bars): three 96x96 carcases down the left edge on the
 * 94 px pitch, at x=16 and y=76/170/264. The bar builds them, slides them in and out over
 * 260 ms and paints the page into them; this module builds one button inside each and owns
 * nothing of the strip.
 *
 * The three are the screen's modes, top to bottom: the **cassette** opens and closes the Menu,
 * the **sliders-v** is the EQ group and the **led** is the RGB group. Each is a 64 px disc with
 * one glyph from the icon font in `xtape_36`, and each is a **toggle**: the group buttons
 * select their group or, tapped again, put the Home group back; the cassette's disc is lit
 * while the Menu is open. All three carry the shared button's latched ink, a k_col_primary disc
 * with a k_col_bg glyph.
 *
 * The buttons are momentary controls that *show* the mode; the view owns which mode is on and
 * calls set_on() to relatch. A button's click is a callback the view registers with set_cb(),
 * run after the shared tap guard and the 160 ms pulse; a click on a cell that is still arriving
 * is not a command (the same gate the panels use).
 *
 * Every button carries the palette's neutral cap for the white page and the palette's line art
 * on the dark one; the latch is the only thing that colours a disc.
 *
 * Private to screen_display. The module builds no object of its own: it hands the left bar one
 * panel value, and the bar builds a button into each carcase it makes. The view installs that
 * value with cassette_bars_set_panel() - the bar is not this module's to install - and calls
 * set_on() to relatch afterwards. Every object is built under the LVGL lock.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_bars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The three modes, in the order they are stacked in the bar. */
typedef enum {
    CASSETTE_MODE_TAPE = 0,   /* the Menu's door */
    CASSETTE_MODE_EQ,         /* the EQ group */
    CASSETTE_MODE_RGB,        /* the RGB group */
    CASSETTE_MODE_COUNT
} cassette_mode_t;

/* What a click means to the view. `ctx` is the value given with it. Called under the LVGL
 * lock, after the tap guard and the pulse. A NULL callback makes the button a pulse. */
typedef void (*cassette_buttons_cb_t)(void *ctx);

/* The left bar's content, for the composition to install: one column of three cells, one
 * button each, built into the carcases the bar makes. */
const cassette_panel_t *cassette_buttons_panel(void);

/* The action a mode's click runs, replaced as a pair. The mode is remembered if this is called
 * before the panel is installed; a click reads the table when it happens, so the order does not
 * matter. */
void cassette_buttons_set_cb(cassette_mode_t which, cassette_buttons_cb_t cb, void *ctx);

/* The latched ink: the mode's disc lit k_col_primary with a k_col_bg glyph, or back to the
 * idle look. The view sets the tape's while the Menu is open and the group's while it is the
 * active one. A call for the state already held does nothing; a mode whose cell does not exist
 * yet is remembered and applied when it is built. */
void cassette_buttons_set_on(cassette_mode_t which, bool on);

#ifdef __cplusplus
}
#endif
