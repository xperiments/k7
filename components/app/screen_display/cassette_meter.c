/*
 * The VU meter in the EQ group's cell; see cassette_meter.h.
 *
 * Cell coordinates, no pose: the meter is a fixed 84x62 panel inside a 96x96 carcase, built when
 * the EQ panel's cells are built and deleted with them. The only state is the two lit counts, and
 * both are reset when the cells are rebuilt, so a swap cannot leave half a bar behind.
 */
#include "cassette_meter.h"

#include <math.h>

#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

#define METER_W           84
#define METER_H           62   /* two 26 px rows, 10 px apart */
#define METER_ROW_H       26
#define METER_ROW_GAP     10
#define METER_ROW_RADIUS  (METER_ROW_H / 2)
#define METER_BADGE_W     18
#define METER_BADGE_RADIUS 4
#define METER_GAP         10
#define METER_BARS        7
#define METER_BAR_W       8
#define METER_X           ((CASSETTE_CELL_W - METER_W) / 2)
#define METER_Y           ((CASSETTE_CELL_H - METER_H) / 2)
/* The track starts after the badge and the gap, and seven bars fill the rest exactly. */
#define METER_TRACK_X     (METER_BADGE_W + METER_GAP)
#define METER_DB_MIN      (-48.0f)
#define METER_DB_MAX      0.0f

static lv_obj_t *s_meter;                          /* the panel */
static lv_obj_t *s_row[2];                         /* L and R rows */
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

/* Size the lit fill over the first `lit` bars. The fill is a pill, so its right end is a
 * semicircle; a zero-length fill is hidden. */
static void fill_place(int ch)
{
    if (!s_meter) return;
    if (s_lit[ch] == 0) {
        lv_obj_set_hidden(s_fill[ch], true);
        return;
    }
    lv_obj_set_hidden(s_fill[ch], false);
    lv_obj_set_pos(s_fill[ch], METER_TRACK_X, 0);
    lv_obj_set_size(s_fill[ch], s_lit[ch] * METER_BAR_W, METER_ROW_H);
}

/* Move one row's lit fill; a row whose count has not changed is not touched. */
static void row_set(int ch, int lit)
{
    if (!s_meter || lit == s_lit[ch]) return;
    s_lit[ch] = lit;
    fill_place(ch);
}

/* The cell is the meter's life: when the bar deletes it, every object below it goes too, so the
 * module forgets them and set() becomes a no-op until the panel builds another. LVGL sends the
 * delete event to the parent and its children; only the panel's own matters. */
static void meter_deleted_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_meter) return;
    s_meter = NULL;
    s_row[0] = s_row[1] = NULL;
    s_fill[0] = s_fill[1] = NULL;
    s_lit[0] = s_lit[1] = 0;
}

void cassette_meter_create(lv_obj_t *cell)
{
    if (!cell || s_meter) return;

    s_meter = lv_obj_create(cell);
    lv_obj_remove_style_all(s_meter);
    lv_obj_set_pos(s_meter, METER_X, METER_Y);
    lv_obj_set_size(s_meter, METER_W, METER_H);
    lv_obj_set_style_bg_opa(s_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_meter, 0, 0);
    lv_obj_set_style_pad_all(s_meter, 0, 0);
    lv_obj_set_scrollable(s_meter, false);
    lv_obj_set_clickable(s_meter, false);
    lv_obj_add_event_cb(s_meter, meter_deleted_cb, LV_EVENT_DELETE, NULL);

    for (int ch = 0; ch < 2; ch++) {
        s_row[ch] = cassette_box(s_meter, 0, ch * (METER_ROW_H + METER_ROW_GAP),
                                 METER_W, METER_ROW_H, METER_ROW_RADIUS, k_col_meter_back);
        /* The row's rounded end clips the track's right edge; the badge's own corners are
         * clipped by the same outline. */
        lv_obj_set_style_clip_corner(s_row[ch], true, 0);

        lv_obj_t *badge = cassette_box(s_row[ch], 0, 0, METER_BADGE_W, METER_ROW_H,
                                       METER_BADGE_RADIUS, k_col_meter_lit);
        lv_obj_t *label = cassette_text(badge, &lv_font_montserrat_14, k_col_bg,
                                        LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(label, ch ? "R" : "L");

        for (int k = 0; k < METER_BARS; k++)
            cassette_box(s_row[ch], METER_TRACK_X + k * METER_BAR_W, 0,
                         METER_BAR_W, METER_ROW_H, 0, k_col_meter_dim);

        /* The lit level is one rounded fill over the track, so its right end follows the row's
         * curvature instead of ending on a square bar edge. */
        s_fill[ch] = cassette_box(s_row[ch], METER_TRACK_X, 0, 1, METER_ROW_H,
                                  LV_RADIUS_CIRCLE, k_col_meter_bar);
        lv_obj_set_hidden(s_fill[ch], true);
        s_lit[ch] = 0;
    }
}

void cassette_meter_set(const cassette_frame_t *frame)
{
    if (!frame) return;

    /* L drives the top row, R the bottom. */
    row_set(0, meter_lit(frame->vu_l));
    row_set(1, meter_lit(frame->vu_r));
}
