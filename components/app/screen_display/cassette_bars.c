/*
 * The two edge strips and their cells; see cassette_bars.h.
 *
 * A bar is a value: the cells it built, their resting x, the offset that takes them off
 * screen and the panel that fills the ones that are not chrome. A slide animates the offset
 * and writes each root's x, so no cell owns the animation and a reversal replaces one anim,
 * not six. The cells are hidden when a slide out completes and unhidden when a slide in
 * starts.
 *
 * The carcases are the bar's, not the panels': one 96x96 rounded box per cell at the layout's
 * rect, with the shared 2 px border, made clickable and handed to the panel's build(). That is
 * what makes a cell's control swappable without moving the grid, and it is why a panel that
 * arrives late still lands on the current page and at the current offset.
 *
 * The chrome is the bar's too: the right bar's M is one cell no panel sees, built with the
 * shared cassette_button and calling the Menu-tap callback the view installed.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_bars.h"

#include <stdint.h>
#include <string.h>

#include "cassette_button.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

/* A bar can hold a chrome cell plus a full 2x3 panel: six on the right, three on the left. */
#define BARS_MAX_ROOTS   8
#define BARS_MAX_CHROME  1

/* One cell the bar keeps for itself, present whatever panel is up. `text` is the button's
 * label; the cap is the palette's and is set where the button is built. */
typedef struct {
    int col;
    int row;
    const char *name;   /* the control's identity */
    const char *text;
} bar_chrome_t;

/* M: the Menu's only door, in the first column's middle cell. */
static const bar_chrome_t k_chrome_right[] = {
    { .col = 0, .row = 1, .name = "MENU", .text = "M" },
};

/* A bar's fixed part: its layout geometry, its chrome and the hit rule its cells default to
 * before a panel is installed. */
typedef struct {
    cassette_bar_t layout;
    const bar_chrome_t *chrome;
    int chrome_count;
    bool gesture_bubble;
} bar_spec_t;

static const bar_spec_t k_spec[CASSETTE_BARS_COUNT] = {
    /* The Transport drawer's buttons bubble gestures, so a swipe that starts on one still
     * toggles the drawer. */
    { CASSETTE_BAR_LEFT, NULL, 0, true },
    /* The DJ cells do not: a swipe that starts on one is not a drawer or Level gesture. */
    { CASSETTE_BAR_RIGHT, k_chrome_right, 1, false },
};

typedef struct {
    int side;                             /* the bar_side_t, as an index */
    const cassette_panel_t *panel;
    lv_obj_t *roots[BARS_MAX_ROOTS];      /* the chrome's cells first, then the panel's */
    int rest_x[BARS_MAX_ROOTS];
    int count;
    int chrome_count;
    cassette_button_t chrome_button[BARS_MAX_CHROME];
    int hidden_dx;
    int32_t offset;                       /* the x offset the roots are drawn at */
} bar_t;

static bar_t s_bars[CASSETTE_BARS_COUNT];
static lv_obj_t *s_screen;
static bool s_created;
static bool s_shown;
static bool s_page_white;
static void (*s_menu_cb)(void *ctx);
static void *s_menu_ctx;

/* ---------------------------------------------------------------- the cells */

static bool bar_is_chrome(const bar_t *b, int col, int row)
{
    const bar_spec_t *spec = &k_spec[b->side];
    for (int i = 0; i < spec->chrome_count; i++)
        if (spec->chrome[i].col == col && spec->chrome[i].row == row) return true;
    return false;
}

/* One carcase, the bar's own: the layout's rect on the current page, with the shared corner,
 * border and hit. The panel builds its control inside it and never moves it. */
static lv_obj_t *bar_cell_new(bar_t *b, cassette_rect_t rect, bool gesture_bubble)
{
    lv_obj_t *cell = cassette_border_box(s_screen, rect.x + b->offset, rect.y, rect.w, rect.h,
                                         CASSETTE_CORNER_R,
                                         s_page_white ? k_col_white : k_col_bg, LV_OPA_COVER,
                                         k_col_button_rect, CASSETTE_BORDER_W);
    lv_obj_set_clickable(cell, true);
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

/* The page ink: the cells' default first, then the chrome's buttons (M has a cap and a ring
 * of its own) and then the panel's exceptions - the knob plate, the discs, R's cap. */
static void bar_ink(bar_t *b)
{
    for (int i = 0; i < b->count; i++) {
        lv_obj_t *cell = b->roots[i];
        if (!cell) continue;
        lv_obj_set_style_bg_color(cell, s_page_white ? k_col_white : k_col_bg, 0);
        lv_obj_set_style_border_color(cell, k_col_button_rect, 0);
    }
    for (int i = 0; i < b->chrome_count; i++)
        cassette_button_set_page(&b->chrome_button[i], s_page_white);
    if (b->panel && b->panel->set_page) b->panel->set_page(s_page_white, b->panel->ctx);
}

/* --------------------------------------------------------------- the chrome */

/* M's click: the Menu opens, if the bar is out and settled. The button has already applied the
 * tap guard and started the pulse; what is left is the strip's own gate. The rest - the
 * settled controls pose, no knob editing, the Menu not already open - is the Controls model's,
 * behind the view's callback. */
static void bar_chrome_clicked(void *ctx)
{
    bar_t *b = ctx;
    if (!b || !s_shown || cassette_bars_is_sliding()) return;
    if (s_menu_cb) s_menu_cb(s_menu_ctx);
}

static void bar_chrome_build(bar_t *b)
{
    const bar_spec_t *spec = &k_spec[b->side];
    for (int i = 0; i < spec->chrome_count && i < BARS_MAX_CHROME; i++) {
        const bar_chrome_t *c = &spec->chrome[i];
        cassette_rect_t rect = cassette_cell_rect(spec->layout, c->col, c->row);
        if (rect.w <= 0 || rect.h <= 0) continue;

        /* Chrome keeps the bar's own hit rule: the right bar's cells do not bubble
         * gestures, so a swipe that starts on M is not a drawer gesture either. */
        lv_obj_t *cell = bar_cell_new(b, rect, spec->gesture_bubble);
        bar_root_add(b, cell, rect.x);
        lv_obj_set_hidden(cell, true);

        /* M's cap is the page's black with its own light letter; the carcase is shared with
         * the column below, so it does not thicken on a press - M's pressed look is the
         * disc's ring on the dark page and the pulse alone on the white one. */
        cassette_cap_t cap = { k_col_bg, k_col_button_white };
        cassette_button_spec_t button = {
            .name = c->name,
            .text = c->text,
            .font = NULL,                 /* the button's own Jura 36 */
            .cap = cap,
            .carcase_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W },
            .disc_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
            .on_click = bar_chrome_clicked,
            .ctx = b,
        };
        cassette_button_create(&b->chrome_button[i], cell, &button);
        b->chrome_count++;
    }
}

/* ---------------------------------------------------------------- the panel */

/* Take the current panel's cells down. The panel is told it is not visible first, so its own
 * state - a knob edit, a pending open - goes with them; the carcases and everything built
 * inside them are the bar's to delete. */
static void bar_panel_clear(bar_t *b)
{
    if (b->panel && b->panel->shown) b->panel->shown(false, b->panel->ctx);
    for (int i = b->chrome_count; i < b->count; i++) {
        if (!b->roots[i]) continue;
        /* A click pulse may still be running on a carcase, and LVGL does not cancel an
         * animation when its variable is deleted. */
        lv_anim_delete(b->roots[i], NULL);
        lv_obj_delete(b->roots[i]);
    }
    b->count = b->chrome_count;
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
        bar_chrome_build(b);
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
            /* The chrome cells are the bar's; no panel builds over them. */
            if (bar_is_chrome(b, col, row)) continue;

            cassette_rect_t rect = panel->cell_rect ? panel->cell_rect(col, row)
                                                    : cassette_cell_rect(k_spec[side].layout, col, row);
            if (rect.w <= 0 || rect.h <= 0) continue;

            lv_obj_t *cell = bar_cell_new(b, rect, panel->gesture_bubble);
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

void cassette_bars_set_menu_cb(void (*cb)(void *ctx), void *ctx)
{
    s_menu_cb = cb;
    s_menu_ctx = ctx;
}
