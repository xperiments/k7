/*
 * The cassette screen.
 *
 * The cassette body is drawn here: a light grey shell and two tape packs of different
 * sizes, each a set of concentric line circles rather than a filled disc. The tape leaves
 * each pack on the outer tangent, wraps a 30 px roller whose bottom is 16 px above the
 * screen edge, and runs along the bottom between the two rollers. The screen has
 * three levels - Bare, Label
 * cover and Artwork - and exactly one sheet is shown at a time. A one-finger down/up
 * swipe moves one level; the two-finger swipe is reserved for the menu and does
 * nothing yet. The level changes in one frame, before LVGL draws: a transition that
 * redraws a full-screen sheet per frame was measured to stutter on this panel.
 *
 * The Label cover comes from assets/labels/k7logo.png: an 800x480 image, the panel's
 * own size, exported as RGB565A8 and drawn full-bleed at 0,0. Its black triangle and
 * circle carry alpha, so the reels show through them; once the cover is up the screen
 * background fades from k_col_bg to the cover's own k_col_cover_bg. While the
 * cover is up the reels are behind it, and so is the VU meter: both are drawn with the
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
 * Everything the cassette draws is a child of one container object, itself a child of the
 * screen, so the picture can be moved, faded or hidden as one and the Level sheets are its
 * siblings rather than sharing the screen root with every part. The container is full-bleed
 * at 0,0 and paints nothing, so every position in this file stays a screen coordinate,
 * which is what the draw callbacks that work in screen space assume.
 *
 * The cassette's geometry is written in the design grid and scaled on the way out by
 * s_pose - the scale, then the offset - and centred. The pose is a value, so the cassette
 * can shrink, grow and slide while it is up: the small label's peek takes it from 256 to
 * 200 and back, and a menu can take it anywhere. Scaling the numbers rather than the
 * pixels is deliberate: lv_obj_set_style_transform_scale() on the container puts an ARGB
 * layer under it and re-samples the whole picture on every repaint, which on this part is
 * the >100 ms/frame above and which also drops the 1-2 px pack rings and tape strands.
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

LV_FONT_DECLARE(jura_36);

static const char *TAG = "screen";

/* The cassette's layout is written in the panel's own coordinates - the 800x480 grid the
 * label cover is drawn on - and s_pose turns one of those into a pixel: the design box is
 * scaled, centred in the panel, then moved by (dx, dy). 256 (LV_SCALE_NONE) is full size;
 * anything less leaves a margin. It is a value, not a constant, so the cassette can
 * shrink, grow and slide while it is up (cassette_view_set_scale): the whole layout is a
 * function of it, and cassette_place() moves every object when it changes.
 *
 * Scaling the numbers rather than the pixels is deliberate. The body is drawn, not
 * blitted, so the 1-2 px lines stay sharp at any scale. The transform style is what
 * costs: it puts an ARGB layer under the container and re-samples the whole picture on
 * every repaint (see the header). */
#define CASSETTE_SCALE       256   /* at rest: the design grid, pixel for pixel */
#define CASSETTE_SCALE_LABEL 200   /* while the small label is up, so it clears the tape */

typedef struct {
    int scale;   /* 256 is the design box, pixel for pixel */
    int dx;      /* screen pixels from the centred position: +x right, +y down */
    int dy;
} cassette_pose_t;

static cassette_pose_t s_pose = { CASSETTE_SCALE, 0, 0 };

#define SC(v)  ((v) * s_pose.scale / 256)            /* a design length, in pixels */
/* The design box, centred in the panel and then moved by the pose's offset. */
#define CASSETTE_OX ((800 - SC(800)) / 2 + s_pose.dx)
#define CASSETTE_OY ((480 - SC(480)) / 2 + s_pose.dy)
#define SCX(x) (CASSETTE_OX + SC(x))                 /* a design x, in screen pixels */
#define SCY(y) (CASSETTE_OY + SC(y))                 /* a design y, in screen pixels */

/* The 800x480 panel: the hub rim is 168 px. The packs stay the reference's 245/310 so
 * the tape rings the hub at both ends of a side. A pack is not a filled disc: its
 * diameter is the outer circle's, drawn as a line, and each inner circle sits
 * PACK_RING_GAP px inside the one before it. Only the circles outside the rim are drawn
 * - the rim covers what is under it - so an empty pack, whose radius is the rim's,
 * draws none, and a full one draws the lot. */
static const struct { int x, y; } k_hub[2] = { { 216-6, 206 }, { 600-8+2, 206 } };
#define HUB_RING_D 168
#define PACK_MIN_D 168
#define PACK_MAX_D 356
#define PACK_RING_GAP 8
#define PACK_RING_W 1

/* The drawn hub: a rim with a divided circle turning inside it. The frame count is the
 * angular resolution of the animation (see the header). */
#define HUB_FRAME_COUNT 36
#define HUB_RIM_W 2
#define HUB_SPOKE_W 4
#define HUB_SPOKE_R0 66    /* the divider stops short of the centre and of the rim (82); */
#define HUB_SPOKE_R1 72    /* together they cut the box it invalidates ~7x on 0..84 */

/* One frame's angle, and half a turn, where the opposite spoke sits. The model's
 * hub_frame rises with the phase (cassette.c:84-88), but the view walks the angle the
 * other way: LVGL measures from 3 o'clock with positive clockwise, so negating the
 * frame turns the reels counter-clockwise as the tape runs. That is the direction the
 * transport should read in, not the old art's clockwise marker (assets/hub/001-005). */
static const double k_hub_step = 2.0 * 3.14159265358979323846 / (double)HUB_FRAME_COUNT;
#define HUB_HALF_TURN 3.14159265358979323846

/* The tape path at the bottom. Each side leaves its pack on the outer tangent, wraps a
 * 30 px roller and joins a horizontal run between the two rollers - the stretch of tape
 * a cassette shows across its front. The rollers' bottoms sit STRAND_GUIDE_GAP px above
 * the screen edge and the run is at that height, so that much screen stays clear below
 * it. The centres put each roller's outer edge 16 px from the screen's side. */
#define STRAND_Y 466
#define STRAND_W 2
#define STRAND_GUIDE_D 38
#define STRAND_GUIDE_GAP 16
#define STRAND_GUIDE_X_L 39    /* 16 px margin + the roller's radius */
#define STRAND_GUIDE_X_R 761   /* 800 - 39 */
#define STRAND_RUN_Y (STRAND_Y - STRAND_GUIDE_GAP)
#define STRAND_WRAP_STEPS 10
#define STRAND_POINTS_MAX (2 + STRAND_WRAP_STEPS)

/* The roller in pixels. Its centre is derived from the run rather than from a scaled
 * STRAND_GUIDE_Y, and its diameter is rounded to an even number, so the tape's wrap ends
 * on the run's own row and on the roller's own rim. Scaling the design centre and the
 * design radius separately rounds them onto different pixels. */
#define CASSETTE_GUIDE_D (SC(STRAND_GUIDE_D) & ~1)
#define CASSETTE_GUIDE_R (CASSETTE_GUIDE_D / 2)
#define CASSETTE_GUIDE_Y (SCY(STRAND_RUN_Y) - CASSETTE_GUIDE_R)

/* The small label: the bar with the Now playing title. It is not a Level: the up
 * swipe at Bare has no Level to move to, so it peeks this bar and the cassette gives it
 * the room by going to CASSETTE_SCALE_LABEL, and the next up swipe or any Level change
 * puts it away. It rests at LABEL_Y and hides below the screen, so the show slides up
 * and the hide slides back. */
#define LABEL_X 116
#define LABEL_Y 424
#define LABEL_W 568
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
 * The title is a child of the cover; it sits on the empty band at the top of the art. The cover hides just above the screen,
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
 * title on the band below. The Case is opaque, so the cassette is covered rather than
 * redrawn. The title sits in the free band under the case,
 * clear of the window. */
#define ART_X 101
#define ART_Y 23
#define ART_W 603
#define ART_H 385
#define ART_TITLE_Y 424
#define ART_TITLE_W 720
#define ART_HIDDEN_Y (-480 - 8)
#endif

/* The palette, read off the reference, as 0xRRGGBB. lv_color_hex() is a function
 * (lv_color.h:283), so it cannot initialise a constant; COL_HEX() is the same split done
 * at compile time on top of LVGL's own aggregate initialiser (lv_color.h:215). The result
 * is a real lv_color_t, so no conversion runs where a colour is used. */
#define COL_HEX(c) LV_COLOR_MAKE(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

/* A darker 0xRRGGBB: `pct` percent of the original, folded at compile time so a shade is a
 * const like any other colour - COL_DARKEN_HEX(COL_PRIMARY, 70) is 0xA83F17. It takes a
 * literal, not a name, because the channels of an lv_color_t are not a constant expression
 * once it is an object; to darken a colour that is already an lv_color_t, LVGL has
 * lv_color_darken(). */
#define COL_DARKEN_HEX(c, pct)                                                        \
    COL_HEX((((((c) >> 16) & 0xFF) * (pct) / 100) << 16) |                            \
            (((((c) >> 8) & 0xFF) * (pct) / 100) << 8) |                              \
             ((((c) & 0xFF) * (pct) / 100)))

#define COL_BLACK 0x111111
#define COL_PRIMARY 0xF15A22

static const lv_color_t k_col_black        = COL_HEX(COL_BLACK);
static const lv_color_t k_col_bg        = COL_HEX(COL_BLACK);
/* The playing red: the hub's ring and divider, and the small label's ink. */
static const lv_color_t k_col_tape_dark = COL_HEX(COL_PRIMARY);
/* The tape's own body, and the dim end of the rewind fade: the hub ring cross-fades
 * from the playing red into this. */
static const lv_color_t k_col_tape      = COL_DARKEN_HEX(COL_PRIMARY, 80);
static const lv_color_t k_col_hub_ring  = COL_DARKEN_HEX(COL_PRIMARY, 10);
static const lv_color_t k_col_hub_ink   = COL_DARKEN_HEX(COL_PRIMARY, 80);

/* The VU meter, read off assets: a grid of the lit red at half strength, and segments
 * that are either the lit red or this dim red. There is no panel behind them - the
 * cassette shows through. */
static const lv_color_t k_col_meter_dim = COL_HEX(0x7F2C18);
static const lv_color_t k_col_meter_lit = COL_HEX(COL_PRIMARY);
static const lv_color_t k_col_meter_back = COL_DARKEN_HEX(COL_PRIMARY, 30);

/* The touch board's own Levels: its cover, its small label and the Artwork backdrop. They
 * live inside the guard because a board without those Levels never reads them. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
static const lv_color_t k_col_back      = COL_DARKEN_HEX(COL_PRIMARY, 20); /* the frames' border */
static const lv_color_t k_col_cover_bg  = COL_HEX(0xB48744); /* behind the cover's shapes */
static const lv_color_t k_col_cover_ink = COL_HEX(0xEEEEEE); /* the dark cover needs light ink */
static const lv_color_t k_col_art_bg    = COL_HEX(0x141414); /* until the Case lands */
static const lv_color_t k_col_paper     = COL_HEX(0x000000); /* the small label's fill */
static const lv_color_t k_col_primary   = COL_HEX(COL_PRIMARY); /* its border and title */
#endif

/* The hub ring is the playing indication: full red while a track plays, dimmed while a
 * reel change winds the tape back, back over its last second as the new side starts.
 * One second each way, over the eight-second window. */
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
static lv_obj_t *s_cassette;        /* the cassette's container: everything the body draws */
static lv_obj_t *s_rim[2];          /* the 168 px hub circle the rewind dims */
static lv_obj_t *s_pack[2], *s_strand[2];  /* the packs, and the two side tape paths */
static lv_obj_t *s_roller[2];               /* the two rollers under the tape */
static lv_obj_t *s_run;                    /* the horizontal run between the rollers */
static lv_point_precise_t s_run_pts[2];
#if CONFIG_APP_BOARD_SUNTON_8048S050C
static lv_obj_t *s_frame[2];        /* the cassette's two borders */
#endif
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
static lv_opa_t s_tape_opa = LV_OPA_COVER; /* the hub ring's; 0 through the reel change */

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

/* Lay the tape path for side `i` as one polyline: the external tangent from the pack to
 * its roller, then the wrap around the roller down to the horizontal run. The tangent
 * touches both circles at the same angle - the centre line's, turned by alpha, where
 * cos(alpha) = (R - r) / distance - and the wrap turns from that angle to the roller's
 * bottom. The points are screen coordinates; strand_set() sizes the object from them and
 * the draw callback asks for them again. */
static int strand_points(int i, lv_point_precise_t *pts)
{
    /* The path is scaled before the arithmetic, so the tangent and the wrap below read the
     * same at any scale. */
    double cx = SCX(k_hub[i].x), cy = SCY(k_hub[i].y), R = (double)SC(s_pack_d[i]) / 2.0;
    double gx = SCX(i ? STRAND_GUIDE_X_R : STRAND_GUIDE_X_L), gy = CASSETTE_GUIDE_Y;
    double gr = CASSETTE_GUIDE_R;
    double dx = gx - cx, dy = gy - cy;
    double len = sqrt(dx * dx + dy * dy);
    double phi = atan2(dy, dx);
    double alpha = acos((R - gr) / len);
    double a = phi + (i ? -alpha : alpha);  /* the outer tangent, left or right */
    double bottom = 0.5 * 3.14159265358979323846;
    double sweep = bottom - a;

    /* The wrap is always the short way round, from the tangent point to the bottom. */
    if (sweep > 3.14159265358979323846) sweep -= 2.0 * 3.14159265358979323846;
    if (sweep < -3.14159265358979323846) sweep += 2.0 * 3.14159265358979323846;

    int n = 0;
    pts[n].x = (lv_value_precise_t)lround(cx + R * cos(a));
    pts[n].y = (lv_value_precise_t)lround(cy + R * sin(a));
    n++;
    pts[n].x = (lv_value_precise_t)lround(gx + gr * cos(a));
    pts[n].y = (lv_value_precise_t)lround(gy + gr * sin(a));
    n++;
    for (int k = 1; k <= STRAND_WRAP_STEPS; k++) {
        double t = a + sweep * (double)k / (double)STRAND_WRAP_STEPS;
        pts[n].x = (lv_value_precise_t)lround(gx + gr * cos(t));
        pts[n].y = (lv_value_precise_t)lround(gy + gr * sin(t));
        n++;
    }
    return n;
}

/* The path's invalidation box is the bounding rect of the polyline, padded for the
 * stroke. It is as narrow as the tape: the horizontal run has its own object, so a fill
 * change repaints two narrow sides, not the whole bottom. */
static void strand_set(int i)
{
    lv_point_precise_t pts[STRAND_POINTS_MAX];
    int n = strand_points(i, pts);
    int pad = STRAND_W / 2 + 1;
    int x1 = (int)pts[0].x, x2 = x1, y1 = (int)pts[0].y, y2 = y1;

    for (int k = 1; k < n; k++) {
        x1 = LV_MIN(x1, (int)pts[k].x);
        x2 = LV_MAX(x2, (int)pts[k].x);
        y1 = LV_MIN(y1, (int)pts[k].y);
        y2 = LV_MAX(y2, (int)pts[k].y);
    }

    lv_obj_set_pos(s_strand[i], x1 - pad, y1 - pad);
    lv_obj_set_size(s_strand[i], x2 - x1 + 1 + 2 * pad, y2 - y1 + 1 + 2 * pad);
}

/* One polyline per side, as a single line draw task. lv_draw_line() copies the points, so
 * the array can live on the stack here. */
static void strand_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_point_precise_t pts[STRAND_POINTS_MAX];
    int n = strand_points(i, pts);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = k_col_tape;
    dsc.width = STRAND_W;
    dsc.opa = LV_OPA_COVER;
    dsc.points = pts;
    dsc.point_cnt = n;
    lv_draw_line(layer, &dsc);
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
    int x0 = cx + (int)lround(SC(HUB_SPOKE_R0) * cos(a));
    int y0 = cy + (int)lround(SC(HUB_SPOKE_R0) * sin(a));
    int x1 = cx + (int)lround(SC(HUB_SPOKE_R1) * cos(a));
    int y1 = cy + (int)lround(SC(HUB_SPOKE_R1) * sin(a));

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
    double a = -(double)frame * k_hub_step;   /* the reels turn counter-clockwise */

    for (int k = 0; k < 2; k++)
        spoke_set(&s_hub_spoke[i][k], SCX(k_hub[i].x), SCY(k_hub[i].y),
                  a + (k ? HUB_HALF_TURN : 0.0));
}

/* The outer frame: 16 px in from the screen, 360 px tall. The meter is centred in it
 * vertically, so its own centre is METER_Y. */
#define FRAME_X      16
#define FRAME_Y      16
#define FRAME_W      (800 - 32)
#define FRAME_H      360
#define FRAME_R      16

/* The VU meter: a lattice with one column of segments per channel filling from the
 * bottom, the mockup's geometry scaled 300/149 so the meter is 300 px tall. Segments are
 * 18 px wide, 8 tall, 10 apart, twelve to a column, the bottom one 209 px down the box;
 * the lattice is the sketch's 12 cells less two at each end, and there is no panel behind
 * either. The box is centred in the frame, so the grid lands on the frame's middle. */
#define METER_W      88      /* the 8-cell lattice plus a 4 px margin each side */
#define METER_H      300
#define METER_X      740     /* right edge on the frame's right edge */
#define METER_Y      (FRAME_Y + FRAME_H / 2)
#define METER_COLS   2        /* one bar per channel */
#define METER_CELLS  12
#define METER_SEG_W  18
#define METER_SEG_H  8
#define METER_PITCH  10
#define METER_BOTTOM 209      /* panel-relative y of the bottom segment's foot */
#define METER_GAP    12
#define METER_GRID_X 4        /* the lattice: 8 columns, 14 rows, 10 px apart, centred */
#define METER_GRID_Y 80
#define METER_GRID_C 8
#define METER_GRID_R 14

static lv_obj_t *s_meter;                          /* the panel; the grid is drawn in it */
static lv_obj_t *s_meter_seg[METER_COLS][METER_CELLS];
static int s_meter_lit[METER_COLS];                /* segments lit, so a redraw is rare */

/* The lattice behind the segments: one line per 10 px, the lit red at half strength,
 * which over black is the same value drawn opaque. */
static void meter_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    /* A draw callback works in screen coordinates, so the panel's origin is added to
     * the lattice's own offsets. */
    lv_area_t panel;
    lv_obj_get_coords(lv_event_get_target_obj(e), &panel);
    int x0 = panel.x1 + SC(METER_GRID_X), y0 = panel.y1 + SC(METER_GRID_Y);
    int pitch = SC(METER_PITCH);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = k_col_meter_back;
    dsc.width = 1;
    dsc.opa = LV_OPA_COVER;

    for (int c = 0; c <= METER_GRID_C; c++) {
        dsc.p1.x = x0 + c * pitch;
        dsc.p1.y = y0;
        dsc.p2.x = dsc.p1.x;
        dsc.p2.y = y0 + METER_GRID_R * pitch;
        lv_draw_line(layer, &dsc);
    }
    for (int r = 0; r <= METER_GRID_R; r++) {
        dsc.p1.x = x0;
        dsc.p1.y = y0 + r * pitch;
        dsc.p2.x = x0 + METER_GRID_C * pitch;
        dsc.p2.y = dsc.p1.y;
        lv_draw_line(layer, &dsc);
    }
}

/* A column's segments, lit from the bottom. The scale is dB over -48..0, as the old
 * meter used, so the bars move like a VU rather than following a linear amplitude. */
static void meter_level_set(int ch, float vu)
{
    if (!s_meter) return;

    float db = vu > 0.001f ? 20.0f * log10f(vu) : -48.0f;
    if (db < -48.0f) db = -48.0f;
    if (db > 0.0f) db = 0.0f;
    int lit = (int)lroundf((db + 48.0f) / 48.0f * METER_CELLS);

    if (lit == s_meter_lit[ch]) return;
    s_meter_lit[ch] = lit;
    for (int k = 0; k < METER_CELLS; k++) {
        lv_obj_set_style_bg_color(s_meter_seg[ch][k],
                                  k < lit ? k_col_meter_lit : k_col_meter_dim, 0);
    }
}

/* A pack's rings: the outer circle at the pack's radius, then one every PACK_RING_GAP
 * px inwards until the hub rim, which covers anything under it. All of them come from
 * one object's draw callback rather than one object per circle: the count follows the
 * fill, so a circle-per-object would resize and invalidate a dozen boxes on every frame
 * the tape moves, while this invalidates the pack's box once. The rings are the only
 * paint - the object has no background - so the screen shows through between them. */
static void pack_draw_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = k_col_tape;
    dsc.width = PACK_RING_W;
    dsc.opa = LV_OPA_COVER;
    dsc.center.x = SCX(k_hub[i].x);
    dsc.center.y = SCY(k_hub[i].y);
    dsc.start_angle = 0;
    dsc.end_angle = 360;

    /* The gap shrinks with the cassette, but a ring is a pixel: a gap of 0 would draw the
     * same circle for ever. */
    int gap = SC(PACK_RING_GAP);
    if (gap < 1) gap = 1;

    for (int r = SC(s_pack_d[i]) / 2; r > SC(HUB_RING_D) / 2; r -= gap) {
        dsc.radius = (uint16_t)r;
        lv_draw_arc(layer, &dsc);
    }
}

/* Where the pack sits and how big it is, for the diameter the fill last gave it. It is
 * split from pack_set() so a scale change can re-place a pack whose fill has not moved. */
static void pack_place(int i)
{
    int sd = SC(s_pack_d[i]);
    lv_obj_set_size(s_pack[i], sd, sd);
    lv_obj_set_pos(s_pack[i], SCX(k_hub[i].x) - sd / 2, SCY(k_hub[i].y) - sd / 2);
}

/* The pack's fill is the outer radius, and the rings are drawn from it in
 * pack_draw_cb. A size change invalidates the pack's old and new boxes, and the
 * callback then draws every ring for the new radius, so nothing is left stale. */
static void pack_set(int i, int d)
{
    if (d == s_pack_d[i]) return;
    s_pack_d[i] = d;
    pack_place(i);
    strand_set(i);
}

/* The pack area, and therefore the tape length on it, is proportional to the fill:
 * the diameter grows as the square root, the same relation the model uses for the
 * radius. An even diameter keeps the outer circle symmetric on the pixel grid. */
static int pack_diameter(uint16_t fill)
{
    double min = PACK_MIN_D, max = PACK_MAX_D;
    double d = sqrt(min * min + (max * max - min * min) * (double)fill / 1000.0);
    return ((int)lround(d)) & ~1;
}

/* ------------------------------------------------------------------ layout */

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* Put a widget at a design position and size. The frames are the board's, so this helper
 * is too: a board without them has no rectangle of its own to place. */
static void place_rect(lv_obj_t *o, int x, int y, int w, int h)
{
    lv_obj_set_pos(o, SCX(x), SCY(y));
    lv_obj_set_size(o, SC(w), SC(h));
}
#endif

/* Put a circle of a design diameter on a design centre. The object carries the circle
 * radius; only its box changes with the scale. */
static void place_disc(lv_obj_t *o, int cx, int cy, int d)
{
    lv_obj_set_pos(o, SCX(cx) - SC(d) / 2, SCY(cy) - SC(d) / 2);
    lv_obj_set_size(o, SC(d), SC(d));
}

/* Move every object of the cassette to where the pose puts it. The layout is a function
 * of s_pose alone, so this is the one place that has to run when the scale or the offset
 * changes; the draw callbacks read the same pose, so the rings and the lattice follow
 * without a pass of their own. The strands and the spokes are the exceptions worth
 * naming: their geometry is baked into their points, so they are asked for it again
 * here. */
static void cassette_place(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    place_rect(s_frame[0], FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
    lv_obj_set_style_radius(s_frame[0], SC(FRAME_R), 0);
    place_rect(s_frame[1], 122, 122, 556, 168);
    lv_obj_set_style_radius(s_frame[1], SC(16), 0);
#endif

    for (int i = 0; i < 2; i++) {
        pack_place(i);

        /* The roller's centre and radius are already pixels (see CASSETTE_GUIDE_*): the
         * tape's wrap ends on its bottom, so both have to come from the same pair. */
        int gd = CASSETTE_GUIDE_D;
        lv_obj_set_pos(s_roller[i], SCX(i ? STRAND_GUIDE_X_R : STRAND_GUIDE_X_L) - gd / 2,
                       CASSETTE_GUIDE_Y - gd / 2);
        lv_obj_set_size(s_roller[i], gd, gd);

        strand_set(i);
    }

    /* The run is a line whose points are its own coordinates: the object stays at 0,0 and
     * the points are the screen's. */
    s_run_pts[0].x = SCX(STRAND_GUIDE_X_L);
    s_run_pts[0].y = SCY(STRAND_RUN_Y);
    s_run_pts[1].x = SCX(STRAND_GUIDE_X_R);
    s_run_pts[1].y = SCY(STRAND_RUN_Y);
    lv_line_set_points(s_run, s_run_pts, 2);

    lv_obj_set_pos(s_meter, SCX(METER_X) - SC(METER_W) / 2,
                   SCY(METER_Y) - SC(METER_H) / 2);
    lv_obj_set_size(s_meter, SC(METER_W), SC(METER_H));

    /* The segments are children of the meter, so their offsets are scaled without the
     * design box's own origin. */
    int bar_x = (METER_W - (METER_COLS * METER_SEG_W + (METER_COLS - 1) * METER_GAP)) / 2;
    int seg_pitch = SC(METER_PITCH), seg_w = SC(METER_SEG_W), seg_h = SC(METER_SEG_H);
    int seg_gap = SC(METER_GAP), seg_x0 = SC(bar_x), seg_y0 = SC(METER_BOTTOM);
    for (int ch = 0; ch < METER_COLS; ch++) {
        for (int k = 0; k < METER_CELLS; k++) {
            lv_obj_set_pos(s_meter_seg[ch][k], seg_x0 + ch * (seg_w + seg_gap),
                           seg_y0 - k * seg_pitch - seg_h);
            lv_obj_set_size(s_meter_seg[ch][k], seg_w, seg_h);
        }
    }

    for (int i = 0; i < 2; i++) {
        place_disc(s_rim[i], k_hub[i].x, k_hub[i].y, HUB_RING_D);
        hub_frame_set(i, s_shown_hub[i]);
    }
}

/* A change of place is one animation, not three: LVGL eases a 0..1024 step and the step
 * mixes the pose, so a size change and a move cannot drift apart. At the end of the
 * animation the step is exactly 1024, so the pose lands on its target with no rounding
 * left behind. */
static cassette_pose_t s_pose_from;   /* the pose the change in flight started at */
static cassette_pose_t s_pose_to;     /* and the one it is heading for */

static int pose_lerp(int from, int to, int32_t step)
{
    return from + (int)(((int64_t)(to - from) * step) / 1024);
}

static void pose_anim_cb(void *var, int32_t step)
{
    (void)var;
    s_pose.scale = pose_lerp(s_pose_from.scale, s_pose_to.scale, step);
    s_pose.dx    = pose_lerp(s_pose_from.dx, s_pose_to.dx, step);
    s_pose.dy    = pose_lerp(s_pose_from.dy, s_pose_to.dy, step);
    cassette_place();
}

void cassette_view_set_scale(int scale, int offset_x, int offset_y, uint32_t ms)
{
    if (!s_cassette) return;
    if (scale < 32) scale = 32;      /* below this the tape and the rings stop reading */
    if (scale > 1024) scale = 1024;

    /* A change in flight is replaced, not stacked, and the new one starts from the pose
     * that is on screen now - the same way the backdrop fade reverses. */
    bool was_animating = lv_anim_delete(s_cassette, pose_anim_cb);
    if (!was_animating && scale == s_pose.scale &&
        offset_x == s_pose.dx && offset_y == s_pose.dy) return;

    if (ms == 0) {
        s_pose.scale = scale;
        s_pose.dx = offset_x;
        s_pose.dy = offset_y;
        cassette_place();
        return;
    }

    s_pose_from = s_pose;
    s_pose_to.scale = scale;
    s_pose_to.dx = offset_x;
    s_pose_to.dy = offset_y;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cassette);
    lv_anim_set_exec_cb(&a, pose_anim_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, ms);
    /* The small label's peek drives this today, so the ease is the bar's: the bar and the
     * cassette arrive as one picture. */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* The rewind envelope, from the model's permille: full at the window's start, zero over
 * its first second, zero for the body, and back over its last second as the new side
 * starts. The permille is linear in time, so the ramp is a plain interpolation. */
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

/* Push the envelope into the hub rings: the full red at full opacity, the dim red at
 * zero. Only the border changes, so each ring invalidates its own 168 px circle and
 * nothing else. */
static void tape_opa_apply(lv_opa_t opa)
{
    s_tape_opa = opa;
    for (int i = 0; i < 2; i++) {
        if (!s_rim[i]) continue;
        lv_obj_set_style_border_color(s_rim[i],
                                      lv_color_mix(k_col_tape_dark, k_col_tape, opa), 0);
    }
}

/* Apply a model frame: only what changed is touched. */
static void apply_frame(const cassette_frame_t *f)
{
    if (!f) return;

    /* The reel change owns the hub ring: it dims over the window's first second, stays
     * dim, and comes back over its last as the new side starts. Do this before the rest
     * of the frame draws, so this frame's redraw already carries the new colour. */
    lv_opa_t tape_opa = rewind_tape_opa(f);
    if (tape_opa != s_tape_opa) {
        tape_opa_apply(tape_opa);
        if (tape_opa == LV_OPA_TRANSP)
            ESP_LOGI(TAG, "rewind: the hub ring dims");
        else if (tape_opa == LV_OPA_COVER)
            ESP_LOGI(TAG, "rewind: the hub ring back to the playing red");
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

    for (int i = 0; i < 2; i++) {
        if (f->hub_frame[i] != s_shown_hub[i]) {
            s_shown_hub[i] = f->hub_frame[i];
            hub_frame_set(i, s_shown_hub[i]);
        }
    }

    /* The meter: one column per channel. */
    meter_level_set(0, f->vu_l);
    meter_level_set(1, f->vu_r);

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

    /* The bar and the cassette move together: the cassette gives the bar its room, so the
     * label never covers the tape. Same duration, so the two arrive as one. */
    cassette_view_set_scale(show ? CASSETTE_SCALE_LABEL : CASSETTE_SCALE, 0, show ? -60:0,
                            PAPER_ANIM_MS);

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
    /* A Level change is one frame: the cassette goes back to its resting size with it,
     * not on the bar's slide. */
    cassette_view_set_scale(CASSETTE_SCALE, 0, 0, 0);
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

    /* The cassette's own container: the body's objects are its children, not the screen's,
     * so the picture is one thing. It is full-bleed at 0,0 and has neither fill nor border,
     * which keeps every coordinate below a screen coordinate and lets the screen's own
     * backdrop show through. It takes no press: make_decorative() clears that with the
     * rest of the tree. */
    s_cassette = lv_obj_create(scr);
    lv_obj_remove_style_all(s_cassette);
    lv_obj_set_pos(s_cassette, 0, 0);
    lv_obj_set_size(s_cassette, 800, 480);
    lv_obj_set_style_pad_all(s_cassette, 0, 0);
    lv_obj_set_style_bg_opa(s_cassette, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cassette, 0, 0);
    lv_obj_set_scrollable(s_cassette, false);
    lv_obj_set_clickable(s_cassette, false);

    /* Every part of the cassette below is created with its styles and no geometry:
     * cassette_place() is the one place that knows where the scale puts it, and it runs
     * once the picture is complete. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    s_frame[0] = border_box(s_cassette, 0, 0, 1, 1, 0, k_col_bg, 255, k_col_back, 1);
    s_frame[1] = border_box(s_cassette, 0, 0, 1, 1, 0, k_col_bg, 255, k_col_back, 1);
#endif
    /* The tape: two packs, two bottom rollers, the run between them and the two side
     * paths. A pack's box is transparent and carries no border; its rings are the draw
     * callback's and nothing else, which is also why it is created here and then updated
     * by pack_set(). */
    for (int i = 0; i < 2; i++) {
        s_pack_d[i] = (i == 0) ? PACK_MAX_D : PACK_MIN_D;
        s_pack[i] = lv_obj_create(s_cassette);
        lv_obj_remove_style_all(s_pack[i]);
        lv_obj_set_style_bg_opa(s_pack[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_pack[i], 0, 0);
        lv_obj_set_scrollable(s_pack[i], false);
        lv_obj_add_event_cb(s_pack[i], pack_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
    }

    /* The rollers, under the tape: a ring whose bottom is STRAND_GUIDE_GAP px above the
     * screen edge. Its middle is empty - the tape runs round its edge. */
    for (int i = 0; i < 2; i++) {
        s_roller[i] = box(s_cassette, 0, 0, 1, 1, LV_RADIUS_CIRCLE, k_col_bg);
        lv_obj_set_style_bg_opa(s_roller[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_roller[i], k_col_tape, 0);
        lv_obj_set_style_border_opa(s_roller[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_roller[i], 1, 0);
    }

    /* The run between the rollers, at their bottom. It is static: only the two side
     * paths change with the fill. */
    s_run = lv_line_create(s_cassette);
    lv_obj_remove_style_all(s_run);
    lv_obj_set_style_line_color(s_run, k_col_tape, 0);
    lv_obj_set_style_line_width(s_run, STRAND_W, 0);

    /* The side paths, over the rollers: off the pack on the outer tangent, round the
     * roller and into the run. */
    for (int i = 0; i < 2; i++) {
        s_strand[i] = lv_obj_create(s_cassette);
        lv_obj_remove_style_all(s_strand[i]);
        lv_obj_set_style_bg_opa(s_strand[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_strand[i], 0, 0);
        lv_obj_set_scrollable(s_strand[i], false);
        lv_obj_add_event_cb(s_strand[i], strand_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
    }

    /* The VU meter, top right. It is created before the hubs, so anything drawn later
     * sits over it; the lattice is its draw callback and the segments are objects, so a
     * level change repaints a few cells and nothing else. */
    s_meter = lv_obj_create(s_cassette);
    lv_obj_remove_style_all(s_meter);
    lv_obj_set_style_bg_opa(s_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_meter, 0, 0);
    lv_obj_set_style_pad_all(s_meter, 0, 0);
    lv_obj_set_scrollable(s_meter, false);
    lv_obj_set_clickable(s_meter, false);
    lv_obj_add_event_cb(s_meter, meter_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    for (int ch = 0; ch < METER_COLS; ch++) {
        for (int k = 0; k < METER_CELLS; k++) {
            lv_obj_t *seg = box(s_meter, 0, 0, 1, 1, 0, k_col_meter_dim);
            lv_obj_set_clickable(seg, false);
            s_meter_seg[ch][k] = seg;
        }
        s_meter_lit[ch] = 0;
    }

    /* The hubs: the rim is painted once and never invalidated. Only the two spokes
     * move, and each is its own object so its bounding box stays small. */
    for (int i = 0; i < 2; i++) {
        s_rim[i] = box(s_cassette, 0, 0, 1, 1, LV_RADIUS_CIRCLE, k_col_hub_ring);
        lv_obj_set_style_border_color(s_rim[i], k_col_black, 0);
        lv_obj_set_style_border_opa(s_rim[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_rim[i], HUB_RIM_W, 0);

        for (int k = 0; k < 2; k++) {
            lv_obj_t *sp = lv_line_create(s_cassette);
            lv_obj_remove_style_all(sp);
            lv_obj_set_style_line_color(sp, k_col_hub_ink, 0);
            lv_obj_set_style_line_width(sp, HUB_SPOKE_W, 0);
            lv_obj_set_style_line_rounded(sp, true, 0);
            s_hub_spoke[i][k].obj = sp;
        }

        s_shown_hub[i] = 0;
    }

    /* The picture is built: put it where the starting scale says it goes. */
    cassette_place();

    /* The small label: the cream bar the up swipe at Bare peeks. Its title is its
     * child, so the bar and the title move as one. It hides above the screen until
     * that swipe, and any Level change puts it away. It is not a Level. */

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    s_paper = border_box(scr, LABEL_X, LABEL_Y, LABEL_W, LABEL_H, LABEL_R,
                         k_col_paper, 200, k_col_primary, 2);
    lv_obj_set_y(s_paper, PAPER_HIDDEN_Y);
    lv_obj_set_hidden(s_paper, true);
    s_title = text(s_paper, &jura_36, k_col_primary,
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
    s_cover_title = text(s_cover, &jura_36, k_col_cover_ink,
                         LV_ALIGN_TOP_LEFT, COVER_TITLE_X, COVER_TITLE_Y);
    lv_label_set_long_mode(s_cover_title, LV_LABEL_LONG_SCROLL);
    lv_obj_set_width(s_cover_title, COVER_TITLE_W);
#endif




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

    s_art_title = text(s_art_sheet, &jura_36, k_col_cover_ink,
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
