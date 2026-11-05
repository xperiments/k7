/*
 * cassette_card.c - the white page's cassette; see cassette_card.h.
 *
 * A tinted rounded rectangle with a pill window, above the tape and under the print. Its
 * one real cost is the A8 mask it shares with the print's container: a bitmap of the
 * Card's own size, 255 everywhere and 0 in the stadium. LVGL aligns such a mask to the
 * centre of its object's area and clears everything outside the mask's rectangle
 * (lv_draw_sw_img.c), so the mask must be exactly the object's box - and because both
 * masked objects are 426x272 at (136,82), they share a centre and therefore one buffer.
 *
 * The stadium is zeroed with the same float, pixel-centre arithmetic the Cover's own mask
 * used, so its caps agree with the frame's pill to the pixel. The window is 291x87 - wider
 * than it is tall - so only the horizontal case exists, and the static assert below keeps
 * it that way.
 *
 * **The mask is off on this branch.** The window is painted above the print by the cover's
 * porthole (cassette_cover.c), so the Card is an ordinary opaque rounded rect with no hole
 * to cut and the 116,416 B buffer is never allocated. CARD_MASK=1 brings the whole mask back
 * for the A/B this branch exists for; nothing else about the Card changes between the two.
 */
#define CARD_MASK 1   /* 1 = the A8 mask and the cut window; 0 = the porthole above the print */

#include "cassette_card.h"

#include "screen_caps.h"

#include "cassette_style.h"
#include "cassette_widgets.h"

#if CARD_MASK
#include <math.h>
#include <string.h>

/* LVGL wants every A8 stride aligned to 4 bytes; the mask's size is the object's size,
 * so this is also what makes it 116,416 B rather than 115,872. */
#define MASK_STRIDE(w) (((w) + 3) & ~3)

_Static_assert(CASSETTE_CARD_WINDOW_W >= CASSETTE_CARD_WINDOW_H,
               "the Card's mask only cuts a stadium wider than it is tall");
#endif

static lv_obj_t *s_card;
static lv_obj_t *s_plate;          /* the shell's trapezoid, drawn from one transparent object */
static lv_color_t s_plate_ink;     /* its fill: the Card's ink, 30 % darker */
static int s_layout;               /* 0 = the Controls pose, 1 = the entry grid */

/* The two layouts. The first is the screen's own numbers, untouched since it shipped. The
 * second is the design box those numbers were derived from: the Controls pose is this Card at
 * 134/256, so the entry grid is the same Card at 256/256 - 814x520 at (-8,-23), which bleeds
 * off every edge of the 800x480 panel exactly as it was drawn to. Its window is 556x166, the
 * two hubs' band, and its print is 768x359. */
static const cassette_card_geometry_t k_card_layout[2] = {
    { 136, 82, 426, 272, 12,
      67, 75, 291, 87, 43,
      12, 20, 402, 188, 10,
      88, 7,
      54, 216, 318, 56, 20 },
    { -8, -23, 814, 520, 23,
      128, 143, 556, 166, 83,
      39, 38, 736, 359, 19,   /* the print: 16 px in from each side of the Card */
      167, 13,
      103, 413, 607, 107, 38 },
};

static const cassette_card_geometry_t *layout(void)
{
    return &k_card_layout[s_layout];
}

#if CARD_MASK
static lv_image_dsc_t s_mask_dsc[2];
static uint8_t *s_mask[2];
#endif

#if CARD_MASK
/* Zero the mask in a stadium centred on the window's box: a central band and two
 * semicircular ends whose radius is half the shorter side. The ends are sampled at the
 * pixel centres (y + 0.5), which is what keeps them on the same pixel grid as a rounded
 * LVGL box of the same radius. */
static void mask_zero_stadium(uint8_t *buf, int w, int h, int stride,
                              int hx, int hy, int hw, int hh, int radius)
{
    int r = radius;
    if (r <= 0) return;

    float rf = (float)r;
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
        if (px < 0) {
            n += px;
            px = 0;
        }
        if (px + n > w) n = w - px;
        if (n > 0) memset(buf + (size_t)py * stride + px, 0, (size_t)n);
    }
}

/* Build the one mask both masked objects share. Failure leaves the Card a plain rectangle
 * with no window, which is the same fallback the Cover's mask had: the tape is then under
 * an opaque card, and the page still reads. */
static void mask_build(int i)
{
    const cassette_card_geometry_t *g = &k_card_layout[i];
    const int w = g->w, h = g->h;
    const int stride = MASK_STRIDE(w);

    s_mask[i] = lv_malloc((size_t)stride * h);
    if (!s_mask[i]) return;

    memset(s_mask[i], 0xFF, (size_t)stride * h);
    mask_zero_stadium(s_mask[i], w, h, stride, g->wx, g->wy, g->ww, g->wh, g->wr);

    memset(&s_mask_dsc[i], 0, sizeof s_mask_dsc[i]);
    s_mask_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
    s_mask_dsc[i].header.cf = LV_COLOR_FORMAT_A8;
    s_mask_dsc[i].header.w = w;
    s_mask_dsc[i].header.h = h;
    s_mask_dsc[i].header.stride = stride;
    s_mask_dsc[i].data_size = (uint32_t)(stride * h);
    s_mask_dsc[i].data = s_mask[i];
}
#endif /* CARD_MASK */

/* The front plate: a trapezoid, its top edge inset and its bottom edge the Card's own. LVGL has
 * no polygon, so it is two triangles drawn from one transparent object - the same trick the
 * packs use for their rings. The points are the Card's, the ink follows the plastic. */
static void plate_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    const int inset = cassette_card_geometry()->pl_inset;
    const int top_l = a.x1 + inset;
    const int top_r = a.x2 - inset;
    const int bot_l = a.x1;
    const int bot_r = a.x2;

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = s_plate_ink;
    dsc.opa = LV_OPA_COVER;

    dsc.p[0].x = top_l; dsc.p[0].y = a.y1;
    dsc.p[1].x = top_r; dsc.p[1].y = a.y1;
    dsc.p[2].x = bot_r; dsc.p[2].y = a.y2;
    lv_draw_triangle(layer, &dsc);

    dsc.p[0].x = top_l; dsc.p[0].y = a.y1;
    dsc.p[1].x = bot_r; dsc.p[1].y = a.y2;
    dsc.p[2].x = bot_l; dsc.p[2].y = a.y2;
    lv_draw_triangle(layer, &dsc);
}

/* Put the Card and its plate where layout `i` says they go, and give the Card that layout's
 * mask. Called once at create() and again on the full-screen switch, in one frame. */
static void layout_apply(int i)
{
    const cassette_card_geometry_t *g = &k_card_layout[i];
    s_layout = i;

    if (s_card) {
        lv_obj_set_pos(s_card, g->x, g->y);
        lv_obj_set_size(s_card, g->w, g->h);
        lv_obj_set_style_radius(s_card, g->r, 0);
#if CARD_MASK
        lv_obj_set_style_bitmap_mask_src(s_card, s_mask[i] ? &s_mask_dsc[i] : NULL, 0);
#endif
    }

    if (s_plate) {
        lv_obj_set_pos(s_plate, g->plx, g->ply);
        lv_obj_set_size(s_plate, g->plw, g->plh);
        lv_obj_invalidate(s_plate);
    }
}

void cassette_card_create(lv_obj_t *parent)
{
    if (!parent || s_card) return;

    /* The Card is the white page's body, and the white page is a capability: a board with no
     * touch and no card-side loader never reaches it, so it never spends a mask on one.
     * Asked, not compiled out (screen_caps.h). */
    if (!screen_caps()->cover) return;

#if CARD_MASK
    /* Both layouts' masks, here: 116,416 B and 424,320 B. The full one is built up front
     * rather than on the gesture, which would otherwise carry a 424 KB allocation. */
    mask_build(0);
    mask_build(1);
#endif

    const cassette_card_geometry_t *g = &k_card_layout[0];
    s_card = cassette_box(parent, g->x, g->y, g->w, g->h, g->r, k_col_cassette[0].plastic);

    /* The plate: a child of the Card, below the print's bottom edge and inside the mask's
     * window, so it is neither clipped nor covered. */
    s_plate_ink = lv_color_darken(k_col_cassette[0].plastic, CASSETTE_CARD_PLATE_DARKEN);
    s_plate = lv_obj_create(s_card);
    lv_obj_remove_style_all(s_plate);
    lv_obj_set_style_bg_opa(s_plate, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_plate, 0, 0);
    lv_obj_set_style_pad_all(s_plate, 0, 0);
    lv_obj_set_scrollable(s_plate, false);
    lv_obj_set_clickable(s_plate, false);
    lv_obj_add_event_cb(s_plate, plate_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    layout_apply(0);
    lv_obj_set_hidden(s_card, true);
}

const cassette_card_geometry_t *cassette_card_geometry(void)
{
    return layout();
}

void cassette_card_set_full(bool full)
{
    if (!s_card || (full ? 1 : 0) == s_layout) return;
    layout_apply(full ? 1 : 0);
}

bool cassette_card_is_full(void)
{
    return s_layout == 1;
}

void cassette_card_set_shown(bool shown)
{
    if (!s_card) return;
    lv_obj_set_hidden(s_card, !shown);
}

void cassette_card_set_ink(lv_color_t ink)
{
    if (!s_card) return;
    /* The plate is cut into the same plastic, so it moves with it: 30 % towards black. */
    s_plate_ink = lv_color_darken(ink, CASSETTE_CARD_PLATE_DARKEN);
    if (s_plate) lv_obj_invalidate(s_plate);
    /* A cut, not a colour animation: a colour animation would repaint the panel on every
     * step, and the Card is 426x272 of it. */
    lv_obj_set_style_bg_color(s_card, ink, 0);
}

const lv_image_dsc_t *cassette_card_mask(void)
{
#if CARD_MASK
    return s_mask[s_layout] ? &s_mask_dsc[s_layout] : NULL;
#else
    return NULL;   /* no hole to cut: the porthole paints the window above the print */
#endif
}

cassette_rect_t cassette_card_window(void)
{
    /* No Card, no window. The headless board mounts no Card (create() returns before the mask's
     * 116 KB is spent), and a window with nothing behind it is not a place the tape stops: the
     * view clamps the packs' boxes to this rect while the white page is up, so returning the
     * numbers anyway clipped the reels to a hole that did not exist. A live leak of the board
     * seam, found by a review rather than by a person looking at that build. */
    if (!s_card) {
        cassette_rect_t none = { 0, 0, 0, 0 };
        return none;
    }

    const cassette_card_geometry_t *g = layout();
    cassette_rect_t r = { g->x + g->wx, g->y + g->wy, g->ww, g->wh };
    return r;
}
