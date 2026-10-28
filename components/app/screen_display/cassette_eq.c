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
 * The edit ordering is knob_edit_t's (knob_model): the tap that arms the delayed open, the
 * double tap that disarms it and resets, the expiry that opens, and the close that ends a
 * pending or an open edit. This file feeds those events in and applies the effects it gets
 * back, so it owns only the 260 ms timer, the overlay, the blink and the gates.
 *
 * The edit overlay is the whole input policy for the panel: while a knob is active it is the
 * only clickable object on the screen, so M and R, the Transport panel and the peek cannot take
 * the same touch. Every gesture is LVGL's now (the two-finger VU is gone, ADR 0025), so the
 * overlay's clickability is the whole of the arbitration.
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
#include "knob.h"
#include "cassette_style.h"
#include "cassette_view.h"
#include "cassette_widgets.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_36);
LV_FONT_DECLARE(jura_28);

/* The six cells: two 96 px columns touching at x=688, three rows at 76/170/264, so the
 * bottom of the bar is 360. The 16 px is the inset of the 64 px circle inside each
 * 96 px carcase, not a gap: the carcases share their borders like the Transport panel's.
 * R keeps that 64 px circle - and so does M, the bar's chrome cell in the first column's
 * middle - while the four knobs use a 30% smaller one (44 px) and the dot crown sits outside
 * it, with the line running from the centre towards the rim. The next/previous arrows are
 * the Transport panel's < and >.
 *
 * The edit view is one knob at 3x, 284 px (96+94+94) at x=206 (206..490, over the cassette)
 * with its top on the GAIN cell's top, so it spans the bar's 76..360 band. Circle 132,
 * crown at r=96 with 12 px dots, a line from the centre towards the rim. A click on the
 * ring sets the target angle; a click on the centre circle closes. The cassette cuts out
 * and the knob appears; on close the knob disappears and the cassette cuts back in. The
 * edit opens 260 ms after the first tap, so a double tap can reset to 0 dB first. */
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
/* The 3x knob is 90% of the 284 px it used to be (96+94+94): the dial's crown and the name
 * pillow were touching, so the dial shrinks around the same centre and the pillow moves
 * below it, inside the cell. */
#define BIG_D        256
#define BIG_X        220   /* 348 is still the centre: 220..476 */
#define BIG_Y        90    /* 218 is still the centre: 90..346 */
#define BIG_CX       (BIG_X + BIG_D / 2)
#define BIG_CY       (BIG_Y + BIG_D / 2)
#define BIG_R        22
#define BIG_BORDER   2   /* the same 2 px stroke as the small carcase and circle */
#define BIG_INSET    69
#define BIG_CIRCLE   118
#define BIG_RADIUS   59
#define BIG_DOT_R    86
#define BIG_DOT_D    11
#define BIG_STICK_W  4   /* the big line's width; the owner's tuned value */
#define BIG_STICK_L  41
#define BIG_STICK_R  2
/* The name pillow under the 3x knob: the knob's own name, GAIN / HI / MED / LOW, in the
 * palette's primary with the light cap ink. Its top clears the six o'clock crown dot. */
#define BIG_PILL_W   150
#define BIG_PILL_H   32
#define BIG_PILL_X   ((BIG_D - BIG_PILL_W) / 2)
#define BIG_PILL_Y   (BIG_D - BIG_PILL_H - 2)
/* The edit opens after this delay. It is shorter than LVGL's 500 ms double-click window on
 * purpose: the second tap of a double tap lands on the 3x knob, whose double click is the
 * same reset, so the reset gesture survives while a single tap feels immediate. */
#define EDIT_DELAY_MS 260
#define BLINK_MS     600
#define BLINK_MIN_OPA 70

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
static knob_edit_t s_edit;            /* the open knob, the pending one and the click owed */
static bool s_shown;                  /* the panel is visible: the bar is out */
static bool s_created;
static lv_obj_t *s_overlay;
static lv_obj_t *s_big_cell;          /* the 3x edit knob, centred */
static lv_obj_t *s_big_circle;
static lv_obj_t *s_big_dot[DOT_COUNT];
static lv_obj_t *s_big_stick;
static lv_obj_t *s_big_name;          /* the name pillow's label under the 3x knob */
static lv_timer_t *s_open_timer;      /* the delayed edit open, 260 ms; s_edit names the knob */

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

/* Paint one crown. The model owns the span: a bipolar band lights from twelve towards the
 * line - clockwise for a cut, counter-clockwise for a boost - with the dot at twelve as the
 * 0 dB tick; a level or stepped knob lights from 7:30 to its line and marks both ends of the
 * arc. The small and the big knob share this. */
static void paint_dots(lv_obj_t *dots[DOT_COUNT], const knob_t *k)
{
    for (int i = 0; i < DOT_COUNT; i++) {
        lv_color_t c = k_col_eq_dim;
        if (knob_dot_tick(k, i, DOT_COUNT)) c = k_col_eq_tick;
        else if (knob_dot_lit(k, i, DOT_COUNT)) c = k_col_eq_lit;
        lv_obj_set_style_bg_color(dots[i], c, 0);
    }
}

static void knob_apply(int slot)
{
    paint_dots(s_dot[slot], &s_knob[slot]);
    lv_obj_set_style_transform_rotation(s_stick[slot], knob_angle_ddeg(&s_knob[slot]), 0);
}

/* The virtual knob shows the same value at 3x; its line is the owner's 4 px one. */
static void big_apply(int slot)
{
    paint_dots(s_big_dot, &s_knob[slot]);
    lv_obj_set_style_transform_rotation(s_big_stick, knob_angle_ddeg(&s_knob[slot]), 0);
    if (s_big_name) {
        const char *name = knob_name(&s_knob[slot]);
        lv_label_set_text(s_big_name, name ? name : "");
    }
}

/* Double-tapping a knob is the mixer's reset-to-centre: 0 dB, like its neutral. `on_big` is
 * the 3x knob: it is painted only while it is the one showing, or about to be closed. */
static void knob_reset_slot(int slot, bool on_big)
{
    if (!knob_reset(&s_knob[slot])) return;
    knob_apply(slot);
    if (s_big_stick && on_big) big_apply(slot);
    knob_send(slot);
}

static void knob_build(lv_obj_t *cell, int slot)
{
    /* Two circles, like the drawer's buttons: the outer one carries the broad shadow and the
     * inner one is the disc with the contact shadow and the ring. */
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
 * action, so its pulse is the whole of its click, as P's is on the left. M is the bar's
 * chrome now and is not this panel's. */
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

/* The 3x knob, hidden until a knob is tapped. Its 284 px (96+94+94) hit area at x=206 is
 * invisible - there is no outer square, only the circle, the crown and the line. The art
 * is the small knob at 3x: circle 132, crown at r=96 with 12 px dots, a 4 px line from
 * the centre towards the rim. The centre circle is the close target; the ring around it
 * is where the target click lands. */
static void big_build(lv_obj_t *parent)
{
    s_big_cell = cassette_border_box(parent, BIG_X, BIG_Y, BIG_D, BIG_D, BIG_R,
                                     k_col_bg, LV_OPA_TRANSP, k_col_button_rect, 0);
    s_big_circle = cassette_border_box(s_big_cell, BIG_INSET, BIG_INSET,
                                       BIG_CIRCLE, BIG_CIRCLE, BIG_RADIUS,
                                       k_col_bg, LV_OPA_TRANSP, k_col_button_circle,
                                       BIG_BORDER);

    for (int i = 0; i < DOT_COUNT; i++) {
        float a = (float)i * 15.0f * 3.14159265358979323846f / 180.0f;
        int x = BIG_D / 2 + (int)lroundf((float)BIG_DOT_R * sinf(a));
        int y = BIG_D / 2 - (int)lroundf((float)BIG_DOT_R * cosf(a));
        s_big_dot[i] = cassette_box(s_big_cell, x - BIG_DOT_D / 2, y - BIG_DOT_D / 2,
                                    BIG_DOT_D, BIG_DOT_D, BIG_DOT_D / 2, k_col_eq_dim);
    }

    s_big_stick = cassette_box(s_big_cell, BIG_D / 2 - BIG_STICK_W / 2,
                               BIG_D / 2 - BIG_STICK_L, BIG_STICK_W, BIG_STICK_L,
                               BIG_STICK_R, k_col_eq_tick);
    lv_obj_set_style_transform_pivot_x(s_big_stick, BIG_STICK_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_big_stick, BIG_STICK_L, 0);

    /* The pillow: the knob's identity, under the dial and above the footer, in the
     * palette's primary with the light cap ink. */
    lv_obj_t *pill = cassette_box(s_big_cell, BIG_PILL_X, BIG_PILL_Y,
                                  BIG_PILL_W, BIG_PILL_H, BIG_PILL_H / 2, k_col_primary);
    s_big_name = cassette_text(pill, &jura_28, k_col_button_white, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_hidden(s_big_cell, true);
    big_apply(SLOT_GAIN);
}

/* --------------------------------------------------------------------- blink */

static void blink_cb(void *var, int32_t v)
{
    lv_obj_set_style_border_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void blink_on(lv_obj_t *circle)
{
    if (!circle) return;
    lv_anim_delete(circle, blink_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, circle);
    lv_anim_set_exec_cb(&a, blink_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, BLINK_MIN_OPA);
    lv_anim_set_duration(&a, BLINK_MS);
    lv_anim_set_reverse_duration(&a, BLINK_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void blink_off(lv_obj_t *circle)
{
    if (!circle) return;
    lv_anim_delete(circle, blink_cb);
    lv_obj_set_style_border_opa(circle, LV_OPA_COVER, 0);
}

/* The active small circle blinks under the cut-away cassette; the big one blinks too, so
 * the edit reads at the centre. */
static void blink_start(int slot)
{
    blink_on(s_circle[slot]);
    blink_on(s_big_circle);
}

static void blink_stop(int slot)
{
    if (slot >= 0 && slot < KNOB_SLOTS) blink_off(s_circle[slot]);
    blink_off(s_big_circle);
}

/* ----------------------------------------------------------------- edit mode */

static void apply_effect(knob_edit_effect_t fx);
static void open_timer_cb(lv_timer_t *t);
static void begin_edit(int slot);
static void end_edit(int slot);

/* The model is clock-free: it asks for the delayed open and for its disarm, and this file
 * keeps the one-shot handle. */
static void close_timer(void)
{
    if (s_open_timer) {
        lv_timer_delete(s_open_timer);
        s_open_timer = NULL;
    }
}

static void arm_timer(void)
{
    close_timer();
    s_open_timer = lv_timer_create(open_timer_cb, EDIT_DELAY_MS, NULL);
    if (s_open_timer) lv_timer_set_repeat_count(s_open_timer, 1);
}

/* The model reports a set of instructions, because a double tap both disarms the delayed open
 * and resets the knob. Reset comes before close: while the editor is open the reset must also
 * paint the 3x knob, and the model has already forgotten the open id when its effect arrives,
 * so the close flag is what says the 3x knob is the one showing. */
static void apply_effect(knob_edit_effect_t fx)
{
    if (fx.cancel_timer) close_timer();
    if (fx.arm_timer) arm_timer();
    if (fx.reset_knob) knob_reset_slot(fx.id, fx.close_editor);
    if (fx.open_editor) begin_edit(fx.id);
    if (fx.close_editor) end_edit(fx.id);
}

/* A click in the centre circle closes. A click on the ring (the 284 px area outside that
 * circle; no outer square is drawn) is the target point: the knob jumps to that angle,
 * 0 dB at twelve, clockwise cutting, counter-clockwise boosting. A click outside the area
 * does nothing. No drag and no relative rotation: one click both chooses and points. */
static void overlay_clicked_cb(lv_event_t *e)
{
    (void)e;
    int slot = knob_edit_open_id(&s_edit);
    if (slot < 0) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dx = p.x - BIG_CX, dy = p.y - BIG_CY;
    if (dx * dx + dy * dy <= BIG_RADIUS * BIG_RADIUS) {
        apply_effect(knob_edit_close(&s_edit));
        return;
    }
    if (p.x < BIG_X || p.x >= BIG_X + BIG_D ||
        p.y < BIG_Y || p.y >= BIG_Y + BIG_D) return;
    /* The name pillow is a label, not a value target: a tap on it does nothing. */
    if (p.y >= BIG_Y + BIG_PILL_Y) return;

    int v = knob_value_at(&s_knob[slot], p.x - BIG_CX, p.y - BIG_CY);
    if (!knob_set(&s_knob[slot], v)) return;
    knob_apply(slot);
    big_apply(slot);
    knob_send(slot);
}

/* LVGL has already applied its own double-click rule (500 ms, 20 px on this touch
 * adapter): the second tap's short click is a DOUBLE_CLICKED on the overlay. The point maps
 * back to the small cell it landed on - the overlay covers the bar too - and the model resets
 * that knob and closes the editor while it is the open one. A double tap anywhere else is not
 * that knob's, so it does nothing; the CLICKED that follows then reaches an idle editor. */
static void overlay_double_clicked_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int slot = knob_at(p.x, p.y);
    if (slot < 0) return;
    apply_effect(knob_edit_double_tap(&s_edit, slot));
}

static void overlay_create(void)
{
    if (s_overlay) return;
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, 800, 480);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_scrollable(s_overlay, false);
    lv_obj_set_clickable(s_overlay, true);
    lv_obj_add_event_cb(s_overlay, overlay_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_overlay, overlay_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
    lv_obj_move_foreground(s_overlay);
}

/* The model only reports an open when nothing is open and the delay has landed, so this is
 * the whole of the entrance: the 3x knob, the dim and the blink. */
static void begin_edit(int slot)
{
    if (slot < 0 || slot >= KNOB_SLOTS) return;

    overlay_create();
    if (s_big_cell) {
        big_apply(slot);
        lv_obj_set_hidden(s_big_cell, false);
        lv_obj_move_foreground(s_big_cell);
        lv_obj_move_foreground(s_overlay);
    }
    cassette_view_dim(true);
    blink_start(slot);
}

/* And the close: `slot` is the knob the model closed, which it has already forgotten. */
static void end_edit(int slot)
{
    blink_stop(slot);
    if (s_big_cell) lv_obj_set_hidden(s_big_cell, true);
    cassette_view_dim(false);
    if (s_overlay) {
        /* The overlay is the event target of this click: delete it after the dispatcher
         * returns rather than under its own callback. */
        lv_obj_delete_async(s_overlay);
        s_overlay = NULL;
    }
}

/* The edit opens late: a second tap inside LVGL's 500 ms double-click window must win and
 * reset the knob to 0 dB, not open the virtual knob. A single tap arms a 260 ms one-shot; the
 * double click disarms it before it fires. The timer has landed either way, so a gate that
 * refuses the open tells the model its armed open is no longer wanted instead of leaving a
 * pending knob behind. */
static void open_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_open_timer = NULL;
    if (!s_shown || cassette_bars_is_sliding()) {
        apply_effect(knob_edit_close(&s_edit));
        return;
    }
    apply_effect(knob_edit_delay_expired(&s_edit));
}

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

/* A knob's click: the model arms the delayed open, or drops this click when a reset gesture
 * still owes one, or ignores it because an editor is open. The overlay should own the touch
 * while editing, so the guard comes first even though a click that reached a cell would also
 * have to get past the hidden or sliding bar. R is the shared button's cell: its click
 * handler is the button's, not this one. */
static void cell_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;

    int slot = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (slot >= KNOB_SLOTS) return;
    if (!knob_click_is_tap()) return;

    apply_effect(knob_edit_tap(&s_edit, slot));
}

/* A double click on a small knob is the reset gesture. It runs before the second CLICKED, so
 * the model disarms the delayed open, resets the knob and owes the click that follows; the
 * virtual knob never appears. */
static void cell_double_clicked_cb(lv_event_t *e)
{
    if (!s_shown || cassette_bars_is_sliding()) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (slot >= KNOB_SLOTS) return;
    apply_effect(knob_edit_double_tap(&s_edit, slot));
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

/* The bar reports when the panel is visible: it is out. Losing it is a close: the model ends
 * the editor or disarms the pending open, so neither the overlay nor the 3x knob can outlive
 * the hidden cells. The bar owns the slide; the Menu owns the swap to its Pad. */
static void eq_shown(bool visible, void *ctx)
{
    (void)ctx;
    s_shown = visible;
    if (!visible) apply_effect(knob_edit_close(&s_edit));
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
    if (!parent || s_created) return;
    s_created = true;

    s_shown = false;
    s_open_timer = NULL;
    knob_edit_init(&s_edit);
    knob_init(&s_knob[SLOT_GAIN], KNOB_RANGE_GAIN);
    for (int slot = 1; slot < KNOB_SLOTS; slot++) knob_init(&s_knob[slot], KNOB_RANGE_BAND);
    for (int slot = 0; slot < KNOB_SLOTS; slot++) knob_set_name(&s_knob[slot], k_knob_name[slot]);

    /* The bar builds the five cells and hands each to eq_cell_build(), and the panel value
     * inks them for the page the bar is on. */
    cassette_bars_set_panel(CASSETTE_BARS_RIGHT, &k_eq_panel);

    /* The edit layer is a screen child above the cells, hidden until a knob is tapped. */
    big_build(parent);

    /* The audio starts from the same values; this makes a rebuilt screen agree with it. */
    for (int slot = 0; slot < KNOB_SLOTS; slot++) knob_send(slot);
}

const cassette_panel_t *cassette_eq_panel(void)
{
    return &k_eq_panel;
}

bool cassette_eq_is_editing(void)
{
    return knob_edit_is_editing(&s_edit);
}
