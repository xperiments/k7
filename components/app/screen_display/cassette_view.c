/*
 * The cassette screen.
 *
 * The cassette body is drawn here: a light grey shell, two black tape packs of
 * different sizes and the progress band. The screen has three levels - Bare, Label
 * cover and Artwork - and exactly one sheet is shown at a time. A one-finger down/up
 * swipe moves one level; the two-finger swipe is reserved for the menu and does
 * nothing yet. The level changes in one frame, before LVGL draws: a transition that
 * redraws a full-screen sheet per frame was measured to stutter on this panel.
 *
 * The Label cover comes from assets/labels/k7logo.png: an 800x480 image, the panel's
 * own size, exported as RGB565A8 and drawn full-bleed at 0,0. Its black triangle and
 * circle carry alpha, so the reels show through them; once the cover is up the screen
 * background fades from k_col_bg to the cover's own k_col_cover_bg. While the
 * cover is up the reels are behind it, and so is the wave view: both are drawn with the
 * cassette, not over the cover.
 *
 * The Artwork level is a screen of its own: the cassette Case backdrop
 * (assets/labels/cover.jpg) full-bleed, the Track's picture filling the case's window,
 * and the title on the band below. Neither picture is decoded here; the board adapter
 * (artwork_loader.c) locates, decodes and fits them and hands the finished descriptors
 * in under the LVGL lock, so the view never touches the card or a decoder.
 *
 * The hubs are drawn, not blitted: a static rim with a divided circle turning inside it.
 * The rim is painted once and never invalidated; the two spokes are separate objects
 * repositioned per frame. That split is the whole point - LVGL invalidates an object's
 * bounding box, so a diameter drawn as one object would invalidate the hub, while two
 * opposite radii invalidate two quadrants.
 * The spokes stop short of the centre and of the rim for the same reason. See
 * docs/research/hub-drawn-vs-blitted.md for the measurements behind the geometry.
 *
 * The frame count is the animation's angular resolution, not an asset: at playback
 * speed one revolution takes 1.451 s and the UI refreshes every 40 ms, so 36 frames is
 * one 10-degree step per refresh. There is no lv_image_set_rotation(): LVGL's software
 * transform is per pixel and was measured at over 100 ms per frame on this part.
 *
 * This file is the adapter: it owns positions, fonts, diameters and LVGL objects. What
 * the screen should show - reel position, pack fill, progress, the cleaned title - is
 * components/portable/cassette, which the host tier can test. Nothing here re-derives any of it.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_view.h"
#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "artwork_loader.h"
#include "label_cover.h"
#include "touch_gt911.h"
#endif

LV_FONT_DECLARE(oxanium_36);

static const char *TAG = "screen";

/* The 800x480 panel: the hub rim is 168 px. The packs stay the reference's 245/310 so
 * the black tape rings the hub at both ends of a side. */
static const struct { int x, y; } k_hub[2] = { { 216-6, 226 }, { 600-8+2, 226 } };
// static const struct { int x, y; } k_window[2] = { { 120+180, 180 }, { 800-120-180, 268+20 } };
#define HUB_RING_D 168
#define PACK_MIN_D 168
#define PACK_MAX_D 380

/* The drawn hub: a rim with a divided circle turning inside it. The frame count is the
 * angular resolution of the animation (see the header). */
#define HUB_FRAME_COUNT 36
#define HUB_RIM_W 2
#define HUB_SPOKE_W 4
#define HUB_SPOKE_R0 26    /* the divider stops short of the centre and of the rim (82); */
#define HUB_SPOKE_R1 46    /* together they cut the box it invalidates ~7x on 0..84 */

/* One frame's angle, and half a turn, where the opposite spoke sits. LVGL measures
 * angles from 3 o'clock, positive clockwise, and the model's hub_frame rises with the
 * phase (cassette.c:84-88) - so a rising angle is the direction the old art turned:
 * its orange marker advanced ~10 deg per frame clockwise, read off assets/hub/001-005. */
static const double k_hub_step = 2.0 * 3.14159265358979323846 / (double)HUB_FRAME_COUNT;
#define HUB_HALF_TURN 3.14159265358979323846

/* The tape strands leave the bottom edge where it is tangent to a full pack:
 * 216 - 400/2 = 16 on the left, 584 + 400/2 = 784 on the right. */
#define STRAND_X_L 16
#define STRAND_X_R 784
#define STRAND_Y 480
#define STRAND_W 4

/* The small label: the bar with the Now playing title. It is not a Level: the up
 * swipe at Bare has no Level to move to, so it peeks this bar over the cassette, and
 * the next up swipe or any Level change puts it away. It rests at LABEL_Y and hides
 * below the screen, so the show slides up and the hide slides back. The band at the
 * bottom uses the same x and width. */
#define LABEL_X 96
#define LABEL_Y 324
#define LABEL_W 608
#define LABEL_H 52
#define LABEL_R 12
#define PAPER_HIDDEN_Y (488)
#define PAPER_ANIM_MS 260
#define SMALL_TITLE_X LABEL_X+24   /* screen coordinates; the child position is relative to the paper */
#define SMALL_TITLE_Y LABEL_Y+2
/* The title's box keeps the same 24 px inset on the right as the text has on the left:
 * from LABEL_X+24 to LABEL_W-24. */
#define SMALL_TITLE_W (LABEL_W - 2 * (SMALL_TITLE_X - LABEL_X))

/* The big cover: the supplied full cover, full-bleed at 0,0, the Label cover level.
 * Cover art is 800x480, the panel's own size, so the cover has no margin. This one
 * keeps its alpha (RGB565A8): the black triangle and circle are transparent, so the
 * reels show through them.
 * The title is a child of the cover; it sits on the empty band at the top of the art,
 * clear of the progress band at the bottom. The cover hides just above the screen,
 * so a level change places it at 0,0 in one frame. It is the panel board's, because
 * the vertical gestures that reach its level are. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
#define COVER_X 0
#define COVER_Y 0
#define COVER_HIDDEN_Y (-LABEL_COVER_H - 8)
#define COVER_BG_FADE_MS 500
#define COVER_TITLE_X 40
#define COVER_TITLE_Y 8
#define COVER_TITLE_W 720

/* The Artwork level: the Case backdrop, assets/labels/cover.jpg, full-bleed at 0,0,
 * with the Track's picture in its window - (101,23) to (704,408), 603x385 - and the
 * title on the band below. The Case is opaque, so the cassette and the bar are covered
 * rather than redrawn. The title sits in the free band under the case,
 * clear of the window. */
#define ART_X 101
#define ART_Y 23
#define ART_W 603
#define ART_H 385
#define ART_TITLE_Y 424
#define ART_TITLE_W 720
#define ART_HIDDEN_Y (-480 - 8)
#endif

/* The band below the tape: the same x and width as the small label, so the two line up. */
#define BAND_Y 434
#define BAND_X LABEL_X
#define BAND_W LABEL_W

/* The palette, read off the reference, as 0xRRGGBB. lv_color_hex() is a function
 * (lv_color.h:283), so it cannot initialise a constant; COL_HEX() is the same split done
 * at compile time on top of LVGL's own aggregate initialiser (lv_color.h:215). The result
 * is a real lv_color_t, so no conversion runs where a colour is used. */
#define COL_HEX(c) LV_COLOR_MAKE(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

/* A darker 0xRRGGBB: `pct` percent of the original, folded at compile time so a shade is a
 * const like any other colour - COL_DARKEN_HEX(0xF15A22, 70) is 0xA83F17. It takes a
 * literal, not a name, because the channels of an lv_color_t are not a constant expression
 * once it is an object; to darken a colour that is already an lv_color_t, LVGL has
 * lv_color_darken(). */
#define COL_DARKEN_HEX(c, pct)                                                        \
    COL_HEX((((((c) >> 16) & 0xFF) * (pct) / 100) << 16) |                            \
            (((((c) >> 8) & 0xFF) * (pct) / 100) << 8) |                              \
             ((((c) & 0xFF) * (pct) / 100)))

static const lv_color_t k_col_bg        = COL_HEX(0x111111);
/* The playing red: the tape's edge, and the hub's rim and divider. */
static const lv_color_t k_col_tape_dark = COL_HEX(0xF15A22);
/* The tape's own body. The same value as the edge today, and kept apart because the
 * rewind cross-fades the edge into it (tape_edge_color()), so the two are free to
 * differ again without touching a call site. */
static const lv_color_t k_col_tape      = COL_DARKEN_HEX(0xF15A22, 50);
static const lv_color_t k_col_hub_ring  = COL_DARKEN_HEX(0xF15A22, 70); 
static const lv_color_t k_col_hub_ink   = COL_DARKEN_HEX(0xF15A22, 50); /* the divider and the hub's centre dot */
static const lv_color_t k_col_bar_bg    = COL_HEX(0xB9C0BB);
static const lv_color_t k_col_bar_fg    = COL_HEX(0xF15A22); /* the tape's red */
static const lv_color_t k_col_wave_l    = COL_HEX(0xF15A22); /* the wave's left trace */
static const lv_color_t k_col_wave_r    = COL_HEX(0xFFD08A); /* the wave's right trace */
// static const lv_color_t k_col_guide     = COL_HEX(0x573534); /* the layout guides at the top */

/* The touch board's own Levels: its cover, its small label and the Artwork backdrop. They
 * live inside the guard because a board without those Levels never reads them. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
static const lv_color_t k_col_cover_bg  = COL_HEX(0xB48744); /* behind the cover's shapes */
static const lv_color_t k_col_cover_ink = COL_HEX(0xEEEEEE); /* the dark cover needs light ink */
static const lv_color_t k_col_art_bg    = COL_HEX(0x141414); /* until the Case lands */
static const lv_color_t k_col_paper     = COL_HEX(0x000000); /* the small label's fill */
#endif

/* The tape's solid edge enters at the tangent and runs TAPE_ARC_DEG to the top. LVGL
 * angles: 0 = 3 o'clock, 90 = 6, and positive is clockwise. The left reel runs
 * clockwise; the right mirrors it. */
#define TAPE_ARC_DEG 90
#define TAPE_WIDTH 4

/* The tape's red edge is a playing indication, not part of the reel change:
 * when the rewind starts they fade out, stay dark while the tape winds back, and fade
 * in as the new side begins. One second each way, over the eight-second window. */
#define REWIND_FADE_MS 1000

/* The three positions of the cassette screen, ordered by how much of it the sheet
 * covers: Bare, then the K7 sticker, then Artwork. A one-finger down swipe moves one
 * level towards Artwork, an up swipe one towards Bare, and the ends do not wrap.
 * Exactly one sheet is shown at a time. */
enum {
    LEVEL_BARE = 0,
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    LEVEL_COVER,
    LEVEL_ART,
#endif
};
#if CONFIG_APP_BOARD_SUNTON_8048S050C
#define LEVEL_MAX LEVEL_ART
#else
#define LEVEL_MAX LEVEL_BARE
#endif

static cassette_t *s_model;
typedef struct {
    lv_obj_t *obj;
    lv_point_precise_t pts[2];
} hub_spoke_t;

static hub_spoke_t s_hub_spoke[2][2];
static lv_obj_t *s_pack[2], *s_strand[2];
static lv_point_precise_t s_strand_pts[2][2];
static lv_obj_t *s_bar;
static int       s_level;
static mini_icecast_status_t s_status;  /* the last status, for the Artwork request */
static bool      s_have_status;
#if CONFIG_APP_BOARD_SUNTON_8048S050C
static lv_obj_t *s_paper;          /* the small cream label, an overlay on Bare */
static lv_obj_t *s_title;          /* the paper's title */
static bool      s_paper_shown;
static lv_obj_t *s_cover_title;    /* the big cover's title, on the empty top band */
static lv_obj_t *s_cover;          /* the full label cover (Label cover level) */
static lv_obj_t *s_art_sheet;      /* the Artwork level: its own full screen */
static lv_obj_t *s_case_img;       /* the Case backdrop; the loader owns its pixels */
static lv_obj_t *s_art_img;        /* the Track picture in the Case's window */
static lv_obj_t *s_art_title;      /* the Artwork level's title */
/* The touch adapter reports a two-finger swipe from the two contacts; this is what
 * suppresses the one-finger action and the click for the same touch. It resets on the
 * next press. */
static bool      s_two_finger_seen;
/* The view remembers the last artwork request, so a 25 Hz update is a string compare
 * rather than a loader call. */
static char      s_art_path[LIB_PATH_MAX];
static mi_fmt_t  s_art_fmt;
static bool      s_art_requested;
#endif
static int      s_pack_d[2];      /* px, so a redraw happens only when it changes */
static uint8_t  s_shown_hub[2];   /* the animation frame currently on screen */
static int      s_tangent_deg[2]; /* where the strand leaves the pack, LVGL degrees */
static lv_opa_t s_tape_opa = LV_OPA_COVER; /* the tape edge; 0 through the reel change */

/* ------------------------------------------------------------------ helpers */

/*
 * The screen is a picture, not a control surface today: every decoration must stop
 * taking a press, or the press stops at whichever box is under the finger and the
 * screen's own click never fires. Clearing the flag on the whole tree makes the screen
 * the one hit-test target, which is the "tap anywhere" this view gives. A future
 * control is created after this walk and stays clickable, so it takes its own press.
 */
static void make_decorative(lv_obj_t *obj)
{
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        lv_obj_set_clickable(child, false);
        make_decorative(child);
    }
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

/* A rounded box with a border. `bg_opa` makes the fill optional: LV_OPA_TRANSP leaves
 * only the border, so whatever is under the box shows through. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
static lv_obj_t *border_box(lv_obj_t *parent, int x, int y, int w, int h, int r,
                            lv_color_t bg, lv_opa_t bg_opa,
                            lv_color_t border_col, int border_width)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, bg_opa, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_color(o, border_col, 0);
    lv_obj_set_style_border_width(o, border_width, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}
#endif

static lv_obj_t *disc(lv_obj_t *parent, int cx, int cy, int d, lv_color_t bg)
{
    return box(parent, cx - d / 2, cy - d / 2, d, d, LV_RADIUS_CIRCLE, bg);
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                      lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_align(l, align, x, y);
    return l;
}
#endif

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

/* One half of the hub's divider: a 2 px radius from HUB_SPOKE_R0 to HUB_SPOKE_R1 at
 * `a` radians, LVGL's convention (0 = 3 o'clock, positive clockwise). The object's
 * box is the stroke's bounding box, padded so the line and its round caps are not
 * clipped - and that box is the whole invalidation cost, which is why the radius stops
 * short of the centre. Two opposite spokes are two objects on purpose: each is clipped
 * to its own quadrant, and two quadrant boxes are never joined into the hub's full
 * box (lv_refr_join_area() joins only when the union is smaller than the sum). */
static void spoke_set(hub_spoke_t *sp, int cx, int cy, double a)
{
    int x0 = cx + (int)lround(HUB_SPOKE_R0 * cos(a));
    int y0 = cy + (int)lround(HUB_SPOKE_R0 * sin(a));
    int x1 = cx + (int)lround(HUB_SPOKE_R1 * cos(a));
    int y1 = cy + (int)lround(HUB_SPOKE_R1 * sin(a));

    int pad = HUB_SPOKE_W / 2 + 1;
    int ox = LV_MIN(x0, x1) - pad, oy = LV_MIN(y0, y1) - pad;

    /* The size is explicit, so lv_line's own self-size refresh returns early
     * (lv_obj_pos.c:759-764) and the pad survives the points update below. */
    sp->pts[0].x = x0 - ox;
    sp->pts[0].y = y0 - oy;
    sp->pts[1].x = x1 - ox;
    sp->pts[1].y = y1 - oy;

    lv_obj_set_pos(sp->obj, ox, oy);
    lv_obj_set_size(sp->obj, LV_ABS(x1 - x0) + 2 * pad + 1,
                    LV_ABS(y1 - y0) + 2 * pad + 1);
    lv_line_set_points(sp->obj, sp->pts, 2);
}

/* The moving parts of hub `i` for a frame: the two spokes. Only these objects are ever
 * invalidated; the rim is painted once and left alone. The divider is 2-fold symmetric,
 * so the picture repeats every half turn - which reads fine in motion, because a 10 deg
 * step is well inside the 90 deg where the shortest motion would turn ambiguous. */
static void hub_frame_set(int i, uint8_t frame)
{
    double a = (double)frame * k_hub_step;

    for (int k = 0; k < 2; k++)
        spoke_set(&s_hub_spoke[i][k], k_hub[i].x, k_hub[i].y, a + (k ? HUB_HALF_TURN : 0.0));

}
/* The wave view: the last WAVE_SAMPLES levels of each channel, one sample per UI tick,
 * drawn as two traces in a square box between the reels. The box is created before the
 * hubs, so a hub paints over the wave rather than under it. */
#define WAVE_SIZE 120
#define WAVE_SAMPLES WAVE_SIZE   /* one sample per column: ~4.8 s at 40 ms a tick */
#define WAVE_X 400               /* the gap between the two reels */
#define WAVE_Y 226

static lv_obj_t *s_wave;                    /* the box a frame invalidates */
static lv_obj_t *s_wave_line[2];            /* L and R traces */
static lv_point_precise_t s_wave_pts[2][WAVE_SAMPLES];
static uint8_t s_wave_amp[2][WAVE_SAMPLES]; /* dB-mapped level, 0..255, oldest first */

/* Map a 0..1 peak-hold onto 0..255 over the meter's own scale: dB over -48..0, so the
 * traces move like a VU rather than a linear amplitude. */
static uint8_t wave_amp(float vu)
{
    if (vu <= 0.001f) return 0;
    float db = 20.0f * log10f(vu);
    if (db < -48.0f) db = -48.0f;
    if (db > 0.0f) db = 0.0f;
    return (uint8_t)lroundf((db + 48.0f) / 48.0f * 255.0f);
}

/* Rebuild both traces from the history. The points are local to their line object,
 * which is fixed at WAVE_SIZE square, so a points update never resizes it. */
static void wave_draw(void)
{
    for (int ch = 0; ch < 2; ch++) {
        for (int x = 0; x < WAVE_SAMPLES; x++) {
            int h = s_wave_amp[ch][x] * (WAVE_SIZE - 2) / 255;
            s_wave_pts[ch][x].x = x;
            s_wave_pts[ch][x].y = WAVE_SIZE - 1 - h;
        }
        lv_line_set_points(s_wave_line[ch], s_wave_pts[ch], WAVE_SAMPLES);
    }
}

/* Push one tick: shift the history left, append, redraw. Every column moves, so the box
 * is invalidated unless the whole history already equals the new sample - a silent track
 * then costs no redraw at all. */
static void wave_push(float level_l, float level_r)
{
    if (!s_wave) return;
    float level[2] = { level_l, level_r };
    bool still = true;

    for (int ch = 0; ch < 2; ch++) {
        uint8_t amp = wave_amp(level[ch]);
        for (int x = 0; x < WAVE_SAMPLES; x++) {
            if (s_wave_amp[ch][x] != amp) { still = false; break; }
        }
        memmove(&s_wave_amp[ch][0], &s_wave_amp[ch][1], WAVE_SAMPLES - 1);
        s_wave_amp[ch][WAVE_SAMPLES - 1] = amp;
    }

    if (still) return;
    wave_draw();
    lv_obj_invalidate(s_wave);
}

static void pack_set(int i, int d)
{
    if (d == s_pack_d[i]) return;
    s_pack_d[i] = d;
    lv_obj_set_size(s_pack[i], d, d);
    lv_obj_set_pos(s_pack[i], k_hub[i].x - d / 2, k_hub[i].y - d / 2);
    strand_set(i, d);
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
/* The tape edge's colour follows the rewind envelope: the playing red (k_col_tape_dark)
 * when s_tape_opa is full, the tape's own brown (k_col_tape) when it is zero. On the pack
 * the edge then merges with the tape; the strands stay visible as dark tape. */
static lv_color_t tape_edge_color(void)
{
    return lv_color_mix(k_col_tape_dark, k_col_tape, s_tape_opa);
}

/* The solid tape edge, drawn on the pack so it redraws only when the pack resizes. */
static void pack_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    int r = s_pack_d[i] / 2;
    int dir = i ? -1 : 1;
    draw_arc(layer, k_hub[i].x -(i==0 ?  3 : -3), k_hub[i].y, r - 2,
             s_tangent_deg[i], TAPE_ARC_DEG, dir,
             tape_edge_color(), LV_OPA_COVER, TAPE_WIDTH, 1);
}
/* The tape's red edge belongs to playback, not to the reel change. The rewind
 * cross-fades the edge to the tape's own colour:
 * full at the window's start, zero over its first second, zero for the body, and back
 * over its last second as the new side starts. The rewind's permille is linear in
 * time, so the ramp is a plain interpolation. */
static lv_opa_t rewind_tape_opa(const cassette_frame_t *f)
{
    if (!f || !f->rewinding) return LV_OPA_COVER;

    uint32_t elapsed = (uint32_t)f->rewind_permille * CASSETTE_REWIND_MS / 1000u;
    uint32_t fade_in_at = CASSETTE_REWIND_MS - REWIND_FADE_MS;
    if (elapsed < REWIND_FADE_MS)
        return (lv_opa_t)((uint32_t)LV_OPA_COVER * (REWIND_FADE_MS - elapsed) /
                          REWIND_FADE_MS);
    if (elapsed > fade_in_at)
        return (lv_opa_t)((uint32_t)LV_OPA_COVER * (elapsed - fade_in_at) / REWIND_FADE_MS);
    return LV_OPA_TRANSP;
}

/* Push a new tape opacity into the objects that carry the edge: the strand lines take
 * the mixed colour, and the packs (their draw callback paints the solid arc) read it from
 * tape_edge_color(). The packs grow and shrink through the rewind anyway, so this adds no
 * full-screen redraw of its own. */
static void tape_opa_apply(lv_opa_t opa)
{
    s_tape_opa = opa;
    lv_color_t edge = tape_edge_color();
    for (int i = 0; i < 2; i++) {
        if (s_strand[i]) {
            lv_obj_set_style_line_color(s_strand[i], edge, 0);
            lv_obj_invalidate(s_strand[i]);
        }
        if (s_pack[i]) lv_obj_invalidate(s_pack[i]);
    }
}

/* Apply a model frame: only what changed is touched. */
static void apply_frame(const cassette_frame_t *f)
{
    if (!f) return;

    /* The reel change owns the tape edge: it fades out over its first
     * second, stay dark, and fade back in for the new side. Do this before the pack
     * draws so this frame's redraw already carries the new opacity. */
    lv_opa_t tape_opa = rewind_tape_opa(f);
    if (tape_opa != s_tape_opa) {
        tape_opa_apply(tape_opa);
        if (tape_opa == LV_OPA_TRANSP)
            ESP_LOGI(TAG, "rewind: tape edge now the tape's colour");
        else if (tape_opa == LV_OPA_COVER)
            ESP_LOGI(TAG, "rewind: tape edge back to the playing red");
    }

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (f->title_changed) {
        const char *label = f->title[0] ? f->title
                          : f->playing ? ""
                          : "no card, or nothing to play";
        lv_label_set_text(s_title, label);
        lv_label_set_text(s_cover_title, label);
        lv_label_set_text(s_art_title, label);
    }
#endif
    if (f->permille_changed) lv_bar_set_value(s_bar, f->permille, LV_ANIM_OFF);

    for (int i = 0; i < 2; i++) {
        if (f->hub_frame[i] != s_shown_hub[i]) {
            s_shown_hub[i] = f->hub_frame[i];
            hub_frame_set(i, s_shown_hub[i]);
        }
    }

    /* The wave: one history step per frame. */
    wave_push(f->vu_l, f->vu_r);

    /* Pack fill is the model's; the diameter is the screen's. Both packs use the
     * same range. */
    pack_set(0, pack_diameter(f->pack_fill[0]));
    pack_set(1, pack_diameter(f->pack_fill[1]));

}

/* ------------------------------------------------------------------- build */

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The cover's alpha shapes look through to the screen. The screen's backdrop is mixed
 * between the cassette's colour and the cover's: 0 is k_col_bg, 255 is k_col_cover_bg, so
 * the Label cover level can fade its own backdrop in behind the picture. */
#define COVER_BG_MIX_COVER 255
static uint8_t s_cover_bg_mix;

static void cover_bg_mix_set(uint8_t mix)
{
    if (mix == s_cover_bg_mix) return;
    s_cover_bg_mix = mix;
    lv_obj_set_style_bg_color(lv_screen_active(),
                              lv_color_mix(k_col_cover_bg, k_col_bg, mix), 0);
}

static void cover_bg_fade_cb(void *var, int32_t value)
{
    (void)var;
    cover_bg_mix_set((uint8_t)value);
}

/* Fade between the two backdrops from whatever is on screen now, so a fade can be
 * reversed in flight without a jump. */
static void cover_bg_fade(bool cover, uint32_t ms)
{
    lv_obj_t *scr = lv_screen_active();
    lv_anim_delete(scr, cover_bg_fade_cb);
    int32_t target = cover ? COVER_BG_MIX_COVER : 0;
    if ((int32_t)s_cover_bg_mix == target) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scr);
    lv_anim_set_exec_cb(&a, cover_bg_fade_cb);
    lv_anim_set_values(&a, s_cover_bg_mix, target);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* No animation: used before a level change settles the backdrop at once. */
static void cover_bg_now(void)
{
    lv_anim_delete(lv_screen_active(), cover_bg_fade_cb);
    cover_bg_mix_set(0);
}
#endif /* CONFIG_APP_BOARD_SUNTON_8048S050C */

/* ------------------------------------------------------------- the level machine */

/* Where each sheet rests and where it hides. The full-screen sheets are full-bleed at
 * 0,0. */
static int sheet_rest_y(int level)
{
    switch (level) {
#if CONFIG_APP_BOARD_SUNTON_8048S050C
        case LEVEL_COVER: return COVER_Y;
        case LEVEL_ART:   return 0;
#endif
        default:          return 0;
    }
}

static int sheet_hidden_y(int level)
{
    switch (level) {
#if CONFIG_APP_BOARD_SUNTON_8048S050C
        case LEVEL_COVER: return COVER_HIDDEN_Y;
        case LEVEL_ART:   return ART_HIDDEN_Y;
#endif
        default:          return 0;
    }
}

static lv_obj_t *sheet_obj(int level)
{
    switch (level) {
#if CONFIG_APP_BOARD_SUNTON_8048S050C
        case LEVEL_COVER: return s_cover;
        case LEVEL_ART:   return s_art_sheet;
#endif
        default:          return NULL;
    }
}

/* No level change animates. The full-screen sheets are 800x480, and a transition that
 * redraws one of them per frame was measured to stutter on this panel; the level moves
 * in one frame, before LVGL draws. */
static void sheet_hide_now(int level)
{
    lv_obj_t *o = sheet_obj(level);
    if (!o) return;
    lv_anim_delete(o, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(o, sheet_hidden_y(level));
    lv_obj_set_hidden(o, true);
}

/* Put a sheet where it rests, visible; every other sheet is hidden by level_settled(). */
static void sheet_show_at_rest(int level)
{
    lv_obj_t *o = sheet_obj(level);
    if (!o) return;
    lv_anim_delete(o, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(o, sheet_rest_y(level));
    lv_obj_set_hidden(o, false);
    /* The Artwork screen sits above the decorations. The K7 cover stays below the bar:
     * it is the cassette's own label. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (level == LEVEL_ART) {
        lv_obj_move_foreground(o);
    }
#endif
}

/* Leave exactly the target sheet visible. */
static void level_settled(void)
{
    for (int level = LEVEL_BARE + 1; level <= LEVEL_MAX; level++) {
        if (level != s_level) sheet_hide_now(level);
    }
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* Ask the loader for the current Track's picture. The loader dedupes too, but this
 * remembers the request so a 25 Hz update is a string compare, not a lock. */
static void art_request(void)
{
    if (!s_have_status || !s_art_sheet) return;
    if (s_art_requested && s_art_fmt == s_status.format &&
        strcmp(s_art_path, s_status.path) == 0) return;
    s_art_requested = true;
    s_art_fmt = s_status.format;
    snprintf(s_art_path, sizeof s_art_path, "%s", s_status.path);
    /* A new Track: put the frame back until its picture lands, rather than leaving the
     * previous Track's cover up while it decodes. */
    if (s_art_img) lv_obj_set_hidden(s_art_img, true);
    ESP_LOGI(TAG, "artwork request: %s (fmt %d)", s_status.path, (int)s_status.format);
    artwork_loader_request(s_status.path, s_status.format);
}
#endif

#if CONFIG_APP_BOARD_SUNTON_8048S050C
static void paper_anim_hidden_cb(lv_anim_t *a)
{
    (void)a;
    if (!s_paper_shown) lv_obj_set_hidden(s_paper, true);
}

/* The small label, the one overlay that lives on Bare. It is not a Level: the up swipe
 * at Bare has no Level to move to, so it peeks the bar instead. Showing it brings it in
 * front of the cassette's own art; hiding it slides it back above the screen and hides
 * the object once the slide is over. */
static void paper_set_shown(bool show)
{
    if (!s_paper || show == s_paper_shown) return;
    s_paper_shown = show;

    lv_anim_delete(s_paper, (lv_anim_exec_xcb_t)lv_obj_set_y);
    if (show) {
        lv_obj_set_hidden(s_paper, false);
        lv_obj_move_foreground(s_paper);
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_paper);
    lv_anim_set_values(&a, lv_obj_get_y(s_paper), show ? LABEL_Y : PAPER_HIDDEN_Y);
    lv_anim_set_duration(&a, PAPER_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    if (!show) lv_anim_set_completed_cb(&a, paper_anim_hidden_cb);
    lv_anim_start(&a);
}

/* Put the paper away at once, deleting a slide in flight. A Level change uses this so
 * the label never rides over the K7 cover or the Artwork screen. */
static void paper_hide_now(void)
{
    if (!s_paper) return;
    lv_anim_delete(s_paper, (lv_anim_exec_xcb_t)lv_obj_set_y);
    s_paper_shown = false;
    lv_obj_set_y(s_paper, PAPER_HIDDEN_Y);
    lv_obj_set_hidden(s_paper, true);
}
#endif

/* Move the level by one. The level is a position, not a command, so a swipe at an end
 * does nothing and the ends do not wrap. */
static void set_level(int target)
{
    if (target < LEVEL_BARE) target = LEVEL_BARE;
    if (target > LEVEL_MAX) target = LEVEL_MAX;
    if (target == s_level) return;

    s_level = target;

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The small label belongs to Bare alone. Any Level change puts it away before the
     * target sheet is placed. */
    paper_hide_now();

    if (target == LEVEL_ART) art_request();

    if (target == LEVEL_COVER) {
        cover_bg_now();
        cover_bg_fade(true, COVER_BG_FADE_MS);
    } else if (target == LEVEL_ART) {
        cover_bg_now();
    } else {
        cover_bg_fade(false, COVER_BG_FADE_MS);
    }
#endif

    if (target != LEVEL_BARE) sheet_show_at_rest(target);
    level_settled();

    ESP_LOGI(TAG, "level %d", s_level);
}

/* A new touch clears the two-finger latch from the previous one. */
static void screen_pressed_cb(lv_event_t *e)
{
    (void)e;
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    s_two_finger_seen = false;
#endif
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The two-finger swipe is reserved for the menu (on/off), which is not designed yet.
 * The port still reports it, and this callback keeps the one-finger handler and the
 * click from acting on the same touch: a two-finger gesture must not change a Level,
 * skip a Track or open the sticker by accident. When the menu lands, it takes this
 * callback and nothing in touch_gt911.c has to change. */
static void screen_two_finger_cb(int dx, int dy)
{
    (void)dx;
    (void)dy;
    s_two_finger_seen = true;
    ESP_LOGI(TAG, "two-finger swipe: reserved for the menu");
}
#endif

/*
 * The classic one-finger gestures: left/right are the transport; down/up move one
 * level, towards Artwork and towards Bare. At Bare the up swipe has no Level to move
 * to, so it peeks the small title label; while the label is up, either vertical swipe
 * puts it away before a down gesture is allowed to move to the Label cover. A
 * two-finger swipe does not come through here; the touch adapter reports it, and `seen`
 * keeps this handler from acting on the same touch as well.
 */
static void screen_gesture_cb(lv_event_t *e)
{
    (void)e;
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (s_two_finger_seen) return;
#endif
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    switch (lv_indev_get_gesture_dir(indev)) {
        case LV_DIR_LEFT:
            mini_icecast_skip();
            ESP_LOGI(TAG, "swipe left: next track");
            break;
        case LV_DIR_RIGHT:
            mini_icecast_prev();
            ESP_LOGI(TAG, "swipe right: previous track");
            break;
        case LV_DIR_BOTTOM:
#if CONFIG_APP_BOARD_SUNTON_8048S050C
            /* The label is a peek, not a Level: while it is up, the down swipe puts it
             * away first; only the next one moves to the K7 cover. */
            if (s_level == LEVEL_BARE && s_paper_shown) {
                paper_set_shown(false);
                ESP_LOGI(TAG, "swipe down at Bare: small label hidden");
                break;
            }
#endif
            set_level(s_level + 1);
            break;
        case LV_DIR_TOP:
#if CONFIG_APP_BOARD_SUNTON_8048S050C
            /* At Bare there is no Level above to move to: the up swipe peeks the small
             * label, and while it is up the next up swipe puts it away. */
            if (s_level == LEVEL_BARE) {
                paper_set_shown(!s_paper_shown);
                ESP_LOGI(TAG, "swipe up at Bare: small label %s",
                         s_paper_shown ? "shown" : "hidden");
                break;
            }
#endif
            set_level(s_level - 1);
            break;
        default:
            break;
    }
}

/* The one thing a tap does today, and it is the same action the BOOT button has. The
 * status the next frame reads carries the new title, so the tap needs no other reply. */
static void screen_clicked_cb(lv_event_t *e)
{
    (void)e;
    /* A swipe is a release too, and LVGL sends CLICKED after the gesture. The gesture
     * has already acted; the click must not act a second time. A two-finger swipe
     * sets `seen`; a one-finger one leaves its direction on the input device. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (s_two_finger_seen) return;
#endif
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;
    mini_icecast_skip();
    ESP_LOGI(TAG, "tap: next track");
}

void cassette_view_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, k_col_bg, 0);
    lv_obj_set_scrollable(scr, false);


    // static lv_point_precise_t points[][2] = {
    //     {{40, 30},  {760, 30}},
    //     {{40, 70}, {760, 70}},
    //     {{40, 110}, {760, 110}}
    // };

    // for (int i = 0; i < 3; i++) {
    //     lv_obj_t *line = lv_line_create(scr);

    //     lv_line_set_points(line, points[i], 2);

    //     lv_obj_set_style_line_color(line, k_col_guide, 0);
    //     lv_obj_set_style_line_width(line, 3, 0);
    //     lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    // }

    /* The window: a border box around the cassette view. */
    // lv_obj_t *window = box(
    //     scr,
    //     k_window[0].x, k_window[0].y, k_window[1].x - k_window[0].x, k_window[1].y - k_window[0].y, 16,
    //     0x220000);

    /* The tape: two packs, then the tape strand that joins each to the bottom edge.
     * Each pack carries its strand in the same colour. */
    for (int i = 0; i < 2; i++) {

        s_pack_d[i] = (i == 0) ? PACK_MAX_D : PACK_MIN_D;
        s_pack[i] = disc(scr, k_hub[i].x, k_hub[i].y, s_pack_d[i], k_col_tape);

        s_strand[i] = lv_line_create(scr);
        lv_obj_remove_style_all(s_strand[i]);
        lv_obj_set_style_line_color(s_strand[i], k_col_tape_dark, 0);
        lv_obj_set_style_line_width(s_strand[i], STRAND_W, 0);
        strand_set(i, s_pack_d[i]);
        lv_obj_add_event_cb(s_pack[i], pack_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
    }

    /* The wave view: two traces in a 120x120 box in the gap between the reels. Created
     * before the hubs, so it sits behind them; transparent, so the tape shows through. */
    s_wave = lv_obj_create(scr);
    lv_obj_remove_style_all(s_wave);
    lv_obj_set_style_bg_opa(s_wave, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wave, 0, 0);
    lv_obj_set_style_pad_all(s_wave, 0, 0);
    lv_obj_set_scrollable(s_wave, false);
    lv_obj_set_clickable(s_wave, false);
    lv_obj_set_pos(s_wave, WAVE_X - WAVE_SIZE / 2, WAVE_Y - WAVE_SIZE / 2);
    lv_obj_set_size(s_wave, WAVE_SIZE, WAVE_SIZE);

    for (int ch = 0; ch < 2; ch++) {
        lv_obj_t *ln = lv_line_create(s_wave);
        lv_obj_remove_style_all(ln);
        lv_obj_set_pos(ln, 0, 0);
        lv_obj_set_size(ln, WAVE_SIZE, WAVE_SIZE);
        lv_obj_set_style_line_color(ln, ch ? k_col_wave_r : k_col_wave_l, 0);
        lv_obj_set_style_line_width(ln, 2, 0);
        lv_obj_set_style_line_rounded(ln, false, 0);
        s_wave_line[ch] = ln;
    }
    wave_draw();

    /* The hubs: the rim is painted once and never invalidated. Only the two spokes
     * move, and each is its own object so its bounding box stays small. */
    for (int i = 0; i < 2; i++) {
        int cx = k_hub[i].x, cy = k_hub[i].y;
        lv_obj_t *rim = disc(scr, cx, cy, HUB_RING_D, k_col_hub_ring);
        lv_obj_set_style_border_color(rim, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_opa(rim, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rim, HUB_RIM_W, 0);

        for (int k = 0; k < 2; k++) {
            lv_obj_t *sp = lv_line_create(scr);
            lv_obj_remove_style_all(sp);
            lv_obj_set_style_line_color(sp, k_col_hub_ink, 0);
            lv_obj_set_style_line_width(sp, HUB_SPOKE_W, 0);
            lv_obj_set_style_line_rounded(sp, true, 0);
            s_hub_spoke[i][k].obj = sp;
        }

        lv_obj_t *pack_center = disc(scr, k_hub[i].x, k_hub[i].y, 10, k_col_hub_ink);
        s_shown_hub[i] = 0;
        hub_frame_set(i, 0);
    }

    /* The small label: the cream bar the up swipe at Bare peeks. Its title is its
     * child, so the bar and the title move as one. It hides above the screen until
     * that swipe, and any Level change puts it away. It is not a Level. */

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    s_paper = border_box(scr, LABEL_X, LABEL_Y, LABEL_W, LABEL_H, LABEL_R,
                         k_col_paper, 30, k_col_tape_dark, 2);
    lv_obj_set_y(s_paper, PAPER_HIDDEN_Y);
    lv_obj_set_hidden(s_paper, true);
    s_title = text(s_paper, &oxanium_36, k_col_tape_dark,
                   LV_ALIGN_TOP_LEFT, SMALL_TITLE_X - LABEL_X, SMALL_TITLE_Y - LABEL_Y);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(s_title, SMALL_TITLE_W);

    /* The big cover: the supplied full cover at 0,0, one swipe past Bare. Cover art is
     * 800x480, the panel's size, so it sits full-bleed. Its black shapes are
     * transparent, so the reels show through them; the title is written on the empty
     * band at the top and is its child. */
    s_cover = lv_image_create(scr);
    lv_image_set_src(s_cover, &k_label_cover);
    /* No background or padding of the widget's own, so the image sits at the object's
     * origin and the title's coordinates are the image's. */
    lv_obj_set_style_bg_opa(s_cover, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cover, 0, 0);
    lv_obj_set_style_pad_all(s_cover, 0, 0);
    lv_obj_set_pos(s_cover, COVER_X, COVER_Y);
    lv_obj_set_size(s_cover, LABEL_COVER_W, LABEL_COVER_H);
    lv_obj_set_y(s_cover, COVER_HIDDEN_Y);
    lv_obj_set_hidden(s_cover, true);
    s_cover_title = text(s_cover, &oxanium_36, k_col_cover_ink,
                         LV_ALIGN_TOP_LEFT, COVER_TITLE_X, COVER_TITLE_Y);
    lv_label_set_long_mode(s_cover_title, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(s_cover_title, COVER_TITLE_W);
#endif

    /* The band: how far into the file we are, under the label and the same width as
     * it. Dark ink on the light shell, with the tape red for the progress. */
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, BAND_W, 20);
    lv_obj_set_pos(s_bar, BAND_X, BAND_Y);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_bar, k_col_bar_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, k_col_bar_fg, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 10, LV_PART_INDICATOR);




    /* The Artwork level: its own full screen, above everything else. The Case backdrop
     * is full-bleed at 0,0; the Track picture fills its window at 101,23. The loader
     * owns both sets of pixels, and both are hidden until they land. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    s_art_sheet = box(scr, 0, 0, 800, 480, 0, k_col_art_bg);
    lv_obj_set_y(s_art_sheet, ART_HIDDEN_Y);
    lv_obj_set_hidden(s_art_sheet, true);

    s_case_img = lv_image_create(s_art_sheet);
    lv_obj_set_style_bg_opa(s_case_img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_case_img, 0, 0);
    lv_obj_set_style_pad_all(s_case_img, 0, 0);
    lv_obj_set_pos(s_case_img, 0, 0);
    lv_obj_set_size(s_case_img, 800, 480);
    lv_obj_set_hidden(s_case_img, true);

    s_art_img = lv_image_create(s_art_sheet);
    lv_obj_set_style_bg_opa(s_art_img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_art_img, 0, 0);
    lv_obj_set_style_pad_all(s_art_img, 0, 0);
    lv_obj_set_pos(s_art_img, ART_X, ART_Y);
    lv_obj_set_size(s_art_img, ART_W, ART_H);
    lv_obj_set_hidden(s_art_img, true);

    s_art_title = text(s_art_sheet, &oxanium_36, k_col_cover_ink,
                       LV_ALIGN_TOP_MID, 0, ART_TITLE_Y);
    lv_obj_set_width(s_art_title, ART_TITLE_W);
    lv_label_set_long_mode(s_art_title, LV_LABEL_LONG_SCROLL);
#endif

    /* The model: the art's frame count is the angular resolution; the tape
     * geometry supplies the speeds. */
    s_model = cassette_new(HUB_FRAME_COUNT, CASSETTE_DEFAULT_BAR_MS);
    if (!s_model) ESP_LOGW(TAG, "no model for the cassette: the screen will not update");
    else cassette_reset(s_model);

    /* LVGL's own performance monitor: CPU and FPS in a label on the system layer,
     * placed top-right by CONFIG_LV_PERF_MONITOR_ALIGN_TOP_RIGHT. */
    // lv_sysmon_show_performance(NULL);

    /* Draw the empty screen now; the first status update replaces it. */
    if (s_model) apply_frame(cassette_step(s_model, NULL, lv_tick_get()));

    /* The picture is built. Make the screen the one thing a finger can hit; its click
     * is next track, a one-finger swipe is the transport or the next Level, and a
     * two-finger swipe is reserved for the menu. */
    lv_obj_set_clickable(scr, true);
    make_decorative(scr);
    lv_obj_add_event_cb(scr, screen_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, screen_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The touch adapter's two-finger swipe; it is called before the one-finger
     * gesture for the same touch, so `seen` is already set when that one arrives. It
     * does not move a Level: the menu will take it later. */
    touch_gt911_set_two_finger_cb(screen_two_finger_cb);
#endif
}

/* ------------------------------------------------------------------ update */

void cassette_view_update(const mini_icecast_status_t *st)
{
    if (!s_model) return;
    if (st) {
        s_status = *st;
        s_have_status = true;
    }
    const cassette_frame_t *f = cassette_step(s_model, st, lv_tick_get());
    apply_frame(f);

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* At the Artwork level a track change means a new picture. The view remembers the
     * request, so this is a string compare on the frames that do not change it. */
    if (s_level == LEVEL_ART) art_request();
#endif

#if CONFIG_APP_UI_SERIAL_DUMP
    /* A still image cannot show that the hubs turn, so a diagnostic build says it. */
    static uint32_t last_log;
    uint32_t now = lv_tick_get();
    if (f && now - last_log > 1000) {
        last_log = now;
        /* The reel change lasts 8 s, so a one-second log catches most of its frames -
         * enough to tune the spin and the ease against real frame costs. */
        ESP_LOGI(TAG, "hubs %u/%u and %u/%u, packs %d/%d px, %u permille%s%u",
                 (unsigned)f->hub_frame[0], HUB_FRAME_COUNT,
                 (unsigned)f->hub_frame[1], HUB_FRAME_COUNT,
                 s_pack_d[0], s_pack_d[1], (unsigned)f->permille,
                 f->rewinding ? ", rewinding " : "",
                 f->rewinding ? (unsigned)f->rewind_permille : 0u);
    }
#endif
}

#if CONFIG_APP_BOARD_SUNTON_8048S050C
void cassette_view_set_artwork(const lv_image_dsc_t *dsc)
{
    if (!s_art_img) return;
    if (dsc) {
        lv_image_set_src(s_art_img, dsc);
        lv_obj_set_hidden(s_art_img, false);
    } else {
        lv_obj_set_hidden(s_art_img, true);
    }
}

void cassette_view_set_case(const lv_image_dsc_t *dsc)
{
    if (!s_case_img) return;
    if (dsc) {
        lv_image_set_src(s_case_img, dsc);
        lv_obj_set_hidden(s_case_img, false);
    } else {
        lv_obj_set_hidden(s_case_img, true);
    }
}
#endif
