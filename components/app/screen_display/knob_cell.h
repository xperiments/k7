#pragma once
/*
 * knob_cell - one small knob inside one of the bar's 96x96 carcases.
 *
 * The idiom both knob panels share, built out of the bar's cell: two circles (the transparent
 * ambient one that carries the broad shadow and the 44 px disc inside it, 30% smaller than a
 * button's), a 24-dot crown outside the disc at r=32, and a 4 px line from the centre towards the
 * rim. The value, the dB or unit law, the crown's lit run and the neutral are the model's
 * (knob_model); the 3x surface is knob_editor's; this module is the object set at 1x and its two
 * inks.
 *
 * The white page is the plate: the cell fills k_col_knob_rect with a k_col_bg border, so no
 * carcase line runs round a knob, and the disc becomes the cap - a flat k_col_knob_cap face with
 * a soft shadow and no rim. The dark page is the palette's line art: the cell keeps the bar's
 * fill and border, the disc is a red ring and the crown and the line keep their inks.
 *
 * Private to screen_display. Every object is built under the LVGL lock, on the bar's cell.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "knob.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The crown's dot count, which the model's lit-run maths needs. */
#define KNOB_CELL_DOTS 24

typedef struct {
    lv_obj_t *cell;                         /* the bar's carcase; NULL until created */
    lv_obj_t *ambient;                      /* the transparent circle carrying the broad shadow */
    lv_obj_t *disc;                         /* the 44 px circle: cap face, ring, the blink */
    lv_obj_t *dot[KNOB_CELL_DOTS];
    lv_obj_t *stick;                        /* the line, pivoted at the disc's centre */
} knob_cell_t;

/* Build the knob inside `cell` and paint it for `k`'s current value. False and nothing built if
 * `cell` is NULL. */
bool knob_cell_create(knob_cell_t *out, lv_obj_t *cell);

/* Repaint the crown and the line after the value moved. */
void knob_cell_apply(knob_cell_t *c, const knob_t *k);

/* The page's ink: the plate under the knob and the cap, or the dark page's line art. */
void knob_cell_set_page(knob_cell_t *c, bool white);

/* The disc, for a host that blinks the edited knob. NULL before create(). */
lv_obj_t *knob_cell_disc(const knob_cell_t *c);

#ifdef __cplusplus
}
#endif
