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
 *
 * A Track without a picture prints the row's label colour as a plain fill. There are no
 * words on it: the label's ink column is carried by the palette but nothing prints it.
 */
#include "cassette_cover.h"

#include "screen_caps.h"

#include <math.h>
#include <string.h>

#include "lvgl.h"

#include "cassette_card.h"
#include "cassette_style.h"
#include "cassette_tapes.h"
#include "cassette_widgets.h"

static lv_obj_t *s_masked;          /* the Card's rect: the object the shared mask is sized to */
static lv_obj_t *s_print;          /* the print proper: 402x188 at +12,+20 */
static lv_obj_t *s_img;            /* the Track picture, at the print's top-left */
static lv_obj_t *s_fallback;       /* opaque fallback while the shared mask is not there */
static lv_obj_t *s_window;         /* the porthole: the window's capsule, above the print */
static lv_obj_t *s_slice;          /* its child: the tape slice, cut to the capsule's shape */
static void (*s_slice_draw)(lv_layer_t *layer);   /* the tapes' drawer, handed in by the view */
static lv_image_dsc_t s_window_mask_dsc;          /* the capsule as A8, the slice's box */
static uint8_t *s_window_mask;
static lv_obj_t *s_rim;            /* the window's pill, over the print's cut edge */
static const lv_image_dsc_t *s_dsc;
static bool s_wanted;              /* the white page asked for the cover */

static void cover_hide(void);

/* The slice, in the porthole's child: the view gives the tapes' drawer, so this module owns
 * the object and the tapes own the paint. */
static void slice_draw_cb(lv_event_t *e)
{
    if (s_slice_draw) s_slice_draw(lv_event_get_layer(e));
}

/* The label is the print's paper: the row's `label` fill, cut by the same mask as a picture.
 * The row's `ink` is the colour a word would use; the label is wordless, so it is not read
 * here. The fill is opaque and the mask does the shaping, like the picture. */
void cassette_cover_set_inks(lv_color_t plastic, lv_color_t ink, lv_color_t label)
{
    (void)ink; /* the palette carries it; the label has no words to print it with */
    if (s_print) {
        lv_obj_set_style_bg_color(s_print, label, 0);
        lv_obj_set_style_bg_opa(s_print, LV_OPA_COVER, 0);
    }
    if (s_fallback) lv_obj_set_style_bg_color(s_fallback, label, 0);
    /* The window's rim is the Card's own plastic: the cassette is moulded around its window,
     * so the frame and the body are one material and only the print is another. */
    if (s_rim) lv_obj_set_style_border_color(s_rim, plastic, 0);
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

/* The print is cut in and out, never faded. A page flip, a Track change and a Track with no
 * picture all land in one frame: the label fill is what shows when there is nothing to blit,
 * so a fade would only draw attention to a hole that is already filled. */
static void cover_show(void)
{
    if (!s_masked || !s_wanted) return;
    cassette_cover_place();
    lv_obj_set_hidden(s_masked, false);
}

static void cover_hide(void)
{
    if (!s_masked) return;
    lv_obj_set_hidden(s_masked, true);
}

void cassette_cover_set_image(const lv_image_dsc_t *dsc)
{
    s_dsc = dsc;
    if (s_img) {
        if (dsc) lv_image_set_src(s_img, dsc);
        /* Hidden rather than cleared: the loader's last descriptor stays put, and a print
         * with no picture must not keep drawing it. */
        lv_obj_set_hidden(s_img, dsc == NULL);
    }
    if (!dsc) {
        /* The loader's verdict for this Track: no picture. The label fill is the print. */
        if (s_wanted) cover_show();
        else cover_hide();
        return;
    }

    if (s_wanted) cover_show();
}

void cassette_cover_set_shown(bool shown)
{
    /* The window's rim is the Card's, not the print's: it is up whenever Real is, picture or
     * no picture, and it goes with the page in one frame. The print is a cut either way. The
     * porthole follows the same bit: the tape shows through the window whether or not a
     * picture landed. */
    if (s_rim) lv_obj_set_hidden(s_rim, !shown);
    if (s_window) {
        lv_obj_set_hidden(s_window, !shown);
        /* The porthole is opaque and covers the body's reels outright; hide them so their
         * 25 Hz ticks cannot re-render the Card and the porthole behind it. */
        cassette_tapes_set_body_hidden(shown);
    }

    s_wanted = shown;
    if (shown) cover_show();
    else cover_hide();
}

bool cassette_cover_has_picture(void)
{
    return s_dsc != NULL;
}

bool cassette_cover_is_shown(void)
{
    return s_wanted;
}

/* The window's capsule as an A8 mask: 255 inside the stadium, 0 outside, the ends sampled at
 * the pixel centres so they land on the same grid as the rim's pill - the same arithmetic the
 * Card's mask used. It is 291x87 (25,317 B) because the slice's box is the window, and it is
 * built once.
 *
 * The slice is cut by this rather than by clip_corner on the capsule, and the difference is
 * not taste: the window's radius is half its height, so clip_corner has no middle band to draw
 * directly - it renders the whole child twice, into two 43-row ARGB layers of 50,052 B each,
 * on every repaint. That is also why the first build flashed and starved the stream. A bitmap
 * mask is the path the Card and the print have used since ADR 0039, and per repaint LVGL cuts
 * it into 24 KB stripes that are only as large as the dirty area. */
static void mask_build(int w, int h, int radius)
{
    const int stride = (w + 3) & ~3;   /* LVGL wants every A8 stride 4-byte aligned */
    s_window_mask = lv_malloc((size_t)stride * h);
    if (!s_window_mask) return;

    memset(s_window_mask, 0, (size_t)stride * h);

    const float rf = (float)radius;
    const float cy = h / 2.0f;
    for (int y = 0; y < h; y++) {
        float dy = (y + 0.5f) - cy;
        float dx2 = rf * rf - dy * dy;
        if (dx2 <= 0.0f) continue;

        float dx = sqrtf(dx2);
        int x0 = (int)ceilf(rf - dx - 0.5f);
        int x1 = (int)floorf((float)w - rf + dx - 0.5f);
        if (x1 < x0) continue;
        if (x0 < 0) x0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        memset(s_window_mask + (size_t)y * stride + x0, 0xFF, (size_t)(x1 - x0 + 1));
    }

    memset(&s_window_mask_dsc, 0, sizeof s_window_mask_dsc);
    s_window_mask_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_window_mask_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s_window_mask_dsc.header.w = w;
    s_window_mask_dsc.header.h = h;
    s_window_mask_dsc.header.stride = stride;
    s_window_mask_dsc.data_size = (uint32_t)(stride * h);
    s_window_mask_dsc.data = s_window_mask;
}

/* The porthole and its one child. The capsule is opaque and is the window's ground; the child
 * is transparent, carries the capsule's mask, and does all the painting through the tapes'
 * drawer. Sized from the Card's window, which is the Card's own rect - there is no Card and no
 * window on a board without the cover capability, and create() never reaches this then. */
static void window_create(lv_obj_t *parent)
{
    /* The Card cuts the window out with its own A8 mask, and the print is cut by the same
     * buffer: the tape under them is the picture, seen through the hole, and there is nothing
     * to paint here. The porthole exists only when that mask is off (CARD_MASK 0), which is
     * the experiment this branch was opened for. */
    if (cassette_card_mask() != NULL) return;

    cassette_rect_t w = cassette_card_window();
    if (w.w <= 0 || w.h <= 0) return;

    s_window = cassette_box(parent, w.x, w.y, w.w, w.h, CASSETTE_CARD_WINDOW_R, k_col_bg);
    lv_obj_set_style_pad_all(s_window, 0, 0);
    lv_obj_set_hidden(s_window, true);

    s_slice = lv_obj_create(s_window);
    lv_obj_remove_style_all(s_slice);
    lv_obj_set_style_bg_opa(s_slice, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_slice, 0, 0);
    lv_obj_set_style_pad_all(s_slice, 0, 0);
    lv_obj_set_pos(s_slice, 0, 0);
    lv_obj_set_size(s_slice, w.w, w.h);
    lv_obj_set_scrollable(s_slice, false);
    lv_obj_set_clickable(s_slice, false);
    lv_obj_add_event_cb(s_slice, slice_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* The mask is the child's box, which is why the child is the window's size and not the
     * Card's: LVGL aligns a bitmap mask to the centre of its object and clears everything
     * outside the mask's rectangle. */
    mask_build(w.w, w.h, CASSETTE_CARD_WINDOW_R);
    if (s_window_mask) lv_obj_set_style_bitmap_mask_src(s_slice, &s_window_mask_dsc, 0);
}

void cassette_cover_set_window_draw(void (*draw)(lv_layer_t *layer))
{
    s_slice_draw = draw;
}

void cassette_cover_window_invalidate(void)
{
    /* Hidden, LVGL drops the invalidation; shown, the capsule and its child repaint. */
    if (s_window) lv_obj_invalidate(s_window);
}

void cassette_cover_window_invalidate_area(const lv_area_t *area)
{
    /* The child, not the capsule: the same area, but LVGL clips it to the child and the
     * capsule's rounded-corner pass is then only as wide as the dirty strip. */
    if (s_slice && area) lv_obj_invalidate_area(s_slice, area);
}

void cassette_cover_create(lv_obj_t *parent)
{
    if (!parent || s_masked) return;

    /* The print is the Cover capability's: a board with no card-side loader and no white page
     * to print on builds none of it. Asked here rather than gated in the composition, because
     * "show the cover" is a request the part is allowed to answer with nothing - and a print
     * with no Card behind it is not a picture of anything. */
    if (!screen_caps()->cover) return;

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
    lv_obj_set_style_bg_color(s_print, k_col_cassette[0].label, 0);
    lv_obj_set_style_bg_opa(s_print, LV_OPA_COVER, 0);
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

    /* The fallback window: opaque, and hidden as soon as place() finds the shared mask. It is
     * the mask's stadium re-based on the print - the mask has it at (67,75) in the Card - so
     * the two are the same pill on the panel. */
    s_fallback = cassette_box(s_print,
                          CASSETTE_CARD_WINDOW_DX - CASSETTE_CARD_PRINT_DX,
                          CASSETTE_CARD_WINDOW_DY - CASSETTE_CARD_PRINT_DY,
                          CASSETTE_CARD_WINDOW_W, CASSETTE_CARD_WINDOW_H,
                          CASSETTE_CARD_WINDOW_R, k_col_cassette[0].label);
    lv_obj_set_hidden(s_fallback, true);

    /* The porthole: an opaque capsule at the window, carrying the tape slice in a child, so
     * the Card no longer has to be masked to make a hole. clip_corner rounds a parent's
     * *children*, not its own paint (lv_refr.c:118-129, :159-257), which is why the slice is a
     * child; the capsule itself is k_col_bg - the back cover the tape stands on - so nothing
     * behind it (the print, the fallback pill) can show through. The rim, built next, covers
     * its edge, which is why the porthole is created before it. */
    window_create(parent);

    /* The window's rim, last: 3 px of the Card's plastic on a 291x88 box with the window's
     * radius, a child of the cassette container rather than of the print, because the print is
     * masked to the window and the rim's bottom row lies on the Card just outside it. Drawn
     * after the print, it also covers the mask's last row of antialiasing, which is what makes
     * the cut edge read as a moulded rim instead of a soft one. */
    s_rim = cassette_border_box(parent,
                                CASSETTE_CARD_X + CASSETTE_CARD_WINDOW_DX,
                                CASSETTE_CARD_Y + CASSETTE_CARD_WINDOW_DY,
                                CASSETTE_CARD_WINDOW_W, CASSETTE_CARD_WINDOW_RIM_H,
                                CASSETTE_CARD_WINDOW_R, k_col_bg, LV_OPA_TRANSP,
                                k_col_cassette[0].plastic, CASSETTE_CARD_WINDOW_RIM_W);
    lv_obj_set_hidden(s_rim, true);

    lv_obj_set_hidden(s_masked, true);
}
