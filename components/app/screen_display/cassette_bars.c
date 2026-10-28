/*
 * The two edge strips and their cells; see cassette_bars.h.
 *
 * A bar is a value: the cells it built, their resting x, the offset that takes them off
 * screen and the panel that fills them. A slide animates the offset
 * and writes each root's x, so no cell owns the animation and a reversal replaces one anim,
 * not six. The cells are hidden when a slide out completes and unhidden when a slide in
 * starts.
 *
 * The carcases are the bar's, not the panels': one 96x96 rounded box per cell at the layout's
 * rect, with the shared 2 px border, made clickable and handed to the panel's build(). That is
 * what makes a cell's control swappable without moving the grid, and it is why a panel that
 * arrives late still lands on the current page and at the current offset.
 *
 * There is no chrome cell: every cell belongs to the panel that is up. The Menu's door is the
 * left bar's tape button, so no panel can strand it and no bar has to hold a cell back.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_bars.h"

#include <stdint.h>
#include <string.h>

#include "cassette_button.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

/* A bar can hold a full 2x3 panel: six on the right, three on the left. */
#define BARS_MAX_ROOTS   6

/* A bar's fixed part: its layout geometry and the hit rule its cells default to before a
 * panel is installed. */
typedef struct {
    cassette_bar_t layout;
    bool gesture_bubble;
} bar_spec_t;

static const bar_spec_t k_spec[CASSETTE_BARS_COUNT] = {
    /* The mode bar's buttons bubble gestures, so a swipe that starts on one still moves the
     * ladder. */
    { CASSETTE_BAR_LEFT, true },
    /* The group cells do not: a swipe that starts on a knob is not a ladder gesture. */
    { CASSETTE_BAR_RIGHT, false },
};

typedef struct {
    int side;                             /* the bar_side_t, as an index */
    const cassette_panel_t *panel;
    lv_obj_t *roots[BARS_MAX_ROOTS];
    int rest_x[BARS_MAX_ROOTS];
    int count;
    int hidden_dx;
    int32_t offset;                       /* the x offset the roots are drawn at */
} bar_t;

static bar_t s_bars[CASSETTE_BARS_COUNT];
static lv_obj_t *s_screen;
static bool s_created;
static bool s_shown;
static bool s_page_white;

/* ---------------------------------------------------------------- the cells */

/* One carcase, the bar's own: the layout's rect on the current page, with the shared corner,
 * border and hit. The panel builds its control inside it and never moves it. An inert panel is
 * a picture, so its carcase stays out of the hit test and the touch falls through to the
 * screen's gesture handler. */
static lv_obj_t *bar_cell_new(bar_t *b, cassette_rect_t rect, bool gesture_bubble, bool clickable)
{
    lv_obj_t *cell = cassette_border_box(s_screen, rect.x + b->offset, rect.y, rect.w, rect.h,
                                         CASSETTE_CORNER_R,
                                         s_page_white ? k_col_white : k_col_bg, LV_OPA_COVER,
                                         k_col_button_rect, CASSETTE_BORDER_W);
    lv_obj_set_clickable(cell, clickable);
    /* The press bubbles so a part's press is still a press the screen sees; the gesture only
     * where the panel says a swipe that starts on a cell is still the screen's. */
    lv_obj_set_event_bubble(cell, true);
    lv_obj_set_gesture_bubble(cell, gesture_bubble);
    return cell;
}

static void bar_root_add(bar_t *b, lv_obj_t *obj, int rest_x)
{
    if (!obj || b->count >= BARS_MAX_ROOTS) return;
    b->roots[b->count] = obj;
    b->rest_x[b->count] = rest_x;
    b->count++;
}

static void bar_apply(bar_t *b)
{
    for (int i = 0; i < b->count; i++)
        if (b->roots[i]) lv_obj_set_x(b->roots[i], b->rest_x[i] + b->offset);
}

static void bar_apply_visibility(bar_t *b)
{
    for (int i = 0; i < b->count; i++)
        if (b->roots[i]) lv_obj_set_hidden(b->roots[i], !s_shown);
}

/* The page ink: the cells' default first, then the panel's exceptions - the knob plate, the
 * discs, a button's cap, the grille's dots. */
static void bar_ink(bar_t *b)
{
    for (int i = 0; i < b->count; i++) {
        lv_obj_t *cell = b->roots[i];
        if (!cell) continue;
        lv_obj_set_style_bg_color(cell, s_page_white ? k_col_white : k_col_bg, 0);
        lv_obj_set_style_border_color(cell, k_col_button_rect, 0);
    }
    if (b->panel && b->panel->set_page) b->panel->set_page(s_page_white, b->panel->ctx);
}

/* ---------------------------------------------------------------- the panel */

/* Take the current panel's cells down. The panel is told it is not visible first, so its own
 * state - a knob edit, a pending open - goes with them; the carcases and everything built
 * inside them are the bar's to delete. */
static void bar_panel_clear(bar_t *b)
{
    if (b->panel && b->panel->shown) b->panel->shown(false, b->panel->ctx);
    for (int i = 0; i < b->count; i++) {
        if (!b->roots[i]) continue;
        /* A click pulse may still be running on a carcase, and LVGL does not cancel an
         * animation when its variable is deleted. */
        lv_anim_delete(b->roots[i], NULL);
        lv_obj_delete(b->roots[i]);
    }
    b->count = 0;
    b->panel = NULL;
}

/* ---------------------------------------------------------------- the slide */

static void bar_slide_cb(void *var, int32_t offset)
{
    bar_t *b = var;
    b->offset = offset;
    bar_apply(b);
}

/* The slide out is over: hide the roots so they cannot take a touch off screen. A slide in
 * never reaches here hidden; a reverse deleted this animation, so it does not run. */
static void bar_slide_done_cb(lv_anim_t *a)
{
    bar_t *b = lv_anim_get_user_data(a);
    if (!b || s_shown) return;
    for (int i = 0; i < b->count; i++)
        if (b->roots[i]) lv_obj_set_hidden(b->roots[i], true);
}

static void bar_slide(bar_t *b, bool shown)
{
    /* A bar with no cells yet still follows the shown state, so a panel built later lands
     * where the bar already is instead of at the hidden offset. */
    if (b->count <= 0) {
        b->offset = shown ? 0 : b->hidden_dx;
        return;
    }

    if (b->panel && b->panel->shown) b->panel->shown(s_shown, b->panel->ctx);
    if (shown)
        for (int i = 0; i < b->count; i++)
            if (b->roots[i]) lv_obj_set_hidden(b->roots[i], false);

    lv_anim_delete(b, bar_slide_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, bar_slide_cb);
    lv_anim_set_values(&a, b->offset, shown ? 0 : b->hidden_dx);
    lv_anim_set_duration(&a, CASSETTE_BARS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, b);
    if (!shown) lv_anim_set_completed_cb(&a, bar_slide_done_cb);
    lv_anim_start(&a);
}

/* Both bars' cells are the bars', not a part's: a show raises them. One root at a time, so a
 * raised bar keeps the order the cells were built in. */
static void bars_raise(void)
{
    for (int i = 0; i < CASSETTE_BARS_COUNT; i++) {
        bar_t *b = &s_bars[i];
        if (!b->count) continue;
        for (int r = 0; r < b->count; r++)
            if (b->roots[r]) lv_obj_move_foreground(b->roots[r]);
    }
}

/* ----------------------------------------------------------------- the public */

void cassette_bars_create(lv_obj_t *screen)
{
    if (s_created || !screen) return;
    s_created = true;
    s_screen = screen;
    memset(s_bars, 0, sizeof s_bars);
    s_shown = false;
    s_page_white = false;

    for (int i = 0; i < CASSETTE_BARS_COUNT; i++) {
        bar_t *b = &s_bars[i];
        b->side = i;
        const cassette_bar_geometry_t *g = cassette_bar_geometry(k_spec[i].layout);
        b->hidden_dx = g ? g->hidden_dx : 0;
        b->offset = b->hidden_dx;
    }
}

void cassette_bars_set_panel(cassette_bar_side_t side, const cassette_panel_t *panel)
{
    if (!s_created || !panel || (unsigned)side >= CASSETTE_BARS_COUNT) return;
    bar_t *b = &s_bars[side];
    if (b->panel == panel) return;

    bar_panel_clear(b);
    b->panel = panel;

    const cassette_bar_geometry_t *g = cassette_bar_geometry(k_spec[side].layout);
    int columns = panel->columns > 0 ? panel->columns : 0;
    int rows = panel->rows > 0 ? panel->rows : 0;
    if (g) {
        if (columns > g->columns) columns = g->columns;
        if (rows > g->rows) rows = g->rows;
    }

    for (int col = 0; col < columns; col++) {
        for (int row = 0; row < rows; row++) {
            cassette_rect_t rect = panel->cell_rect ? panel->cell_rect(col, row)
                                                    : cassette_cell_rect(k_spec[side].layout, col, row);
            if (rect.w <= 0 || rect.h <= 0) continue;

            lv_obj_t *cell = bar_cell_new(b, rect, panel->gesture_bubble, !panel->inert);
            bar_root_add(b, cell, rect.x);
            if (panel->build) panel->build(cell, col, row, panel->ctx);
        }
    }

    bar_ink(b);
    bar_apply_visibility(b);
    if (panel->shown) panel->shown(s_shown, panel->ctx);
}

void cassette_bars_set_shown(bool shown)
{
    if (!s_created || shown == s_shown) return;
    s_shown = shown;
    for (int i = 0; i < CASSETTE_BARS_COUNT; i++) bar_slide(&s_bars[i], shown);
    if (shown) bars_raise();
}

bool cassette_bars_is_sliding(void)
{
    for (int i = 0; i < CASSETTE_BARS_COUNT; i++) {
        const bar_t *b = &s_bars[i];
        int target = s_shown ? 0 : b->hidden_dx;
        if (b->count > 0 && b->offset != target) return true;
    }
    return false;
}

void cassette_bars_set_page(bool white)
{
    if (!s_created) return;
    s_page_white = white;
    for (int i = 0; i < CASSETTE_BARS_COUNT; i++) bar_ink(&s_bars[i]);
}

