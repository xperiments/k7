/*
 * The EQ panel of the right bar; see cassette_eq.h.
 *
 * The panel fills five of the bar's six cells: GAIN, HI, MED, LOW and R. The first column's
 * middle cell is M, the bar's chrome. The four knobs are a 30% smaller 44 px circle with the
 * 24-dot crown outside it, and a line from the centre towards the rim. R is the shared
 * cassette_button on the bar's cell, with the same 64 px circle, letter, one pressed state and
 * click pulse as the Transport panel's; it has no action yet. The next/previous arrows are the
 * Transport panel's < and >.
 *
 * The bar builds the carcases and hands each one to eq_cell_build() through the panel value at
 * the bottom of this file; this module creates no carcase and registers no root. The bar also
 * slides them, dispatches the page and swaps the panel out when the Menu puts its Pad up; a
 * swap back rebuilds the cells in place without touching this module's state.
 *
 * The modal half - the 3x dial, the overlay, the 260 ms delayed open, the blink and the
 * cassette's cut-out - is knob_editor's, shared with the RGB panel. This file hands it a bank:
 * the four knobs, the cell at a point, the pillow's text, the repaint after a value moves, the
 * audio call, the disc to blink and the shown gate. It keeps only the cells, the values and the
 * audio.
 *
 * The panel is hidden at rest, so the entry view is the untouched full-scale cassette; the bar
 * shows it with the Transport panel, and losing it ends edit mode and the delayed open.
 */
#include <math.h>

#include "lvgl.h"

#include "cassette_bars.h"
#include "cassette_button.h"
#include "cassette_eq.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "knob.h"
#include "knob_editor.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_36);

/* The six cells: two 96 px columns touching at x=688, three rows at 76/170/264, so the
 * bottom of the bar is 360. The 16 px is the inset of the 64 px circle inside each
 * 96 px carcase, not a gap: the carcases share their borders like the Transport panel's.
 * R keeps that 64 px circle - and so does M, the bar's chrome cell in the first column's
 * middle - while the four knobs use a 30% smaller one (44 px) and the dot crown sits outside
 * it, with the line running from the centre towards the rim. */
#define CELL_W       CASSETTE_CELL_W
#define CELL_H       CASSETTE_CELL_H
#define CELL_PITCH   CASSETTE_CELL_PITCH
#define KNOB_D       44
#define KNOB_R       (KNOB_D / 2)
#define KNOB_INSET   ((CELL_W - KNOB_D) / 2)
#define DOT_COUNT    24
#define DOT_R        32   /* outside the knob circle, inside the carcase */
#define DOT_D        4
#define STICK_W      4
#define STICK_L      16   /* the owner's tuned line: 4 wide, 16 long */
#define STICK_R      3

enum {
    SLOT_GAIN = 0,
    SLOT_HI   = 1,
    SLOT_MED  = 2,
    SLOT_LOW  = 3,
    SLOT_R    = 4,   /* the R button; its function is TBD */
    SLOT_COUNT = 5,
    KNOB_SLOTS = 4,
};

/* Which cell each slot occupies. The gain column is on the inside, next to the cassette;
 * the EQ column is the outer one. The first column's middle cell (0,1) is not here: it is
 * the bar's chrome, the Menu's M. */
static const int8_t k_slot_col[SLOT_COUNT] = { 0, 1, 1, 1, 0 };
static const int8_t k_slot_row[SLOT_COUNT] = { 0, 0, 1, 2, 2 };

/* The panel's slot at one bar cell, in the bar's (col,row) order. -1 is the bar's chrome,
 * which no panel builds over. */
static const int8_t k_cell_slot[2][3] = {
    { SLOT_GAIN, -1, SLOT_R },
    { SLOT_HI, SLOT_MED, SLOT_LOW },
};

/* Which audio band a knob drives: HI=2, MED=1, LOW=0. The gain is not a band. */
static const int8_t k_slot_band[KNOB_SLOTS] = { -1, 2, 1, 0 };

/* The controls' identities: what the 3x knob's pillow says, and what the mixer calls them. */
static const char *const k_knob_name[KNOB_SLOTS] = { "GAIN", "HI", "MED", "LOW" };

static lv_obj_t *s_cell[KNOB_SLOTS];   /* the four knob carcases, for the plate's page ink */
static lv_obj_t *s_circle[KNOB_SLOTS];
static lv_obj_t *s_ambient[KNOB_SLOTS];   /* the transparent circle carrying the broad shadow */
static lv_obj_t *s_dot[KNOB_SLOTS][DOT_COUNT];
static lv_obj_t *s_stick[KNOB_SLOTS];
static knob_t s_knob[KNOB_SLOTS];     /* 0..100, 50 = 0 dB; the model owns the maths */
static bool s_shown;                  /* the panel is visible: the bar is out */
static bool s_created;

/* ------------------------------------------------------------------ geometry */

/* The right bar's cell at a slot, from the layout: the two columns share their 2 px
 * border, so the drawn carcases overlap and the pitch separates them. */
static cassette_rect_t cell_rect(int slot)
{
    return cassette_cell_rect(CASSETTE_BAR_RIGHT, k_slot_col[slot], k_slot_row[slot]);
}
static int cell_x(int slot) { return cell_rect(slot).x; }
static int cell_y(int slot) { return cell_rect(slot).y; }

static int knob_at(int x, int y)
{
    for (int slot = 0; slot < KNOB_SLOTS; slot++) {
        int cx = cell_x(slot), cy = cell_y(slot);
        /* The carcases share a 2 px border (CELL_PITCH 94); the tap target stops at the
         * shared edge so a point on the border cannot belong to the knob above it. */
        if (x >= cx && x < cx + CELL_W && y >= cy && y < cy + CELL_PITCH) return slot;
    }
    return -1;
}

/* --------------------------------------------------------------------- audio */

static void knob_send(int slot)
{
    float db = knob_db(&s_knob[slot]);
    if (slot == SLOT_GAIN) mini_icecast_gain_set(db);
    else mini_icecast_eq_set(k_slot_band[slot], db);
}

/* ---------------------------------------------------------------------- draw */

static void knob_apply(int slot)
{
    knob_editor_crown_ink(s_dot[slot], DOT_COUNT, &s_knob[slot]);
    lv_obj_set_style_transform_rotation(s_stick[slot], knob_angle_ddeg(&s_knob[slot]), 0);
}

static void knob_build(lv_obj_t *cell, int slot)
{
    /* Two circles, like the buttons: the outer one carries the broad shadow and the inner one
     * is the disc with the contact shadow and the ring. */
    lv_obj_t *ambient = cassette_border_box(cell, KNOB_INSET, KNOB_INSET,
                                            KNOB_D, KNOB_D, KNOB_R,
                                            k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    lv_obj_t *circle = cassette_border_box(ambient, 0, 0,
                                           KNOB_D, KNOB_D, KNOB_R,
                                           k_col_bg, LV_OPA_TRANSP, k_col_button_circle, 2);
    s_ambient[slot] = ambient;
    s_circle[slot] = circle;

    int cx = CELL_W / 2, cy = CELL_H / 2;
    for (int i = 0; i < DOT_COUNT; i++) {
        float a = (float)i * 15.0f * 3.14159265358979323846f / 180.0f;
        int x = cx + (int)lroundf((float)DOT_R * sinf(a));
        int y = cy - (int)lroundf((float)DOT_R * cosf(a));
        s_dot[slot][i] = cassette_box(cell, x - DOT_D / 2, y - DOT_D / 2,
                                      DOT_D, DOT_D, DOT_D / 2, k_col_eq_dim);
    }

    /* The stem is inside the circle: from its centre to the rim, its pivot at the
     * centre, so it swings like the knob's line and stops at the circle's edge. */
    s_stick[slot] = cassette_box(cell, cx - STICK_W / 2, cy - STICK_L,
                                 STICK_W, STICK_L, STICK_R, k_col_eq_tick);
    lv_obj_set_style_transform_pivot_x(s_stick[slot], STICK_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_stick[slot], STICK_L, 0);

    knob_apply(slot);
}

/* R is the shared cassette_button on the bar's own cell. The cell's carcase is shared with
 * the column and never thickens, so R's pressed look is the disc's ring on the dark page and
 * the pulse alone on the white one, where the disc is the cap and its rim is fixed. R has no
 * action, so its pulse is the whole of its click, as P's is on the left. */
static cassette_button_t s_button_r;

static void r_button_build(lv_obj_t *cell)
{
    cassette_cap_t cap = { k_col_button_r, k_col_button_white };
    cassette_button_spec_t spec = {
        .name = "REC",
        .text = "R",
        .font = &jura_36,
        .cap = cap,
        /* The bar's carcase is shared with the column and never thickens. */
        .carcase_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W },
        .disc_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .on_click = NULL,
        .ctx = NULL,
    };
    cassette_button_create(&s_button_r, cell, &spec);
}

/* ------------------------------------------------------------------- the bank */

/* The knob whose cell is at a point. The overlay covers the bar too, so the second tap of a
 * double click maps back this way. */
static int bank_id_at(int x, int y, void *ctx)
{
    (void)ctx;
    return knob_at(x, y);
}

static void bank_repaint(int slot, void *ctx)
{
    (void)ctx;
    if (slot >= 0 && slot < KNOB_SLOTS) knob_apply(slot);
}

static void bank_publish(int slot, void *ctx)
{
    (void)ctx;
    if (slot >= 0 && slot < KNOB_SLOTS) knob_send(slot);
}

static lv_obj_t *bank_disc(int slot, void *ctx)
{
    (void)ctx;
    return (slot >= 0 && slot < KNOB_SLOTS) ? s_circle[slot] : NULL;
}

static bool bank_open_allowed(void *ctx)
{
    (void)ctx;
    return s_shown && !cassette_bars_is_sliding();
}

static const knob_editor_bank_t k_eq_bank = {
    .knobs = s_knob,
    .count = KNOB_SLOTS,
    .id_at = bank_id_at,
    .label = NULL,            /* the knob's own name */
    .repaint = bank_repaint,
    .publish = bank_publish,
    .cell_disc = bank_disc,
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

/* A knob's click arms the editor's delayed open; the editor drops it when a reset gesture
 * still owes one, or ignores it because an editor is open. R is the shared button's cell:
 * its click handler is the button's, not this one. */
static void cell_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;

    int slot = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (slot >= KNOB_SLOTS) return;
    if (!knob_click_is_tap()) return;

    knob_editor_tap(slot);
}

/* A double click on a small knob is the reset gesture. It runs before the second CLICKED, so
 * the editor disarms the delayed open, resets the knob and owes the click that follows; the
 * 3x dial never appears. */
static void cell_double_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (slot >= KNOB_SLOTS) return;
    knob_editor_double_tap(slot);
}

/* One cell of the panel: the bar made the carcase and this fills it. A knob gets its circle,
 * crown, stick and its two click handlers; R gets the shared button. The payload is the slot,
 * plus one so a valid slot 0 is not NULL. */
static void eq_cell_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (!cell || col < 0 || col > 1 || row < 0 || row > 2) return;
    int slot = k_cell_slot[col][row];
    if (slot < 0) return;   /* the bar's chrome: M is not this panel's */

    if (slot < KNOB_SLOTS) {
        s_cell[slot] = cell;
        knob_build(cell, slot);
        lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(slot + 1));
        lv_obj_add_event_cb(cell, cell_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED,
                            (void *)(intptr_t)(slot + 1));
    } else {
        r_button_build(cell);
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
    knob_editor_set_bank(visible ? &k_eq_bank : NULL);
}

/* The white page paints the four knobs the way it paints a button: the 44 px circle becomes
 * the cap - a flat disc with a soft lower-right shadow, no rim - while the crown and the stick
 * keep their inks, except the four knobs' cells: those are the knob plate here - filled
 * k_col_knob_rect with a k_col_bg border, so no carcase line runs round a knob. R's carcase
 * keeps the page's fill and the drawer's border, inked by its own button. The dark page is the
 * palette's line art throughout. The bar has already painted every cell's default. */
static void eq_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int slot = 0; slot < KNOB_SLOTS; slot++) {
        if (!s_cell[slot]) continue;
        lv_obj_set_style_bg_color(s_cell[slot], white ? k_col_knob_rect : k_col_bg, 0);
        lv_obj_set_style_border_color(s_cell[slot],
                                      white ? k_col_bg : k_col_button_rect, 0);
        if (!s_circle[slot]) continue;
        cassette_circle_face(s_ambient[slot], s_circle[slot], white, k_col_knob_cap);
        lv_obj_set_style_border_width(s_circle[slot], white ? 0 : 2, 0);
        if (!white) lv_obj_set_style_border_color(s_circle[slot], k_col_button_circle, 0);
    }

    /* R: the shared button inks its own carcase, disc and letter from its cap. */
    cassette_button_set_page(&s_button_r, white);
}

/* The panel value the right bar fills: two columns of three, of which the bar keeps (0,1) for
 * its chrome, M. The cells do not bubble gestures - a swipe that starts on a knob is not a
 * drawer gesture - and the page ink is this panel's: the knobs' plate and R's cap. */
static const cassette_panel_t k_eq_panel = {
    .columns = 2,
    .rows = 3,
    .cell_rect = NULL,
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
    knob_init(&s_knob[SLOT_GAIN], KNOB_RANGE_GAIN);
    for (int slot = 1; slot < KNOB_SLOTS; slot++) knob_init(&s_knob[slot], KNOB_RANGE_BAND);
    for (int slot = 0; slot < KNOB_SLOTS; slot++) knob_set_name(&s_knob[slot], k_knob_name[slot]);

    /* The bar builds the five cells and hands each to eq_cell_build(), and the panel value
     * inks them for the page the bar is on. */
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_eq_panel);

    /* The audio starts from the same values; this makes a rebuilt screen agree with it. */
    for (int slot = 0; slot < KNOB_SLOTS; slot++) knob_send(slot);
}

const cassette_panel_t *cassette_eq_panel(void)
{
    return &k_eq_panel;
}
