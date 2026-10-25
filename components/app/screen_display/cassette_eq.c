/*
 * The DJ block on the right; see cassette_eq.h.
 *
 * Six carcases, two touching columns of three, the same 96 px box as the transport
 * buttons. The arrows keep the buttons' 64 px circle; the four knobs are a 30% smaller
 * 44 px circle with the 24-dot crown outside it, and a line from the centre to the rim.
 * The first column is GAIN above the < and > arrows; the second is HI, MED and LOW.
 *
 * The edit overlay is the whole input policy for the block: while a knob is active it is
 * the only clickable object on the screen, so the arrows, the drawer and the Levels cannot
 * take the same touch. The two-finger menu bypasses LVGL (the touch adapter calls the
 * view directly), which is why the view also checks is_editing().
 *
 * The block is hidden at rest, so the entry view is the untouched full-scale cassette; the
 * view shows it with the transport drawer and hides it again, leaving edit mode, when the
 * drawer slides away.
 */
#include <math.h>

#include "lvgl.h"

#include "cassette_eq.h"
#include "cassette_style.h"
#include "cassette_view.h"
#include "cassette_widgets.h"
#include "mini_icecast.h"
#include "screen_input.h"

LV_FONT_DECLARE(jura_36);

/* The six cells: two 96 px columns touching at x=688, three rows at 76/170/264, so the
 * bottom of the block is 360. The 16 px is the inset of the 64 px circle inside each
 * 96 px carcase, not a gap: the carcases share their borders like P/R/W. The arrows keep
 * that 64 px circle; the four knobs use a 30% smaller one (44 px) and the dot crown sits
 * outside it, with the line running from the centre to the rim.
 *
 * The edit view is one knob at 3x, 284 px (96+94+94) at x=206 (206..490, over the cassette)
 * with its top on the GAIN button's top, so it spans the block's 76..360 band. Circle 132,
 * crown at r=96 with 12 px dots, a 6x66 line from the centre to the rim. A click on the
 * ring sets the target angle; a click on the centre circle closes. The cassette fades out
 * first and the knob fades in after it; on close the knob fades out first and the cassette
 * fades back in. The edit opens 550 ms after the first tap, so a double tap can reset to
 * 0 dB first. */
#define CELL_W       96
#define CELL_H       96
#define CELL_PITCH   94
#define BLOCK_LEFT   592
#define BLOCK_TOP    76
#define CELL_R       8
#define CIRCLE_D     64
#define CIRCLE_R     32
#define CIRCLE_INSET 16
#define KNOB_D       44
#define KNOB_R       (KNOB_D / 2)
#define KNOB_INSET   ((CELL_W - KNOB_D) / 2)
#define DOT_COUNT    24
#define DOT_R        32   /* outside the knob circle, inside the carcase */
#define DOT_D        4
#define STICK_W      6
#define STICK_L      KNOB_R   /* the radius: from the centre to the rim */
#define STICK_R      3
#define BIG_D        284
#define BIG_X        206   /* the owner's x: 206..490, over the cassette */
#define BIG_Y        BLOCK_TOP   /* the GAIN button's top: 76..360, the block's band */
#define BIG_CX       (BIG_X + BIG_D / 2)
#define BIG_CY       (BIG_Y + BIG_D / 2)
#define BIG_R        24
#define BIG_BORDER   2   /* the same 2 px stroke as the small carcase and circle */
#define BIG_INSET    76
#define BIG_CIRCLE   132
#define BIG_RADIUS   66
#define BIG_DOT_R    96
#define BIG_DOT_D    12
#define BIG_STICK_W  6   /* the centre line stays 6 px, like the small knob's */
#define BIG_STICK_L  BIG_RADIUS
#define BIG_STICK_R  3
/* A knob's edit opens after this delay so a second tap is read as LVGL's double click
 * and resets to 0 dB instead of opening the virtual knob. The touch adapter's window is
 * 500 ms, so wait a little past it. */
#define EDIT_DELAY_MS 550
#define BIG_FADE_MS  180   /* the virtual knob's own fade; it follows the cassette's */
#define BLINK_MS     600
#define BLINK_MIN_OPA 70
/* The block enters and leaves with the transport drawer's 260 ms slide, from the right
 * edge: at +208 the block's left edge is the screen's, so it is fully off. */
#define EQ_SLIDE_MS     260
#define EQ_SLIDE_OFFSET (800 - BLOCK_LEFT)

enum {
    SLOT_GAIN = 0,
    SLOT_HI   = 1,
    SLOT_MED  = 2,
    SLOT_LOW  = 3,
    SLOT_PREV = 4,
    SLOT_NEXT = 5,
    SLOT_COUNT = 6,
    KNOB_SLOTS = 4,
};

/* Which cell each slot occupies. The gain column is on the inside, next to the cassette;
 * the EQ column is the outer one. */
static const int8_t k_slot_col[SLOT_COUNT] = { 0, 1, 1, 1, 0, 0 };
static const int8_t k_slot_row[SLOT_COUNT] = { 0, 0, 1, 2, 1, 2 };

/* Which audio band a knob drives: HI=2, MED=1, LOW=0. The gain is not a band. */
static const int8_t k_slot_band[KNOB_SLOTS] = { -1, 2, 1, 0 };

static lv_obj_t *s_cell[SLOT_COUNT];
static lv_obj_t *s_circle[KNOB_SLOTS];
static lv_obj_t *s_dot[KNOB_SLOTS][DOT_COUNT];
static lv_obj_t *s_stick[KNOB_SLOTS];
static int s_value[KNOB_SLOTS];       /* 0..100; 50 is 0 dB on every knob */
static int s_active;                  /* -1, or the knob slot being edited */
static bool s_shown;                  /* the block is out with the transport drawer */
static bool s_sliding;                /* the show/hide slide is in flight */
static int32_t s_slide_offset = EQ_SLIDE_OFFSET;  /* px right of the resting position */
static lv_obj_t *s_overlay;
static lv_obj_t *s_big_cell;          /* the 3x edit knob, centred */
static lv_obj_t *s_big_circle;
static lv_obj_t *s_big_dot[DOT_COUNT];
static lv_obj_t *s_big_stick;
static lv_timer_t *s_open_timer;      /* the delayed edit open, 550 ms */
static int s_pending_slot;            /* which knob that timer will open */
static bool s_reset_click;            /* the double click's second CLICKED is consumed */

/* ------------------------------------------------------------------ geometry */

static int cell_x(int slot) { return BLOCK_LEFT + k_slot_col[slot] * CELL_W; }
static int cell_y(int slot) { return BLOCK_TOP + k_slot_row[slot] * CELL_PITCH; }

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

/* The DJ ranges: a trim of -12..+12 around 0 dB, and bands of -26..+6, so the cut side
 * is the coarse one. 50 on the knob is 0 dB; the mapping is linear on each side. */
static float knob_db(int slot, int value)
{
    float v = (float)value - 50.0f;
    if (slot == SLOT_GAIN) return v * (12.0f / 50.0f);
    if (v >= 0.0f) return v * (6.0f / 50.0f);
    return v * (26.0f / 50.0f);
}

static void knob_send(int slot)
{
    float db = knob_db(slot, s_value[slot]);
    if (slot == SLOT_GAIN) mini_icecast_gain_set(db);
    else mini_icecast_eq_set(k_slot_band[slot], db);
}

/* ---------------------------------------------------------------------- draw */

/* Paint one crown. The arc starts at twelve o'clock and runs to the line on the side it
 * points: clockwise for a cut, counter-clockwise for a boost. The dot at twelve is always
 * the 0 dB tick. The small and the big knob share this. */
static void paint_dots(lv_obj_t *dots[DOT_COUNT], int value)
{
    int span = (int)lroundf(fabsf((float)(value - 50)) / 50.0f * (float)(DOT_COUNT / 2));

    for (int i = 0; i < DOT_COUNT; i++) {
        lv_color_t c = k_col_eq_dim;
        if (i == 0) c = k_col_eq_tick;
        else if (value < 50 && i <= span) c = k_col_eq_lit;
        else if (value > 50 && i >= DOT_COUNT - span) c = k_col_eq_lit;
        lv_obj_set_style_bg_color(dots[i], c, 0);
    }
}

static void knob_apply(int slot)
{
    int value = s_value[slot];
    paint_dots(s_dot[slot], value);
    float deg = (50.0f - (float)value) * 3.6f;   /* 180 degrees per 50 units */
    lv_obj_set_style_transform_rotation(s_stick[slot], (int32_t)lroundf(deg * 10.0f), 0);
}

/* The virtual knob shows the same value at 3x; its line stays 6 px wide. */
static void big_apply(int slot)
{
    int value = s_value[slot];
    paint_dots(s_big_dot, value);
    float deg = (50.0f - (float)value) * 3.6f;
    lv_obj_set_style_transform_rotation(s_big_stick, (int32_t)lroundf(deg * 10.0f), 0);
}

/* Double-tapping a knob is the mixer's reset-to-centre: 0 dB, like its neutral. */
static void knob_reset(int slot)
{
    s_value[slot] = 50;
    knob_apply(slot);
    if (s_big_stick && s_active == slot) big_apply(slot);
    knob_send(slot);
}

static void knob_build(lv_obj_t *cell, int slot)
{
    lv_obj_t *circle = cassette_border_box(cell, KNOB_INSET, KNOB_INSET,
                                           KNOB_D, KNOB_D, KNOB_R,
                                           k_col_bg, LV_OPA_TRANSP, k_col_eq_lit, 2);
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

static void arrow_build(lv_obj_t *cell, int slot)
{
    lv_obj_t *circle = cassette_border_box(cell, CIRCLE_INSET, CIRCLE_INSET,
                                           CIRCLE_D, CIRCLE_D, CIRCLE_R,
                                           k_col_bg, LV_OPA_TRANSP, k_col_eq_lit, 2);
    lv_obj_t *label = cassette_text(circle, &jura_36, k_col_eq_tick, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label, slot == SLOT_PREV ? "<" : ">");
}

/* The 3x knob, hidden until a knob is tapped. Its carcase is 284 px, the 96+94+94 length
 * of the block, at x=206 over the cassette with its top on the GAIN button's top. The art
 * is the small knob at 3x: circle 132, crown at r=96 with 12 px dots, a 6x66 line from the
 * centre to the rim. The centre circle is the close target; the ring around it is where
 * the target click lands. */
static void big_build(lv_obj_t *parent)
{
    s_big_cell = cassette_border_box(parent, BIG_X, BIG_Y, BIG_D, BIG_D, BIG_R,
                                     k_col_bg, LV_OPA_TRANSP, k_col_eq_lit, BIG_BORDER);
    s_big_circle = cassette_border_box(s_big_cell, BIG_INSET, BIG_INSET,
                                       BIG_CIRCLE, BIG_CIRCLE, BIG_RADIUS,
                                       k_col_bg, LV_OPA_TRANSP, k_col_eq_lit, BIG_BORDER);

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

/* The active small circle blinks under the faded cassette; the big one blinks too, so
 * the edit reads at the centre. */
static void blink_start(int slot)
{
    blink_on(s_circle[slot]);
    blink_on(s_big_circle);
}

static void blink_stop(int slot)
{
    blink_off(s_circle[slot]);
    blink_off(s_big_circle);
}

/* The virtual knob has its own opacity: it fades in only after the cassette's 180 ms
 * fade-out, and fades out before the cassette is brought back. opa_layered keeps the
 * carcase, circle, crown and line together. */
static void big_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa_layered((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void big_out_done_cb(lv_anim_t *a)
{
    (void)a;
    if (s_active >= 0) return;   /* a new edit took over before this finished */
    if (s_big_cell) lv_obj_set_hidden(s_big_cell, true);
    cassette_view_dim(false, 0);
}

static void big_fade_in(void)
{
    if (!s_big_cell) return;
    lv_anim_delete(s_big_cell, big_opa_cb);
    lv_obj_set_style_opa_layered(s_big_cell, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_big_cell);
    lv_anim_set_exec_cb(&a, big_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, BIG_FADE_MS);
    lv_anim_set_delay(&a, BIG_FADE_MS);   /* after the cassette's fade-out */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void big_fade_out(void)
{
    if (!s_big_cell) return;
    lv_anim_delete(s_big_cell, big_opa_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_big_cell);
    lv_anim_set_exec_cb(&a, big_opa_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa_layered(s_big_cell, 0), LV_OPA_TRANSP);
    lv_anim_set_duration(&a, BIG_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, big_out_done_cb);
    lv_anim_start(&a);
}

/* ----------------------------------------------------------------- edit mode */

static void end_edit(void);
static void begin_edit(int slot);
static void open_later(int slot);
static void cancel_open(void);

/* The angle from the big knob's centre: 0 at twelve, positive clockwise, in degrees. */
static float angle_at(lv_point_t p)
{
    float dx = (float)(p.x - BIG_CX);
    float uy = (float)(BIG_CY - p.y);
    if (dx == 0.0f && uy == 0.0f) return 0.0f;
    return atan2f(dx, uy) * 180.0f / 3.14159265358979323846f;
}

/* A click in the centre circle closes. A click on the ring (the 284 px carcase outside
 * that circle) is the target point: the knob jumps to that angle, 0 dB at twelve,
 * clockwise cutting, counter-clockwise boosting. A click outside the knob does nothing.
 * No drag and no relative rotation: one click both chooses and points. */
static void overlay_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_active < 0) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dx = p.x - BIG_CX, dy = p.y - BIG_CY;
    if (dx * dx + dy * dy <= BIG_RADIUS * BIG_RADIUS) {
        end_edit();
        return;
    }
    if (p.x < BIG_X || p.x >= BIG_X + BIG_D ||
        p.y < BIG_Y || p.y >= BIG_Y + BIG_D) return;

    int v = 50 - (int)lroundf(angle_at(p) * (50.0f / 180.0f));
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v == s_value[s_active]) return;

    s_value[s_active] = v;
    knob_apply(s_active);
    big_apply(s_active);
    knob_send(s_active);
}

/* LVGL has already applied its own double-click rule (500 ms, 20 px on this touch
 * adapter): the second tap's short click is a DOUBLE_CLICKED on the overlay. Reset the
 * active knob and leave; the CLICKED that follows sees s_active == -1 and does nothing. */
static void overlay_double_clicked_cb(lv_event_t *e)
{
    (void)e;
    if (s_active < 0) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int slot = knob_at(p.x, p.y);
    if (slot != s_active) return;
    knob_reset(slot);
    end_edit();
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

static void begin_edit(int slot)
{
    if (s_active >= 0) return;
    cancel_open();
    s_active = slot;

    overlay_create();
    if (s_big_cell) {
        big_apply(slot);
        lv_obj_set_hidden(s_big_cell, false);
        lv_obj_move_foreground(s_big_cell);
        lv_obj_move_foreground(s_overlay);
        big_fade_in();
    }
    cassette_view_dim(true, 0);
    blink_start(slot);
}

static void end_edit(void)
{
    if (s_active < 0) return;
    blink_stop(s_active);
    s_active = -1;
    if (s_big_cell) big_fade_out();
    /* The cassette comes back in big_out_done_cb, after the knob is gone. */
    if (s_overlay) {
        /* The overlay is the event target of this click: delete it after the dispatcher
         * returns rather than under its own callback. */
        lv_obj_delete_async(s_overlay);
        s_overlay = NULL;
    }
}

/* The edit opens late: a second tap inside LVGL's 500 ms double-click window must win and
 * reset the knob to 0 dB, not open the virtual knob. A single tap starts a 550 ms one-shot;
 * the double click cancels it before it fires. */
static void open_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_open_timer = NULL;
    if (!s_shown || s_sliding || s_active >= 0) return;
    begin_edit(s_pending_slot);
}

static void cancel_open(void)
{
    if (s_open_timer) {
        lv_timer_delete(s_open_timer);
        s_open_timer = NULL;
    }
}

static void open_later(int slot)
{
    cancel_open();
    s_pending_slot = slot;
    s_open_timer = lv_timer_create(open_timer_cb, EDIT_DELAY_MS, NULL);
    if (s_open_timer) lv_timer_set_repeat_count(s_open_timer, 1);
}

/* ------------------------------------------------------------------- the cells */

static void cell_clicked_cb(lv_event_t *e)
{
    /* The overlay should own the touch while editing; the guard comes first so even a
     * click that somehow reaches a cell cannot skip a track. A hidden or still-sliding
     * block takes no click at all. */
    if (s_active >= 0 || !s_shown || s_sliding) return;

    int slot = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    /* The two-finger menu is reserved: a gesture that starts on a cell must not also be
     * the cell's click. The latch is cleared by the next press, which the cell bubbles. */
    if (screen_input_two_finger_seen()) return;
    lv_indev_t *indev = lv_indev_active();
    if (indev && (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE ||
                  lv_indev_get_press_moved(indev))) return;

    if (slot == SLOT_PREV) {
        mini_icecast_prev();
        return;
    }
    if (slot == SLOT_NEXT) {
        mini_icecast_skip();
        return;
    }
    /* The second CLICKED of a double click: the DOUBLE_CLICKED handler already reset the
     * knob and cancelled the pending open, so this click must not start a new one. */
    if (s_reset_click) {
        s_reset_click = false;
        return;
    }
    open_later(slot);
}

/* A double click on a small knob is the reset gesture. It runs before the second CLICKED,
 * cancels the delayed open and leaves s_active alone: the virtual knob never appears. */
static void cell_double_clicked_cb(lv_event_t *e)
{
    if (s_active >= 0 || !s_shown || s_sliding) return;
    int slot = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (slot >= KNOB_SLOTS) return;
    cancel_open();
    knob_reset(slot);
    s_reset_click = true;
}

static lv_obj_t *cell_create(lv_obj_t *parent, int slot)
{
    lv_obj_t *cell = cassette_border_box(parent, cell_x(slot), cell_y(slot),
                                         CELL_W, CELL_H, CELL_R,
                                         k_col_bg, LV_OPA_TRANSP, k_col_eq_lit, 2);
    lv_obj_set_clickable(cell, true);
    /* The press bubbles so screen_input clears its two-finger latch; the gesture does not,
     * so a swipe that starts on a cell is not a drawer or Level gesture. */
    lv_obj_set_event_bubble(cell, true);
    /* Slot 0 is a valid user value, so the payload is slot + 1. */
    lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(slot + 1));
    lv_obj_add_event_cb(cell, cell_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED,
                        (void *)(intptr_t)(slot + 1));
    s_cell[slot] = cell;
    return cell;
}

/* ---------------------------------------------------------------------- public */

/* The slide: move all six cells together, so the block enters from the right edge. The
 * animation's variable is cell 0 only so a new show/hide can replace the old one. */
static void eq_slide_cb(void *var, int32_t v)
{
    (void)var;
    s_slide_offset = v;
    for (int slot = 0; slot < SLOT_COUNT; slot++)
        if (s_cell[slot]) lv_obj_set_x(s_cell[slot], cell_x(slot) + v);
}

static void eq_slide_done_cb(lv_anim_t *a)
{
    (void)a;
    s_sliding = false;
    s_slide_offset = s_shown ? 0 : EQ_SLIDE_OFFSET;
    if (s_shown) {
        cassette_eq_raise();
        return;
    }
    for (int slot = 0; slot < SLOT_COUNT; slot++)
        if (s_cell[slot]) lv_obj_set_hidden(s_cell[slot], true);
}

void cassette_eq_create(lv_obj_t *parent)
{
    if (!parent || s_cell[0]) return;

    s_active = -1;
    s_shown = false;
    s_sliding = false;
    s_slide_offset = EQ_SLIDE_OFFSET;
    s_open_timer = NULL;
    s_reset_click = false;
    s_value[SLOT_GAIN] = 50;   /* 0 dB, the same neutral as the three bands */
    s_value[SLOT_HI] = 50;
    s_value[SLOT_MED] = 50;
    s_value[SLOT_LOW] = 50;

    for (int slot = 0; slot < SLOT_COUNT; slot++) {
        lv_obj_t *cell = cell_create(parent, slot);
        if (slot < KNOB_SLOTS) knob_build(cell, slot);
        else arrow_build(cell, slot);
        lv_obj_set_hidden(cell, true);
    }
    big_build(parent);

    /* The audio starts from the same values; this makes a rebuilt screen agree with it. */
    for (int slot = 0; slot < KNOB_SLOTS; slot++) knob_send(slot);
}

void cassette_eq_set_shown(bool shown)
{
    if (!s_cell[0]) return;
    if (shown == s_shown && !s_sliding) return;
    s_shown = shown;
    if (!shown) {
        cancel_open();
        s_reset_click = false;
    }

    /* A hidden block cannot own the edit overlay. The view gates the drawer's hide on edit
     * mode, but keeping the rule here makes it true from any caller. */
    if (!shown && s_active >= 0) end_edit();

    lv_anim_delete(s_cell[0], eq_slide_cb);
    s_sliding = true;
    for (int slot = 0; slot < SLOT_COUNT; slot++) {
        if (!s_cell[slot]) continue;
        lv_obj_set_hidden(s_cell[slot], false);
        lv_obj_set_x(s_cell[slot], cell_x(slot) + s_slide_offset);
    }
    if (shown) cassette_eq_raise();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cell[0]);
    lv_anim_set_exec_cb(&a, eq_slide_cb);
    lv_anim_set_values(&a, s_slide_offset, shown ? 0 : EQ_SLIDE_OFFSET);
    lv_anim_set_duration(&a, EQ_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, eq_slide_done_cb);
    lv_anim_start(&a);
}

void cassette_eq_raise(void)
{
    /* Raising the cells above the overlay would break the modal edit; the view does not
     * ask while editing, but the guard keeps that true from any caller. A hidden block has
     * nothing to raise. */
    if (!s_shown || s_active >= 0) return;
    for (int slot = 0; slot < SLOT_COUNT; slot++)
        if (s_cell[slot]) lv_obj_move_foreground(s_cell[slot]);
}

bool cassette_eq_is_editing(void)
{
    return s_active >= 0;
}
