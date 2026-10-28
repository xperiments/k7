/*
 * The EQ panel of the right bar; see cassette_eq.h.
 *
 * The panel fills all six of the bar's cells, two columns of three:
 *
 *   [Back] [HI]
 *   [VU  ] [MED]
 *   [Gain] [LOW]
 *
 * The inside column is the group's chrome - the cell that leaves the group, the VU meter and the
 * gain - and the outer column is the three bands. The four knobs are a 30% smaller 44 px circle
 * with the 24-dot crown outside it, and a line from the centre towards the rim. The Back cell is
 * the shared cassette_button with the icon font's left chevron; its click is the view's, which
 * turns the group back to Empty. The VU cell is cassette_meter's, two 84 px rows.
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
#include "cassette_icons.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_meter.h"
#include "cassette_widgets.h"
#include "knob.h"
#include "knob_editor.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_36);

/* The six cells: two 96 px columns touching at x=688, three rows at 76/170/264, so the
 * bottom of the bar is 360. The 16 px is the inset of the 64 px circle inside each 96 px
 * carcase, not a gap: the carcases share their borders. The Back cell keeps that 64 px circle
 * while the four knobs use a 30% smaller one (44 px) and the dot crown sits outside it, with the
 * line running from the centre towards the rim. */
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

/* The knobs, in the bank's id order: the inside cell's gain, then the outer column's three
 * bands. The ids are contiguous because the editor indexes its bank by them. */
enum {
    KNOB_GAIN = 0,
    KNOB_HI,
    KNOB_MED,
    KNOB_LOW,
    KNOB_COUNT
};

/* The two cells that are not knobs, as negative kinds in the cell map. */
enum {
    CELL_BACK = -1,
    CELL_VU   = -2,
};

/* The panel's control at one bar cell, in the bar's (col,row) order:
 *
 *   [Back] [HI ]
 *   [VU  ] [MED]
 *   [Gain] [LOW]
 */
static const int8_t k_cell_kind[2][3] = {
    { CELL_BACK, CELL_VU, KNOB_GAIN },
    { KNOB_HI, KNOB_MED, KNOB_LOW },
};

/* Where each knob's cell is, for the overlay's point-to-knob mapping. */
static const int8_t k_knob_col[KNOB_COUNT] = { 0, 1, 1, 1 };
static const int8_t k_knob_row[KNOB_COUNT] = { 2, 0, 1, 2 };

/* Which audio band a knob drives: HI=2, MED=1, LOW=0. The gain is not a band. */
static const int8_t k_knob_band[KNOB_COUNT] = { -1, 2, 1, 0 };

/* The controls' identities: what the 3x knob's pillow says, and what the mixer calls them. */
static const char *const k_knob_name[KNOB_COUNT] = { "GAIN", "HI", "MED", "LOW" };

static lv_obj_t *s_cell[KNOB_COUNT];   /* the four knob carcases, for the plate's page ink */
static lv_obj_t *s_circle[KNOB_COUNT];
static lv_obj_t *s_ambient[KNOB_COUNT];   /* the transparent circle carrying the broad shadow */
static lv_obj_t *s_dot[KNOB_COUNT][DOT_COUNT];
static lv_obj_t *s_stick[KNOB_COUNT];
static knob_t s_knob[KNOB_COUNT];     /* 0..100, 50 = 0 dB; the model owns the maths */
static cassette_button_t s_button_back;
static void (*s_back_cb)(void *ctx);  /* the view's: the group goes back to Empty */
static void *s_back_ctx;
static bool s_shown;                  /* the panel is visible: the bar is out */
static bool s_created;

/* ------------------------------------------------------------------ geometry */

/* The right bar's cell at a point, from the layout: the two columns share their 2 px border, so
 * the drawn carcases overlap and the pitch separates them. */
static cassette_rect_t knob_cell(int id)
{
    return cassette_cell_rect(CASSETTE_BAR_RIGHT, k_knob_col[id], k_knob_row[id]);
}

static int knob_at(int x, int y)
{
    for (int id = 0; id < KNOB_COUNT; id++) {
        cassette_rect_t r = knob_cell(id);
        /* The carcases share a 2 px border (CELL_PITCH 94); the tap target stops at the
         * shared edge so a point on the border cannot belong to the knob above it. */
        if (x >= r.x && x < r.x + CELL_W && y >= r.y && y < r.y + CELL_PITCH) return id;
    }
    return -1;
}

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
    knob_editor_crown_ink(s_dot[id], DOT_COUNT, &s_knob[id]);
    lv_obj_set_style_transform_rotation(s_stick[id], knob_angle_ddeg(&s_knob[id]), 0);
}

static void knob_build(lv_obj_t *cell, int id)
{
    /* Two circles, like the buttons: the outer one carries the broad shadow and the inner one
     * is the disc with the contact shadow and the ring. */
    lv_obj_t *ambient = cassette_border_box(cell, KNOB_INSET, KNOB_INSET,
                                            KNOB_D, KNOB_D, KNOB_R,
                                            k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    lv_obj_t *circle = cassette_border_box(ambient, 0, 0,
                                           KNOB_D, KNOB_D, KNOB_R,
                                           k_col_bg, LV_OPA_TRANSP, k_col_button_circle, 2);
    s_ambient[id] = ambient;
    s_circle[id] = circle;

    int cx = CELL_W / 2, cy = CELL_H / 2;
    for (int i = 0; i < DOT_COUNT; i++) {
        float a = (float)i * 15.0f * 3.14159265358979323846f / 180.0f;
        int x = cx + (int)lroundf((float)DOT_R * sinf(a));
        int y = cy - (int)lroundf((float)DOT_R * cosf(a));
        s_dot[id][i] = cassette_box(cell, x - DOT_D / 2, y - DOT_D / 2,
                                      DOT_D, DOT_D, DOT_D / 2, k_col_eq_dim);
    }

    /* The stem is inside the circle: from its centre to the rim, its pivot at the
     * centre, so it swings like the knob's line and stops at the circle's edge. */
    s_stick[id] = cassette_box(cell, cx - STICK_W / 2, cy - STICK_L,
                                 STICK_W, STICK_L, STICK_R, k_col_eq_tick);
    lv_obj_set_style_transform_pivot_x(s_stick[id], STICK_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_stick[id], STICK_L, 0);

    knob_apply(id);
}

/* The Back cell is the shared cassette_button with the icon font's left chevron. Its carcase is
 * its own, so the drawer's pressed ring thickens there; the click is the view's, which turns the
 * group back to Empty. */
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

/* ------------------------------------------------------------------- the bank */

/* The knob whose cell is at a point. The overlay covers the bar too, so the second tap of a
 * double click maps back this way. */
static int bank_id_at(int x, int y, void *ctx)
{
    (void)ctx;
    return knob_at(x, y);
}

static void bank_repaint(int id, void *ctx)
{
    (void)ctx;
    if (id >= 0 && id < KNOB_COUNT) knob_apply(id);
}

static void bank_publish(int id, void *ctx)
{
    (void)ctx;
    if (id >= 0 && id < KNOB_COUNT) knob_send(id);
}

static lv_obj_t *bank_disc(int id, void *ctx)
{
    (void)ctx;
    return (id >= 0 && id < KNOB_COUNT) ? s_circle[id] : NULL;
}

static bool bank_open_allowed(void *ctx)
{
    (void)ctx;
    return s_shown && !cassette_bars_is_sliding();
}

static const knob_editor_bank_t k_eq_bank = {
    .knobs = s_knob,
    .count = KNOB_COUNT,
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
 * still owes one, or ignores it because an editor is open. The Back and VU cells have no
 * handler of their own: Back's is the shared button's, the meter is a picture. */
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

/* One cell of the panel: the bar made the carcase and this fills it. A knob gets its circle,
 * crown, stick and its two click handlers, the Back cell the shared button, the VU cell the
 * meter. The click payload is the knob's id plus one, so a valid id 0 is not NULL. */
static void eq_cell_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (!cell || col < 0 || col > 1 || row < 0 || row > 2) return;
    int kind = k_cell_kind[col][row];

    if (kind == CELL_BACK) {
        back_build(cell);
    } else if (kind == CELL_VU) {
        cassette_meter_create(cell);
    } else {
        s_cell[kind] = cell;
        knob_build(cell, kind);
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
    knob_editor_set_bank(visible ? &k_eq_bank : NULL);
}

/* The white page paints the four knobs the way it paints a button: the 44 px circle becomes
 * the cap - a flat disc with a soft lower-right shadow, no rim - while the crown and the stick
 * keep their inks, except the four knobs' cells: those are the knob plate here - filled
 * k_col_knob_rect with a k_col_bg border, so no carcase line runs round a knob. The Back cell's
 * button inks itself, and the VU cell keeps the page's own fill: the meter's rows are its own
 * inks on both pages. The dark page is the palette's line art throughout. The bar has already
 * painted every cell's default. */
static void eq_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int id = 0; id < KNOB_COUNT; id++) {
        if (!s_cell[id]) continue;
        lv_obj_set_style_bg_color(s_cell[id], white ? k_col_knob_rect : k_col_bg, 0);
        lv_obj_set_style_border_color(s_cell[id],
                                      white ? k_col_bg : k_col_button_rect, 0);
        if (!s_circle[id]) continue;
        cassette_circle_face(s_ambient[id], s_circle[id], white, k_col_knob_cap);
        lv_obj_set_style_border_width(s_circle[id], white ? 0 : 2, 0);
        if (!white) lv_obj_set_style_border_color(s_circle[id], k_col_button_circle, 0);
    }

    /* Back: the shared button inks its own carcase, disc and letter from its cap. */
    cassette_button_set_page(&s_button_back, white);
}

/* The panel value the right bar fills: two columns of three, all six the panel's. The cells do
 * not bubble gestures - a swipe that starts on a knob is not a ladder gesture - and the page ink
 * is this panel's: the knobs' plate and the Back button's cap. */
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
    s_back_cb = NULL;
    s_back_ctx = NULL;
    knob_init(&s_knob[KNOB_GAIN], KNOB_RANGE_GAIN);
    for (int id = 1; id < KNOB_COUNT; id++) knob_init(&s_knob[id], KNOB_RANGE_BAND);
    for (int id = 0; id < KNOB_COUNT; id++) knob_set_name(&s_knob[id], k_knob_name[id]);

    /* The bar builds the five cells and hands each to eq_cell_build(), and the panel value
     * inks them for the page the bar is on. */
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_eq_panel);

    /* The audio starts from the same values; this makes a rebuilt screen agree with it. */
    for (int id = 0; id < KNOB_COUNT; id++) knob_send(id);
}

void cassette_eq_set_back_cb(void (*cb)(void *ctx), void *ctx)
{
    s_back_cb = cb;
    s_back_ctx = ctx;
}

const cassette_panel_t *cassette_eq_panel(void)
{
    return &k_eq_panel;
}
