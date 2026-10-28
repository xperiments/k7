/*
 * The meter in the EQ group's plate; see cassette_meter.h.
 *
 * Two tall L/R columns of dots, lit from the bottom, with a small L/R pill under each - the shape
 * of the reference the owner gave. The plate is 96x190 (the inside column's top two cells), so the
 * columns are 30x148 with 5x24 dots each; the pill is the channel's label.
 *
 * Cell coordinates, no pose: the meter is built when the EQ panel's cells are built and deleted
 * with them. The only state is the two lit counts, both reset when the cells are rebuilt, so a
 * swap cannot leave half a column behind.
 */
#include "cassette_meter.h"

#include <math.h>

#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

/* The two columns: 30 wide, 148 tall, 12 apart, centred in the 96 px plate; the pills below
 * them, and 14 px of air under those. */
#define BAR_W        30
#define BAR_H        148
#define BAR_GAP      12
#define BAR_X0       ((CASSETTE_CELL_W - (2 * BAR_W + BAR_GAP)) / 2)
#define BAR_Y        8
#define PILL_W       BAR_W
#define PILL_H       14
#define PILL_Y       (BAR_Y + BAR_H + 6)
/* The dot matrix inside a column: 5 columns and 24 rows of 4 px dots on a 6 px pitch, so the
 * matrix is 28x142 and sits 1 px from the column's sides and 3 px from its ends. */
#define DOT          4
#define DOT_PITCH    6
#define BAR_COLS     5
#define BAR_ROWS     24
#define MATRIX_W     ((BAR_COLS - 1) * DOT_PITCH + DOT)
#define MATRIX_H     ((BAR_ROWS - 1) * DOT_PITCH + DOT)

#define METER_DB_MIN (-48.0f)
#define METER_DB_MAX 0.0f

static lv_obj_t *s_cell;          /* the plate the meter is built into */
static lv_obj_t *s_bar[2];        /* the two dot columns */
static int s_lit[2];              /* dots lit per column, 0..BAR_ROWS, from the bottom */

/* The number of lit rows for a level. The scale is dB over -48..0, as it always was. */
static int meter_lit(float vu)
{
    float db = vu > 0.001f ? 20.0f * log10f(vu) : METER_DB_MIN;
    if (db < METER_DB_MIN) db = METER_DB_MIN;
    if (db > METER_DB_MAX) db = METER_DB_MAX;
    return (int)lroundf((db - METER_DB_MIN) / (METER_DB_MAX - METER_DB_MIN) * BAR_ROWS);
}

/* A column's dots, in one draw callback: the rows below the level in the lit ink, the rest in
 * the track's. The matrix is centred in the column so a bar and its own outline agree. */
static void bar_draw_cb(lv_event_t *e)
{
    int ch = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *bar = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t area;
    lv_obj_get_coords(bar, &area);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;

    int x0 = (BAR_W - MATRIX_W) / 2;
    int y0 = (BAR_H - MATRIX_H) / 2;

    for (int row = 0; row < BAR_ROWS; row++) {
        /* Row 0 is the top of the column, so the level fills from the bottom up. */
        bool lit = (BAR_ROWS - 1 - row) < s_lit[ch];
        dsc.bg_color = lit ? k_col_meter_lit : k_col_meter_dim;
        for (int col = 0; col < BAR_COLS; col++) {
            lv_area_t dot;
            dot.x1 = area.x1 + x0 + col * DOT_PITCH;
            dot.y1 = area.y1 + y0 + row * DOT_PITCH;
            dot.x2 = dot.x1 + DOT - 1;
            dot.y2 = dot.y1 + DOT - 1;
            lv_draw_rect(layer, &dsc, &dot);
        }
    }
}

/* Move one column; a column whose count has not changed is not touched. */
static void row_set(int ch, int lit)
{
    if (!s_cell || lit == s_lit[ch]) return;
    s_lit[ch] = lit;
    if (s_bar[ch]) lv_obj_invalidate(s_bar[ch]);
}

/* The plate is the meter's life: when the bar deletes it, every object below it goes too, so the
 * module forgets them and set() becomes a no-op until the panel builds another. */
static void meter_deleted_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_cell) return;
    s_cell = NULL;
    s_bar[0] = s_bar[1] = NULL;
    s_lit[0] = s_lit[1] = 0;
}

/* One column: a transparent box the draw callback paints, and under it the pill with the
 * channel's letter. */
static void column_build(lv_obj_t *cell, int ch)
{
    int x = BAR_X0 + ch * (BAR_W + BAR_GAP);

    s_bar[ch] = cassette_border_box(cell, x, BAR_Y, BAR_W, BAR_H, 0,
                                    k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    lv_obj_add_event_cb(s_bar[ch], bar_draw_cb, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)ch);

    lv_obj_t *pill = cassette_box(cell, x, PILL_Y, PILL_W, PILL_H, PILL_H / 2,
                                  k_col_meter_lit);
    lv_obj_t *label = cassette_text(pill, &lv_font_montserrat_14, k_col_bg,
                                    LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label, ch ? "R" : "L");
    s_lit[ch] = 0;
}

void cassette_meter_create(lv_obj_t *cell)
{
    if (!cell || s_cell) return;
    s_cell = cell;
    lv_obj_add_event_cb(cell, meter_deleted_cb, LV_EVENT_DELETE, NULL);

    for (int ch = 0; ch < 2; ch++) column_build(cell, ch);
}

void cassette_meter_set(const cassette_frame_t *frame)
{
    if (!frame) return;

    /* L is the left column, R the right. */
    row_set(0, meter_lit(frame->vu_l));
    row_set(1, meter_lit(frame->vu_r));
}
