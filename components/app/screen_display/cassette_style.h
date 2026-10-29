#pragma once
/*
 * The Cassette screen's palette, read off the reference, as 0xRRGGBB.
 *
 * lv_color_hex() is a function (lv_color.h:283), so it cannot initialise a constant.
 * COL_HEX() is the same split done at compile time on top of LVGL's own aggregate
 * initialiser (lv_color.h:215); the result is a real lv_color_t, so no conversion
 * runs where a colour is used. COL_DARKEN_HEX() folds a shade at compile time:
 * COL_DARKEN_HEX(COL_PRIMARY, 70) is 0xA83F17. It takes a literal, not a name,
 * because the channels of an lv_color_t are not a constant expression once it is an
 * object; to darken a colour that is already an lv_color_t, LVGL has
 * lv_color_darken().
 *
 * The values are extern constants, defined once in cassette_style.c, so the view,
 * the tapes, the meter and the Levels all read the same palette.
 */
#include "lvgl.h"

#define COL_HEX(c) LV_COLOR_MAKE(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

#define COL_DARKEN_HEX(c, pct)                                                        \
    COL_HEX((((((c) >> 16) & 0xFF) * (pct) / 100) << 16) |                            \
            (((((c) >> 8) & 0xFF) * (pct) / 100) << 8) |                              \
             ((((c) & 0xFF) * (pct) / 100)))

#define COL_BLACK   0x111111
#define COL_PRIMARY 0xF15A22
#define COL_WHITE   0x909090

extern const lv_color_t k_col_bg;

/* The controls view's page: the whole backdrop, and the fills that read as holes in the
 * cassette, turn white while the two bars are out. The line art
 * keeps its inks on top of it. */
extern const lv_color_t k_col_white;

/* A muted glass tone, 40 % of white: the controls page's edge mark is drawn with it. */
extern const lv_color_t k_col_glass;

/* The playing red: the hub's ring and divider, and the peek's ink. */
extern const lv_color_t k_col_tape_dark;

/* The tape's own body, and the dim end of the rewind fade: the hub ring cross-fades
 * from the playing red into this. */
extern const lv_color_t k_col_tape;
extern const lv_color_t k_col_hub_ring;
extern const lv_color_t k_col_hub_ink;

/* The VU meter: the lit fill (85% of the playing red), the playing red of the L/R badge,
 * the dim track, and the row background behind them. There is no panel behind them. */
extern const lv_color_t k_col_meter_dim;
extern const lv_color_t k_col_meter_bar;
extern const lv_color_t k_col_meter_lit;
extern const lv_color_t k_col_meter_back;

/* The meter's own screen on the white page: a k_col_bg rectangle inset 3 px inside its plate, so
 * the dots read on the dark page's inks whatever the plate is. There is no second set of meter
 * colours - the window is the ink. */
extern const lv_color_t k_col_meter_window;

/* The knobs on the right: the unlit crown, the lit arc and the stick, hub and 0 dB
 * tick. The carcase and the circle reuse the primary red. */
extern const lv_color_t k_col_eq_dim;
extern const lv_color_t k_col_eq_lit;
extern const lv_color_t k_col_eq_tick;

/* The two rounded rectangles the cassette body is drawn on: the board's outer and inner
 * frames. */
extern const lv_color_t k_col_frame;

/* The touch board's own Levels: its cover, its Label and the Artwork backdrop. They
 * are defined for both boards; a board without those Levels never reads them, except
 * k_col_primary, which the header's K7 mark uses as its dark-page ink too. */
extern const lv_color_t k_col_back;
extern const lv_color_t k_col_cover_bg;
extern const lv_color_t k_col_cover_ink;
extern const lv_color_t k_col_art_bg;
extern const lv_color_t k_col_paper;
/* COL_PRIMARY itself: the line art's red, and the header mark's ink on the dark page. */
extern const lv_color_t k_col_primary;

/* The left drawer's line art: the carcase rectangle, the inner disc and the letter inside it
 * are coloured separately, so a control's inks are its spec data (cassette_cap_t) and the
 * page turns them. These two are the dark page's line art, the same for the mode bar's
 * three buttons; the white page's cap defaults to
 * k_col_dome and its letter to k_col_bg, further down. */
extern const lv_color_t k_col_button_rect;

/* The selected mode button's cap on the white page, 0xBBBBBB: the idle cap is k_col_dome, so a
 * lit mode reads as a lighter disc on paper. The dark page's latch is k_col_primary instead - it
 * draws no cap, only a filled line-art disc. */
extern const lv_color_t k_col_button_cap;

extern const lv_color_t k_col_button_circle;
extern const lv_color_t k_col_button_letter;

/* The white page's idle cap: the neutral grey every button starts in. It is also, always, the
 * 4 px inset rim of that cap (CASSETTE_CAP_RIM_W), which is why a selected cap still shows the
 * original grey at its edge. */
extern const lv_color_t k_col_dome;

/* The light ink a letter takes when it sits on a coloured cap. k_col_white is 0x909090, which
 * is the page's own white but reads as grey over a saturated face; this one reads white. */
extern const lv_color_t k_col_button_white;

/* The four EQ knobs' cap on the white page: much darker than the buttons' k_col_dome, so a
 * knob does not read as a button. The knobs have no rim and no ink of their own. */
extern const lv_color_t k_col_knob_cap;

/* The four knobs' cell on the white page: the plate's fill, with k_col_bg for its border, so
 * no carcase line runs round a knob. M's and R's cells keep the page's fill and the drawer's
 * border. */
extern const lv_color_t k_col_knob_rect;

extern const lv_color_t k_col_logo_back;