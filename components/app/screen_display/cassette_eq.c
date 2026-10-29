/*
 * The EQ panel of the right bar; see cassette_eq.h.
 *
 * The panel fills all six of the bar's cells, two columns of three:
 *
 *   [VU  ] [HI]
 *   [VU  ] [MID]
 *   [Gain] [LOW]
 *
 * The inside column is the VU meter - one 96x190 plate over the top two cells - and the gain,
 * and the outer column is the three bands. There is no Back cell: the mode bar's sliders button
 * is the toggle, so a tap on it puts the group away. The four knobs are a 30% smaller 44 px cap
 * with a ring of marks outside it - no dots - and a line from the centre towards the rim; the
 * marks are the two ends and 0 dB's twelve o'clock, and the value marker is the brighter one
 * that points at the value. The VU plate is cassette_meter's, two 84 px rows.
 *
 * A knob's value moves by dragging it up and down - 2 px a step, with a magnet at 0 dB - and a
 * tap on it opens the 3x dial, which prints the two ends (-26 and +6, or -12 and +12), the
 * graduations, and the live value in its pillow. The bar builds the carcases and hands each one
 * to eq_cell_build() through the panel value at the bottom of this file; this module creates no
 * carcase and registers no root. The bar also slides them, dispatches the page and swaps the
 * panel out when the Menu puts its Pad up; a swap back rebuilds the cells in place without
 * touching this module's state.
 *
 * The modal half - the 3x dial, the 260 ms delayed open and the cassette's cut-out -
 * is knob_editor's, shared with the RGB panel, and the moving is the Pad's: while the dial is up
 * the editor takes these cells away and the site of them is the Pad's ratchet, tap and left
 * swipe. This file hands the editor a bank: the four knobs, the cell at a point, the repaint
 * after a value moves, the audio call and the shown gate. It keeps only the
 * cells, the values, the marks and the audio.
 *
 * The panel is hidden at rest, so the entry view is the untouched full-scale cassette; the bar
 * shows it with the mode bar, and losing it ends edit mode and the delayed open.
 */
#include "lvgl.h"

#include "cassette_bars.h"
#include "cassette_button_rule.h"
#include "cassette_eq.h"
#include "cassette_layout.h"
#include "cassette_meter.h"
#include "knob.h"
#include "knob_cell.h"
#include "knob_editor.h"
#include "mini_icecast.h"

/* The six cells: two 96 px columns touching at x=688, three rows at 76/170/264, so the bottom
 * of the bar is 360. The carcases share their borders; the knobs themselves are knob_cell's. */
#define CELL_W       CASSETTE_CELL_W
#define CELL_PITCH   CASSETTE_CELL_PITCH

/* The knobs, in the bank's id order: the inside cell's gain, then the outer column's three
 * bands. The ids are contiguous because the editor indexes its bank by them. */
enum {
    KNOB_GAIN = 0,
    KNOB_HI,
    KNOB_MID,
    KNOB_LOW,
    KNOB_COUNT
};

/* The cells that are not knobs, as negative kinds in the cell map. CELL_METER's plate covers the
 * cell below it too, so that cell is never built. */
enum {
    CELL_METER = -1,
    CELL_COVERED = -2,
};

/* The panel's control at one bar cell, in the bar's (col,row) order:
 *
 *   [VU  ] [HI ]
 *   [VU  ] [MID]
 *   [Gain] [LOW]
 */
static const int8_t k_cell_kind[2][3] = {
    { CELL_METER, CELL_COVERED, KNOB_GAIN },
    { KNOB_HI, KNOB_MID, KNOB_LOW },
};

/* The meter's plate: the top two cells of the inside column as one 96x190 surface, and nothing
 * where the second of them would have been. */
static cassette_rect_t eq_cell_rect(int col, int row)
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

/* Which audio band a knob drives: HI=2, MID=1, LOW=0. The gain is not a band. */
static const int8_t k_knob_band[KNOB_COUNT] = { -1, 2, 1, 0 };

/* The controls' identities: what the 3x dial's pillow says, and what the mixer calls them. */
static const char *const k_knob_name[KNOB_COUNT] = { "GAIN", "HI", "MID", "LOW" };

/* What the cell's name pillow prints, one letter each: the pillow is a badge on a 96 px cell,
 * and the 3x dial is where the names are spelled out (ADR 0035). */
static const char *const k_knob_short[KNOB_COUNT] = { "G", "H", "M", "L" };

/* The two ends of each knob, printed on the 3x dial at their own angles: the bands cut to
 * -26 and boost to +6, the trim is symmetric. knob_label() reads these back when the knob
 * sits on one, so it is a marked position as well as a plaque. */
static const knob_mark_t k_band_marks[] = {
    { 0, "-26", true },
    { 100, "+6", true },
};
static const knob_mark_t k_gain_marks[] = {
    { 0, "-12", true },
    { 100, "+12", true },
};

static knob_cell_t s_knob_cell[KNOB_COUNT];
static knob_t s_knob[KNOB_COUNT];     /* 0..100, 50 = 0 dB; the model owns the maths */
static bool s_shown;                  /* the panel is visible: the bar is out */
static bool s_created;

/* ------------------------------------------------------------------ geometry */

/* --------------------------------------------------------------------- audio */

static void knob_send(int id)
{
    float db = knob_db(&s_knob[id]);
    if (id == KNOB_GAIN) mini_icecast_gain_set(db);
    else mini_icecast_eq_set(k_knob_band[id], db);
}

/* ---------------------------------------------------------------------- draw */

static void knob_apply(int id)
{
    knob_cell_apply(&s_knob_cell[id], &s_knob[id]);
}

static void knob_build(lv_obj_t *cell, int id)
{
    if (!knob_cell_create(&s_knob_cell[id], cell)) return;
    /* The cell names its control in the pillow over the dial, so the 3x surface only has to
     * say where the value is (ADR 0035). */
    knob_cell_add_name_pill(&s_knob_cell[id], k_knob_short[id]);
    knob_apply(id);
}

/* ------------------------------------------------------------------- the bank */

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
    if (id >= 0 && id < KNOB_COUNT) knob_send(id);
}

static bool bank_open_allowed(void *ctx)
{
    (void)ctx;
    return s_shown && !cassette_bars_is_sliding();
}

static const knob_editor_bank_t k_eq_bank = {
    .knobs = s_knob,
    .count = KNOB_COUNT,
    .repaint = bank_repaint,
    .publish = bank_publish,
    .open_allowed = bank_open_allowed,
    .ctx = NULL,
};

/* ------------------------------------------------------------------- the cells */

/* A knob's own tap guard: the same rule the buttons apply, because a swipe that starts on a
 * knob is not the knob's click. The knob is not a button - it has no press look and no pulse
 * - so it reads the portable rule directly. */
static bool knob_click_is_tap(void)
{
    lv_indev_t *indev = lv_indev_active();
    bool gesturing = indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
    bool moved = indev && lv_indev_get_press_moved(indev);
    return cassette_button_is_tap(moved, gesturing);
}

/* The knob's value is the drag's, and the drag is the only way it moves (ADR 0027). The press
 * at 1x runs at 2 px a step; the same finger on the 3x dial would be four times as coarse, so
 * that one is the editor's.
 */
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

/* A short tap arms the editor's delayed open; the editor drops it when a reset gesture still
 * owes one, or ignores it because an editor is open. The Back and VU cells have no handler of
 * their own: Back's is the shared button's, the meter is a picture. */
static void cell_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;

    int id = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (id < 0 || id >= KNOB_COUNT) return;
    if (!knob_click_is_tap()) return;

    knob_editor_tap(id);
}

/* A double click on a small knob is the reset gesture. It runs before the second CLICKED, so
 * the editor disarms the delayed open, resets the knob and owes the click that follows; the
 * 3x dial never appears. */
static void cell_double_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (id < 0 || id >= KNOB_COUNT) return;
    knob_editor_double_tap(id);
}

/* One cell of the panel: the bar made the carcase and this fills it. A knob gets its dial and
 * the five handlers that make it one - press, move and release for the drag, click and double
 * click for the editor; the meter's plate gets the meter. The payload is the knob's id plus
 * one, so a valid id 0 is not NULL. */
static void eq_cell_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (!cell || col < 0 || col > 1 || row < 0 || row > 2) return;
    int kind = k_cell_kind[col][row];

    if (kind == CELL_METER) {
        cassette_meter_create(cell);
    } else if (kind == CELL_COVERED) {
        return;   /* the meter's plate already covers this cell */
    } else {
        knob_build(cell, kind);
        lv_obj_add_event_cb(cell, cell_pressed_cb, LV_EVENT_PRESSED,
                            (void *)(intptr_t)(kind + 1));
        lv_obj_add_event_cb(cell, cell_pressing_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(cell, cell_released_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(kind + 1));
        lv_obj_add_event_cb(cell, cell_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED,
                            (void *)(intptr_t)(kind + 1));
    }
}

/* ---------------------------------------------------------------------- public */

/* The bar reports when the panel is visible: it is out. The bank goes with it, so a tap on a
 * cell and a tap after a swap can only ever reach the knobs that are on screen, and losing the
 * panel ends the editor or disarms the pending open. */
static void eq_shown(bool visible, void *ctx)
{
    (void)ctx;
    s_shown = visible;
    /* Losing the panel ends the editor - unless the editor is the one that took the bar: it
     * puts its Pad where these cells were, and the values it is editing are these, so the
     * bank has to survive that swap. The cells themselves are gone meanwhile, which is why
     * every hook above checks s_shown. */
    if (!visible && knob_editor_is_editing()) return;
    knob_editor_set_bank(visible ? &k_eq_bank : NULL);
}

/* The white page paints the four knobs the way it paints a button: the 44 px cap becomes a flat
 * disc with a soft lower-right shadow, no rim - while the ring and the stick keep their inks,
 * except the four knobs' cells: those are the knob plate here - filled k_col_knob_rect with a
 * k_col_bg border, so no carcase line runs round a knob. The meter's plate takes the same ink, so
 * the block reads as one surface; the columns' own inks do not turn. The dark page is the
 * palette's line art throughout. The bar has already painted every cell's default. */
static void eq_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int id = 0; id < KNOB_COUNT; id++) knob_cell_set_page(&s_knob_cell[id], white);

    /* The meter's plate takes the same ink as a knob's: on the white page it is the knob plate, so
     * the block reads as one surface and not as four dials beside a button. */
    cassette_meter_set_page(white);
}

/* The panel value the right bar fills: two columns of three, the inside column's top two cells
 * one plate. The cells do not bubble gestures - a swipe that starts on a knob is not a ladder
 * gesture - and the page ink is this panel's: the knobs' plate. */
static const cassette_panel_t k_eq_panel = {
    .columns = 2,
    .rows = 3,
    .cell_rect = eq_cell_rect,
    .gesture_bubble = false,
    .build = eq_cell_build,
    .set_page = eq_set_page,
    .shown = eq_shown,
    .ctx = NULL,
};

void cassette_eq_create(lv_obj_t *parent)
{
    (void)parent;                 /* the 3x dial is knob_editor's, on the screen */
    if (s_created) return;
    s_created = true;

    s_shown = false;
    knob_init(&s_knob[KNOB_GAIN], KNOB_RANGE_GAIN);
    knob_set_marks(&s_knob[KNOB_GAIN], k_gain_marks, 2);
    for (int id = 1; id < KNOB_COUNT; id++) {
        knob_init(&s_knob[id], KNOB_RANGE_BAND);
        knob_set_marks(&s_knob[id], k_band_marks, 2);
    }
    for (int id = 0; id < KNOB_COUNT; id++) knob_set_name(&s_knob[id], k_knob_name[id]);

    /* The bar builds the five cells and hands each to eq_cell_build(), and the panel value
     * inks them for the page the bar is on. */
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_eq_panel);

    /* The audio starts from the same values; this makes a rebuilt screen agree with it. */
    for (int id = 0; id < KNOB_COUNT; id++) knob_send(id);
}

const cassette_panel_t *cassette_eq_panel(void)
{
    return &k_eq_panel;
}
