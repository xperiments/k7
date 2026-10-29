/*
 * The RGB group's panel; see cassette_rgb.h.
 *
 * The EQ panel's twin, and deliberately thin: a table of cells, five knob values and the publish
 * seam. There is no Back cell: the mode bar's led button is the toggle. Everything a knob *is* -
 * the dial, the drag, the 3x surface, the tapped-open ordering - is knob_cell's, knob_editor's
 * and knob_model's. Everything the strip *is* - the channels, the brightness, the effects and
 * their frames - is rgb.h's.
 *
 * The effect selector is the one stepped knob: its six slots are marked positions carrying the
 * effect's own name, so the 3x dial's pillow says RAINBOW rather than EFFECT, and the 1x plate
 * shows the same name in a bottom pillow under the dial. The group's look is due a rework; this
 * module only carries the new knob vocabulary (ADR 0027).
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_rgb.h"

#include "cassette_button_rule.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "knob_cell.h"
#include "knob_editor.h"

/* The dial's box inside the effect's tall plate: the knob in the upper part, the selected
 * effect in the pillow below it. The box's top-left, so the cap lands at (48, 52). */
#define FX_DIAL_X 0
#define FX_DIAL_Y 4
#define FX_PILL_Y 136

_Static_assert(RGB_EFFECT_COUNT <= KNOB_MARKS_MAX, "the marks array has a slot per effect");

/* The knobs, in the bank's id order: the three channels, the strip's brightness, the effect. */
enum {
    KNOB_RED = 0,
    KNOB_GREEN,
    KNOB_BLUE,
    KNOB_BRIGHT,
    KNOB_FX,
    KNOB_COUNT
};

/* The effect's plate covers the cells below it, so one of them is never built. */
enum {
    CELL_EFFECT = -1,
    CELL_COVERED = -2,
};

/* The panel's control at one bar cell, in the bar's (col,row) order:
 *
 *   [Effect] [Red  ]
 *   [Effect] [Green]
 *   [Bright] [Blue ]
 */
static const int8_t k_cell_kind[2][3] = {
    { CELL_EFFECT, CELL_COVERED, KNOB_BRIGHT },   /* the inside column */
    { KNOB_RED, KNOB_GREEN, KNOB_BLUE },          /* the outer column */
};

/* The effect's plate: the top two cells of the inside column as one 96x190 surface, because the
 * stepped knob has a name to show and a 96 px cell has no room for both. */
static cassette_rect_t rgb_cell_rect(int col, int row)
{
    if (col == 0 && row == 0) {
        cassette_rect_t a = cassette_cell_rect(CASSETTE_BAR_RIGHT, 0, 0);
        cassette_rect_t b = cassette_cell_rect(CASSETTE_BAR_RIGHT, 0, 1);
        cassette_rect_t plate = { a.x, a.y, a.w, b.y + b.h - a.y };
        return plate;
    }
    if (col == 0 && row == 1) {
        cassette_rect_t none = { 0, 0, 0, 0 };
        return none;
    }
    return cassette_cell_rect(CASSETTE_BAR_RIGHT, col, row);
}

/* The controls' identities, which is what the 3x dial's pillow says. The effect's own name is
 * not a name but a marked position: it rides knob_label() instead. */
static const char *const k_knob_name[KNOB_COUNT] = { "RED", "GREEN", "BLUE", "BRIGHT", "EFFECT" };

static knob_cell_t s_knob_cell[KNOB_COUNT];
static knob_t s_knob[KNOB_COUNT];
static rgb_state_t s_state;           /* what the knobs last published */
static knob_mark_t s_fx_marks[RGB_EFFECT_COUNT];   /* one per effect, at its slot */
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

/* ---------------------------------------------------------------------- draw */

static void knob_apply(int id)
{
    knob_cell_apply(&s_knob_cell[id], &s_knob[id]);
    /* A stepped knob's selected value is a value, not a range: it reads on the dial's bottom
     * pillow (ADR 0027), which is the only text a 96 px cell has room for. */
    if (id == KNOB_FX) knob_cell_set_value(&s_knob_cell[id], knob_label(&s_knob[id]));
}

/* ---------------------------------------------------------------------- bank */

static void bank_repaint(int id, void *ctx)
{
    (void)ctx;
    /* Nothing to paint while the editor owns the bar: the Pad has taken these cells. */
    if (!s_shown) return;
    if (id >= 0 && id < KNOB_COUNT) knob_apply(id);
}

static void bank_publish(int id, void *ctx)
{
    (void)ctx;
    (void)id;
    rgb_publish();
}

static bool bank_open_allowed(void *ctx)
{
    (void)ctx;
    return s_shown && !cassette_bars_is_sliding();
}

static const knob_editor_bank_t k_rgb_bank = {
    .knobs = s_knob,
    .count = KNOB_COUNT,
    .repaint = bank_repaint,
    .publish = bank_publish,
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

static void cell_pressed_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (id < 0 || id >= KNOB_COUNT) return;
    knob_editor_drag_begin(id);
}

static void cell_pressing_cb(lv_event_t *e)
{
    (void)e;
    knob_editor_drag_update();
}

static void cell_released_cb(lv_event_t *e)
{
    (void)e;
    knob_editor_drag_end();
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

/* A knob's five handlers: the drag's three and the editor's two. */
static void knob_handlers(lv_obj_t *cell, int id)
{
    lv_obj_add_event_cb(cell, cell_pressed_cb, LV_EVENT_PRESSED, (void *)(intptr_t)(id + 1));
    lv_obj_add_event_cb(cell, cell_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(cell, cell_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(id + 1));
    lv_obj_add_event_cb(cell, cell_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED,
                        (void *)(intptr_t)(id + 1));
}

static void rgb_cell_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (!cell || col < 0 || col > 1 || row < 0 || row > 2) return;
    int kind = k_cell_kind[col][row];
    if (kind == CELL_COVERED) return;

    if (kind == CELL_EFFECT) {
        /* The stepped knob sits in the plate's upper part; its selected value - the effect's
         * name - goes below it, in the room the covered cell would have taken. */
        knob_cell_create_at(&s_knob_cell[KNOB_FX], cell, &k_knob_dial_cell, FX_DIAL_X,
                            FX_DIAL_Y);
        knob_cell_add_value_pill(&s_knob_cell[KNOB_FX], CASSETTE_CELL_W / 2, FX_PILL_Y);
        knob_apply(KNOB_FX);
        knob_handlers(cell, KNOB_FX);
        return;
    }

    if (knob_cell_create(&s_knob_cell[kind], cell)) knob_apply(kind);
    knob_handlers(cell, kind);
}

/* ---------------------------------------------------------------------- ink */

static void rgb_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int id = 0; id < KNOB_COUNT; id++) knob_cell_set_page(&s_knob_cell[id], white);
}

/* ------------------------------------------------------------------- public */

static void rgb_shown(bool visible, void *ctx)
{
    (void)ctx;
    s_shown = visible;
    /* See cassette_eq's eq_shown: the editor's own swap keeps the bank, and the cells are
     * gone until it hands the bar back. */
    if (!visible && knob_editor_is_editing()) return;
    knob_editor_set_bank(visible ? &k_rgb_bank : NULL);
}

static const cassette_panel_t k_rgb_panel = {
    .columns = 2,
    .rows = 3,
    .cell_rect = rgb_cell_rect,
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

    /* Every slot of the selector is a marked position carrying the effect's own name, printed
     * as a plaque beside its position. That is the whole of the dial's text: a selector has no
     * title pillow, because the filled plaque already says which one is on (ADR 0029). */
    for (int slot = 0; slot < RGB_EFFECT_COUNT; slot++) {
        knob_set_step(&s_knob[KNOB_FX], slot);
        s_fx_marks[slot].value = knob_value(&s_knob[KNOB_FX]);
        s_fx_marks[slot].text = rgb_effect_name(rgb_effect_at(slot));
        s_fx_marks[slot].plaque = true;
    }
    knob_set_marks(&s_knob[KNOB_FX], s_fx_marks, RGB_EFFECT_COUNT);
    knob_reset(&s_knob[KNOB_FX]);   /* the slot walk left it on the last one */

    rgb_compose();

    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_rgb_panel);
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
