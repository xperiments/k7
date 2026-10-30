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

#include "cassette_bars.h"   /* the chrome's slide time */
#include "chrome_entrance.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

static lv_obj_t *s_window;

/* The window has no edge to come in from, so its arrival is an opacity rather than a slide.
 * The rule is the same one the bands use: 0 is away, 255 is here. */
static chrome_entrance_t s_fade;

static void fade_write(int32_t opa, void *ctx)
{
    (void)ctx;
    lv_obj_set_style_opa(s_window, (lv_opa_t)opa, 0);
    lv_obj_set_hidden(s_window, false);
}

static void fade_hide(void *ctx)
{
    (void)ctx;
    if (s_window) lv_obj_set_hidden(s_window, true);
}

void cassette_window_create(lv_obj_t *screen)
{
    if (!screen || s_window) return;

    /* The layout's window; the corner and the stroke are the frame's own, shared with
     * the bands and the carcases. */
    cassette_rect_t rect = cassette_part_rect(CASSETTE_PART_WINDOW);
    s_window = cassette_border_box(screen, rect.x, rect.y, rect.w, rect.h,
                                   CASSETTE_CORNER_R, k_col_bg, LV_OPA_COVER, k_col_bg,
                                   CASSETTE_BORDER_W);
    lv_obj_set_style_opa(s_window, LV_OPA_TRANSP, 0);
    lv_obj_set_hidden(s_window, true);

    chrome_entrance_init(&s_fade, LV_OPA_TRANSP, LV_OPA_COVER, fade_write, fade_hide, NULL);
}

void cassette_window_set_shown(bool shown)
{
    if (!s_window) return;
    chrome_entrance_set(&s_fade, shown);
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
