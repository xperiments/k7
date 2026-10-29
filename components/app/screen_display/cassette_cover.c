/*
 * cassette_cover.c - the white page's Track picture; see cassette_cover.h.
 *
 * The Cover is printed on the Card (ADR 0039). Its container is therefore the Card's own
 * rectangle, 426x272 at (136,82), and it carries the Card's A8 mask: the picture is cut at
 * the pill window the Card and the print share, so the tape below shows through both.
 * Inside that container a 402x188 box at +12,+20 - frame[0]'s rect at the controls pose - is
 * the print proper: the width the picture is sized to and the rounded corner it is clipped
 * by. The window lands at (55,55) in it, because the mask's stadium is at (67,75) in the
 * Card. The 1 s fade is the container's, so the picture and the mask go out together and the
 * Card does not fade with them.
 *
 * The mask is not this module's, and it does not exist yet when this module is created: the
 * Cover is the cassette's bottom layer and the Card is built after the tape body, above it.
 * The descriptor is applied at the top of place() - which cover_show() and the view's pose
 * step both reach once the Card is there - and never in create(). That ordering is why
 * set_image() alone cannot make the mask appear; it looks like a bug and is not. While the
 * descriptor is NULL (the Card's allocation failed, or nothing has built it) the opaque pill
 * under the print stands in for the window.
 *
 * The picture is top-aligned and its width is the print's: its height follows from the
 * source's aspect and the print clips the rest. The image object is stretched to the exact
 * size that follows, so no separate pivot arithmetic is needed.
 */
#include "cassette_cover.h"

#include "lvgl.h"

#include "cassette_card.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(jura_28);

static lv_obj_t *s_masked;          /* the Card's rect: the object the shared mask is sized to */
static lv_obj_t *s_print;          /* the print proper: 402x188 at +12,+20 */
static lv_obj_t *s_img;            /* the Track picture, at the print's top-left */
static lv_obj_t *s_fallback;           /* opaque fallback while the shared mask is not there */
static const lv_image_dsc_t *s_dsc;
static bool s_wanted;              /* the white page asked for the cover */
static lv_obj_t *s_label;          /* the drawn label: the print when there is no picture */
static bool s_pending;             /* a picture is on its way, so the label stays down */

static void cover_hide(bool fade);

/* The drawn label is ink on the Card - no fill - so the plastic's own colour is its paper, and
 * its ink is the table's, picked with the plastic it sits on (ADR 0040). Only the top band is
 * free: the window is the print's middle, 55 px down, so that is where the words go. It has no
 * alpha, so the mask cuts it like anything else in the print - the mask does that, not the
 * label. */
void cassette_cover_set_label_ink(lv_color_t ink)
{
    if (!s_label) return;
    lv_obj_set_style_text_color(s_label, ink, 0);
}

/* The label is the print only when there is no picture to print: not while one is decoding -
 * the loader clears its buffer then, and a label that appeared for those 200 ms would flash
 * before every Track - and not under a picture that has landed. */
static void label_apply(void)
{
    if (!s_label) return;
    lv_obj_set_hidden(s_label, s_pending || s_dsc != NULL);
}

void cassette_cover_set_title(const char *title)
{
    if (!s_label) return;
    lv_label_set_text(s_label, title ? title : "");
}

void cassette_cover_set_pending(void)
{
    s_pending = true;
    s_dsc = NULL;
    label_apply();
    cover_hide(false);
}

static void cassette_cover_place(void)
{
    if (!s_masked) return;

    /* Lazy by necessity, not by taste: the Card - and with it the one A8 buffer both its
     * objects are masked by - is created after this module, so the descriptor cannot be
     * applied in create(). place() is where the print is about to be looked at. */
    const lv_image_dsc_t *mask = cassette_card_mask();
    lv_obj_set_style_bitmap_mask_src(s_masked, mask, 0);
    lv_obj_set_hidden(s_fallback, mask != NULL);

    if (!s_wanted || !s_dsc) return;

    /* The width is the print's; the height is what that width makes of the source, so the
     * picture is never squeezed. The print clips whatever runs past its bottom. */
    int w = CASSETTE_CARD_PRINT_W;
    int ih = CASSETTE_CARD_PRINT_H;
    if (s_dsc->header.w > 0) {
        ih = (int)(((int64_t)w * s_dsc->header.h + s_dsc->header.w / 2) /
                   s_dsc->header.w);
    }
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_set_size(s_img, w, ih);
    lv_image_set_inner_align(s_img, LV_IMAGE_ALIGN_STRETCH);
}

/* The owner's fade: the label comes in and goes out over a full second. A Track change
 * cuts instead - the loader is about to write the buffer. */
#define COVER_FADE_MS 1000

static void cover_fade_cb(void *var, int32_t opa)
{
    lv_obj_set_style_opa_layered((lv_obj_t *)var, (lv_opa_t)opa, 0);
}

/* A hide fade is over: the label leaves the draw tree. A show in flight would have deleted
 * this animation, so s_wanted is the guard. */
static void cover_hide_done_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_masked || s_wanted) return;
    lv_obj_set_hidden(s_masked, true);
    lv_obj_set_style_opa_layered(s_masked, LV_OPA_TRANSP, 0);
}

/* The label comes in. The Cover is the cassette's bottom layer and the Card is built over
 * it, so the print is raised before the first faded frame - the Card is opaque outside the
 * window and would hide the picture otherwise. */
static void cover_show(bool fade)
{
    if (!s_masked || !s_wanted) return;

    label_apply();
    bool visible = !lv_obj_is_hidden(s_masked);
    lv_opa_t from = lv_obj_get_style_opa_layered(s_masked, 0);

    lv_obj_move_foreground(s_masked);
    cassette_cover_place();
    lv_obj_set_hidden(s_masked, false);
    lv_anim_delete(s_masked, cover_fade_cb);

    if (!fade || (visible && from == LV_OPA_COVER)) {
        lv_obj_set_style_opa_layered(s_masked, LV_OPA_COVER, 0);
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_masked);
    lv_anim_set_exec_cb(&a, cover_fade_cb);
    lv_anim_set_values(&a, from, LV_OPA_COVER);
    lv_anim_set_duration(&a, COVER_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* The label goes out. A fade keeps it in the tree until the last transparent frame; a cut
 * (a Track change) hides it at once. */
static void cover_hide(bool fade)
{
    if (!s_masked) return;
    lv_anim_delete(s_masked, cover_fade_cb);

    if (!fade || lv_obj_is_hidden(s_masked)) {
        lv_obj_set_hidden(s_masked, true);
        lv_obj_set_style_opa_layered(s_masked, LV_OPA_TRANSP, 0);
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_masked);
    lv_anim_set_exec_cb(&a, cover_fade_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa_layered(s_masked, 0), LV_OPA_TRANSP);
    lv_anim_set_duration(&a, COVER_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, cover_hide_done_cb);
    lv_anim_start(&a);
}

void cassette_cover_set_image(const lv_image_dsc_t *dsc)
{
    s_pending = false;
    s_dsc = dsc;
    if (s_img) {
        if (dsc) lv_image_set_src(s_img, dsc);
        /* Hidden rather than cleared: the loader's last descriptor stays put, and a print
         * with no picture must not keep drawing it behind the drawn label. */
        lv_obj_set_hidden(s_img, dsc == NULL);
    }
    label_apply();

    if (!dsc) {
        /* The loader's verdict for this Track: no picture. The drawn label is the print
         * then, cut in rather than faded - a Track change never fades anything. */
        if (s_wanted) cover_show(false);
        else cover_hide(false);
        return;
    }

    if (s_wanted) cover_show(true);
}

void cassette_cover_set_shown(bool shown)
{
    if (shown == s_wanted) {
        if (shown) cover_show(true);
        return;
    }
    s_wanted = shown;
    if (shown) cover_show(true);
    else cover_hide(true);
}

bool cassette_cover_is_shown(void)
{
    return s_wanted;
}

void cassette_cover_create(lv_obj_t *parent)
{
    if (!parent || s_masked) return;

    /* The Card's rect, not the print's: LVGL aligns a bitmap mask to the centre of the
     * object's area, so the two objects that share one buffer have to share a centre
     * (ADR 0039). Nothing here rounds or clips - the window is the mask's and the corners
     * are the print's. */
    s_masked = lv_obj_create(parent);
    lv_obj_remove_style_all(s_masked);
    lv_obj_set_style_bg_opa(s_masked, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_masked, 0, 0);
    lv_obj_set_style_pad_all(s_masked, 0, 0);
    lv_obj_set_scrollable(s_masked, false);
    lv_obj_set_clickable(s_masked, false);
    lv_obj_set_pos(s_masked, CASSETTE_CARD_X, CASSETTE_CARD_Y);
    lv_obj_set_size(s_masked, CASSETTE_CARD_W, CASSETTE_CARD_H);

    /* The print: frame[0]'s rect at the controls pose. Its corner is the owner's 2 px over
     * the frame's, so the picture reads as a loose label on the Card. The fade stays on the
     * container, so the picture and the mask go together and the Card does not fade. */
    s_print = lv_obj_create(s_masked);
    lv_obj_remove_style_all(s_print);
    lv_obj_set_style_bg_opa(s_print, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_print, 0, 0);
    lv_obj_set_style_pad_all(s_print, 0, 0);
    lv_obj_set_style_radius(s_print, CASSETTE_CARD_PRINT_R, 0);
    /* The picture's rounded corners are this clip, not the image's own radius: an image
     * cannot round its own draw. */
    lv_obj_set_style_clip_corner(s_print, true, 0);
    lv_obj_set_scrollable(s_print, false);
    lv_obj_set_clickable(s_print, false);
    lv_obj_set_pos(s_print, CASSETTE_CARD_PRINT_DX, CASSETTE_CARD_PRINT_DY);
    lv_obj_set_size(s_print, CASSETTE_CARD_PRINT_W, CASSETTE_CARD_PRINT_H);

    s_img = lv_image_create(s_print);
    lv_obj_set_style_bg_opa(s_img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_img, 0, 0);
    lv_obj_set_style_pad_all(s_img, 0, 0);
    lv_obj_set_clickable(s_img, false);
    /* The controls pose scales the 603 px buffer down to the print's width; antialiasing
     * costs only the frame that redraws the print, not the hubs' animation. */
    lv_image_set_antialias(s_img, true);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_set_size(s_img, 1, 1);
    lv_obj_set_hidden(s_img, true);

    /* The drawn label: the print when the Track carries no picture. It sits above the image
     * (which is empty then) and under the fallback pill. Jura 28 in the print's top band, the
     * only room the window leaves: 55 px. */
    s_label = cassette_text(s_print, &jura_28, k_col_cassette[0].label, LV_ALIGN_TOP_LEFT,
                            12, 10);
    lv_obj_set_width(s_label, CASSETTE_CARD_PRINT_W - 24);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_hidden(s_label, true);

    /* The fallback window: opaque, and hidden as soon as place() finds the shared mask. It is
     * the mask's stadium re-based on the print - the mask has it at (67,75) in the Card - so
     * the two are the same pill on the panel. */
    s_fallback = cassette_box(s_print,
                          CASSETTE_CARD_WINDOW_DX - CASSETTE_CARD_PRINT_DX,
                          CASSETTE_CARD_WINDOW_DY - CASSETTE_CARD_PRINT_DY,
                          CASSETTE_CARD_WINDOW_W, CASSETTE_CARD_WINDOW_H,
                          CASSETTE_CARD_WINDOW_R, k_col_bg);
    lv_obj_set_hidden(s_fallback, true);

    lv_obj_set_style_opa_layered(s_masked, LV_OPA_TRANSP, 0);
    lv_obj_set_hidden(s_masked, true);
}
