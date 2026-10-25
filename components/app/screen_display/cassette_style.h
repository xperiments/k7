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

extern const lv_color_t k_col_black;
extern const lv_color_t k_col_bg;

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

/* The DJ block on the right: the unlit crown, the lit arc and the stick, hub and 0 dB
 * tick. The carcase and the circle reuse the primary red. */
extern const lv_color_t k_col_eq_dim;
extern const lv_color_t k_col_eq_lit;
extern const lv_color_t k_col_eq_tick;

/* The two rounded rectangles the cassette body is drawn on: the board's outer and inner
 * frames. */
extern const lv_color_t k_col_frame;

/* The touch board's own Levels: its cover, its Label and the Artwork backdrop. They
 * are defined for both boards; a board without those Levels never reads them. */
extern const lv_color_t k_col_back;
extern const lv_color_t k_col_cover_bg;
extern const lv_color_t k_col_cover_ink;
extern const lv_color_t k_col_art_bg;
extern const lv_color_t k_col_paper;
extern const lv_color_t k_col_primary;

/* The left drawer's line art: the carcase rectangle and the inner circle are coloured
 * separately. */
extern const lv_color_t k_col_button_rect;
extern const lv_color_t k_col_button_circle;
