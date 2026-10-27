/*
 * The band across the bottom of the controls screen; see cassette_footer.h.
 *
 * The band and its three Menu text lines are built once with their styles at the band's
 * hidden position. It is the header's mirror, and it arrives the way the drawer and the DJ
 * block do - one 260 ms ease-out slide, hidden again when it finishes leaving - so the whole
 * frame of the controls screen lands together. A full-width band has no direction to come in
 * from sideways, so its slide is vertical: it rises from below the panel.
 *
 * The Menu puts the now playing here: the song and the group on two left lines and the
 * list's position on the right, all empty and hidden outside the Menu. Their ink is the
 * page's label ink, repainted with the band by set_page().
 */
#include "cassette_footer.h"

#include <stdio.h>

#include "lvgl.h"

#include "cassette_bars.h"   /* the chrome's slide time */
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(jura_28);

/* The band's rect comes from the layout: 768 px wide at (16,358), 116 tall, 6 px short of
 * the panel's bottom, so its top edge is on the last button's and the last DJ cell's bottom
 * line and its sides are 16 px from the screen, the cassette's own frame inset. */

/* Where the slide starts and ends: out of sight below the panel, the band's top edge on the
 * screen's bottom edge. */
#define FOOTER_HIDDEN_Y 480

/* The Menu's now playing. The song and the group are top-left placed in band coordinates -
 * Jura 28 at (24,21) and (24,55) - and ellipsized at 512 px so they stop 8 px before the
 * position's box. The position is right-aligned at band x=744 and vertically centred, its
 * 200 px box large enough for a "1204 / 1204" and clear of the two left lines. LVGL measures
 * a child's position from the parent's content box, which starts after the band's 2 px
 * border, so the style offsets are the band's coordinates less that border: the song at
 * (22,19), the group at (22,53) and the position 22 px in from the inner right edge. */
#define PLAY_TEXT_X     24
#define PLAY_SONG_Y     21
#define PLAY_GROUP_Y    55
#define PLAY_TEXT_W     512
#define PLAY_POS_X      744
#define PLAY_POS_W      200
#define PLAY_TEXT_ALIGN_X  (PLAY_TEXT_X - CASSETTE_BORDER_W)
#define PLAY_SONG_ALIGN_Y  (PLAY_SONG_Y - CASSETTE_BORDER_W)
#define PLAY_GROUP_ALIGN_Y (PLAY_GROUP_Y - CASSETTE_BORDER_W)

/* The Menu's texts are copied because the caller's buffers may be reused. The Menu view
 * builds the song and the group in 128-byte lines, and the position is short
 * ("3 / 1204"); these hold the same without an earlier truncation. */
#define PLAY_SONG_MAX   128
#define PLAY_GROUP_MAX  128
#define PLAY_POS_MAX    32

static lv_obj_t *s_footer;
static lv_obj_t *s_song;
static lv_obj_t *s_group;
static lv_obj_t *s_position;
static char s_song_text[PLAY_SONG_MAX];
static char s_group_text[PLAY_GROUP_MAX];
static char s_position_text[PLAY_POS_MAX];
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

    cassette_rect_t band = cassette_part_rect(CASSETTE_PART_FOOTER);
    lv_anim_delete(s_footer, footer_slide_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_footer);
    lv_anim_set_exec_cb(&a, footer_slide_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_footer), shown ? band.y : FOOTER_HIDDEN_Y);
    lv_anim_set_duration(&a, CASSETTE_BARS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, s_footer);
    if (!shown) lv_anim_set_completed_cb(&a, footer_slide_done_cb);
    lv_anim_start(&a);
}

/* The now playing's ink on the page: the page's own label ink, k_col_bg on the white page
 * and k_col_primary on the dark one, the same pair a Row's label takes. */
static void footer_play_page(bool white)
{
    lv_color_t ink = white ? k_col_bg : k_col_primary;
    if (s_song) lv_obj_set_style_text_color(s_song, ink, 0);
    if (s_group) lv_obj_set_style_text_color(s_group, ink, 0);
    if (s_position) lv_obj_set_style_text_color(s_position, ink, 0);
}

/* Copy one line into its fixed buffer and show or hide its label. */
static void play_text_set(lv_obj_t *label, char *buf, size_t n, const char *text)
{
    if (!label) return;
    snprintf(buf, n, "%s", text ? text : "");
    lv_label_set_text(label, buf);
    lv_obj_set_hidden(label, buf[0] == '\0');
}

void cassette_footer_create(lv_obj_t *screen)
{
    if (!screen || s_footer) return;

    cassette_rect_t band = cassette_part_rect(CASSETTE_PART_FOOTER);
    int pos_align_x = -(band.w - CASSETTE_BORDER_W - PLAY_POS_X);

    s_footer = cassette_border_box(screen, band.x, FOOTER_HIDDEN_Y, band.w, band.h,
                                   CASSETTE_CORNER_R, k_col_bg, LV_OPA_COVER,
                                   k_col_button_rect, CASSETTE_BORDER_W);

    /* The Menu's now playing, empty and hidden until the Menu fills it. The two left lines
     * are top-left placed at the band's coordinates (the style offsets are inside the 2 px
     * border, hence the subtraction); only the position uses the band's right alignment,
     * where LVGL's vertical centring is what is wanted. */
    s_song = cassette_text(s_footer, &jura_28, k_col_primary, LV_ALIGN_TOP_LEFT,
                           PLAY_TEXT_ALIGN_X, PLAY_SONG_ALIGN_Y);
    lv_obj_set_width(s_song, PLAY_TEXT_W);
    lv_label_set_long_mode(s_song, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_song, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_hidden(s_song, true);

    s_group = cassette_text(s_footer, &jura_28, k_col_primary, LV_ALIGN_TOP_LEFT,
                            PLAY_TEXT_ALIGN_X, PLAY_GROUP_ALIGN_Y);
    lv_obj_set_width(s_group, PLAY_TEXT_W);
    lv_label_set_long_mode(s_group, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_group, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_hidden(s_group, true);

    s_position = cassette_text(s_footer, &jura_28, k_col_primary, LV_ALIGN_RIGHT_MID,
                               pos_align_x, 0);
    lv_obj_set_width(s_position, PLAY_POS_W);
    /* The fixed box the text is right-aligned in; the alignment style keeps the right edge
     * on the band's 744 as the width and the text change. */
    lv_obj_align(s_position, LV_ALIGN_RIGHT_MID, pos_align_x, 0);
    lv_label_set_long_mode(s_position, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_position, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_hidden(s_position, true);

    footer_play_page(false);
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
    footer_play_page(white);
}

void cassette_footer_set_playing(const char *song, const char *group, const char *position)
{
    play_text_set(s_song, s_song_text, sizeof s_song_text, song);
    play_text_set(s_group, s_group_text, sizeof s_group_text, group);
    play_text_set(s_position, s_position_text, sizeof s_position_text, position);
}
