/*
 * The frame around the cassette on the controls screen; see cassette_window.h.
 *
 * One object, built once with its styles at its final position, transparent and hidden. It
 * has no edge to slide in from - it is a window, not a band - so it fades: 0..255 over the
 * drawer's 260 ms, reversed from whatever opacity it holds if the controls change their
 * mind, and hidden at the end of the fade out. Only the object's own box is invalidated per
 * step, not the screen.
 */
#include "cassette_window.h"

#include "lvgl.h"

#include "cassette_buttons.h"   /* the drawer's slide time */
#include "cassette_style.h"
#include "cassette_widgets.h"

/* The owner's geometry: 470x284 at (110,76). The top and bottom edges share the bands'
 * lines and the sides sit in the gaps either side of the scaled tape. */
#define WINDOW_X        110
#define WINDOW_Y        76
#define WINDOW_W        470
#define WINDOW_H        284

/* The drawer buttons' corner and the carcases' 2 px stroke: the three rectangles and the
 * carcases are one frame. */
#define WINDOW_R        8
#define WINDOW_BORDER_W 2

static lv_obj_t *s_window;
static bool s_shown;

static void window_fade_cb(void *var, int32_t opa)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)opa, 0);
}

/* The fade out is over: hide the window so it is not in the draw tree. A fade in never
 * reaches here hidden; if a reverse deleted this animation, it does not run. */
static void window_fade_done_cb(lv_anim_t *a)
{
    lv_obj_t *w = lv_anim_get_user_data(a);
    if (w && !s_shown) lv_obj_set_hidden(w, true);
}

static void window_fade(bool shown)
{
    if (!s_window) return;
    if (shown) lv_obj_set_hidden(s_window, false);

    lv_anim_delete(s_window, window_fade_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_window);
    lv_anim_set_exec_cb(&a, window_fade_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa(s_window, 0),
                       shown ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_anim_set_duration(&a, CASSETTE_BUTTONS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, s_window);
    if (!shown) lv_anim_set_completed_cb(&a, window_fade_done_cb);
    lv_anim_start(&a);
}

void cassette_window_create(lv_obj_t *screen)
{
    if (!screen || s_window) return;

    s_window = cassette_border_box(screen, WINDOW_X, WINDOW_Y, WINDOW_W, WINDOW_H, WINDOW_R,
                                   k_col_bg, LV_OPA_COVER, k_col_bg, 
                                   WINDOW_BORDER_W);
    lv_obj_set_style_opa(s_window, LV_OPA_TRANSP, 0);
    lv_obj_set_hidden(s_window, true);
}

void cassette_window_set_shown(bool shown)
{
    if (!s_window || shown == s_shown) return;
    s_shown = shown;
    window_fade(shown);
}

void cassette_window_set_menu_page(bool on, bool white)
{
    if (!s_window) return;
    /* Only the fill: the Menu draws its Rows on it, so while it is up the window is the
     * page's ground (k_col_white on the white page, k_col_bg on the dark one). Outside the
     * Menu it is k_col_bg again, the ground the cassette stands on. The border, the corner
     * and the fade state belong to the cassette's window and are not touched. */
    lv_obj_set_style_bg_color(s_window, (on && white) ? k_col_white : k_col_bg, 0);
}

void cassette_window_rect(int *x, int *y, int *w, int *h)
{
    if (x) *x = WINDOW_X;
    if (y) *y = WINDOW_Y;
    if (w) *w = WINDOW_W;
    if (h) *h = WINDOW_H;
}
