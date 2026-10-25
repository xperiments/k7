/*
 * The band across the bottom of the controls screen; see cassette_footer.h.
 *
 * One object, built once with its styles at its hidden position. It is the header's mirror,
 * and it arrives the way the drawer and the DJ block do - one 260 ms ease-out slide, hidden
 * again when it finishes leaving - so the whole frame of the controls screen lands together.
 * A full-width band has no direction to come in from sideways, so its slide is vertical: it
 * rises from below the panel.
 */
#include "cassette_footer.h"

#include "lvgl.h"

#include "cassette_buttons.h"   /* the drawer's slide time */
#include "cassette_style.h"
#include "cassette_widgets.h"

/* The owner's geometry: 768 px wide at (16,359), 6 px short of the panel's bottom, so the
 * band's top edge is on the last button's and the last DJ cell's bottom line and its sides
 * are 16 px from the screen, the cassette's own frame inset. */
#define FOOTER_X        16
#define FOOTER_Y        359
#define FOOTER_W        768
#define FOOTER_H        121-6

/* The drawer buttons' corner and the carcases' 2 px stroke, the header's own: the two
 * bands and the carcases are one frame. */
#define FOOTER_R        8
#define FOOTER_BORDER_W 2

/* Where the slide starts and ends: out of sight below the panel, the band's top edge on the
 * screen's bottom edge. */
#define FOOTER_HIDDEN_Y 480

static lv_obj_t *s_footer;
static bool s_shown;

static void footer_slide_cb(void *var, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)var, y);
}

/* The slide out is over: hide the band so it is not in the draw tree. A slide in never
 * reaches here hidden; if a reverse deleted this animation, it does not run. */
static void footer_slide_done_cb(lv_anim_t *a)
{
    lv_obj_t *band = lv_anim_get_user_data(a);
    if (band && !s_shown) lv_obj_set_hidden(band, true);
}

static void footer_slide(bool shown)
{
    if (!s_footer) return;
    if (shown) lv_obj_set_hidden(s_footer, false);

    lv_anim_delete(s_footer, footer_slide_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_footer);
    lv_anim_set_exec_cb(&a, footer_slide_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_footer), shown ? FOOTER_Y : FOOTER_HIDDEN_Y);
    lv_anim_set_duration(&a, CASSETTE_BUTTONS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, s_footer);
    if (!shown) lv_anim_set_completed_cb(&a, footer_slide_done_cb);
    lv_anim_start(&a);
}

void cassette_footer_create(lv_obj_t *screen)
{
    if (!screen || s_footer) return;

    s_footer = cassette_border_box(screen, FOOTER_X, FOOTER_HIDDEN_Y, FOOTER_W, FOOTER_H,
                                   FOOTER_R, k_col_bg, LV_OPA_COVER, k_col_button_rect,
                                   FOOTER_BORDER_W);
    lv_obj_set_hidden(s_footer, true);
}

void cassette_footer_set_shown(bool shown)
{
    if (!s_footer || shown == s_shown) return;
    s_shown = shown;
    footer_slide(shown);
}

void cassette_footer_set_page(bool white)
{
    if (!s_footer) return;
    lv_obj_set_style_bg_color(s_footer, white ? k_col_white : k_col_bg, 0);
}
