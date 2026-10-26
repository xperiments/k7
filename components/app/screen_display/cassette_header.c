/*
 * The band across the top of the controls screen; see cassette_header.h.
 *
 * The band and the K7 mark it carries are built once with their styles at the band's
 * hidden position. They arrive the way the drawer and the DJ block do - one 260 ms
 * ease-out slide, hidden again when it finishes leaving - so the whole frame of the
 * controls screen lands together. A full-width band has no direction to come in from
 * sideways, so its slide is vertical: it drops from just above the panel, and its height
 * is the distance.
 *
 * The mark is greyscale art kept as an A8 mask, tinted at draw time: COL_PRIMARY on the
 * line-art page and black on the white one. It is centred on the transport buttons
 * horizontally and on the band vertically, so it reads as the drawer's own mark.
 */
#include "cassette_header.h"

#include "lvgl.h"

#include "cassette_buttons.h"   /* the drawer's slide time */
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "header_logo.h"

/* The owner's geometry: 768x71 at (16,6). The band's bottom edge is on the first button's
 * and the first DJ cell's top line, and its sides are 16 px from the screen, the cassette's
 * own frame inset. */
#define HEADER_X        16
#define HEADER_Y        6
#define HEADER_W        768
#define HEADER_H        71

/* The drawer buttons' corner and the carcases' 2 px stroke, so the band reads as one of
 * them stretched across the screen. */
#define HEADER_R        8
#define HEADER_BORDER_W 2

/* The mark inside the band. Its horizontal centre is the buttons' - x=64 on the screen,
 * which is 48 in the band - and its vertical centre is the band's 35.5, so the 75x40 art
 * sits at 11,15 in the band (27,21 on the screen). The half pixel the odd width leaves
 * over goes to the right. */
#define LOGO_X          11
#define LOGO_Y          15

/* Where the slide starts and ends: out of sight above the panel, the band's bottom edge on
 * y=0, so the slide is exactly its height. */
#define HEADER_HIDDEN_Y (-HEADER_H)

static lv_obj_t *s_header;
static lv_obj_t *s_logo;
static bool s_shown;

static void header_slide_cb(void *var, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)var, y);
}

/* The slide out is over: hide the band so it is not in the draw tree. A slide in never
 * reaches here hidden; if a reverse deleted this animation, it does not run. */
static void header_slide_done_cb(lv_anim_t *a)
{
    lv_obj_t *band = lv_anim_get_user_data(a);
    if (band && !s_shown) lv_obj_set_hidden(band, true);
}

static void header_slide(bool shown)
{
    if (!s_header) return;
    if (shown) lv_obj_set_hidden(s_header, false);

    lv_anim_delete(s_header, header_slide_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_header);
    lv_anim_set_exec_cb(&a, header_slide_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_header), shown ? HEADER_Y : HEADER_HIDDEN_Y);
    lv_anim_set_duration(&a, CASSETTE_BUTTONS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, s_header);
    if (!shown) lv_anim_set_completed_cb(&a, header_slide_done_cb);
    lv_anim_start(&a);
}

/* The mark's ink. The A8 draw paints the mask in the style's recolor colour, so the page
 * flip is one property: the line-art page's COL_PRIMARY, the white page's black. */
static void header_logo_page(bool white)
{
    if (!s_logo) return;
    lv_obj_set_style_image_recolor(s_logo, white ? k_col_bg : k_col_primary, 0);
    lv_obj_set_style_image_recolor_opa(s_logo, LV_OPA_COVER, 0);
}

void cassette_header_create(lv_obj_t *screen)
{
    if (!screen || s_header) return;

    s_header = cassette_border_box(screen, HEADER_X, HEADER_HIDDEN_Y, HEADER_W, HEADER_H,
                                   HEADER_R, k_col_bg, LV_OPA_COVER, k_col_button_rect,
                                   HEADER_BORDER_W);
    s_logo = cassette_image(s_header, &k_header_logo, LOGO_X, LOGO_Y,
                            HEADER_LOGO_W, HEADER_LOGO_H);
    header_logo_page(false);
    lv_obj_set_hidden(s_header, true);
}

void cassette_header_set_shown(bool shown)
{
    if (!s_header || shown == s_shown) return;
    s_shown = shown;
    header_slide(shown);
}

void cassette_header_set_page(bool white)
{
    if (!s_header) return;
    lv_obj_set_style_bg_color(s_header, white ? k_col_white : k_col_bg, 0);
    header_logo_page(white);
}
