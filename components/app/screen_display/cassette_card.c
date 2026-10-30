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
 */
#include "cassette_card.h"

#include <math.h>
#include <string.h>

#include "cassette_style.h"
#include "cassette_widgets.h"

/* LVGL wants every A8 stride aligned to 4 bytes; the mask's size is the object's size,
 * so this is also what makes it 116,416 B rather than 115,872. */
#define MASK_STRIDE(w) (((w) + 3) & ~3)

_Static_assert(CASSETTE_CARD_WINDOW_W >= CASSETTE_CARD_WINDOW_H,
               "the Card's mask only cuts a stadium wider than it is tall");

static lv_obj_t *s_card;
static lv_obj_t *s_plate;          /* the shell's trapezoid, drawn from one transparent object */
static lv_color_t s_plate_ink;     /* its fill: the Card's ink, 30 % darker */
static lv_image_dsc_t s_mask_dsc;
static uint8_t *s_mask;

/* Zero the mask in a stadium centred on the window's box: a central band and two
 * semicircular ends whose radius is half the shorter side. The ends are sampled at the
 * pixel centres (y + 0.5), which is what keeps them on the same pixel grid as a rounded
 * LVGL box of the same radius. */
static void mask_zero_stadium(int w, int h, int stride, int hx, int hy, int hw, int hh,
                              int radius)
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
        if (n > 0) memset(s_mask + (size_t)py * stride + px, 0, (size_t)n);
    }
}

/* Build the one mask both masked objects share. Failure leaves the Card a plain rectangle
 * with no window, which is the same fallback the Cover's mask had: the tape is then under
 * an opaque card, and the page still reads. */
static void mask_build(void)
{
    int w = CASSETTE_CARD_W;
    int h = CASSETTE_CARD_H;
    int stride = MASK_STRIDE(w);

    s_mask = lv_malloc((size_t)stride * h);
    if (!s_mask) return;

    memset(s_mask, 0xFF, (size_t)stride * h);
    mask_zero_stadium(w, h, stride, CASSETTE_CARD_WINDOW_DX, CASSETTE_CARD_WINDOW_DY,
                      CASSETTE_CARD_WINDOW_W, CASSETTE_CARD_WINDOW_H,
                      CASSETTE_CARD_WINDOW_R);

    memset(&s_mask_dsc, 0, sizeof s_mask_dsc);
    s_mask_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_mask_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s_mask_dsc.header.w = w;
    s_mask_dsc.header.h = h;
    s_mask_dsc.header.stride = stride;
    s_mask_dsc.data_size = (uint32_t)(stride * h);
    s_mask_dsc.data = s_mask;
}

/* The front plate: a trapezoid, its top edge inset and its bottom edge the Card's own. LVGL has
 * no polygon, so it is two triangles drawn from one transparent object - the same trick the
 * packs use for their rings. The points are the Card's, the ink follows the plastic. */
static void plate_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    const int top_l = a.x1 + CASSETTE_CARD_PLATE_INSET;
    const int top_r = a.x2 - CASSETTE_CARD_PLATE_INSET;
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

void cassette_card_create(lv_obj_t *parent)
{
    if (!parent || s_card) return;

#if !CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The white page is the touch board's only (screen_display/CMakeLists.txt), so the other
     * board never mounts a Card and never spends the mask's 116,416 B on one. */
    return;
#endif

    mask_build();

    s_card = cassette_box(parent, CASSETTE_CARD_X, CASSETTE_CARD_Y, CASSETTE_CARD_W,
                          CASSETTE_CARD_H, CASSETTE_CARD_R, k_col_cassette[0].plastic);
    if (s_mask) lv_obj_set_style_bitmap_mask_src(s_card, &s_mask_dsc, 0);

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
    lv_obj_set_pos(s_plate, CASSETTE_CARD_PLATE_DX, CASSETTE_CARD_PLATE_DY);
    lv_obj_set_size(s_plate, CASSETTE_CARD_PLATE_W, CASSETTE_CARD_PLATE_H);
    lv_obj_add_event_cb(s_plate, plate_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    lv_obj_set_hidden(s_card, true);
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
    return s_mask ? &s_mask_dsc : NULL;
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

    cassette_rect_t r = { CASSETTE_CARD_X + CASSETTE_CARD_WINDOW_DX,
                          CASSETTE_CARD_Y + CASSETTE_CARD_WINDOW_DY,
                          CASSETTE_CARD_WINDOW_W, CASSETTE_CARD_WINDOW_H };
    return r;
}
