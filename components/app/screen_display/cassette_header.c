/*
 * The band across the top of the controls screen; see cassette_header.h.
 *
 * The band, the K7 badge and the Menu's title are built once with their styles at the band's
 * hidden position. They arrive the way the drawer and the DJ block do - one 260 ms
 * ease-out slide, hidden again when it finishes leaving - so the whole frame of the
 * controls screen lands together. A full-width band has no direction to come in from
 * sideways, so its slide is vertical: it drops from just above the panel, and its height
 * is the distance.
 *
 * The mark is greyscale art kept as an A8 mask, tinted at draw time, inside a rounded badge
 * at the band's left: a primary plate with the light cap ink on the line-art page, a white
 * plate with the page's dark border and dark ink on the white one. The badge is the band's
 * sibling and slides with it.
 *
 * The Menu's list title is the band's other occupant: a Jura 36 label at band x=104,
 * vertically centred and ellipsized at 648 px. It is empty and hidden while the Menu is
 * closed, and its ink is the page's own label ink - k_col_bg on the white page,
 * k_col_primary on the dark one - like a Row's.
 */
#include "cassette_header.h"

#include <stdio.h>

#include "lvgl.h"

#include "cassette_buttons.h"   /* the drawer's slide time */
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "header_logo.h"

LV_FONT_DECLARE(jura_36);

/* The frame's top band and the badge beside it: both 71 px tall at y=6, on the first
 * button's and the first DJ cell's top line. The band starts HEADER_X px in, two pixels
 * inside the 96 px badge, so the badge's right border is the divider and no second line runs
 * beside it; the band's own right edge keeps the frame's 16 px inset. */
#define HEADER_X        16+94
#define HEADER_Y        6
#define HEADER_W        768-94
#define HEADER_H        71

/* The drawer buttons' corner and the carcases' 2 px stroke, so the band reads as one of
 * them stretched across the screen. */
#define HEADER_R        8
#define HEADER_BORDER_W 2

/* The badge at the band's left: a 96x71 rounded plate at (16, HEADER_Y) that holds the K7
 * mark. The band's own left edge starts two pixels inside it (HEADER_X), so the plate's
 * border is the whole divider between them and no second line runs beside it. */
#define LOGO_BOX_X      16
#define LOGO_BOX_W      96
#define LOGO_BOX_R      HEADER_R

/* The Menu's list title: Jura 36 at band x=104, vertically centred and ellipsized at 648 px.
 * The alignment box starts inside the band's 2 px border, so the offset is 102 for the band's
 * 104. */
#define TITLE_X         16
#define TITLE_ALIGN_X   (TITLE_X - HEADER_BORDER_W)
#define TITLE_W         648

/* The title is copied because the caller's buffer may be reused. 648 px of Jura 36 holds
 * well over a row's 96-byte label; anything longer is truncated here and ellipsized by the
 * label itself. */
#define TITLE_MAX       128

/* Where the slide starts and ends: out of sight above the panel, the band's bottom edge on
 * y=0, so the slide is exactly its height. */
#define HEADER_HIDDEN_Y (-HEADER_H)

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

    s_header = cassette_border_box(screen, HEADER_X, HEADER_HIDDEN_Y, HEADER_W, HEADER_H,
                                   HEADER_R, k_col_bg, LV_OPA_COVER, k_col_button_rect,
                                   HEADER_BORDER_W);

    /* The badge: a plate of its own at the band's left, a sibling above the band so its
     * border, not the band's, is the divider between them, and the mark centred inside it.
     * The image is the plate's child, so it is clipped by it and centred on its content. */
    s_logo_box = cassette_border_box(screen, LOGO_BOX_X, HEADER_HIDDEN_Y, LOGO_BOX_W,
                                     HEADER_H, LOGO_BOX_R, k_col_primary, LV_OPA_COVER,
                                     k_col_primary, HEADER_BORDER_W);
    s_logo = cassette_image(s_logo_box, &k_header_logo, 2, 0, HEADER_LOGO_W, HEADER_LOGO_H);
    lv_obj_align(s_logo, LV_ALIGN_CENTER, 0, 0);
    header_logo_page(false);

    /* The Menu's title, empty until the Menu gives it one. The width is the fixed box the
     * dots are cut in; the alignment (a style, so it follows the label's size) puts its
     * left edge on the band's 104 and centres it vertically. */
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
