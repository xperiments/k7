/*
 * The cassette's tape body; see cassette_tapes.h.
 *
 * Every number is on the design grid the layout module owns, and the pose scales it on
 * the way out. The hubs are 168 px across, the packs stay the reference's 245/310, and a
 * pack is a set of concentric line circles rather than a filled disc: its outer circle is
 * the fill's diameter and each inner one sits PACK_RING_GAP px inside the one before it,
 * stopping at the hub rim, which covers what is under it.
 *
 * The tape leaves each pack on the outer tangent, wraps a 30 px roller whose bottom is
 * 16 px above the screen edge, and runs along the bottom between the two rollers - the
 * stretch a cassette shows across its front.
 *
 * The model's frame and the pose are the module's only inputs: the fill, the reel phase
 * and the tape's ink are the model's, the pixel geometry is the design grid's. Nothing
 * here re-derives either.
 */
#include <math.h>
#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette.h"
#include "cassette_layout.h"
#include "cassette_style.h"
#include "cassette_tapes.h"
#include "cassette_widgets.h"

static const char *TAG = "screen";

/* The pose the objects were last placed at. The view used to own it; the module keeps the
 * one its place() was given, because the draw callbacks work in screen space and are
 * handed no pose - they read the same numbers the placement did. 256 (LV_SCALE_NONE) is
 * the design grid, pixel for pixel. */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };

#define SC(v)  cassette_len(s_pose, (v))             /* a design length, in pixels */
#define SCX(x) cassette_x(s_pose, (x))               /* a design x, in screen pixels */
#define SCY(y) cassette_y(s_pose, (y))               /* a design y, in screen pixels */

/* The 800x480 panel: the hub rim is 168 px. The packs keep the reference's range
 * (PACK_MIN_D..PACK_MAX_D) so the tape rings the hub at both ends of a side. A pack is
 * not a filled disc: its diameter is the outer circle's, drawn as a line, and each inner
 * circle sits PACK_RING_GAP px inside the one before it. Only the circles outside the rim
 * are drawn - the rim covers what is under it - so an empty pack, whose radius is the
 * rim's, draws none, and a full one draws the lot. */
static const struct { int x, y; } k_hub[2] = { { 216-6, 206 }, { 600-8+2, 206 } };
#define HUB_RING_D 168
#define PACK_MIN_D 168
#define PACK_MAX_D 356
#define PACK_RING_GAP 10
#define PACK_RING_W 1

/* The drawn hub: a rim with a divided circle turning inside it. The frame count is the
 * angular resolution of the animation. */
#define HUB_FRAME_COUNT CASSETTE_TAPES_FRAME_COUNT
#define HUB_RIM_W 2
#define HUB_SPOKE_W 4
#define HUB_SPOKE_R0 66    /* the divider stops short of the centre and of the rim (82); */
#define HUB_SPOKE_R1 72    /* together they cut the box it invalidates ~7x on 0..84 */

/* One frame's angle, and half a turn, where the opposite spoke sits. The model's
 * hub_frame rises with the phase, but the drawing walks the angle the other way: LVGL
 * measures from 3 o'clock with positive clockwise, so negating the frame turns the reels
 * counter-clockwise as the tape runs. That is the direction the transport should read
 * in, not the old art's clockwise marker (assets/hub/001-005). */
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
#define CASSETTE_GUIDE_D (cassette_len_even(s_pose, STRAND_GUIDE_D))
#define CASSETTE_GUIDE_R (CASSETTE_GUIDE_D / 2)
#define CASSETTE_GUIDE_Y (SCY(STRAND_RUN_Y) - CASSETTE_GUIDE_R)

/* The outer frame: 16 px in from the screen, 360 px tall. */
#define FRAME_X      16
#define FRAME_Y      16
#define FRAME_W      (800 - 32)
#define FRAME_H      360
#define FRAME_R      16

typedef struct {
    lv_obj_t *obj;
    lv_point_precise_t pts[2];
} hub_spoke_t;

static hub_spoke_t s_hub_spoke[2][2];
static lv_obj_t *s_pack[2], *s_strand[2];  /* the packs, and the two side tape paths */
static lv_obj_t *s_roller[2];               /* the two rollers under the tape */
static lv_obj_t *s_run;                    /* the horizontal run between the rollers */
static lv_point_precise_t s_run_pts[2];
static lv_obj_t *s_rim[2];                  /* the 168 px hub circle the rewind dims */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
static lv_obj_t *s_frame[2];        /* the cassette's two borders; the board's only */
/* The three small rounded boxes along the bottom; the board's only, like the frames. */
static const struct { int x, y, w, h; } k_chip[3] = {
    { 120, 440, 50, 20 },
    { 360, 430, 80, 30 },
    { 800 - 120 - 50, 440, 50, 20 },
};
#define CHIP_R 8
static lv_obj_t *s_chip[3];
#endif
static int      s_pack_d[2];      /* px, so a redraw happens only when it changes */
static uint8_t  s_shown_hub[2];   /* the animation frame currently on screen */
/* The ink currently on the rims, at the resolution the border mix takes: tape_ink 1 is
 * LV_OPA_COVER. Like the view's cache it starts full, so a first frame that is not a reel
 * change writes nothing - and the rims, which are created with black borders, take the
 * playing red the first time the ink moves. */
static lv_opa_t s_tape_ink = LV_OPA_COVER;

/* ------------------------------------------------------------------ helpers */

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
    return cassette_even((int)lround(d));
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

void cassette_tapes_place(cassette_pose_t pose)
{
    s_pose = pose;

    if (!s_run) return;   /* nothing has been built yet */

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    place_rect(s_frame[0], FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
    lv_obj_set_style_radius(s_frame[0], SC(FRAME_R), 0);
    place_rect(s_frame[1], 122, 122, 556, 168);
    lv_obj_set_style_radius(s_frame[1], SC(16), 0);

    for (int i = 0; i < 3; i++) {
        place_rect(s_chip[i], k_chip[i].x, k_chip[i].y, k_chip[i].w, k_chip[i].h);
        lv_obj_set_style_radius(s_chip[i], SC(CHIP_R), 0);
    }
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

    for (int i = 0; i < 2; i++) {
        place_disc(s_rim[i], k_hub[i].x, k_hub[i].y, HUB_RING_D);
        hub_frame_set(i, s_shown_hub[i]);
    }
}

/* ------------------------------------------------------- the model's frame */

void cassette_tapes_hub_anchor(int index, bool top, int *x, int *y)
{
    if (!x || !y) return;
    if (index < 0 || index > 1) index = 0;
    int r = SC(HUB_RING_D) / 2;
    *x = SCX(k_hub[index].x);
    *y = SCY(k_hub[index].y) + (top ? -r : r);
}

/* Push the tape's ink into the hub rims: the playing red at LV_OPA_COVER, the tape's own
 * colour at LV_OPA_TRANSP. Only the border changes, so each rim invalidates its own
 * 168 px circle and nothing else. */
static void tape_ink_apply(lv_opa_t ink)
{
    s_tape_ink = ink;
    for (int i = 0; i < 2; i++) {
        if (!s_rim[i]) continue;
        lv_obj_set_style_border_color(s_rim[i],
                                      lv_color_mix(k_col_tape_dark, k_col_tape, ink), 0);
    }
}

/* Apply a model frame: only what changed is touched. */
void cassette_tapes_set(const cassette_frame_t *frame)
{
    if (!frame || !s_run) return;

    /* The tape's ink is the model's now: 1 is the playing red, 0 the tape's own colour,
     * and the fade between them is the model's rule. Do this before the packs and the
     * hubs are written, so this frame's redraw already carries the new colour. */
    lv_opa_t ink = (lv_opa_t)(frame->tape_ink * LV_OPA_COVER);
    if (ink != s_tape_ink) {
        tape_ink_apply(ink);
        if (ink == LV_OPA_TRANSP)
            ESP_LOGI(TAG, "rewind: the hub ring dims");
        else if (ink == LV_OPA_COVER)
            ESP_LOGI(TAG, "rewind: the hub ring back to the playing red");
    }

    for (int i = 0; i < 2; i++) {
        if (frame->hub_frame[i] != s_shown_hub[i]) {
            s_shown_hub[i] = frame->hub_frame[i];
            hub_frame_set(i, s_shown_hub[i]);
        }
    }

    /* Pack fill is the model's; the diameter is the screen's. Both packs use the
     * same range. */
    pack_set(0, pack_diameter(frame->pack_fill[0]));
    pack_set(1, pack_diameter(frame->pack_fill[1]));
}

/* ------------------------------------------------------------------- build */

void cassette_tapes_create(lv_obj_t *parent)
{
    /* Every part is created with its styles and no geometry: cassette_tapes_place() is
     * the one place that knows where the scale puts it, and it runs once the body is
     * complete. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The two rounded rectangles the body is drawn on. */
    s_frame[0] = cassette_border_box(parent, 0, 0, 1, 1, 0, k_col_frame, 0, k_col_back, 1);
    s_frame[1] = cassette_border_box(parent, 0, 0, 1, 1, 0, k_col_frame, 0, k_col_back, 1);

    /* The three small boxes along the bottom are objects too, placed by the pose in
     * cassette_tapes_place(); a fixed position here would not scale with the cassette. */
    for (int i = 0; i < 3; i++)
        s_chip[i] = cassette_border_box(parent, 0, 0, 1, 1, 0, k_col_frame, 255,
                                        k_col_tape_dark, 1);
#endif
    /* The tape: two packs, two bottom rollers, the run between them and the two side
     * paths. A pack's box is transparent and carries no border; its rings are the draw
     * callback's and nothing else, which is also why it is created here and then updated
     * by pack_set(). */
    for (int i = 0; i < 2; i++) {
        s_pack_d[i] = (i == 0) ? PACK_MAX_D : PACK_MIN_D;
        s_pack[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_pack[i]);
        lv_obj_set_style_bg_opa(s_pack[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_pack[i], 0, 0);
        lv_obj_set_scrollable(s_pack[i], false);
        lv_obj_set_clickable(s_pack[i], false);
        lv_obj_add_event_cb(s_pack[i], pack_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
    }

    /* The rollers, under the tape: a ring whose bottom is STRAND_GUIDE_GAP px above the
     * screen edge. Its middle is empty - the tape runs round its edge. */
    for (int i = 0; i < 2; i++) {
        s_roller[i] = cassette_box(parent, 0, 0, 1, 1, LV_RADIUS_CIRCLE, k_col_bg);
        lv_obj_set_style_bg_opa(s_roller[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_roller[i], k_col_tape, 0);
        lv_obj_set_style_border_opa(s_roller[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_roller[i], 1, 0);
    }

    /* The run between the rollers, at their bottom. It is static: only the two side
     * paths change with the fill. */
    s_run = lv_line_create(parent);
    lv_obj_remove_style_all(s_run);
    lv_obj_set_style_line_color(s_run, k_col_tape, 0);
    lv_obj_set_style_line_width(s_run, STRAND_W, 0);
    lv_obj_set_clickable(s_run, false);

    /* The side paths, over the rollers: off the pack on the outer tangent, round the
     * roller and into the run. */
    for (int i = 0; i < 2; i++) {
        s_strand[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_strand[i]);
        lv_obj_set_style_bg_opa(s_strand[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_strand[i], 0, 0);
        lv_obj_set_scrollable(s_strand[i], false);
        lv_obj_set_clickable(s_strand[i], false);
        lv_obj_add_event_cb(s_strand[i], strand_draw_cb, LV_EVENT_DRAW_MAIN_END,
                            (void *)(intptr_t)i);
    }

    /* The hubs: the rim is painted once and never invalidated. Only the two spokes
     * move, and each is its own object so its bounding box stays small. */
    for (int i = 0; i < 2; i++) {
        s_rim[i] = cassette_box(parent, 0, 0, 1, 1, LV_RADIUS_CIRCLE, k_col_hub_ring);
        lv_obj_set_style_border_color(s_rim[i], k_col_black, 0);
        lv_obj_set_style_border_opa(s_rim[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_rim[i], HUB_RIM_W, 0);

        for (int k = 0; k < 2; k++) {
            lv_obj_t *sp = lv_line_create(parent);
            lv_obj_remove_style_all(sp);
            lv_obj_set_style_line_color(sp, k_col_hub_ink, 0);
            lv_obj_set_style_line_width(sp, HUB_SPOKE_W, 0);
            lv_obj_set_style_line_rounded(sp, true, 0);
            lv_obj_set_clickable(sp, false);
            s_hub_spoke[i][k].obj = sp;
        }

        s_shown_hub[i] = 0;
    }

    /* The body is built: put it where the pose it holds says it goes. */
    cassette_tapes_place(s_pose);
}
