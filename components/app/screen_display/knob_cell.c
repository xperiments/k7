/*
 * One dial, at 1x or 3x; see knob_cell.h.
 *
 * The ring is one object and one draw callback: three printed marks, the value marker and -
 * on the big dial - thirteen graduations, all drawn as short lines pointing at the centre.
 * That is why there is no dot array here any more: a value move invalidates one object and
 * writes one rotation, where it used to write twenty-four colours. The geometry is the
 * knob_dial_t the caller passed, so the 44 px cell and the 256 px dial are the same code.
 *
 * A cell knob has one object the 3x dial has not: the name pillow in its top-left corner.
 * The dial's box starts KNOB_CELL_DIAL_Y below the cell's top, so that row is the pillow's.
 */
#include "knob_cell.h"

#include <math.h>
#include <stdint.h>

#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(jura_20);

#define PI_F 3.14159265358979323846f

/* The value pillow: a stepped knob's selected slot, under the dial, black on the primary fill
 * like every pillow the knobs print. Sized for the effect selector's longest name (BREATHE,
 * RAINBOW) at the 20 px font. */
#define PILL_W 84
#define PILL_H 24

/* The name pillow: the knob's short name in the meter's own L/R font, in a 14 px round rect
 * inset KNOB_NAME_PILL_INSET into the cell's top-left. The dial's box starts KNOB_CELL_DIAL_Y
 * below the cell's top, and the ring's top mark is 16 px inside that box, so the pillow's row
 * and the dial's ink do not touch. NAME_PAD is the pillow's breathing room either side of its
 * text. */
#define NAME_H   14
#define NAME_PAD 12

/* The graduations over the 270 degree arc. A continuous knob gets one every 22.5 degrees, both
 * ends included, so thirteen. A stepped knob gets one per **slot**, because a tick that does not
 * sit on a position is a tick that lies: six steps are six ticks 54 degrees apart, and the value
 * marker lands on one of them every time (ADR 0031). */
#define GRAD_STEP_DDEG 225
#define GRAD_COUNT     13
#define ARC_DDEG       2700

const knob_dial_t k_knob_dial_cell = {
    .box = CASSETTE_CELL_W,
    .disc_d = 44,
    .disc_inset = 26,
    .border = 2,
    .ring_r = 32,
    .mark_len = 6,
    .mark_w = 2,
    .value_len = 6,
    .value_w = 4,
    .grad_len = 0,
    .grad_w = 0,
    .stick_len = 16,
    .stick_w = 4,
};

const knob_dial_t k_knob_dial_big = {
    .box = 256,
    .disc_d = 118,
    .disc_inset = 69,
    .border = 2,
    .ring_r = 86,
    .mark_len = 8,
    .mark_w = 2,
    .value_len = 9,
    .value_w = 5,
    .grad_len = 6,
    .grad_w = 2,
    .stick_len = 41,
    .stick_w = 4,
};

/* ---------------------------------------------------------------------- the ring */

/* One radial line: from `r0` to `r1` at `angle` tenths of a degree clockwise from twelve. */
static void radial(lv_layer_t *layer, int cx, int cy, int r0, int r1, int angle, int w,
                   lv_color_t colour, bool round)
{
    float rad = (float)angle / 10.0f * PI_F / 180.0f;
    float s = sinf(rad);
    float c = cosf(rad);

    lv_point_precise_t pts[2];
    pts[0].x = cx + (int)lroundf((float)r0 * s);
    pts[0].y = cy - (int)lroundf((float)r0 * c);
    pts[1].x = cx + (int)lroundf((float)r1 * s);
    pts[1].y = cy - (int)lroundf((float)r1 * c);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = colour;
    dsc.width = w;
    dsc.opa = LV_OPA_COVER;
    dsc.points = pts;
    dsc.point_cnt = 2;
    dsc.round_start = round;
    dsc.round_end = round;
    lv_draw_line(layer, &dsc);
}

static void ring_draw_cb(lv_event_t *e)
{
    knob_cell_t *c = lv_event_get_user_data(e);
    if (!c || !c->k) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(c->ring, &area);

    const knob_dial_t *d = &c->dial;
    int cx = area.x1 + d->box / 2;
    int cy = area.y1 + d->box / 2;

    if (d->grad_len > 0) {
        int count = GRAD_COUNT;
        int step = GRAD_STEP_DDEG;
        if (c->k->sweep == KNOB_SWEEP_STEPPED && c->k->steps >= 2) {
            count = c->k->steps;
            step = ARC_DDEG / (count - 1);
        }
        for (int i = 0; i < count; i++) {
            radial(layer, cx, cy, d->ring_r - d->grad_len, d->ring_r,
                   knob_angle_for_value(KNOB_MIN) + i * step, d->grad_w, k_col_eq_lit, false);
        }
    }

    /* Both ends, and the detent only where 0 dB lives: a level knob's twelve o'clock is
     * nobody's home, and a stepped knob's is one slot among several. */
    radial(layer, cx, cy, d->ring_r - d->mark_len, d->ring_r, knob_angle_for_value(KNOB_MIN),
           d->mark_w, k_col_eq_tick, false);
    radial(layer, cx, cy, d->ring_r - d->mark_len, d->ring_r, knob_angle_for_value(KNOB_MAX),
           d->mark_w, k_col_eq_tick, false);
    if (c->k->sweep == KNOB_SWEEP_BIPOLAR) {
        radial(layer, cx, cy, d->ring_r - d->mark_len, d->ring_r,
               knob_angle_for_value(KNOB_DETENT), d->mark_w, k_col_eq_tick, false);
    }

    /* The value marker last, in the tick's own ink: the ends and the detent are printed in it
     * too, so what makes this line the value is its width and its round cap - it is as long as
     * the marks are (ADR 0036). Drawn on top, which is also why a knob resting on a mark reads as
     * one heavier stroke there. */
    radial(layer, cx, cy, d->ring_r - d->value_len, d->ring_r, knob_angle_ddeg(c->k),
           d->value_w, k_col_eq_tick, true);
}

/* ------------------------------------------------------------------ construction */

bool knob_cell_create(knob_cell_t *out, lv_obj_t *cell)
{
    return knob_cell_create_at(out, cell, &k_knob_dial_cell, 0, KNOB_CELL_DIAL_Y);
}

bool knob_cell_create_at(knob_cell_t *out, lv_obj_t *parent, const knob_dial_t *dial,
                         int x, int y)
{
    if (!out || !parent || !dial) return false;

    out->cell = parent;
    out->k = NULL;
    out->name_pill = NULL;
    out->name_text = NULL;
    out->value_pill = NULL;
    out->value_text = NULL;
    out->dial = *dial;

    const knob_dial_t *d = &out->dial;
    int cx = x + d->box / 2;
    int cy = y + d->box / 2;

    /* Two circles, like the buttons: the outer one carries the broad shadow and the inner
     * one is the cap with the contact shadow and the ring. */
    out->ambient = cassette_border_box(parent, cx - d->disc_d / 2, cy - d->disc_d / 2,
                                       d->disc_d, d->disc_d, d->disc_d / 2,
                                       k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    out->disc = cassette_border_box(out->ambient, 0, 0, d->disc_d, d->disc_d,
                                    d->disc_d / 2, k_col_bg, LV_OPA_TRANSP,
                                    k_col_button_circle, d->border);

    /* The ring is its own object, the box's size, so one invalidation repaints it all. */
    out->ring = cassette_border_box(parent, x, y, d->box, d->box, 0,
                                    k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    lv_obj_add_event_cb(out->ring, ring_draw_cb, LV_EVENT_DRAW_MAIN_END, out);

    /* The line is inside the cap: from its centre to the rim, its pivot at the centre, so it
     * swings like a knob's line and stops at the circle's edge. */
    out->stick = cassette_box(parent, cx - d->stick_w / 2, cy - d->stick_len,
                              d->stick_w, d->stick_len, d->stick_w / 2, k_col_eq_tick);
    lv_obj_set_style_transform_pivot_x(out->stick, d->stick_w / 2, 0);
    lv_obj_set_style_transform_pivot_y(out->stick, d->stick_len, 0);

    return true;
}

/* A name's width in the pillow's font, glyph by glyph: the pillow is sized to its text, so it
 * has to be measured rather than given a box wide enough for the longest knob. */
static int name_width(const char *text)
{
    int w = 0;
    for (const char *c = text; *c; c++)
        w += lv_font_get_glyph_width(&lv_font_montserrat_14, (uint32_t)(unsigned char)*c,
                                     (uint32_t)(unsigned char)c[1]);
    return w + NAME_PAD;
}

void knob_cell_apply(knob_cell_t *c, const knob_t *k)
{
    if (!c || !c->disc || !k) return;
    c->k = k;
    if (c->stick) lv_obj_set_style_transform_rotation(c->stick, knob_angle_ddeg(k), 0);
    if (c->ring) lv_obj_invalidate(c->ring);
}

void knob_cell_set_page(knob_cell_t *c, bool white)
{
    if (!c || !c->cell) return;

    /* The cell becomes the plate on the white page: a fill and a dark border, so the
     * carcase's line does not run round the knob. */
    lv_obj_set_style_bg_color(c->cell, white ? k_col_knob_rect : k_col_bg, 0);
    lv_obj_set_style_border_color(c->cell, white ? k_col_bg : k_col_button_rect, 0);

    /* The name pillow turns over with the page: primary with the dark page's ink on the line
     * art, and black with the page's white on paper - so it is the one badge that reads on
     * both, and the knob's identity never changes weight between the two rungs. */
    if (c->name_pill) {
        lv_obj_set_style_bg_color(c->name_pill, white ? k_col_bg : k_col_primary, 0);
        if (c->name_text)
            lv_obj_set_style_text_color(c->name_text,
                                        white ? k_col_button_white : k_col_bg, 0);
    }

    if (!c->disc) return;

    cassette_circle_face(c->ambient, c->disc, white, k_col_knob_cap);
    lv_obj_set_style_border_width(c->disc, white ? 0 : c->dial.border, 0);
    if (!white) lv_obj_set_style_border_color(c->disc, k_col_button_circle, 0);
}

/* ---------------------------------------------------------------- the name pillow */

void knob_cell_add_name_pill(knob_cell_t *c, const char *text)
{
    if (!c || !c->cell || c->name_pill) return;
    c->name_pill = cassette_box(c->cell, KNOB_NAME_PILL_INSET, KNOB_NAME_PILL_INSET,
                                NAME_H, NAME_H, 3, k_col_primary);
    c->name_text = cassette_text(c->name_pill, &lv_font_montserrat_14, k_col_bg,
                                 LV_ALIGN_CENTER, 0, 0);
    if (text && *text) {
        lv_obj_set_size(c->name_pill, name_width(text), NAME_H);
        lv_label_set_text(c->name_text, text);
    } else {
        lv_obj_set_hidden(c->name_pill, true);
    }
}

/* ---------------------------------------------------------------- the value pillow */

void knob_cell_add_value_pill(knob_cell_t *c, int cx, int y)
{
    if (!c || !c->cell || c->value_pill) return;
    c->value_pill = cassette_box(c->cell, cx - PILL_W / 2, y, PILL_W, PILL_H, PILL_H / 2,
                                 k_col_primary);
    c->value_text = cassette_text(c->value_pill, &jura_20, k_col_bg,
                                  LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(c->value_pill, true);
}

void knob_cell_set_value(knob_cell_t *c, const char *text)
{
    if (!c || !c->value_pill) return;
    lv_obj_set_hidden(c->value_pill, !text);
    if (text) lv_label_set_text(c->value_text, text);
}
