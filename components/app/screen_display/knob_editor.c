/*
 * The one 3x dial and its edit session; see knob_editor.h.
 *
 * Everything here is the modal half of a knob: the overlay that takes the touch, the 256 px
 * dial with its crown, line and pillow, the 260 ms delay that keeps a double tap able to win,
 * the blink and the cassette's cut-out. The values, the small cells and what a value publishes
 * are the panel's, reached through the bank, so the editor knows no band, no LED and no
 * palette of its own beyond the dial's ink.
 *
 * The ordering is knob_edit_t's: this file feeds it the tap, the double tap, the expiry and the
 * close, and applies the instructions it returns. Called under the LVGL lock.
 */
#include "knob_editor.h"

#include <math.h>

#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_view.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(jura_28);

/* The 3x dial: 90% of the 284 px the right bar's three rows span (96+94+94), so its crown and
 * its name pillow stop touching. 348 is still the centre: 220..476, top at y=90. */
#define BIG_D        256
#define BIG_X        220
#define BIG_Y        90
#define BIG_CX       (BIG_X + BIG_D / 2)
#define BIG_CY       (BIG_Y + BIG_D / 2)
#define BIG_R        22
#define BIG_BORDER   2
#define BIG_INSET    69
#define BIG_CIRCLE   118
#define BIG_RADIUS   59
#define BIG_DOT_R    86
#define BIG_DOT_D    11
#define BIG_DOT_COUNT 24
#define BIG_STICK_W  4
#define BIG_STICK_L  41
#define BIG_STICK_R  2
/* The name pillow under the dial, clear of the six o'clock crown dots. */
#define BIG_PILL_W   150
#define BIG_PILL_H   32
#define BIG_PILL_X   ((BIG_D - BIG_PILL_W) / 2)
#define BIG_PILL_Y   (BIG_D - BIG_PILL_H - 2)
/* The edit opens after this delay, shorter than LVGL's 500 ms double-click window on purpose:
 * the second tap lands on the 3x dial, whose double click is the same reset. */
#define EDIT_DELAY_MS 260
#define BLINK_MS     600
#define BLINK_MIN_OPA 70

static lv_obj_t *s_overlay;
static lv_obj_t *s_big_cell;
static lv_obj_t *s_big_circle;
static lv_obj_t *s_big_dot[BIG_DOT_COUNT];
static lv_obj_t *s_big_stick;
static lv_obj_t *s_big_name;
static lv_timer_t *s_open_timer;      /* the delayed open; s_edit names the knob */
static knob_edit_t s_edit;
static const knob_editor_bank_t *s_bank;

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

void knob_editor_crown_ink(lv_obj_t *const dots[], int dot_count, const knob_t *k)
{
    if (!dots || dot_count <= 0 || !k) return;
    for (int i = 0; i < dot_count; i++) {
        lv_color_t c = k_col_eq_dim;
        if (knob_dot_tick(k, i, dot_count)) c = k_col_eq_tick;
        else if (knob_dot_lit(k, i, dot_count)) c = k_col_eq_lit;
        lv_obj_set_style_bg_color(dots[i], c, 0);
    }
}

/* The pillow's text: the bank's label, else the knob's name, else nothing. A stepped knob's
 * bank answers with the value's name (SOLID, RAINBOW...), which is why the hook exists. */
static void big_name_apply(int id)
{
    if (!s_big_name) return;
    const char *text = NULL;
    if (s_bank && s_bank->label) text = s_bank->label(id, s_bank->ctx);
    if (!text) {
        knob_t *k = knob_of(id);
        text = knob_name(k);
    }
    lv_label_set_text(s_big_name, text ? text : "");
}

static void big_apply(int id)
{
    knob_t *k = knob_of(id);
    if (!k) return;
    knob_editor_crown_ink(s_big_dot, BIG_DOT_COUNT, k);
    lv_obj_set_style_transform_rotation(s_big_stick, knob_angle_ddeg(k), 0);
    big_name_apply(id);
}

static void big_build(lv_obj_t *parent)
{
    s_big_cell = cassette_border_box(parent, BIG_X, BIG_Y, BIG_D, BIG_D, BIG_R,
                                     k_col_bg, LV_OPA_TRANSP, k_col_button_rect, 0);
    s_big_circle = cassette_border_box(s_big_cell, BIG_INSET, BIG_INSET,
                                       BIG_CIRCLE, BIG_CIRCLE, BIG_RADIUS,
                                       k_col_bg, LV_OPA_TRANSP, k_col_button_circle,
                                       BIG_BORDER);

    for (int i = 0; i < BIG_DOT_COUNT; i++) {
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

    lv_obj_t *pill = cassette_box(s_big_cell, BIG_PILL_X, BIG_PILL_Y,
                                  BIG_PILL_W, BIG_PILL_H, BIG_PILL_H / 2, k_col_primary);
    s_big_name = cassette_text(pill, &jura_28, k_col_button_white, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_hidden(s_big_cell, true);
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

/* The small disc under the cut-away cassette blinks; the big one blinks too, so the edit
 * reads at the centre. */
static void blink_start(int id)
{
    lv_obj_t *disc = (s_bank && s_bank->cell_disc) ? s_bank->cell_disc(id, s_bank->ctx) : NULL;
    blink_on(disc);
    blink_on(s_big_circle);
}

static void blink_stop(int id)
{
    lv_obj_t *disc = (s_bank && s_bank->cell_disc) ? s_bank->cell_disc(id, s_bank->ctx) : NULL;
    blink_off(disc);
    blink_off(s_big_circle);
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

/* A click in the centre circle closes. A click on the ring (the 256 px area outside that
 * circle; no outer square is drawn) is the target point: the knob jumps to that angle. A click
 * outside the area, or on the pillow, does nothing. No drag and no relative rotation. */
static void overlay_clicked_cb(lv_event_t *e)
{
    (void)e;
    int id = knob_edit_open_id(&s_edit);
    knob_t *k = knob_of(id);
    if (!k) return;
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

    int v = knob_value_at(k, dx, dy);
    if (!knob_set(k, v)) return;
    bank_repaint(id);
    big_apply(id);
    bank_publish(id);
}

/* A double click with the overlay up is the reset gesture: the point maps back to the small
 * cell it landed on, and the model resets that knob (and closes, while it is the open one). */
static void overlay_double_clicked_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int id = (s_bank && s_bank->id_at) ? s_bank->id_at(p.x, p.y, s_bank->ctx) : -1;
    knob_editor_double_tap(id);
}

static void overlay_create(void)
{
    if (s_overlay) return;
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, CASSETTE_DESIGN_W, CASSETTE_DESIGN_H);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_scrollable(s_overlay, false);
    lv_obj_set_clickable(s_overlay, true);
    lv_obj_add_event_cb(s_overlay, overlay_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_overlay, overlay_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
    lv_obj_move_foreground(s_overlay);
}

/* The whole of the entrance: the dial, the dim and the blink. */
static void begin_edit(int id)
{
    if (!knob_of(id)) return;

    overlay_create();
    if (s_big_cell) {
        big_apply(id);
        lv_obj_set_hidden(s_big_cell, false);
        lv_obj_move_foreground(s_big_cell);
        lv_obj_move_foreground(s_overlay);
    }
    cassette_view_dim(true);
    blink_start(id);
}

/* And the close: `id` is the knob the model closed, which it has already forgotten. */
static void end_edit(int id)
{
    blink_stop(id);
    if (s_big_cell) lv_obj_set_hidden(s_big_cell, true);
    cassette_view_dim(false);
    if (s_overlay) {
        /* The overlay is the event target of this click: delete it after the dispatcher
         * returns rather than under its own callback. */
        lv_obj_delete_async(s_overlay);
        s_overlay = NULL;
    }
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
    s_bank = NULL;
    big_build(parent);
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
    apply_effect(knob_edit_tap(&s_edit, id));
}

void knob_editor_double_tap(int id)
{
    if (!knob_of(id)) return;
    apply_effect(knob_edit_double_tap(&s_edit, id));
}
