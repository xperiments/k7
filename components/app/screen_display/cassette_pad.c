/*
 * The right bar's Pad; see cassette_pad.h.
 *
 * The plate is the layout's Pad rect turned white on both pages, with a dark inner inset 8 px
 * and one dotted vertical guide inside it. The press machine is pad.h; this file is the plate,
 * the LVGL event codes and the target indirection, and it holds no model of its own.
 */
#include "cassette_pad.h"

#include <stdint.h>

#include "cassette_layout.h"
#include "cassette_style.h"
#include "pad.h"

/* The plate: a white card (k_col_white) at the Pad's rect, with a dark inner (k_col_bg) inset
 * 8 px and one dotted vertical guide inside it. Only the inner and the guide are the Pad's own
 * numbers. */
#define PAD_INSET       8
#define PAD_GUIDE_INSET 66

/* lv_line keeps the caller's points, so they outlive the call; pad_build() fills them from the
 * layout's Pad each time the panel is installed. */
static lv_point_precise_t s_guides[1][2];

static lv_obj_t *s_cell;                       /* the bar's carcase the plate is built into */
static lv_obj_t *s_inner;                      /* the dark ground the guide sits on */
static lv_obj_t *s_guide;                      /* the dotted vertical */
static pad_t s_state;                          /* the press in flight */
static const cassette_pad_target_t *s_target;  /* who the Pad is for, or NULL */

/* ---------------------------------------------------------------- the plate's ink */

/* The Pad's ink, per page. On the white page it is a white card - the same plate the Menu has
 * always been - with no border of its own and the shared 8 px corner. On the dark page it is the
 * palette's line art: the ground, a primary frame and a primary dotted guide, so the one surface
 * that owns the touch does not read as a grey slab among orange lines. The top and bottom rows are
 * where the header's bottom line and the footer's top line cross the Pad; the bar is above the
 * bands now, so those two rows are drawn as rules of their own. */
static void pad_ink(lv_obj_t *cell, bool white)
{
    if (!cell) return;
    lv_obj_set_style_bg_color(cell, white ? k_col_white : k_col_bg, 0);
    lv_obj_set_style_border_width(cell, white ? 0 : 2, 0);
    lv_obj_set_style_border_color(cell, k_col_primary, 0);
    lv_obj_set_style_radius(cell, CASSETTE_CORNER_R, 0);

    if (s_inner) {
        lv_obj_set_style_bg_color(s_inner, k_col_bg, 0);
        lv_obj_set_style_border_width(s_inner, white ? 0 : 2, 0);
        lv_obj_set_style_border_color(s_inner, k_col_primary, 0);
    }
    if (s_guide) lv_obj_set_style_line_color(s_guide, white ? k_col_white : k_col_primary, 0);
}

/* ---------------------------------------------------------------- the press machine */

static void pad_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    /* A gesture on the Pad is the Pad's: the bar's right cells keep their gestures local (the
     * Pad is not the screen's), so the leftward one is read here - it is the owner's Back, at
     * LVGL's 50 px threshold, whether or not the release also reaches the pad's own rule. */
    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_active();
        if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_LEFT) {
            pad_reset(&s_state);
            if (s_target && s_target->back) s_target->back(s_target->ctx);
        }
        return;
    }

    pad_in_t in;
    switch (code) {
        case LV_EVENT_PRESSED: in = PAD_IN_PRESS; break;
        case LV_EVENT_PRESSING: in = PAD_IN_MOVE; break;
        case LV_EVENT_RELEASED: in = PAD_IN_RELEASE; break;
        case LV_EVENT_PRESS_LOST: in = PAD_IN_LOST; break;
        default: return;
    }

    lv_point_t p = { 0, 0 };
    if (in != PAD_IN_LOST) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;
        lv_indev_get_point(indev, &p);
    }

    pad_ev_t ev = pad_feed(&s_state, in, p.x, p.y, lv_tick_get());
    if (!s_target) return;

    switch (ev.kind) {
        case PAD_EV_STEP:
            if (s_target->step) s_target->step(ev.dy, ev.dt, s_target->ctx);
            break;
        case PAD_EV_TAP:
            if (s_target->tap) s_target->tap(s_target->ctx);
            break;
        case PAD_EV_BACK:
            if (s_target->back) s_target->back(s_target->ctx);
            break;
        default:
            break;
    }

    /* The brackets a travel accumulator needs: where the press began, and that it is over.
     * After the event, so a TAP and a RELEASE in the same sample read in that order. */
    if (in == PAD_IN_PRESS && s_target->press) s_target->press(s_target->ctx);
    else if ((in == PAD_IN_RELEASE || in == PAD_IN_LOST) && s_target->release)
        s_target->release(s_target->ctx);
}

/* ---------------------------------------------------------------------- the panel */

/* The Pad is one plate, not a grid: the bar builds one carcase at the layout's Pad rect and
 * build() turns it into the plate. It covers the whole group, so the group's cells go away
 * while something owns the bar and come back when it closes. */
static cassette_rect_t pad_cell_rect(int col, int row)
{
    (void)col;
    (void)row;
    return cassette_part_rect(CASSETTE_PART_PAD);
}

/* One carcase, the bar's, turned into the Pad. The bar made it clickable and gave it the
 * panel's hit rule; the pickable surface is the carcase itself, with the inner and the guide
 * decorative so a press on them falls through to it. */
static void pad_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)col;
    (void)row;
    (void)ctx;
    if (!cell) return;

    s_cell = cell;

    cassette_rect_t pad = cassette_part_rect(CASSETTE_PART_PAD);

    /* The two frame rows the old screen-child Pad let the bands draw over it. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *rule = lv_obj_create(cell);
        lv_obj_remove_style_all(rule);
        lv_obj_set_pos(rule, 0, i == 0 ? 0 : pad.h - 2);
        lv_obj_set_size(rule, pad.w, 2);
        lv_obj_set_style_bg_color(rule, k_col_button_rect, 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rule, 0, 0);
        lv_obj_set_style_radius(rule, 0, 0);
        lv_obj_set_clickable(rule, false);
    }

    int inner_w = pad.w - 2 * PAD_INSET;
    int inner_h = pad.h - 2 * PAD_INSET;
    int guide_x = inner_w / 2 - PAD_GUIDE_INSET;
    s_guides[0][0].x = guide_x;
    s_guides[0][0].y = PAD_GUIDE_INSET;
    s_guides[0][1].x = guide_x;
    s_guides[0][1].y = inner_h - PAD_GUIDE_INSET;

    /* The inner plate: the dark ground the guide sits on. */
    s_inner = lv_obj_create(cell);
    lv_obj_t *inner = s_inner;
    lv_obj_remove_style_all(inner);
    lv_obj_set_pos(inner, PAD_INSET, PAD_INSET);
    lv_obj_set_size(inner, inner_w, inner_h);
    lv_obj_set_style_bg_color(inner, k_col_bg, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(inner, 0, 0);
    lv_obj_set_style_radius(inner, PAD_INSET, 0);
    lv_obj_set_style_pad_all(inner, 0, 0);
    lv_obj_set_scrollable(inner, false);
    lv_obj_set_clickable(inner, false);

    /* The guide: dotted, light on the dark inner. */
    lv_obj_t *line = lv_line_create(inner);
    s_guide = line;
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, k_col_white, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_dash_width(line, 2, 0);
    lv_obj_set_style_line_dash_gap(line, 4, 0);
    lv_obj_set_clickable(line, false);
    lv_line_set_points(line, s_guides[0], 2);

    lv_obj_add_event_cb(cell, pad_event_cb, LV_EVENT_ALL, NULL);
}

/* The bar calls this last in set_panel() and again whenever the bar's visibility changes: after
 * the carcase's default page ink, so the plate is put back white on both pages. A swap also
 * resets the press in flight before the old carcase goes. */
/* The bar inks the plate itself on install and on every page flip (bar_ink() calls set_page),
 * so this only drops whatever press was in flight. */
static void pad_shown(bool visible, void *ctx)
{
    (void)visible;
    (void)ctx;
    pad_reset(&s_state);
}

/* The Pad is white on both pages, so a page flip while it is up must not let the bar's default
 * dark fill through. The model drops every intent while a mode owns the bar, but the hook costs
 * nothing and makes the panel self-contained. */
static void pad_page(bool white, void *ctx)
{
    (void)ctx;
    pad_ink(s_cell, white);
}

static const cassette_panel_t k_pad_panel = {
    .columns = 1,
    .rows = 1,
    .cell_rect = pad_cell_rect,
    .gesture_bubble = false,
    .build = pad_build,
    .set_page = pad_page,
    .shown = pad_shown,
    .ctx = NULL,
};

/* ------------------------------------------------------------------------ public */

void cassette_pad_set_target(const cassette_pad_target_t *target)
{
    s_target = target;
    pad_reset(&s_state);
}

const cassette_panel_t *cassette_pad_panel(void)
{
    return &k_pad_panel;
}
