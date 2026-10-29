/*
 * The band across the top of the controls screen; see cassette_header.h.
 *
 * The band, the K7 badge and the Menu's title are built once with their styles at the band's
 * hidden position. They arrive the way the two bars do - one 260 ms
 * ease-out slide, hidden again when it finishes leaving - so the whole frame of the
 * controls screen lands together. A full-width band has no direction to come in from
 * sideways, so its slide is vertical: it drops from just above the panel, and its height
 * is the distance.
 *
 * The mark is greyscale art kept as an A8 mask, tinted at draw time, inside a rounded badge
 * at the band's left: a k_col_bg plate with the primary border and mark on the line-art
 * page, a k_col_logo_back plate with the page's dark border and mark on the white one. The
 * badge is the band's sibling and slides with it.
 *
 * The Menu's list title is the band's other occupant: a Jura 36 label at screen x=126,
 * vertically centred and ellipsized at 648 px. It is empty and hidden while the Menu is
 * closed, and its ink is the page's own label ink - k_col_bg on the white page,
 * k_col_primary on the dark one - like a Row's.
 */
#include "cassette_header.h"

#include <stdio.h>

#include "lvgl.h"

#include "cassette_bars.h"   /* the chrome's slide time */
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "header_logo.h"

LV_FONT_DECLARE(jura_36);

/* The band and the badge come from the layout: both 72 px tall at y=6, on the first
 * button's and the first DJ cell's top line. The band starts at 110, two pixels inside the
 * 96 px badge, so the badge's right border is the divider and no second line runs beside
 * it; the band's own right edge is the frame's 784. */
#define TITLE_X         16
#define TITLE_ALIGN_X   (TITLE_X - CASSETTE_BORDER_W)
#define TITLE_W         648

/* The title is copied because the caller's buffer may be reused. 648 px of Jura 36 holds
 * well over a row's 96-byte label; anything longer is truncated here and ellipsized by the
 * label itself. */
#define TITLE_MAX       128

/* Where the slide starts: out of sight above the panel, the band's bottom edge on y=0. */

static lv_obj_t *s_header;
static lv_obj_t *s_logo_box;
static lv_obj_t *s_logo;
static lv_obj_t *s_title;
static char s_title_text[TITLE_MAX];
static bool s_shown;

static void header_slide_cb(void *var, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)var, y);
    /* The badge is a sibling, not a child: the band's border is tucked under it, and it
     * rides the same slide at the band's y. */
    if (s_logo_box) lv_obj_set_y(s_logo_box, y);
}

/* The slide out is over: hide the band so it is not in the draw tree. A slide in never
 * reaches here hidden; if a reverse deleted this animation, it does not run. */
static void header_slide_done_cb(lv_anim_t *a)
{
    lv_obj_t *band = lv_anim_get_user_data(a);
    if (!s_shown) {
        if (band) lv_obj_set_hidden(band, true);
        if (s_logo_box) lv_obj_set_hidden(s_logo_box, true);
    }
}

static void header_slide(bool shown)
{
    if (!s_header) return;
    if (shown) {
        lv_obj_set_hidden(s_header, false);
        if (s_logo_box) lv_obj_set_hidden(s_logo_box, false);
    }

    cassette_rect_t band = cassette_part_rect(CASSETTE_PART_HEADER);
    lv_anim_delete(s_header, header_slide_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_header);
    lv_anim_set_exec_cb(&a, header_slide_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_header), shown ? band.y : -band.h);
    lv_anim_set_duration(&a, CASSETTE_BARS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, s_header);
    if (!shown) lv_anim_set_completed_cb(&a, header_slide_done_cb);
    lv_anim_start(&a);
}

/* The badge and the mark's ink. On the line-art page the plate is the palette's primary,
 * border and all, and the mark takes the light ink a letter on a coloured cap takes; on the
 * white page the plate is the page's white with the page's own dark border, and the mark is
 * the page's ink. */
static void header_logo_page(bool white)
{
    if (s_logo_box) {
        lv_obj_set_style_bg_color(s_logo_box, white ? k_col_logo_back : k_col_bg, 0);
        lv_obj_set_style_border_color(s_logo_box, white ? k_col_bg : k_col_primary, 0);
    }
    if (!s_logo) return;
    lv_obj_set_style_image_recolor(s_logo, white ? k_col_bg : k_col_primary, 0);
    lv_obj_set_style_image_recolor_opa(s_logo, LV_OPA_COVER, 0);
}

/* The title's ink on the page: the page's own label ink, k_col_bg on the white page and
 * k_col_primary on the dark one, the same pair a Row's label takes. */
static void header_title_page(bool white)
{
    if (!s_title) return;
    lv_obj_set_style_text_color(s_title, white ? k_col_bg : k_col_primary, 0);
}

void cassette_header_create(lv_obj_t *screen)
{
    if (!screen || s_header) return;

    cassette_rect_t band = cassette_part_rect(CASSETTE_PART_HEADER);
    cassette_rect_t badge = cassette_part_rect(CASSETTE_PART_BADGE);
    int hidden_y = -band.h;

    s_header = cassette_border_box(screen, band.x, hidden_y, band.w, band.h,
                                   CASSETTE_CORNER_R, k_col_bg, LV_OPA_COVER,
                                   k_col_button_rect, CASSETTE_BORDER_W);

    /* The badge: a plate of its own at the band's left, a sibling above the band so its
     * border, not the band's, is the divider between them, and the mark centred inside it.
     * The image is the plate's child, so it is clipped by it and centred on its content. */
    s_logo_box = cassette_border_box(screen, badge.x, hidden_y, badge.w, badge.h,
                                     CASSETTE_CORNER_R, k_col_primary, LV_OPA_COVER,
                                     k_col_primary, CASSETTE_BORDER_W);
    s_logo = cassette_image(s_logo_box, &k_header_logo, 2, 0, HEADER_LOGO_W, HEADER_LOGO_H);
    lv_obj_align(s_logo, LV_ALIGN_CENTER, 0, 0);
    header_logo_page(false);

    /* The Menu's title, empty until the Menu gives it one. The width is the fixed box the
     * dots are cut in; the alignment (a style, so it follows the label's size) puts its
     * left edge 16 px inside the band's content box and centres it vertically. */
    s_title = cassette_text(s_header, &jura_36, k_col_primary, LV_ALIGN_LEFT_MID,
                            TITLE_ALIGN_X, 0);
    lv_obj_set_width(s_title, TITLE_W);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, TITLE_ALIGN_X, 0);
    lv_obj_set_hidden(s_title, true);

    lv_obj_set_hidden(s_header, true);
    lv_obj_set_hidden(s_logo_box, true);
}

void cassette_header_set_shown(bool shown)
{
    if (!s_header || shown == s_shown) return;
    s_shown = shown;
    header_slide(shown);
}

void cassette_header_raise_badge(void)
{
    if (s_logo_box) lv_obj_move_foreground(s_logo_box);
}

void cassette_header_set_page(bool white)
{
    if (!s_header) return;
    lv_obj_set_style_bg_color(s_header, white ? k_col_white : k_col_bg, 0);
    header_logo_page(white);
    header_title_page(white);
}

void cassette_header_set_title(const char *title)
{
    if (!s_title) return;
    /* Copy: the caller's buffer may be reused (the Menu's model hands out its row text). */
    snprintf(s_title_text, sizeof s_title_text, "%s", title ? title : "");
    lv_label_set_text(s_title, s_title_text);
    lv_obj_set_hidden(s_title, s_title_text[0] == '\0');
}
