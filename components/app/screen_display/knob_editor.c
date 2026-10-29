/*
 * The one 3x dial and its edit session; see knob_editor.h.
 *
 * Everything here is the modal half of a knob: the overlay that takes the touch, the 256 px
 * dial, its pillow and its plaques, the 260 ms delay that keeps a double tap able to win, the
 * the Pad's target and the cassette's cut-out. The values, the small cells and what a value
 * publishes are the panel's, reached through the bank, so the editor knows no band, no LED and
 * no palette of its own beyond the dial's ink.
 *
 * The dial's own lines - marks, graduations, value marker - are knob_cell's, so nothing here
 * draws a tick. What this file adds is text: the pillow above the dial (the knob's label, or
 * the live value while a drag is in progress) and one plaque per marked position the panel
 * asked for.
 *
 * The ordering is knob_edit_t's: this file feeds it the tap, the double tap, the expiry and the
 * close, and applies the instructions it returns. Called under the LVGL lock.
 */
#include "knob_editor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cassette_layout.h"
#include "cassette_pad.h"
#include "cassette_style.h"
#include "cassette_view.h"
#include "cassette_widgets.h"
#include "knob_cell.h"

LV_FONT_DECLARE(jura_28);
LV_FONT_DECLARE(jura_20);

#define PI_F 3.14159265358979323846f

/* The 3x dial: 90% of the 284 px the right bar's three rows span (96+94+94), so its ring and
 * its pillow stop touching. 348 is the dial's centre.
 *
 * The container is wider than the dial because LVGL clips a child to its parent's bounds:
 * `LV_OBJ_FLAG_OVERFLOW_VISIBLE` only adds the parent's shadow margin, so a plaque at 9 or 3
 * o'clock has to be inside a box that reaches past it. 360 px is a plaque's half width beyond
 * the ring on both sides, which is what a selector's six names need. */
#define BIG_X        120                 /* the container's left */
#define BIG_Y        76                  /* the bar's own top: the box fills the bar's height */
#define BIG_W        456                 /* wide enough for a column of labels either side */
#define BIG_H        284                 /* what the right bar's six cells span */
/* The multi-value box's padding, and the two things the slot layout needs: how far the side
 * columns' centres are from the dial's, and how far the one above it is. */
#define BIG_PAD      24
#define SLOT_STEP    76
#define SLOT_TOP     116
/* A multi-value knob's values: at least a third, at most six - three a side, which is what the
 * box is sized for. An odd count puts its median above the dial, so five is 2 + 1 + 2. More than
 * six are cut at six, in arc order. */
#define SLOT_MIN     3
#define SLOT_MAX     6
#define BIG_D        k_knob_dial_big.box
#define BIG_MID      (BIG_D / 2)
#define BIG_DIAL_X   (348 - BIG_MID - BIG_X)   /* the dial's box inside the container */
/* Centred in the bar: the dial's centre is the bar's centre, which leaves the same room above for
 * the pillow as below for a range's two plaques. */
#define BIG_DIAL_Y   14
#define BIG_CX       (BIG_DIAL_X + BIG_MID)    /* the dial's centre, in the container */
#define BIG_CY       (BIG_DIAL_Y + BIG_MID)
/* The pillow above the dial, clear of the twelve o'clock mark (the ring's top is at y=42 in
 * the box) and inside the box's own edge. */
#define PILL_W       150
#define PILL_H       33   /* one pixel taller at the bottom than the round 32 the owner first saw */
#define PILL_X       (BIG_DIAL_X + (BIG_D - PILL_W) / 2)
/* The pillow sits at the top of the box, 2 px in from its edge, and the dial is centred in the
 * bar - which leaves the gap between them what the box has to spare: 21 px. */
#define PILL_Y       2
/* A range's composition is not symmetric about the dial: a title pillow above it and two plaques
 * under it. Centred on the bar the pillow would be glued to the top edge (2 px) and the plaques
 * would float 46 px off the bottom, so the whole thing drops until the top margin is about
 * twenty. A selector has no pillow and no such bias; it stays centred. */
#define RANGE_DROP   18

/* The plaques at the marked positions: just outside the ring, far enough out that a plaque's
 * corner cannot touch the tick it names. */
#define PLAQUE_R      118
#define PLAQUE_H      26
#define PLAQUE_W      52    /* the narrowest: -26, +6, -12, +12 */
#define PLAQUE_W_MAX  110   /* a selector's longest word, and no wider */
#define PLAQUE_PAD    22    /* the pill's breathing room either side of its text */

/* The drag on a small cell: a 6 px dead zone keeps a tap a tap, and 2 px of travel is one
 * value step. The Pad trims at half that speed (4 px a step), which is what the dial shows. */
#define DRAG_DEAD_PX  6
#define DRAG_PX_CELL  2
#define DRAG_PX_PAD   4

/* The edit opens after this delay, shorter than LVGL's 500 ms double-click window on purpose:
 * the second tap lands on the 3x dial, whose double click is the same reset. */
#define EDIT_DELAY_MS 260
/* How long the pillow keeps the number after the finger lifts. */
#define VALUE_HOLD_MS 1000

static lv_obj_t *s_container;                 /* the dial, its pillow and its plaques */
static knob_cell_t s_big;
static lv_obj_t *s_pill;
static lv_obj_t *s_plaque[KNOB_MARKS_MAX];
static lv_obj_t *s_plaque_text[KNOB_MARKS_MAX];
static lv_timer_t *s_open_timer;              /* the delayed open; s_edit names the knob */
static lv_timer_t *s_value_timer;             /* puts the pillow's label back */
static knob_edit_t s_edit;
static const knob_editor_bank_t *s_bank;
static int s_drag_id;                         /* -1, or the knob the finger is on */
static int s_drag_start;                      /* the value the press began on */
static int s_drag_y;                          /* where the press began, screen y */
static void (*s_bar_claim)(void *ctx);        /* install the Pad where the cells were */
static void (*s_bar_release)(void *ctx);      /* put the active group back */
static void *s_bar_ctx;
static bool s_drag_moved;                     /* this drag has changed a value */
static bool s_swallow_tap;                    /* the click that ends a drag is not a tap */

/* --------------------------------------------------------------- the bank */

static bool have_bank(void)
{
    return s_bank && s_bank->knobs && s_bank->count > 0;
}

static knob_t *knob_of(int id)
{
    if (!have_bank() || id < 0 || id >= s_bank->count) return NULL;
    return &s_bank->knobs[id];
}

static void bank_repaint(int id)
{
    if (s_bank && s_bank->repaint) s_bank->repaint(id, s_bank->ctx);
}

static void bank_publish(int id)
{
    if (s_bank && s_bank->publish) s_bank->publish(id, s_bank->ctx);
}

static bool allowed(void)
{
    if (!s_bank || !s_bank->open_allowed) return true;
    return s_bank->open_allowed(s_bank->ctx);
}

/* ---------------------------------------------------------------------- draw */

/* What the pillow says with the finger down: the value itself, in the units the knob feeds -
 * a signed dB, a percentage, or a selector's marked slot. */
static void value_text(const knob_t *k, char *buf, size_t n)
{
    if (!k) {
        buf[0] = '\0';
        return;
    }
    if (k->sweep == KNOB_SWEEP_STEPPED) {
        const char *label = knob_label(k);
        snprintf(buf, n, "%s", label ? label : "");
        return;
    }
    if (k->sweep == KNOB_SWEEP_BIPOLAR) {
        float db = knob_db(k);
        /* The detent prints as 0.0, not as -0.0: the value is exactly zero there. */
        if (db > -0.05f && db < 0.05f) snprintf(buf, n, "0.0");
        else snprintf(buf, n, "%+.1f", (double)db);
        return;
    }
    snprintf(buf, n, "%d%%", knob_value(k));
}

/* The pillow is the dial's title, and only a continuous knob has one: a selector's values are
 * names, they are printed beside their own positions, and the plaque that fills is what says
 * which one is on - so there is nothing left for a centre pillow to say. */
/* Whether a knob's dial carries a title pillow. Continuous knobs do; a selector's values are
 * printed beside their positions instead, so it has none. */
static bool has_pillow(const knob_t *k)
{
    return k && k->sweep != KNOB_SWEEP_STEPPED;
}

static void big_pill(int id, bool live)
{
    knob_t *k = knob_of(id);
    if (!s_pill || !k) return;

    lv_obj_t *box = lv_obj_get_parent(s_pill);
    if (!has_pillow(k)) {
        lv_obj_set_hidden(box, true);
        return;
    }
    lv_obj_set_hidden(box, false);

    char buf[16];
    const char *text;
    if (live) {
        value_text(k, buf, sizeof buf);
        text = buf;
    } else {
        text = knob_label(k);
    }
    lv_label_set_text(s_pill, text ? text : "");
}

/* A string's width in the plaque's font, glyph by glyph: the label is a fixed-width box, so it
 * has to be told the width or LVGL clips the right-hand text off (which is how BREATHE lost its
 * last letter and CHASE its first). */
static int text_width(const lv_font_t *font, const char *text)
{
    int w = 0;
    for (const char *c = text; *c; c++) w += lv_font_get_glyph_width(font, (uint32_t)*c, (uint32_t)c[1]);
    return w;
}

/* How a plaque meets the x it is given: a column's labels share an edge, the one above the dial
 * shares a centre. */
typedef enum {
    PLAQUE_AT_LEFT = 0,
    PLAQUE_AT_CENTRE,
    PLAQUE_AT_RIGHT,
} plaque_at_t;

/* One plaque: its width from its own text, its anchor given by the caller, and the position the
 * knob is on filled - the rest are outlines, so a dial with six of them is not six highlights. */
static void plaque_draw(int slot, const char *text, bool on, int x, int cy, plaque_at_t at)
{
    int w = text_width(&jura_20, text) + PLAQUE_PAD;
    if (w < PLAQUE_W) w = PLAQUE_W;
    if (w > PLAQUE_W_MAX) w = PLAQUE_W_MAX;

    int left = (at == PLAQUE_AT_LEFT) ? x : (at == PLAQUE_AT_RIGHT) ? x - w : x - w / 2;
    lv_obj_set_size(s_plaque[slot], w, PLAQUE_H);
    lv_obj_set_pos(s_plaque[slot], left, cy - PLAQUE_H / 2);
    lv_obj_set_width(s_plaque_text[slot], w - 8);
    lv_label_set_text(s_plaque_text[slot], text);

    /* A label is primary, not tick: the tick's brighter orange belongs to the dial's own marks and
     * value line, and a plaque in it read as a second, yellower ink. Unselected is the primary
     * outline and text; selected is the primary fill with black on it, because the label is not
     * the thing being highlighted - the position is. */
    lv_obj_set_style_bg_opa(s_plaque[slot], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_plaque[slot], k_col_primary, 0);
    lv_obj_set_style_border_width(s_plaque[slot], on ? 0 : 2, 0);
    lv_obj_set_style_border_color(s_plaque[slot], k_col_primary, 0);
    lv_obj_set_style_text_color(s_plaque_text[slot], on ? k_col_bg : k_col_primary, 0);
    lv_obj_set_hidden(s_plaque[slot], false);
}

/* Where a label sits in the multi-value box. The values keep their arc's order across the screen:
 * the first ones stacked down the left, right-aligned to the dial, the median above it, the last
 * ones stacked down the right, left-aligned to it - the two columns hug the dial and share an edge
 * each (ADR 0030). A side with one label puts it level with the dial's centre, two put them either
 * side of it, three at the top, the middle and the bottom. */
static void slot_anchor(int index, int count, int *x, int *cy, plaque_at_t *at)
{
    int odd = count % 2;
    int sides = (count - odd) / 2;

    if (odd && index == sides) {              /* the median, above the dial */
        *x = BIG_CX;
        *cy = BIG_CY - SLOT_TOP;
        *at = PLAQUE_AT_CENTRE;
        return;
    }

    bool left = (index < sides);
    int row = left ? index : index - sides - odd;
    int dy = 0;
    if (left) {
        /* The left column reads upwards, because the arc does: its first value is at 7:30 and
         * the last one it holds is at 10:30. */
        if (sides == 2) dy = (row == 0) ? SLOT_STEP / 2 : -SLOT_STEP / 2;
        else if (sides >= 3) dy = (1 - row) * SLOT_STEP;
    } else {
        if (sides == 2) dy = (row == 0) ? -SLOT_STEP / 2 : SLOT_STEP / 2;
        else if (sides >= 3) dy = (row - 1) * SLOT_STEP;
    }

    *x = left ? BIG_PAD : BIG_W - BIG_PAD;
    *cy = BIG_CY + dy;
    *at = left ? PLAQUE_AT_LEFT : PLAQUE_AT_RIGHT;
}

/* The labels a knob prints: one per marked position the panel asked for. Fewer than three - a
 * range knob's two ends - stay on the ring at their own angles, which is the photo's arrangement;
 * three or more is a value list and goes in the box, up to six of them. A mark without `plaque`
 * still feeds knob_label(); it is simply not printed. */
static void plaques_layout(int id)
{
    for (int i = 0; i < KNOB_MARKS_MAX; i++) {
        if (s_plaque[i]) lv_obj_set_hidden(s_plaque[i], true);
    }
    knob_t *k = knob_of(id);
    if (!k) return;

    const knob_mark_t *shown[KNOB_MARKS_MAX];
    int count = 0;
    for (int i = 0; i < knob_mark_count(k) && count < KNOB_MARKS_MAX; i++) {
        const knob_mark_t *m = knob_mark_at(k, i);
        if (!m || !m->plaque || !m->text) continue;
        shown[count++] = m;
    }
    if (count > SLOT_MAX) count = SLOT_MAX;

    for (int i = 0; i < count; i++) {
        const knob_mark_t *m = shown[i];
        int x, cy;
        plaque_at_t at = PLAQUE_AT_CENTRE;
        if (count < SLOT_MIN) {
            float rad = (float)knob_angle_for_value(m->value) / 10.0f * PI_F / 180.0f;
            x = BIG_CX + (int)lroundf((float)PLAQUE_R * sinf(rad));
            cy = BIG_CY - (int)lroundf((float)PLAQUE_R * cosf(rad));
        } else {
            slot_anchor(i, count, &x, &cy, &at);
        }
        plaque_draw(i, m->text, m->value == knob_value(k), x, cy, at);
    }
}

/* The dial's own paint: the ring and the line are knob_cell's. */
static void big_refresh(int id)
{
    knob_t *k = knob_of(id);
    if (!k) return;
    knob_cell_apply(&s_big, k);
    plaques_layout(id);
}

static void big_apply(int id)
{
    /* The whole composition moves as one: the pillow, the dial and the labels are children of
     * the container, so its y is what decides where a knob's dial sits in the bar. */
    if (s_container) {
        knob_t *k = knob_of(id);
        lv_obj_set_pos(s_container, BIG_X, BIG_Y + (has_pillow(k) ? RANGE_DROP : 0));
    }
    big_refresh(id);
    big_pill(id, false);
}

static void big_build(lv_obj_t *parent)
{
    /* One container holds the dial, the pillow and every plaque, so showing and hiding the
     * 3x surface is one object - and the ring's draw callback still sees its own box. */
    s_container = cassette_border_box(parent, BIG_X, BIG_Y, BIG_W, BIG_H, 0,
                                      k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    knob_cell_create_at(&s_big, s_container, &k_knob_dial_big, BIG_DIAL_X, BIG_DIAL_Y);

    lv_obj_t *pill = cassette_box(s_container, PILL_X, PILL_Y, PILL_W, PILL_H, PILL_H / 2,
                                  k_col_primary);
    s_pill = cassette_text(pill, &jura_28, k_col_bg, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < KNOB_MARKS_MAX; i++) {
        s_plaque[i] = cassette_border_box(s_container, 0, 0, PLAQUE_W, PLAQUE_H,
                                          PLAQUE_H / 2, k_col_bg, LV_OPA_TRANSP,
                                          k_col_eq_tick, 2);
        s_plaque_text[i] = cassette_text(s_plaque[i], &jura_20, k_col_eq_tick,
                                         LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(s_plaque_text[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_plaque_text[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_hidden(s_plaque[i], true);
    }

    lv_obj_set_hidden(s_container, true);
}

/* ------------------------------------------------------------------ the drag */

static void value_timer_close(void)
{
    if (s_value_timer) {
        lv_timer_delete(s_value_timer);
        s_value_timer = NULL;
    }
}

static void value_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_value_timer = NULL;
    int id = knob_edit_open_id(&s_edit);
    if (id >= 0) big_pill(id, false);
}

void knob_editor_drag_begin(int id)
{
    s_drag_id = -1;
    /* A fresh press means any click a previous drag owed never came: forget it, so a tap
     * that follows a drag is not swallowed. */
    s_swallow_tap = false;
    s_drag_moved = false;

    knob_t *k = knob_of(id);
    lv_indev_t *indev = lv_indev_active();
    if (!k || !indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_drag_id = id;
    s_drag_start = knob_value(k);
    s_drag_y = p.y;
}

void knob_editor_drag_update(void)
{
    if (s_drag_id < 0) return;
    knob_t *k = knob_of(s_drag_id);
    lv_indev_t *indev = lv_indev_active();
    if (!k || !indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = s_drag_y - p.y;      /* up is positive: a drag up boosts */

    /* The dead zone is travel, not a step count: it keeps a shaky tap from nudging 0 dB off
     * the detent, and it comes out of the travel so the value does not jump when it ends. */
    int travel = 0;
    if (dy > DRAG_DEAD_PX) travel = dy - DRAG_DEAD_PX;
    else if (dy < -DRAG_DEAD_PX) travel = dy + DRAG_DEAD_PX;

    /* The small cell's own drag, at 2 px a step. The Pad - not the dial - is what trims at the
     * 3x resolution while the dial is up, so there is no second case here. */
    int v = knob_drag_value(k, s_drag_start, travel, DRAG_PX_CELL);
    if (!knob_set(k, v)) return;
    s_drag_moved = true;

    bank_repaint(s_drag_id);
    bank_publish(s_drag_id);
}

void knob_editor_drag_end(void)
{
    int id = s_drag_id;
    s_drag_id = -1;

    /* A drag is never a tap. LVGL sends CLICKED after the release, and the panel's own
     * moved/gesture guard is not reliable enough to separate the two - a drag that ended
     * close to where it started reads as a click - so the drag itself says whether the
     * click that follows is owed. That is the bug where trimming a knob opened it. */
    if (s_drag_moved) s_swallow_tap = true;
    s_drag_moved = false;
    (void)id;
}

/* ----------------------------------------------------------------- edit mode */

static void apply_effect(knob_edit_effect_t fx);
static void open_timer_cb(lv_timer_t *t);
static void begin_edit(int id);
static void end_edit(int id);

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

/* The reset comes before the close: while the editor is open the reset must also paint the 3x
 * dial, and the model has already forgotten the open id when the effect arrives, so the close
 * flag is what says the dial is the one showing. */
static void apply_effect(knob_edit_effect_t fx)
{
    if (fx.cancel_timer) close_timer();
    if (fx.arm_timer) arm_timer();
    if (fx.reset_knob) {
        knob_t *k = knob_of(fx.id);
        if (k && knob_reset(k)) {
            bank_repaint(fx.id);
            if (fx.close_editor) big_apply(fx.id);
            bank_publish(fx.id);
        }
    }
    if (fx.open_editor) begin_edit(fx.id);
    if (fx.close_editor) end_edit(fx.id);
}

/* The click that ends a drag is not a tap, and only the drag knows: consumed exactly once, the
 * way knob_edit_t swallows the click a reset gesture owes. */
static bool click_after_drag(void)
{
    if (!s_swallow_tap) return false;
    s_swallow_tap = false;
    return true;
}

/* The Pad, while the dial is up: the ratchet moves the value, a tap cycles the knob's stops
 * and a left swipe closes. That is the whole of the interaction - the dial takes no touch at
 * all, which is why there is no overlay and no hit test here any more (ADR 0028). */

/* Where the current Pad gesture began, so a ratchet is measured from the value it started on
 * and not from whatever the value has become. */
static int s_pad_start;
static int s_pad_dy;

static void pad_press_cb(void *ctx)
{
    (void)ctx;
    knob_t *k = knob_of(knob_edit_open_id(&s_edit));
    s_pad_dy = 0;
    s_pad_start = k ? knob_value(k) : KNOB_DETENT;
}

/* The finger lifts: the pillow keeps the number for a moment, then says what the control is
 * again - the same hold the dial had when the value was dragged on the dial itself. */
static void pad_release_cb(void *ctx)
{
    (void)ctx;
    s_pad_dy = 0;
    value_timer_close();
    s_value_timer = lv_timer_create(value_timer_cb, VALUE_HOLD_MS, NULL);
    if (s_value_timer) lv_timer_set_repeat_count(s_value_timer, 1);
}

/* The ratchet is the drag at the 3x resolution, from the Pad instead of from the cell. The
 * travel arrives as deltas after the first sample (which carries the whole travel since the
 * press), so it is accumulated; the value is recomputed from the gesture's start each time,
 * so a slow finger cannot lose a step to integer division. */
static void pad_step_cb(int dy, uint32_t dt, void *ctx)
{
    (void)dt;
    (void)ctx;
    int id = knob_edit_open_id(&s_edit);
    knob_t *k = knob_of(id);
    if (!k) return;

    s_pad_dy += dy;                       /* the Pad reports travel downwards */

    int v = knob_drag_value(k, s_pad_start, -s_pad_dy, DRAG_PX_PAD);
    if (!knob_set(k, v)) return;

    bank_publish(id);
    big_refresh(id);
    big_pill(id, true);
}

/* A tap visits the knob's stops in value order: -12, 0 dB, +12 for the trim, -26, 0 dB, +6
 * for a band, one slot at a time for a selector, and round again. */
static void pad_tap_cb(void *ctx)
{
    (void)ctx;
    int id = knob_edit_open_id(&s_edit);
    knob_t *k = knob_of(id);
    if (!k) return;

    if (!knob_set(k, knob_cycle(k))) return;
    value_timer_close();
    bank_publish(id);
    big_apply(id);
}

static void pad_back_cb(void *ctx)
{
    (void)ctx;
    apply_effect(knob_edit_close(&s_edit));
}

static const cassette_pad_target_t k_knob_pad_target = {
    .press = pad_press_cb,
    .step = pad_step_cb,
    .tap = pad_tap_cb,
    .back = pad_back_cb,
    .release = pad_release_cb,
    .ctx = NULL,
};

/* The whole of the entrance: the group's cells give way to the Pad, and the dial, the dim and
 * the dial comes up. The bank is kept - the values are the panel's and the editor is between
 * the cells and the dial - so the panel's own hooks see their cells go away, which is why
 * they tolerate it. */
static void begin_edit(int id)
{
    if (!knob_of(id)) return;

    if (s_bar_claim) s_bar_claim(s_bar_ctx);
    cassette_pad_set_target(&k_knob_pad_target);

    if (s_container) {
        big_apply(id);
        lv_obj_set_hidden(s_container, false);
        lv_obj_move_foreground(s_container);
    }
    cassette_view_dim(true);
}

/* And the close: `id` is the knob the model closed, which it has already forgotten. The Pad
 * lets go before the bar is handed back, so no half-gesture can arrive at the group. */
static void end_edit(int id)
{
    cassette_pad_set_target(NULL);
    s_drag_id = -1;
    s_drag_moved = false;
    s_swallow_tap = false;
    s_pad_dy = 0;
    value_timer_close();
    if (s_container) lv_obj_set_hidden(s_container, true);
    cassette_view_dim(false);
    if (s_bar_release) s_bar_release(s_bar_ctx);
}

/* The edit opens late, so a double tap inside LVGL's 500 ms window wins and resets instead. A
 * gate that refuses the open tells the model its armed open is no longer wanted, rather than
 * leaving a pending knob behind. */
static void open_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_open_timer = NULL;
    if (!allowed()) {
        apply_effect(knob_edit_close(&s_edit));
        return;
    }
    apply_effect(knob_edit_delay_expired(&s_edit));
}

/* -------------------------------------------------------------------- public */

void knob_editor_create(lv_obj_t *parent)
{
    if (!parent) return;
    knob_edit_init(&s_edit);
    s_open_timer = NULL;
    s_value_timer = NULL;
    s_bank = NULL;
    s_drag_id = -1;
    big_build(parent);
}

void knob_editor_set_bar(void (*claim)(void *ctx), void (*release)(void *ctx), void *ctx)
{
    s_bar_claim = claim;
    s_bar_release = release;
    s_bar_ctx = ctx;
}

void knob_editor_set_bank(const knob_editor_bank_t *bank)
{
    apply_effect(knob_edit_close(&s_edit));
    s_bank = (bank && bank->knobs && bank->count > 0) ? bank : NULL;
}

bool knob_editor_is_editing(void)
{
    return knob_edit_is_editing(&s_edit);
}

void knob_editor_close(void)
{
    apply_effect(knob_edit_close(&s_edit));
}

void knob_editor_tap(int id)
{
    if (!knob_of(id)) return;
    /* The click that ends a drag: a knob that was just trimmed must not also open. */
    if (click_after_drag()) return;
    apply_effect(knob_edit_tap(&s_edit, id));
}

void knob_editor_double_tap(int id)
{
    if (!knob_of(id)) return;
    apply_effect(knob_edit_double_tap(&s_edit, id));
}
