/*
 * cassette_cover.c - the white page's Track picture; see cassette_cover.h.
 *
 * The label is the top layer of the cassette: below it are the glow, and below that the
 * cinta. It therefore has a real, transparent window, not an opaque hole. The window is an
 * A8 bitmap mask on the label's container: the mask is 255 everywhere except the inner
 * frame's pill, which is 0, so the picture is cut there and the glow and the tape below show
 * through. The outer corners are the container's own rounded clip, and the mask is rebuilt
 * when the container's size changes (the label is only shown at the controls pose, so it is
 * built once at 402x188).
 *
 * The label is created after the tape body and the glow, so its z-order is
 * cinta < glow < label. While it is shown the two frames are raised above it
 * (`cassette_tapes_raise_frames()`), so their borders stay crisp; when it goes they go back
 * to the bottom of the body. The frame[1] border takes the same pill as the mask while the
 * label is up (`cassette_tapes_set_cover_print()`).
 *
 * The picture is top-aligned and its width is the frame's: its height follows from the
 * source's aspect and the box clips the rest. The image object is stretched to the exact
 * size that follows, so no separate pivot arithmetic is needed.
 */
#include "cassette_cover.h"

#include <math.h>
#include <string.h>

#include "lvgl.h"

#include "cassette_style.h"
#include "cassette_tapes.h"
#include "cassette_widgets.h"

static lv_obj_t *s_cover;          /* the label's clip box: the outer frame */
static lv_obj_t *s_img;            /* the Track picture, at the box's top-left */
static lv_obj_t *s_hole;           /* opaque fallback when the mask cannot be allocated */
static const lv_image_dsc_t *s_dsc;
static bool s_wanted;              /* the white page asked for the cover */

/* The A8 mask: 255 over the print, 0 in the label window. It is built at the container's
 * size and kept; the label is only shown at the controls pose. */
static lv_image_dsc_t s_mask_dsc;
static uint8_t *s_mask;
static int s_mask_w, s_mask_h;

#define MASK_SIZE(w)  (((w) + 3) & ~3)

/* Zero the mask in the pill of the label window. The window is a stadium: a central band
 * and two semicircular ends; its radius is half the shorter side. */
static void mask_fill(int w, int h, int hx, int hy, int hw, int hh)
{
    if (!s_mask || w <= 0 || h <= 0) return;
    int stride = MASK_SIZE(w);
    memset(s_mask, 0xFF, (size_t)stride * h);

    int r = LV_MIN(hw, hh) / 2;
    if (r <= 0) {
        for (int y = 0; y < hh; y++) {
            int py = hy + y;
            if (py < 0 || py >= h) continue;
            int x0 = hx > 0 ? hx : 0;
            int x1 = hx + hw;
            if (x1 > w) x1 = w;
            if (x1 > x0) memset(s_mask + (size_t)py * stride + x0, 0, x1 - x0);
        }
        return;
    }

    float rf = (float)r;
    if (hw >= hh) {
        float cy = hh / 2.0f;
        for (int y = 0; y < hh; y++) {
            int py = hy + y;
            if (py < 0 || py >= h) continue;
            float dy = (y + 0.5f) - cy;
            float dx2 = rf * rf - dy * dy;
            if (dx2 <= 0.0f) continue;
            float dx = sqrtf(dx2);
            int x0 = (int)ceilf(rf - dx - 0.5f);
            int x1 = (int)floorf((float)hw - rf + dx - 0.5f);
            if (x1 < x0) continue;
            if (x0 < 0) x0 = 0;
            if (x1 > hw - 1) x1 = hw - 1;
            int px = hx + x0;
            int n = x1 - x0 + 1;
            if (px < 0) { n += px; px = 0; }
            if (px + n > w) n = w - px;
            if (n > 0) memset(s_mask + (size_t)py * stride + px, 0, n);
        }
    } else {
        float cx = hw / 2.0f;
        for (int x = 0; x < hw; x++) {
            int px = hx + x;
            if (px < 0 || px >= w) continue;
            float dx = (x + 0.5f) - cx;
            float dy2 = rf * rf - dx * dx;
            if (dy2 <= 0.0f) continue;
            float dy = sqrtf(dy2);
            int y0 = (int)ceilf(rf - dy - 0.5f);
            int y1 = (int)floorf((float)hh - rf + dy - 0.5f);
            if (y1 < y0) continue;
            if (y0 < 0) y0 = 0;
            if (y1 > hh - 1) y1 = hh - 1;
            int py = hy + y0;
            int n = y1 - y0 + 1;
            if (py < 0) { n += py; py = 0; }
            if (py + n > h) n = h - py;
            for (int i = 0; i < n; i++) s_mask[(size_t)(py + i) * stride + px] = 0;
        }
    }
}

/* Build (or reuse) the mask for a label of w x h. Failure leaves the mask off and the
 * opaque hole as the fallback. */
static void mask_ensure(int w, int h)
{
    if (s_mask && s_mask_w == w && s_mask_h == h) return;

    if (s_mask) lv_free(s_mask);
    s_mask = NULL;
    s_mask_w = s_mask_h = 0;

    int stride = MASK_SIZE(w);
    s_mask = lv_malloc((size_t)stride * h);
    if (!s_mask) {
        lv_obj_set_style_bitmap_mask_src(s_cover, NULL, 0);
        return;
    }

    memset(&s_mask_dsc, 0, sizeof s_mask_dsc);
    s_mask_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_mask_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s_mask_dsc.header.w = w;
    s_mask_dsc.header.h = h;
    s_mask_dsc.header.stride = stride;
    s_mask_dsc.data_size = (uint32_t)(stride * h);
    s_mask_dsc.data = s_mask;

    s_mask_w = w;
    s_mask_h = h;
    lv_obj_set_style_bitmap_mask_src(s_cover, &s_mask_dsc, 0);
}

void cassette_cover_place(void)
{
    if (!s_cover || !s_wanted || !s_dsc) return;

    int x0 = 0, y0 = 0, w0 = 0, h0 = 0, r0 = 0;
    int x1 = 0, y1 = 0, w1 = 0, h1 = 0;
    cassette_tapes_frame_rect(0, &x0, &y0, &w0, &h0, &r0);
    cassette_tapes_frame_rect(1, &x1, &y1, &w1, &h1, NULL);
    if (w0 <= 0 || h0 <= 0 || w1 <= 0 || h1 <= 0) return;

    lv_obj_set_pos(s_cover, x0, y0);
    lv_obj_set_size(s_cover, w0, h0);
    /* The owner's 2 px: the print's own corners are rounded a little more than the
     * frame's border, so the body reads as a loose label inside the shell. */
    lv_obj_set_style_radius(s_cover, r0 + 2, 0);

    /* The width is the frame's; the height is what that width makes of the source, so
     * the picture is never squeezed. The frame clips whatever runs past its bottom. */
    int ih = h0;
    if (s_dsc->header.w > 0) {
        ih = (int)(((int64_t)w0 * s_dsc->header.h + s_dsc->header.w / 2) /
                   s_dsc->header.w);
    }
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_set_size(s_img, w0, ih);
    lv_image_set_inner_align(s_img, LV_IMAGE_ALIGN_STRETCH);

    mask_ensure(w0, h0);
    if (s_mask) {
        mask_fill(w0, h0, x1 - x0, y1 - y0, w1, h1);
        lv_obj_set_hidden(s_hole, true);
        lv_obj_invalidate(s_cover);
    } else {
        /* No mask: fall back to the opaque window, the look before the label moved on
         * top. The frame border still takes the pill. */
        lv_obj_set_pos(s_hole, x1 - x0, y1 - y0);
        lv_obj_set_size(s_hole, w1, h1);
        lv_obj_set_style_radius(s_hole, LV_MIN(w1, h1) / 2, 0);
        lv_obj_set_hidden(s_hole, false);
    }
}

/* The owner's fade: the label comes in and goes out over a full second.
 * A Track change and a page close cut instead - the loader is about to write the buffer,
 * and the pose is leaving the mask's size. */
#define COVER_FADE_MS 1000

static void cover_fade_cb(void *var, int32_t opa)
{
    lv_obj_set_style_opa_layered((lv_obj_t *)var, (lv_opa_t)opa, 0);
}

/* A hide fade is over: the label leaves the draw tree and the frames go back under the
 * body. A show in flight would have deleted this animation, so s_wanted is the guard. */
static void cover_hide_done_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_cover || s_wanted) return;
    lv_obj_set_hidden(s_cover, true);
    lv_obj_set_style_opa_layered(s_cover, LV_OPA_TRANSP, 0);
    cassette_tapes_set_cover_print(false);
    cassette_tapes_lower_frames();
}

/* The label comes in. The frames are lifted and the window takes the pill before the first
 * faded frame, so the print fades in under its border instead of pulling it along. */
static void cover_show(bool fade)
{
    if (!s_cover || !s_wanted || !s_dsc) return;

    bool visible = !lv_obj_is_hidden(s_cover);
    lv_opa_t from = lv_obj_get_style_opa_layered(s_cover, 0);

    cassette_tapes_set_cover_print(true);
    /* The label owns the top of the cassette's child list; the frames go over it after
     * this, so the print can never be under a tape or the glow whatever order the body
     * was built in. */
    lv_obj_move_foreground(s_cover);
    cassette_tapes_raise_frames();
    cassette_cover_place();
    lv_obj_set_hidden(s_cover, false);
    lv_anim_delete(s_cover, cover_fade_cb);

    if (!fade || (visible && from == LV_OPA_COVER)) {
        lv_obj_set_style_opa_layered(s_cover, LV_OPA_COVER, 0);
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cover);
    lv_anim_set_exec_cb(&a, cover_fade_cb);
    lv_anim_set_values(&a, from, LV_OPA_COVER);
    lv_anim_set_duration(&a, COVER_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* The label goes out. A fade keeps it in the tree until the last transparent frame, then
 * lowers the frames; a cut (a page flip or Track change) does both at once. */
static void cover_hide(bool fade)
{
    if (!s_cover) return;
    lv_anim_delete(s_cover, cover_fade_cb);

    if (!fade || lv_obj_is_hidden(s_cover)) {
        lv_obj_set_hidden(s_cover, true);
        lv_obj_set_style_opa_layered(s_cover, LV_OPA_TRANSP, 0);
        cassette_tapes_set_cover_print(false);
        cassette_tapes_lower_frames();
        return;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cover);
    lv_anim_set_exec_cb(&a, cover_fade_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa_layered(s_cover, 0), LV_OPA_TRANSP);
    lv_anim_set_duration(&a, COVER_FADE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, cover_hide_done_cb);
    lv_anim_start(&a);
}

void cassette_cover_set_image(const lv_image_dsc_t *dsc)
{
    s_dsc = dsc;
    if (s_img && dsc) lv_image_set_src(s_img, dsc);

    if (!dsc) {
        /* The loader is about to reuse the buffer: a fade here would draw it while it is
         * being written. The wish stays, so the new picture fades in when it lands. */
        cover_hide(false);
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

void cassette_cover_hide_now(void)
{
    s_wanted = false;
    cover_hide(false);
}

bool cassette_cover_is_shown(void)
{
    return s_wanted;
}

void cassette_cover_create(lv_obj_t *parent)
{
    if (!parent || s_cover) return;

    s_cover = lv_obj_create(parent);
    lv_obj_remove_style_all(s_cover);
    lv_obj_set_style_bg_opa(s_cover, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cover, 0, 0);
    lv_obj_set_style_pad_all(s_cover, 0, 0);
    lv_obj_set_style_radius(s_cover, 0, 0);
    /* The picture's rounded corners are this clip, not the image's own radius: an image
     * cannot round its own draw, and the clip also carries the frame's radius through
     * the pose changes. */
    lv_obj_set_style_clip_corner(s_cover, true, 0);
    lv_obj_set_scrollable(s_cover, false);
    lv_obj_set_clickable(s_cover, false);
    lv_obj_set_pos(s_cover, 0, 0);
    lv_obj_set_size(s_cover, 1, 1);

    s_img = lv_image_create(s_cover);
    lv_obj_set_style_bg_opa(s_img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_img, 0, 0);
    lv_obj_set_style_pad_all(s_img, 0, 0);
    lv_obj_set_clickable(s_img, false);
    /* The controls pose scales the 603 px buffer down to frame 0's width; antialiasing
     * costs only the frame that redraws the print, not the hubs' animation. */
    lv_image_set_antialias(s_img, true);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_set_size(s_img, 1, 1);

    /* The fallback window: opaque, and hidden while the A8 mask can do the hole. */
    s_hole = cassette_box(s_cover, 0, 0, 1, 1, 0, k_col_bg);
    lv_obj_set_hidden(s_hole, true);

    lv_obj_set_style_opa_layered(s_cover, LV_OPA_TRANSP, 0);
    lv_obj_set_hidden(s_cover, true);
}
