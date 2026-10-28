/*
 * The Empty group's speaker grille; see cassette_empty.h.
 *
 * One plate, one draw callback, one ink flag: the dots are painted in the plate's own draw
 * event, at the plate's absolute coordinates, so a slide or a page flip invalidates the plate
 * and nothing else. Called under the LVGL lock.
 */
#include "cassette_empty.h"

#include "cassette_layout.h"
#include "cassette_style.h"

#define GRILLE_INSET 14   /* the pattern's margin to the plate's outer edge */
#define GRILLE_PITCH 14   /* dot to dot, both axes */
#define GRILLE_DOT   4    /* the dot's diameter */

static lv_obj_t *s_plate;
static bool s_white;
static bool s_created;

/* The plate as the bar's cell: the Pad's footprint, so the Empty group and the Menu's Pad are
 * the same surface and swapping between them does not move a pixel. */
static cassette_rect_t plate_rect(int col, int row)
{
    (void)col;
    (void)row;
    return cassette_part_rect(CASSETTE_PART_PAD);
}

/* The grille: staggered rows of dots, centred on the plate's pitch. Each dot is a rounded
 * rectangle whose radius is its half, which is a circle at any scale. All of it is one draw
 * task per dot on one object, and the object has no paint of its own. */
static void grille_draw_cb(lv_event_t *e)
{
    lv_obj_t *plate = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(plate, &area);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = s_white ? k_col_bg : k_col_button_rect;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;

    lv_area_t dot;
    for (int row = 0;; row++) {
        int cy = GRILLE_INSET + row * GRILLE_PITCH;
        if (cy + GRILLE_DOT / 2 > area.y2 - area.y1 - GRILLE_INSET) break;

        int shift = (row & 1) ? GRILLE_PITCH / 2 : 0;
        for (int col = 0;; col++) {
            int cx = GRILLE_INSET + shift + col * GRILLE_PITCH;
            if (cx + GRILLE_DOT / 2 > area.x2 - area.x1 - GRILLE_INSET) break;

            dot.x1 = area.x1 + cx - GRILLE_DOT / 2;
            dot.y1 = area.y1 + cy - GRILLE_DOT / 2;
            dot.x2 = dot.x1 + GRILLE_DOT - 1;
            dot.y2 = dot.y1 + GRILLE_DOT - 1;
            lv_draw_rect(layer, &dsc, &dot);
        }
    }
}

/* The bar's carcase is the plate; the grille is a draw callback on it, so the panel adds no
 * object at all. */
static void grille_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)col;
    (void)row;
    (void)ctx;
    if (!cell) return;
    s_plate = cell;
    lv_obj_add_event_cb(cell, grille_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
}

/* The dots are the panel's ink; the plate's fill and stroke are the bar's, painted before
 * this. A page flip only changes which ink the draw callback reads. */
static void grille_set_page(bool white, void *ctx)
{
    (void)ctx;
    s_white = white;
    if (s_plate) lv_obj_invalidate(s_plate);
}

/* One plate, inert: no cell of this panel is a control, and a swipe that starts on it is still
 * the screen's. */
static const cassette_panel_t k_empty_panel = {
    .columns = 1,
    .rows = 1,
    .cell_rect = plate_rect,
    .gesture_bubble = true,
    .inert = true,
    .build = grille_build,
    .set_page = grille_set_page,
    .shown = NULL,
    .ctx = NULL,
};

void cassette_empty_create(void)
{
    if (s_created) return;
    s_created = true;
    s_white = false;
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_empty_panel);
}

const cassette_panel_t *cassette_empty_panel(void)
{
    return &k_empty_panel;
}
