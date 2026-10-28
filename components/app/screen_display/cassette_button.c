/*
 * One Cassette button; see cassette_button.h.
 *
 * The module is the drawing and the feel of a button and nothing else: it adds three objects
 * to the host's carcase (the ambient circle, the disc and the letter), re-inks the carcase
 * itself, owns the one pressed state, the guard and the pulse, and hands a click to the
 * host's callback. The geometry, the carcase, the visibility, the lattice and the bubbling
 * stay with the host.
 *
 * Nothing here allocates, and the only object churn is at create: a press is two border
 * widths (or one on the white page, where the disc's rim is its cap's) and a release writes
 * them back, so there is no second state tree to keep in step. Called under the LVGL lock.
 */
#include "cassette_button.h"

#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(jura_36);

/* The disc: 64 px centred in the host's 96 px carcase, so 16 px of line art on every side.
 * The 96 is the cell the lattice gives both hosts; the 64 and its inset are the button's. */
#define CIRCLE_D     64
#define CIRCLE_R     (CIRCLE_D / 2)
#define CIRCLE_INSET ((CASSETTE_CELL_W - CIRCLE_D) / 2)

/* The click pulse's clock. The shape is the portable rule's; 160 ms is the feel's, and one
 * period is one run of the 0..1024 triangle. */
#define PULSE_MS 160

/* The ring widths for the current page and press state. The carcase's ring is always the
 * table's: the drawer's thickens (2 -> 3), M's and R's does not (2 -> 2). The disc's ring is
 * the table's on the dark page only; on the white page the disc is the cap and its rim is
 * CASSETTE_CAP_RIM_W whatever the state, which is why M's and R's pressed look on that page is
 * the pulse alone. This is the whole of a press, so it writes borders and nothing else. */
static void button_ring_apply(cassette_button_t *b)
{
    if (!b) return;

    if (b->carcase)
        lv_obj_set_style_border_width(
            b->carcase, cassette_button_ring_w(b->carcase_ring, b->pressed), 0);

    if (!b->disc) return;
    if (b->white) {
        lv_obj_set_style_border_width(b->disc, CASSETTE_CAP_RIM_W, 0);
        lv_obj_set_style_border_color(b->disc, k_col_dome, 0);
    } else {
        lv_obj_set_style_border_width(b->disc, cassette_button_ring_w(b->disc_ring, b->pressed), 0);
        lv_obj_set_style_border_color(b->disc, k_col_button_circle, 0);
    }
}

/* The latch, not the press: a latched button is a filled k_col_primary disc with the dark
 * page's own near-black glyph, on both pages. It is the idle look inverted - the dark page's
 * idle disc is a red ring with a red glyph and no face, the white page's is a grey cap with a
 * near-black glyph - so "on" reads the same way on either page. The white page keeps the cap's
 * 4 px k_col_dome rim, which is what stops a coloured disc dissolving into its own carcase.
 *
 * This is the page's ink and the latch's, so it runs on set_page() and set_on(), never on a
 * press: a press is button_ring_apply() alone. */
static void button_ink_apply(cassette_button_t *b)
{
    if (!b || !b->carcase) return;

    /* The carcase keeps the drawer's line on both pages; only its fill follows the page. */
    lv_obj_set_style_bg_color(b->carcase, b->white ? k_col_white : k_col_bg, 0);
    lv_obj_set_style_border_color(b->carcase, k_col_button_rect, 0);

    if (b->disc) {
        if (b->white) {
            cassette_circle_face(b->ambient, b->disc, true, b->on ? k_col_primary : b->cap.face);
            lv_obj_set_style_border_width(b->disc, CASSETTE_CAP_RIM_W, 0);
            lv_obj_set_style_border_color(b->disc, k_col_dome, 0);
        } else if (b->on) {
            /* The dark page's line art grows a face; it keeps the palette's ring. */
            cassette_circle_face(b->ambient, b->disc, false, k_col_primary);
            lv_obj_set_style_bg_opa(b->disc, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b->disc, k_col_primary, 0);
            lv_obj_set_style_border_width(
                b->disc, cassette_button_ring_w(b->disc_ring, b->pressed), 0);
            lv_obj_set_style_border_color(b->disc, k_col_button_circle, 0);
        } else {
            cassette_circle_face(b->ambient, b->disc, false, b->cap.face);
            lv_obj_set_style_border_width(
                b->disc, cassette_button_ring_w(b->disc_ring, b->pressed), 0);
            lv_obj_set_style_border_color(b->disc, k_col_button_circle, 0);
        }
    }

    if (b->letter)
        lv_obj_set_style_text_color(
            b->letter, b->on ? k_col_bg : (b->white ? b->cap.letter : k_col_button_letter), 0);

    button_ring_apply(b);
}

/* The pulse's triangle lives in the portable rule; this is only where the step is applied.
 * The completion step is the rest scale, so no restoring callback is needed. */
static void button_pulse_cb(void *var, int32_t step)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)var, cassette_button_pulse_scale(step), 0);
}

/* The button is its carcase: the pulse scales the host's 96x96 box about its centre, so the
 * carcase, the disc and the letter move as one picture. A new pulse replaces one still in
 * flight at full size, so a press during a pulse does not compound. */
static void button_pulse(cassette_button_t *b)
{
    if (!b || !b->carcase) return;
    lv_anim_delete(b->carcase, button_pulse_cb);
    lv_obj_set_style_transform_scale(b->carcase, CASSETTE_BUTTON_PULSE_SCALE, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b->carcase);
    lv_anim_set_exec_cb(&a, button_pulse_cb);
    lv_anim_set_values(&a, 0, CASSETTE_BUTTON_PULSE_STEPS);
    lv_anim_set_duration(&a, PULSE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void button_pressed_cb(lv_event_t *e)
{
    cassette_button_t *b = lv_event_get_user_data(e);
    if (!b || !b->carcase) return;

    /* A new press replaces a pulse still in flight, at full size, before the ring is
     * written. */
    lv_anim_delete(b->carcase, button_pulse_cb);
    lv_obj_set_style_transform_scale(b->carcase, CASSETTE_BUTTON_PULSE_SCALE, 0);

    b->pressed = true;
    button_ring_apply(b);
}

static void button_released_cb(lv_event_t *e)
{
    cassette_button_t *b = lv_event_get_user_data(e);
    if (!b) return;
    b->pressed = false;
    button_ring_apply(b);
}

/* The click: restore the ring, then the one tap guard, then the pulse and the host's action.
 *
 * The release of a swipe that started on the button also arrives as a click, so a click is
 * only the button's when the rule says it is a tap. A rejected click does nothing at all: no
 * pulse and no action, which is what keeps a swipe across the drawer from also skipping a
 * track.
 *
 * The action is read into locals before the pulse, so a host's callback may replace this
 * button's action (the Menu closing rewrites all three) without the call depending on what
 * the field says afterwards. */
static void button_clicked_cb(lv_event_t *e)
{
    cassette_button_t *b = lv_event_get_user_data(e);
    if (!b) return;

    b->pressed = false;
    button_ring_apply(b);

    lv_indev_t *indev = lv_indev_active();
    bool gesturing = indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
    bool moved = indev && lv_indev_get_press_moved(indev);
    if (!cassette_button_is_tap(moved, gesturing)) return;

    cassette_button_action_t action = b->action;
    void *ctx = b->ctx;

    button_pulse(b);
    if (action) action(ctx);
}

bool cassette_button_create(cassette_button_t *out, lv_obj_t *carcase,
                            const cassette_button_spec_t *spec)
{
    if (!out || !carcase || !spec) return false;

    out->name = spec->name;
    out->carcase = carcase;
    out->ambient = NULL;
    out->disc = NULL;
    out->letter = NULL;
    out->cap = spec->cap;
    out->carcase_ring = spec->carcase_ring;
    out->disc_ring = spec->disc_ring;
    out->action = spec->on_click;
    out->ctx = spec->ctx;
    out->white = false;
    out->pressed = false;
    out->on = false;

    /* The page is the module's ink on the host's carcase: the fill under the disc and the
     * carcase's line. The ring's width is the released table's, whatever the host built. */
    lv_obj_set_style_bg_color(carcase, k_col_bg, 0);
    lv_obj_set_style_border_color(carcase, k_col_button_rect, 0);

    /* The dome is two circles: this transparent one carries the broad shadow, the one inside
     * it is the disc. Both are invisible on the dark page, where the disc is line art. */
    out->ambient = cassette_border_box(carcase, CIRCLE_INSET, CIRCLE_INSET,
                                       CIRCLE_D, CIRCLE_D, CIRCLE_R,
                                       k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    out->disc = cassette_border_box(out->ambient, 0, 0,
                                    CIRCLE_D, CIRCLE_D, CIRCLE_R,
                                    k_col_bg, LV_OPA_TRANSP, k_col_button_circle,
                                    cassette_button_ring_w(spec->disc_ring, false));
    out->letter = cassette_text(out->disc, spec->font ? spec->font : &jura_36,
                                k_col_button_letter, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(out->letter, spec->text ? spec->text : "");

    /* The pulse is centred on the carcase, not on its top-left corner. */
    lv_obj_set_style_transform_pivot_x(carcase, CASSETTE_CELL_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(carcase, CASSETTE_CELL_H / 2, 0);

    button_ink_apply(out);

    lv_obj_add_event_cb(carcase, button_pressed_cb, LV_EVENT_PRESSED, out);
    lv_obj_add_event_cb(carcase, button_released_cb, LV_EVENT_RELEASED, out);
    lv_obj_add_event_cb(carcase, button_released_cb, LV_EVENT_PRESS_LOST, out);
    lv_obj_add_event_cb(carcase, button_clicked_cb, LV_EVENT_CLICKED, out);
    return true;
}

void cassette_button_set_page(cassette_button_t *b, bool white)
{
    if (!b || !b->carcase) return;
    b->white = white;
    button_ink_apply(b);
}

void cassette_button_set_on(cassette_button_t *b, bool on)
{
    if (!b || !b->carcase || b->on == on) return;
    b->on = on;
    button_ink_apply(b);
}

const char *cassette_button_name(const cassette_button_t *b)
{
    return b ? b->name : NULL;
}

void cassette_button_set_label(cassette_button_t *b, const char *text, const lv_font_t *font)
{
    if (!b || !b->letter || !text) return;
    if (font) lv_obj_set_style_text_font(b->letter, font, 0);
    lv_label_set_text(b->letter, text);
}

void cassette_button_set_action(cassette_button_t *b, cassette_button_action_t action, void *ctx)
{
    if (!b) return;
    b->action = action;
    b->ctx = ctx;
}

