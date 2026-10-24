/*
 * The VU meter.
 *
 * The meter owns its objects and state - the transparent panel, the 24 segments, the
 * lit count of each column and the pose it is placed at - and shares none of it with
 * the view. The view supplies the parent and the pose. The lattice is the panel's draw
 * callback and works in screen coordinates, so the callback asks the panel for its own
 * origin; only the scale of its offsets is kept here, as the pose it was last placed
 * with.
 */
#include <math.h>

#include "lvgl.h"

#include "cassette_layout.h"
#include "cassette_meter.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

/* The VU meter: a lattice with one column of segments per channel filling from the
 * bottom, the mockup's geometry scaled 300/149 so the meter is 300 px tall. Segments are
 * 18 px wide, 8 tall, 10 apart, twelve to a column, the bottom one 209 px down the box;
 * the lattice is the sketch's 12 cells less two at each end, and there is no panel behind
 * either. The box is centred in the frame, so the grid lands on the frame's middle. */
#define METER_W      88      /* the 8-cell lattice plus a 4 px margin each side */
#define METER_H      300
#define METER_X      740     /* right edge on the frame's right edge */
#define METER_Y      196     /* the outer frame's centre: 16 + 360 / 2 */
#define METER_COLS   2        /* one bar per channel */
#define METER_CELLS  12
#define METER_SEG_W  18
#define METER_SEG_H  8
#define METER_PITCH  10
#define METER_BOTTOM 209      /* panel-relative y of the bottom segment's foot */
#define METER_GAP    12
#define METER_GRID_X 4        /* the lattice: 8 columns, 14 rows, 10 px apart, centred */
#define METER_GRID_Y 80
#define METER_GRID_C 8
#define METER_GRID_R 14

/* The pose the meter was last placed with, as a value. The draw callback scales the
 * lattice's offsets with it, the same way the view scales its own. */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };

#define SC(v)  cassette_len(s_pose, (v))             /* a design length, in pixels */
#define SCX(x) cassette_x(s_pose, (x))               /* a design x, in screen pixels */
#define SCY(y) cassette_y(s_pose, (y))               /* a design y, in screen pixels */

static lv_obj_t *s_meter;                          /* the panel; the grid is drawn in it */
static lv_obj_t *s_meter_seg[METER_COLS][METER_CELLS];
static int s_meter_lit[METER_COLS];                /* segments lit, so a redraw is rare */

/* The lattice behind the segments: one line per 10 px, the lit red at half strength,
 * which over black is the same value drawn opaque. */
static void meter_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    /* A draw callback works in screen coordinates, so the panel's origin is added to
     * the lattice's own offsets. */
    lv_area_t panel;
    lv_obj_get_coords(lv_event_get_target_obj(e), &panel);
    int x0 = panel.x1 + SC(METER_GRID_X), y0 = panel.y1 + SC(METER_GRID_Y);
    int pitch = SC(METER_PITCH);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = k_col_meter_back;
    dsc.width = 1;
    dsc.opa = LV_OPA_COVER;

    for (int c = 0; c <= METER_GRID_C; c++) {
        dsc.p1.x = x0 + c * pitch;
        dsc.p1.y = y0;
        dsc.p2.x = dsc.p1.x;
        dsc.p2.y = y0 + METER_GRID_R * pitch;
        lv_draw_line(layer, &dsc);
    }
    for (int r = 0; r <= METER_GRID_R; r++) {
        dsc.p1.x = x0;
        dsc.p1.y = y0 + r * pitch;
        dsc.p2.x = x0 + METER_GRID_C * pitch;
        dsc.p2.y = dsc.p1.y;
        lv_draw_line(layer, &dsc);
    }
}

/* A column's segments, lit from the bottom. The scale is dB over -48..0, as the old
 * meter used, so the bars move like a VU rather than following a linear amplitude. */
static void meter_level_set(int ch, float vu)
{
    if (!s_meter) return;

    float db = vu > 0.001f ? 20.0f * log10f(vu) : -48.0f;
    if (db < -48.0f) db = -48.0f;
    if (db > 0.0f) db = 0.0f;
    int lit = (int)lroundf((db + 48.0f) / 48.0f * METER_CELLS);

    if (lit == s_meter_lit[ch]) return;
    s_meter_lit[ch] = lit;
    for (int k = 0; k < METER_CELLS; k++) {
        lv_obj_set_style_bg_color(s_meter_seg[ch][k],
                                  k < lit ? k_col_meter_lit : k_col_meter_dim, 0);
    }
}

void cassette_meter_create(lv_obj_t *parent)
{
    /* The VU meter, top right. It is created before the hubs, so anything drawn later
     * sits over it; the lattice is its draw callback and the segments are objects, so a
     * level change repaints a few cells and nothing else. */
    s_meter = lv_obj_create(parent);
    lv_obj_remove_style_all(s_meter);
    lv_obj_set_style_bg_opa(s_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_meter, 0, 0);
    lv_obj_set_style_pad_all(s_meter, 0, 0);
    lv_obj_set_scrollable(s_meter, false);
    lv_obj_set_clickable(s_meter, false);
    lv_obj_add_event_cb(s_meter, meter_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    for (int ch = 0; ch < METER_COLS; ch++) {
        for (int k = 0; k < METER_CELLS; k++) {
            lv_obj_t *seg = cassette_box(s_meter, 0, 0, 1, 1, 0, k_col_meter_dim);
            lv_obj_set_clickable(seg, false);
            s_meter_seg[ch][k] = seg;
        }
        s_meter_lit[ch] = 0;
    }
}

void cassette_meter_place(cassette_pose_t pose)
{
    /* The pose is kept before the guard: it is the scale the lattice's draw callback
     * reads, and the view builds the panel before it places anything. */
    s_pose = pose;
    if (!s_meter) return;

    lv_obj_set_pos(s_meter, SCX(METER_X) - SC(METER_W) / 2,
                   SCY(METER_Y) - SC(METER_H) / 2);
    lv_obj_set_size(s_meter, SC(METER_W), SC(METER_H));

    /* The segments are children of the meter, so their offsets are scaled without the
     * design box's own origin. */
    int bar_x = (METER_W - (METER_COLS * METER_SEG_W + (METER_COLS - 1) * METER_GAP)) / 2;
    int seg_pitch = SC(METER_PITCH), seg_w = SC(METER_SEG_W), seg_h = SC(METER_SEG_H);
    int seg_gap = SC(METER_GAP), seg_x0 = SC(bar_x), seg_y0 = SC(METER_BOTTOM);
    for (int ch = 0; ch < METER_COLS; ch++) {
        for (int k = 0; k < METER_CELLS; k++) {
            lv_obj_set_pos(s_meter_seg[ch][k], seg_x0 + ch * (seg_w + seg_gap),
                           seg_y0 - k * seg_pitch - seg_h);
            lv_obj_set_size(s_meter_seg[ch][k], seg_w, seg_h);
        }
    }
}

void cassette_meter_set(const cassette_frame_t *frame)
{
    if (!frame) return;

    /* The meter: one column per channel. */
    meter_level_set(0, frame->vu_l);
    meter_level_set(1, frame->vu_r);
}
