/*
 * The RGB group's panel; see cassette_rgb.h.
 *
 * The EQ panel's twin, and deliberately thin: a table of cells, five knob values, a Back action
 * and the publish seam. Everything a knob *is* - the small dial, the 3x surface, the tapped-
 * open ordering - is knob_cell's, knob_editor's and knob_model's. Everything the strip *is* -
 * the channels, the brightness, the effects and their frames - is rgb.h's.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_rgb.h"

#include "cassette_button.h"
#include "cassette_icons.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "knob_cell.h"
#include "knob_editor.h"

LV_FONT_DECLARE(xtape_36);

/* The knobs, in the bank's id order: the three channels, the strip's brightness, the effect. */
enum {
    KNOB_RED = 0,
    KNOB_GREEN,
    KNOB_BLUE,
    KNOB_BRIGHT,
    KNOB_FX,
    KNOB_COUNT
};

/* The cells that are not knobs, as negative kinds in the cell map. */
enum {
    CELL_BACK = -1,
};

/* The panel's control at one bar cell, in the bar's (col,row) order:
 *
 *   [Back  ] [Red  ]
 *   [Effect] [Green]
 *   [Bright] [Blue ]
 */
static const int8_t k_cell_kind[2][3] = {
    { CELL_BACK, KNOB_FX, KNOB_BRIGHT },   /* the inside column */
    { KNOB_RED, KNOB_GREEN, KNOB_BLUE },   /* the outer column */
};

/* Where each knob's cell is, for the overlay's point-to-knob mapping. */
static const int8_t k_knob_col[KNOB_COUNT] = { 1, 1, 1, 0, 0 };
static const int8_t k_knob_row[KNOB_COUNT] = { 0, 1, 2, 2, 1 };

/* The controls' identities: what the 3x knob's pillow says. The effect's pillow is the effect's
 * own name (the bank's label hook), not this one. */
static const char *const k_knob_name[KNOB_COUNT] = { "RED", "GREEN", "BLUE", "BRIGHT", "EFFECT" };

static knob_cell_t s_knob_cell[KNOB_COUNT];
static knob_t s_knob[KNOB_COUNT];
static cassette_button_t s_button_back;
static rgb_state_t s_state;           /* what the knobs last published */
static void (*s_back_cb)(void *ctx);
static void *s_back_ctx;
static cassette_rgb_publish_t s_publish;
static void *s_publish_ctx;
static bool s_shown;
static bool s_created;

/* --------------------------------------------------------------------- publish */

/* The knobs are the truth; the state is a reading of them. */
static void rgb_compose(void)
{
    s_state.red = knob_value(&s_knob[KNOB_RED]);
    s_state.green = knob_value(&s_knob[KNOB_GREEN]);
    s_state.blue = knob_value(&s_knob[KNOB_BLUE]);
    s_state.brightness = knob_value(&s_knob[KNOB_BRIGHT]);
    s_state.effect = rgb_effect_at(knob_step(&s_knob[KNOB_FX]));
}

static void rgb_publish(void)
{
    rgb_compose();
    if (s_publish) s_publish(&s_state, s_publish_ctx);
}

/* ------------------------------------------------------------------ geometry */

static cassette_rect_t knob_cell_rect(int id)
{
    return cassette_cell_rect(CASSETTE_BAR_RIGHT, k_knob_col[id], k_knob_row[id]);
}

static int knob_at(int x, int y)
{
    for (int id = 0; id < KNOB_COUNT; id++) {
        cassette_rect_t r = knob_cell_rect(id);
        /* The carcases share their 2 px border; the tap target stops at the shared edge. */
        if (x >= r.x && x < r.x + CASSETTE_CELL_W &&
            y >= r.y && y < r.y + CASSETTE_CELL_PITCH) return id;
    }
    return -1;
}

/* ---------------------------------------------------------------------- draw */

static void knob_apply(int id)
{
    knob_cell_apply(&s_knob_cell[id], &s_knob[id]);
}

/* ---------------------------------------------------------------------- bank */

static int bank_id_at(int x, int y, void *ctx)
{
    (void)ctx;
    return knob_at(x, y);
}

/* The effect knob's pillow says the effect's name; every other knob its own. */
static const char *bank_label(int id, void *ctx)
{
    (void)ctx;
    if (id != KNOB_FX) return NULL;
    return rgb_effect_name(rgb_effect_at(knob_step(&s_knob[KNOB_FX])));
}

static void bank_repaint(int id, void *ctx)
{
    (void)ctx;
    if (id >= 0 && id < KNOB_COUNT) knob_apply(id);
}

static void bank_publish(int id, void *ctx)
{
    (void)ctx;
    (void)id;
    rgb_publish();
}

static lv_obj_t *bank_disc(int id, void *ctx)
{
    (void)ctx;
    return (id >= 0 && id < KNOB_COUNT) ? knob_cell_disc(&s_knob_cell[id]) : NULL;
}

static bool bank_open_allowed(void *ctx)
{
    (void)ctx;
    return s_shown && !cassette_bars_is_sliding();
}

static const knob_editor_bank_t k_rgb_bank = {
    .knobs = s_knob,
    .count = KNOB_COUNT,
    .id_at = bank_id_at,
    .label = bank_label,
    .repaint = bank_repaint,
    .publish = bank_publish,
    .cell_disc = bank_disc,
    .open_allowed = bank_open_allowed,
    .ctx = NULL,
};

/* ------------------------------------------------------------------- the cells */

static bool knob_click_is_tap(void)
{
    lv_indev_t *indev = lv_indev_active();
    bool gesturing = indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
    bool moved = indev && lv_indev_get_press_moved(indev);
    return cassette_button_is_tap(moved, gesturing);
}

static void cell_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (id < 0 || id >= KNOB_COUNT) return;
    if (!knob_click_is_tap()) return;
    knob_editor_tap(id);
}

static void cell_double_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (id < 0 || id >= KNOB_COUNT) return;
    knob_editor_double_tap(id);
}

static void back_clicked(void *ctx)
{
    (void)ctx;
    if (!s_shown || cassette_bars_is_sliding()) return;
    if (s_back_cb) s_back_cb(s_back_ctx);
}

static void back_build(lv_obj_t *cell)
{
    cassette_cap_t cap = { k_col_dome, k_col_bg };
    cassette_button_spec_t spec = {
        .name = "BACK",
        .text = CASSETTE_ICON_LEFT,
        .font = &xtape_36,
        .cap = cap,
        .carcase_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .disc_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .on_click = back_clicked,
        .ctx = NULL,
    };
    cassette_button_create(&s_button_back, cell, &spec);
}

static void rgb_cell_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (!cell || col < 0 || col > 1 || row < 0 || row > 2) return;
    int kind = k_cell_kind[col][row];

    if (kind == CELL_BACK) {
        back_build(cell);
        return;
    }
    if (knob_cell_create(&s_knob_cell[kind], cell)) knob_apply(kind);
    lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(kind + 1));
    lv_obj_add_event_cb(cell, cell_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED,
                        (void *)(intptr_t)(kind + 1));
}

/* ---------------------------------------------------------------------- ink */

static void rgb_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int id = 0; id < KNOB_COUNT; id++) knob_cell_set_page(&s_knob_cell[id], white);
    cassette_button_set_page(&s_button_back, white);
}

/* ------------------------------------------------------------------- public */

static void rgb_shown(bool visible, void *ctx)
{
    (void)ctx;
    s_shown = visible;
    knob_editor_set_bank(visible ? &k_rgb_bank : NULL);
}

static const cassette_panel_t k_rgb_panel = {
    .columns = 2,
    .rows = 3,
    .cell_rect = NULL,
    .gesture_bubble = false,
    .inert = false,
    .build = rgb_cell_build,
    .set_page = rgb_set_page,
    .shown = rgb_shown,
    .ctx = NULL,
};

void cassette_rgb_create(void)
{
    if (s_created) return;
    s_created = true;

    s_shown = false;
    s_back_cb = NULL;
    s_back_ctx = NULL;
    s_publish = NULL;
    s_publish_ctx = NULL;

    /* The channels and the brightness are level knobs, neutral at full; the effect is a stepped
     * knob with one slot per effect in rgb.h's list, neutral at OFF. */
    knob_init_unit(&s_knob[KNOB_RED]);
    knob_init_unit(&s_knob[KNOB_GREEN]);
    knob_init_unit(&s_knob[KNOB_BLUE]);
    knob_init_unit(&s_knob[KNOB_BRIGHT]);
    knob_init_steps(&s_knob[KNOB_FX], rgb_effect_count(), (int)RGB_EFFECT_OFF);
    for (int id = 0; id < KNOB_COUNT; id++) knob_set_name(&s_knob[id], k_knob_name[id]);

    rgb_compose();

    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_rgb_panel);
}

void cassette_rgb_set_back_cb(void (*cb)(void *ctx), void *ctx)
{
    s_back_cb = cb;
    s_back_ctx = ctx;
}

void cassette_rgb_set_publish(cassette_rgb_publish_t cb, void *ctx)
{
    s_publish = cb;
    s_publish_ctx = ctx;
    /* A hook installed after the panel is up still gets the values that are already there. */
    if (cb) {
        rgb_compose();
        cb(&s_state, ctx);
    }
}

const cassette_panel_t *cassette_rgb_panel(void)
{
    return &k_rgb_panel;
}
