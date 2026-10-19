/*
 * The cassette screen.
 *
 * The design is drawn here, not shipped as one bitmap: a light grey shell, two black
 * tape packs of different sizes, and a cream label with a red "B" and a rule. The only
 * bitmap left is the hub: assets/hub/ holds the opaque 168x168 keyframes of the whole
 * hub, one revolution. tools/convert_cassette_assets.py converts them to
 * RGB565 exactly as exported - no alpha, no recolour, no compositing - and adds 96x96
 * crop descriptors that point into the same frame data.
 *
 * The firmware draws only the 96x96 crop of each frame; the frame around it is the
 * UI's, not part of the exported blob. That is the MASK_AT_168 = 96 arrangement: no
 * full 168 frame is stored and no 168 frame is redrawn. There is no lv_image_set_rotation():
 * LVGL's software transform is per pixel and was measured at over 100 ms per frame on
 * this part; the frame sequence is the rotation.
 *
 * This file is the adapter: it owns positions, fonts, diameters and LVGL objects. What
 * the screen should show - reel position, pack fill, progress, the cleaned label - is
 * components/portable/cassette, which the host tier can test. Nothing here re-derives any of it.
 */
#include <math.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_view.h"
#include "hub_frames.h"

static const char *TAG = "screen";

/* The 800x480 panel: the hub's static ring is 168 px and the rotating mask is the
 * converter's HUB_FRAME_W (96 px). The packs stay the reference's 245/310 so the
 * black tape rings the hub at both ends of a side. */
static const struct { int x, y; } k_hub[2] = { { 216, 234 }, { 600-16, 234 } };
#define HUB_RING_D 168
#define PACK_MIN_D 168
#define PACK_MAX_D 400

/* The tape strands leave the bottom edge where it is tangent to a full pack:
 * 216 - 400/2 = 16 on the left, 584 + 400/2 = 784 on the right. */
#define STRAND_X_L 16
#define STRAND_X_R 784
#define STRAND_Y 480
#define STRAND_W 4

/* The label: cream bar, red side square, the rule the title sits above. */
#define LABEL_X 32
#define LABEL_Y 16
#define LABEL_W 735
#define LABEL_H 75
#define LABEL_R 12

#define B_X 44
#define B_Y 27
#define B_D 53
#define B_R 9

#define TITLE_X 127
#define TITLE_Y 40
#define TITLE_W 439

/* The band below the tape. */
#define BAND_Y 424
#define BAND_X 91
#define BAND_W 618

/* Colours, read off the reference. */
#define COL_BG          0x444444
#define COL_TAPE        0x993333
#define COL_TAPE_DARK   0x881111
#define COL_LABEL       0xf4eabb
#define COL_LABEL_EDGE  0xe6d8a8
#define COL_RED         0xf03d3a
#define COL_INK         0x230e11
#define COL_HUB_RING    0xe6e6e6
#define COL_BAR_BG      0xb9c0bb
#define COL_BAR_FG      0xf03d3a

/* The tape's solid edge enters at the tangent and runs TAPE_ARC_DEG to the top;
 * the dashed VU continues from there. LVGL angles: 0 = 3 o'clock, 90 = 6, and
 * positive is clockwise. The left reel runs clockwise; the right mirrors it. */
#define TAPE_ARC_DEG 90
#define VU_ARC_DEG CONFIG_APP_UI_VU_ARC_DEG
#define VU_DASH_DEG 4
#define VU_GAP_DEG 4
#define VU_DASH_COUNT ((VU_ARC_DEG + VU_DASH_DEG + VU_GAP_DEG - 1) / (VU_DASH_DEG + VU_GAP_DEG))
#define TAPE_WIDTH 4
#define VU_WIDTH 4
#define VU_PEAK_LIT 0xffd08a

static cassette_t *s_model;
static lv_obj_t *s_hub[2], *s_pack[2], *s_strand[2];
static lv_obj_t *s_vu[2];         /* dashed VU overlay, one per pack */
static lv_point_precise_t s_strand_pts[2][2];
static lv_obj_t *s_title, *s_bar;
static int      s_pack_d[2];      /* px, so a redraw happens only when it changes */
static uint8_t  s_shown_hub[2];   /* the art frame currently on screen */
static int      s_tangent_deg[2]; /* where the strand leaves the pack, LVGL degrees */
static int      s_vu_lit[2];      /* dashes lit by the peak-hold, 0..VU_DASH_COUNT */

/* ------------------------------------------------------------------ helpers */

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_width(o, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, lv_color_hex(COL_TAPE_DARK), LV_PART_MAIN);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *disc(lv_obj_t *parent, int cx, int cy, int d, uint32_t bg)
{
    return box(parent, cx - d / 2, cy - d / 2, d, d, LV_RADIUS_CIRCLE, bg);
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                      lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

/* The tape strand: a 3 px line from the bottom edge to the point where it leaves the
 * pack tangentially, so the strand never cuts across the pack it comes from. The foot
 * is fixed; the tangent point is a function of the radius, which is why the line is
 * vertical when that pack is full and leans as it empties. Of the two tangents from
 * the foot, take the one on the outside of the pack: add the angle on the left,
 * subtract it on the right. */
static void strand_set(int i, int d)
{
    int cx = k_hub[i].x, cy = k_hub[i].y, r = d / 2;
    int foot_x = i ? STRAND_X_R : STRAND_X_L;
    double dx = foot_x - cx, dy = STRAND_Y - cy;
    double dist = sqrt(dx * dx + dy * dy);
    double alpha = acos(r / dist);
    double theta = atan2(dy, dx) + (i ? -alpha : alpha);
    int tip_x = cx + (int)lround(r * cos(theta));
    int tip_y = cy + (int)lround(r * sin(theta));

    int deg = (int)lround(theta * 180.0 / 3.14159265358979323846);
    deg %= 360;
    if (deg < 0) deg += 360;
    s_tangent_deg[i] = deg;

    /* The line's own box is the bounding rect of its ends, so a redraw costs the
     * strand and not the whole screen. */
    int x1 = LV_MIN(foot_x, tip_x);
    int w = LV_MAX(LV_ABS(tip_x - foot_x) + 1, STRAND_W);
    int h = STRAND_Y - tip_y + 1;

    s_strand_pts[i][0].x = foot_x - x1;
    s_strand_pts[i][0].y = STRAND_Y - tip_y;
    s_strand_pts[i][1].x = tip_x - x1 + (i == 0 ? 3 : -3);
    s_strand_pts[i][1].y = 0;

    lv_obj_set_pos(s_strand[i], x1, tip_y);
    lv_obj_set_size(s_strand[i], w, h);
    lv_line_set_points(s_strand[i], s_strand_pts[i], 2);
}

static bool angle_in_arc(int a, int start, int dir, int length)
{
    int d = dir > 0 ? a - start : start - a;
    d = ((d % 360) + 360) % 360;
    return d <= length;
}

/* The overlay is only as large as the VU arc's bounding box, so its 25 Hz
 * invalidation redraws the dashes and the pack under them, not the whole pack. */
static void vu_geom_set(int i)
{
    if (!s_vu[i]) return;
    int r = s_pack_d[i] / 2;
    int cx = k_hub[i].x, cy = k_hub[i].y;
    int dir = i ? -1 : 1;
    int start = s_tangent_deg[i] + dir * TAPE_ARC_DEG;
    int angles[] = { start, start + dir * VU_ARC_DEG, 0, 90, 180, 270 };
    int x1 = INT32_MAX, y1 = INT32_MAX, x2 = INT32_MIN, y2 = INT32_MIN;
    for (int k = 0; k < (int)(sizeof angles / sizeof angles[0]); k++) {
        if (!angle_in_arc(angles[k], start, dir, VU_ARC_DEG)) continue;
        int px = cx + (int)lround((r - 2) * cos(angles[k] * 3.14159265358979323846 / 180.0));
        int py = cy + (int)lround((r - 2) * sin(angles[k] * 3.14159265358979323846 / 180.0));
        if (px < x1) x1 = px;
        if (px > x2) x2 = px;
        if (py < y1) y1 = py;
        if (py > y2) y2 = py;
    }
    if (x1 > x2) { x1 = cx - r; x2 = cx + r; y1 = cy - r; y2 = cy + r; }
    int pad = 6;
    lv_obj_set_pos(s_vu[i], x1 - pad, y1 - pad);
    lv_obj_set_size(s_vu[i], x2 - x1 + 2 * pad, y2 - y1 + 2 * pad);
}

static void pack_set(int i, int d)
{
    if (d == s_pack_d[i]) return;
    s_pack_d[i] = d;
    lv_obj_set_size(s_pack[i], d, d);
    lv_obj_set_pos(s_pack[i], k_hub[i].x - d / 2, k_hub[i].y - d / 2);
    strand_set(i, d);
    vu_geom_set(i);
}

/* The pack area, and therefore the tape length on it, is proportional to the fill:
 * the diameter grows as the square root, the same relation the model uses for the
 * radius. An even diameter keeps the disc symmetric on the pixel grid. */
static int pack_diameter(uint16_t fill)
{
    double min = PACK_MIN_D, max = PACK_MAX_D;
    double d = sqrt(min * min + (max * max - min * min) * (double)fill / 1000.0);
    return ((int)lround(d)) & ~1;
}

/* LVGL draws an arc clockwise from start to end; a counter-clockwise segment is
 * the same points with the ends swapped. */
static void draw_arc(lv_layer_t *layer, int cx, int cy, int r,
                     int start, int length, int dir, lv_color_t color,
                     lv_opa_t opa, int width, int rounded)
{
    if (length <= 0) return;
    int s = dir > 0 ? start : start - length;
    int e = dir > 0 ? start + length : start;
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa = opa;
    dsc.width = width;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = r;
    dsc.start_angle = s;
    dsc.end_angle = e;
    dsc.rounded = rounded;
    lv_draw_arc(layer, &dsc);
}

/* The solid tape edge, drawn on the pack so it redraws only when the pack resizes. */
static void pack_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    int r = s_pack_d[i] / 2;
    int dir = i ? -1 : 1;
    draw_arc(layer, k_hub[i].x, k_hub[i].y, r - 2,
             s_tangent_deg[i], TAPE_ARC_DEG, dir,
             lv_color_hex(COL_TAPE_DARK), LV_OPA_COVER, TAPE_WIDTH, 1);
}

/* The dashed VU, in its own overlay so the 25 Hz level changes do not repaint
 * the pack's fill or its tape line. */
static void vu_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    int r = s_pack_d[i] / 2;
    int dir = i ? -1 : 1;
    int start = s_tangent_deg[i] + dir * TAPE_ARC_DEG;
    int lit = s_vu_lit[i];

    for (int k = 0; k < VU_DASH_COUNT; k++) {
        int off = k * (VU_DASH_DEG + VU_GAP_DEG);
        int len = VU_DASH_DEG;
        if (off + len > VU_ARC_DEG) len = VU_ARC_DEG - off;
        if (len <= 0) break;
        int s = start + dir * off;
        bool peak = lit > 0 && k == lit - 1;
        lv_color_t color = lv_color_hex(peak ? VU_PEAK_LIT : COL_TAPE_DARK);
        lv_opa_t opa = k < lit ? LV_OPA_COVER : LV_OPA_30;
        draw_arc(layer, k_hub[i].x, k_hub[i].y, r - 2, s, len, dir,
                 color, opa, VU_WIDTH, 0);
    }
}

/* Map a 0..1 peak-hold to the VU dashes. The scale is dB over -48..0, so the
 * meter moves like a VU rather than a linear amplitude. */
static int vu_lit(float vu)
{
    if (vu <= 0.001f) return 0;
    float db = 20.0f * log10f(vu);
    if (db < -48.0f) db = -48.0f;
    if (db > 0.0f) db = 0.0f;
    return (int)lroundf((db + 48.0f) / 48.0f * VU_DASH_COUNT);
}

/* Apply a model frame: only what changed is touched. */
static void apply_frame(const cassette_frame_t *f)
{
    if (!f) return;

    if (f->title_changed) {
        const char *label = f->title[0] ? f->title
                          : f->playing ? ""
                          : "no card, or nothing to play";
        lv_label_set_text(s_title, label);
    }
    if (f->permille_changed) lv_bar_set_value(s_bar, f->permille, LV_ANIM_OFF);

    for (int i = 0; i < 2; i++) {
        if (f->hub_frame[i] != s_shown_hub[i]) {
            s_shown_hub[i] = f->hub_frame[i];
            lv_image_set_src(s_hub[i], &k_hub_crops[s_shown_hub[i]]);
        }
    }

    /* Pack fill is the model's; the diameter is the screen's. Both packs use the
     * same range. */
    pack_set(0, pack_diameter(f->pack_fill[0]));
    pack_set(1, pack_diameter(f->pack_fill[1]));

    /* The VU is the only thing that changes every poll; invalidate just its
     * overlay so the pack and its tape line stay put. */
    int lit0 = vu_lit(f->vu_l), lit1 = vu_lit(f->vu_r);
    if (lit0 != s_vu_lit[0]) {
        s_vu_lit[0] = lit0;
        if (s_vu[0]) lv_obj_invalidate(s_vu[0]);
    }
    if (lit1 != s_vu_lit[1]) {
        s_vu_lit[1] = lit1;
        if (s_vu[1]) lv_obj_invalidate(s_vu[1]);
    }
}

/* ------------------------------------------------------------------- build */

void cassette_view_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_scrollable(scr, false);

    /* The tape: two packs, then the tape strand that joins each to the bottom edge.
     * Each pack carries its strand in the same colour. */
    for (int i = 0; i < 2; i++) {
        s_strand[i] = lv_line_create(scr);

        s_pack_d[i] = (i == 0) ? PACK_MAX_D : PACK_MIN_D;
        s_pack[i] = disc(scr, k_hub[i].x, k_hub[i].y, s_pack_d[i], COL_TAPE);

        lv_obj_remove_style_all(s_strand[i]);
        lv_obj_set_style_line_color(s_strand[i], lv_color_hex(COL_TAPE_DARK), 0);
        lv_obj_set_style_line_width(s_strand[i], STRAND_W, 0);
        strand_set(i, s_pack_d[i]);
        lv_obj_add_event_cb(s_pack[i], pack_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
    }

    /* The hubs: only the 96x96 crop is animated. No static 168 frame is stored or
     * drawn; the disc behind it is the UI's own background. No recolor, no alpha. */
    for (int i = 0; i < 2; i++) {
        int cx = k_hub[i].x, cy = k_hub[i].y;
        disc(scr, cx, cy, HUB_RING_D, COL_HUB_RING);

        lv_obj_t *hub = lv_image_create(scr);
        lv_image_set_src(hub, &k_hub_crops[0]);
        lv_obj_set_pos(hub, cx - HUB_CROP_W / 2, cy - HUB_CROP_H / 2);

        s_hub[i] = hub;
        s_shown_hub[i] = 0;
    }

    /* The VU: a transparent overlay sized to its arc, above the hub ring and below
     * the label. Its own 25 Hz invalidation leaves the pack fill and tape line
     * alone. */
    for (int i = 0; i < 2; i++) {
        lv_obj_t *vu = lv_obj_create(scr);
        lv_obj_remove_style_all(vu);
        lv_obj_set_style_bg_opa(vu, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(vu, 0, 0);
        lv_obj_set_style_pad_all(vu, 0, 0);
        lv_obj_set_scrollable(vu, false);
        lv_obj_clear_flag(vu, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(vu, vu_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
        s_vu[i] = vu;
        s_vu_lit[i] = 0;
        vu_geom_set(i);
    }

    /* The label, the red side square and the rule the title sits above. */
    lv_obj_t *paper = box(scr, LABEL_X, LABEL_Y, LABEL_W, LABEL_H, LABEL_R, COL_LABEL);
    lv_obj_set_style_border_color(paper, lv_color_hex(COL_LABEL_EDGE), 0);
    lv_obj_set_style_border_width(paper, 1, 0);
    lv_obj_set_style_border_opa(paper, LV_OPA_COVER, 0);

    lv_obj_t *side = box(scr, B_X, B_Y, B_D, B_D, B_R, COL_RED);
    lv_obj_t *letter = text(side, &lv_font_montserrat_48, lv_color_hex(COL_LABEL),
                            LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(letter, "B");

    s_title = text(scr, &lv_font_montserrat_22, lv_color_hex(COL_INK),
                   LV_ALIGN_TOP_LEFT, TITLE_X, TITLE_Y);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(s_title, TITLE_W);

    /* The band: how far into the file we are. Dark ink on the light shell, with the
     * same red as the label's side square for the progress. */
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, BAND_W, 40);
    lv_obj_set_pos(s_bar, BAND_X, BAND_Y);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COL_BAR_FG), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_INDICATOR);

    /* The model: the art's frame count is the angular resolution; the tape
     * geometry supplies the speeds. */
    s_model = cassette_new(HUB_FRAME_COUNT, CASSETTE_DEFAULT_BAR_MS);
    if (!s_model) ESP_LOGW(TAG, "no model for the cassette: the screen will not update");
    else cassette_reset(s_model);

    /* LVGL's own performance monitor: CPU and FPS in a label on the system layer,
     * placed top-right by CONFIG_LV_PERF_MONITOR_ALIGN_TOP_RIGHT. */
    lv_sysmon_show_performance(NULL);

    /* Draw the empty screen now; the first status update replaces it. */
    if (s_model) apply_frame(cassette_step(s_model, NULL, lv_tick_get()));
}

/* ------------------------------------------------------------------ update */

void cassette_view_update(const mini_icecast_status_t *st)
{
    if (!s_model) return;
    const cassette_frame_t *f = cassette_step(s_model, st, lv_tick_get());
    apply_frame(f);

#if CONFIG_APP_UI_SERIAL_DUMP
    /* A still image cannot show that the hubs turn, so a diagnostic build says it. */
    static uint32_t last_log;
    uint32_t now = lv_tick_get();
    if (f && now - last_log > 1000) {
        last_log = now;
        ESP_LOGI(TAG, "hubs %u/%u and %u/%u, packs %d/%d px, %u permille",
                 (unsigned)f->hub_frame[0], HUB_FRAME_COUNT,
                 (unsigned)f->hub_frame[1], HUB_FRAME_COUNT,
                 s_pack_d[0], s_pack_d[1], (unsigned)f->permille);
    }
#endif
}
