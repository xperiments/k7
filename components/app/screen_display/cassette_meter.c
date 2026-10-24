/*
 * The VU meter.
 *
 * Two rows centred on the right hub: L on top, R below. Each row has an L/R badge and
 * seven touching dim bars; the lit level is one rounded fill that grows from the left, so
 * its right end is a semicircle. The row clips its children to its rounded corners, so the
 * track's right end follows the row's outline too.
 *
 * The meter owns its objects and state - the panel, the two rows, the two badges, the
 * fourteen track bars, the two lit fills and each row's lit count - and shares none of it
 * with the view. The view supplies the parent and the pose. The panel is placed by the
 * design grid and its children are offsets inside it, so a scale change re-places the
 * whole meter in one pass.
 */
#include <math.h>

#include "lvgl.h"

#include "cassette_layout.h"
#include "cassette_meter.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

/* Two 100x24 rows, 8 px apart, centred on the right hub. Each row has a 24 px badge, a
 * 6 px gap and seven 10 px bars. The bars are the dim track; the lit level is a single
 * rounded fill over the first `lit` bars, so its right end is a semicircle. The row's
 * radius is half its height, so the track's right end is rounded too. */
#define METER_W           100
#define METER_H           56
#define METER_ROW_H       24
#define METER_ROW_GAP     8
#define METER_ROW_RADIUS  (METER_ROW_H / 2)
#define METER_BADGE_RADIUS 4
#define METER_BADGE_W     24
#define METER_GAP         6
#define METER_BARS        7
#define METER_BAR_W       10
#define METER_X         594     /* the right hub's centre */
#define METER_Y         206
#define METER_DB_MIN    (-48.0f)
#define METER_DB_MAX    0.0f

/* The pose the meter was last placed with, as a value. */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };

#define SC(v)  cassette_len(s_pose, (v))             /* a design length, in pixels */
#define SCX(x) cassette_x(s_pose, (x))               /* a design x, in screen pixels */
#define SCY(y) cassette_y(s_pose, (y))               /* a design y, in screen pixels */

static lv_obj_t *s_meter;                          /* the panel */
static lv_obj_t *s_row[2];                         /* L and R rows */
static lv_obj_t *s_badge[2];                       /* their L/R badges */
static lv_obj_t *s_label[2];                       /* the letters inside the badges */
static lv_obj_t *s_bar[2][METER_BARS];             /* the dim track */
static lv_obj_t *s_fill[2];                        /* the lit rounded fill */
static int s_lit[2];                               /* bars lit per row, 0..METER_BARS */

/* The number of lit bars for a level. The scale is dB over -48..0. */
static int meter_lit(float vu)
{
    float db = vu > 0.001f ? 20.0f * log10f(vu) : METER_DB_MIN;
    if (db < METER_DB_MIN) db = METER_DB_MIN;
    if (db > METER_DB_MAX) db = METER_DB_MAX;
    return (int)lroundf((db - METER_DB_MIN) / (METER_DB_MAX - METER_DB_MIN) * METER_BARS);
}

/* Size the lit fill over the first `lit` bars. The fill is a pill, so its right end is
 * a semicircle; a zero-length fill is hidden. */
static void fill_place(int ch)
{
    if (!s_meter) return;
    if (s_lit[ch] == 0) {
        lv_obj_set_hidden(s_fill[ch], true);
        return;
    }
    lv_obj_set_hidden(s_fill[ch], false);
    lv_obj_set_pos(s_fill[ch], SC(METER_BADGE_W + METER_GAP), 0);
    lv_obj_set_size(s_fill[ch], SC(s_lit[ch] * METER_BAR_W), SC(METER_ROW_H));
}

/* Move one row's lit fill; a row whose count has not changed is not touched. */
static void row_set(int ch, int lit)
{
    if (!s_meter || lit == s_lit[ch]) return;
    s_lit[ch] = lit;
    fill_place(ch);
}

void cassette_meter_create(lv_obj_t *parent)
{
    /* The VU meter, inside the right hub. It is created after the tapes, so it paints
     * over the hub's middle; only the bars change colour with a level. */
    s_meter = lv_obj_create(parent);
    lv_obj_remove_style_all(s_meter);
    lv_obj_set_style_bg_opa(s_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_meter, 0, 0);
    lv_obj_set_style_pad_all(s_meter, 0, 0);
    lv_obj_set_scrollable(s_meter, false);
    lv_obj_set_clickable(s_meter, false);

    for (int ch = 0; ch < 2; ch++) {
        s_row[ch] = cassette_box(s_meter, 0, ch * (METER_ROW_H + METER_ROW_GAP),
                                 METER_W, METER_ROW_H, METER_ROW_RADIUS, k_col_meter_back);
        /* The row's rounded end clips the track's right edge; the badge's own corners
         * are clipped by the same outline. */
        lv_obj_set_style_clip_corner(s_row[ch], true, 0);

        s_badge[ch] = cassette_box(s_row[ch], 0, 0, METER_BADGE_W, METER_ROW_H,
                                   METER_BADGE_RADIUS, k_col_meter_lit);
        s_label[ch] = cassette_text(s_badge[ch], &lv_font_montserrat_14, k_col_bg,
                                    LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_label[ch], ch ? "R" : "L");

        for (int k = 0; k < METER_BARS; k++)
            s_bar[ch][k] = cassette_box(s_row[ch],
                                        METER_BADGE_W + METER_GAP + k * METER_BAR_W, 0,
                                        METER_BAR_W, METER_ROW_H, 0, k_col_meter_dim);

        /* The lit level is one rounded fill over the track, so its right end follows the
         * row's curvature instead of ending on a square bar edge. */
        s_fill[ch] = cassette_box(s_row[ch], METER_BADGE_W + METER_GAP, 0, 1,
                                  METER_ROW_H, LV_RADIUS_CIRCLE, k_col_meter_bar);
        lv_obj_set_hidden(s_fill[ch], true);
        s_lit[ch] = 0;
    }
}

void cassette_meter_place(cassette_pose_t pose)
{
    /* The pose is kept before the guard: it is the scale the children are placed with,
     * and the view builds the panel before it places anything. */
    s_pose = pose;
    if (!s_meter) return;

    lv_obj_set_pos(s_meter, SCX(METER_X) - SC(METER_W) / 2,
                   SCY(METER_Y) - SC(METER_H) / 2);
    lv_obj_set_size(s_meter, SC(METER_W), SC(METER_H));

    int row_radius = SC(METER_ROW_RADIUS);
    if (row_radius < 1) row_radius = 1;
    int badge_radius = SC(METER_BADGE_RADIUS);
    if (badge_radius < 1) badge_radius = 1;

    for (int ch = 0; ch < 2; ch++) {
        int y = SC(ch * (METER_ROW_H + METER_ROW_GAP));
        int row_h = SC(METER_ROW_H), badge_w = SC(METER_BADGE_W);
        int bar_w = SC(METER_BAR_W);

        lv_obj_set_pos(s_row[ch], 0, y);
        lv_obj_set_size(s_row[ch], SC(METER_W), row_h);
        lv_obj_set_style_radius(s_row[ch], row_radius, 0);

        lv_obj_set_pos(s_badge[ch], 0, 0);
        lv_obj_set_size(s_badge[ch], badge_w, row_h);
        lv_obj_set_style_radius(s_badge[ch], badge_radius, 0);
        lv_obj_align(s_label[ch], LV_ALIGN_CENTER, 0, 0);

        int bar_x = badge_w + SC(METER_GAP);
        for (int k = 0; k < METER_BARS; k++) {
            lv_obj_set_pos(s_bar[ch][k], bar_x + k * bar_w, 0);
            lv_obj_set_size(s_bar[ch][k], bar_w, row_h);
        }
        fill_place(ch);
    }
}

void cassette_meter_set(const cassette_frame_t *frame)
{
    if (!frame) return;

    /* L drives the top row, R the bottom. */
    row_set(0, meter_lit(frame->vu_l));
    row_set(1, meter_lit(frame->vu_r));
}
