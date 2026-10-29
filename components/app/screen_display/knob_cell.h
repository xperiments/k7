#pragma once
/*
 * knob_cell - one dial: the 44 px knob in a bar cell, and the 256 px one the editor opens.
 *
 * The same object set serves both sizes, because a dial is a dial: two circles (the
 * transparent ambient one that carries the broad shadow, and the cap inside it), a ring of
 * marks outside the cap, and the 4 px line that runs from the centre towards the rim. What
 * changes with the size is a row of numbers, so they are a value - `knob_dial_t` - and the
 * two instances are k_knob_dial_cell and k_knob_dial_big.
 *
 * The ring is one object with one draw callback, not a dot per graduation: three printed
 * marks (both ends, and the detent where a bipolar knob has one), the value marker on top of
 * them, and - on the big dial only - graduations every 22.5 degrees. Every line points at
 * the centre, which is the whole of the idiom: no dots anywhere (ADR 0027). The ring reads
 * the value through knob_angle_for_value(), so the marker and the printed marks cannot drift
 * apart, and a value move costs one invalidation and one rotation.
 *
 * The white page is the plate: the cell fills k_col_knob_rect with a k_col_bg border, so no
 * carcase line runs round a knob, and the cap becomes a flat k_col_knob_cap face with a soft
 * shadow and no rim. The dark page is the palette's line art: the cell keeps the bar's fill
 * and border, the cap is a red ring, and the ring and the line keep their inks on both pages.
 *
 * A stepped knob also has a bottom pillow for its selected value, which the panel places and
 * fills: on a selector the dial says which slot is chosen, at 1x where the pillow text can
 * be read (ADR 0027). Every cell knob also carries a **short name** in a small pillow inset
 * into the cell's top-left, in the VU meter's L/R font: one letter or an abbreviation, because
 * the cell is a badge and the 3x dial is where the names are spelled out. The pillow is page
 * ink - primary with black text on the art, black with white on the paper (ADR 0035). Nothing
 * here blinks any more: the editor's cap is a reading, and a gesture the user cannot see the
 * effect of is worse than none (ADR 0028).
 *
 * Private to screen_display. Every object is built under the LVGL lock.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "knob.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One dial's numbers. `box` is the square it occupies in its parent; the rest are measured
 * from that box's centre. A grad_len of 0 leaves the graduations out. */
typedef struct {
    int box;                       /* the dial's square */
    int disc_d, disc_inset;        /* the cap, and its top-left inside the box */
    int border;                    /* the cap's rim on the dark page, 2 px */
    int ring_r;                    /* the marks' outer radius */
    int mark_len, mark_w;          /* one printed mark: outward length, width */
    int value_len, value_w;        /* the value marker: the thickest line, round-capped */
    int grad_len, grad_w;          /* the 3x graduations; 0 for none */
    int stick_len, stick_w;        /* the line inside the cap, from the centre outwards */
} knob_dial_t;

/* The 96 px cell's dial: 44 px cap (30 % smaller than a button's), ring at r=32, no
 * graduations. */
extern const knob_dial_t k_knob_dial_cell;

/* The 3x dial: 256 px box, 118 px cap, ring at r=86, graduations every 22.5 degrees. */
extern const knob_dial_t k_knob_dial_big;

typedef struct {
    lv_obj_t *cell;          /* the object the dial belongs to; NULL until created */
    lv_obj_t *ambient;       /* the transparent circle carrying the broad shadow */
    lv_obj_t *disc;          /* the cap: its face and its ring */
    lv_obj_t *ring;          /* the marks, the value marker and any graduations */
    lv_obj_t *stick;         /* the line, pivoted at the cap's centre */
    lv_obj_t *name_pill;     /* the knob's short name, at the cell's top-left; NULL if none */
    lv_obj_t *name_text;
    lv_obj_t *value_pill;    /* a stepped knob's selected value; NULL until asked for */
    lv_obj_t *value_text;
    const knob_t *k;         /* what the ring and the line draw; NULL until apply */
    knob_dial_t dial;
} knob_cell_t;

/* Where a cell's dial box starts, below the name pillow: far enough that the ring's top mark -
 * 16 px inside the box - clears the pillow's row and its inset. A dial built by knob_cell_create()
 * starts here, and so does a taller plate's own dial. */
#define KNOB_CELL_DIAL_Y 10

/* The name pillow's inset from the cell's top and left edges: the pillow is a badge, not a tab,
 * so its rounded corner stays off the cell's line. Tuned on the panel, with the dial's row. */
#define KNOB_NAME_PILL_INSET 10

/* Build the cell's dial in a bar cell: the box starts KNOB_CELL_DIAL_Y below the cell's top,
 * because the name pillow owns the first row. False and nothing built if `cell` is NULL. */
bool knob_cell_create(knob_cell_t *out, lv_obj_t *cell);

/* The same, at any size and place: the box's top-left corner in `parent`'s coordinates. A
 * cell that is taller than it is wide - the effect selector's 96x190 plate - puts the dial
 * in its upper part by passing a box origin above the plate's middle. */
bool knob_cell_create_at(knob_cell_t *out, lv_obj_t *parent, const knob_dial_t *dial,
                         int x, int y);

/* Repaint the ring and turn the line after the value moved, and remember the knob to draw. */
void knob_cell_apply(knob_cell_t *c, const knob_t *k);

/* The page's ink: the plate under the knob and the cap, or the dark page's line art, and the two
 * inks of the name pillow. */
void knob_cell_set_page(knob_cell_t *c, bool white);

/* Add the name pillow: a 14 px round rect inset KNOB_NAME_PILL_INSET into the cell's top-left
 * corner, carrying `text` in the meter's L/R font and sized to it. The panel passes the knob's
 * short name - a letter per cell - because the 3x dial is where the names are spelled out
 * (ADR 0035). Placed by the panel, like the value pillow; NULL or "" leaves it hidden. */
void knob_cell_add_name_pill(knob_cell_t *c, const char *text);

/* Add the bottom pillow a stepped knob shows its selected value in, centred on `cx` with its
 * top at `y` in the cell's coordinates. A range knob never calls this. */
void knob_cell_add_value_pill(knob_cell_t *c, int cx, int y);

/* Set that pillow's text, or NULL to hide it. */
void knob_cell_set_value(knob_cell_t *c, const char *text);

#ifdef __cplusplus
}
#endif
