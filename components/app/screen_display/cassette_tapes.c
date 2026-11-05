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

#include "screen_caps.h"
#include "cassette_widgets.h"

static const char *TAG = "screen";

/* The pose the objects were last placed at. The view used to own it; the module keeps the
 * one its place() was given, because the draw callbacks work in screen space and are
 * handed no pose - they read the same numbers the placement did. 256 (LV_SCALE_NONE) is
 * the design grid, pixel for pixel. */
static cassette_pose_t s_pose = { CASSETTE_SCALE_NONE, 0, 0 };

/* The window the packs' boxes clamp to while the white page's Card is up, in panel
 * pixels. (0,0,0,0) is no clamp; pack_place() has the why. */
static cassette_rect_t s_window;

#define SC(v)  cassette_len(s_pose, (v))             /* a design length, in pixels */
#define SCX(x) cassette_x(s_pose, (x))               /* a design x, in screen pixels */
#define SCY(y) cassette_y(s_pose, (y))               /* a design y, in screen pixels */

/* The 800x480 panel: the hub rim is 168 px. The packs keep the reference's range
 * (PACK_MIN_D..PACK_MAX_D) so the tape rings the hub at both ends of a side. A pack is
 * not a filled disc: its diameter is the outer circle's, drawn as a line, and each inner
 * circle sits PACK_RING_GAP px inside the one before it. Only the circles outside the rim
 * are drawn - the rim covers what is under it - so an empty pack, whose radius is the
 * rim's, draws none, and a full one draws the lot. */
/* The hubs' design coordinates and the rim's diameter are cassette_layout's: the Peek puts a
 * tether on the same circle, and two parts reading one number is what keeps the line on the
 * rim. */
#define HUB_X(i)    ((i) ? CASSETTE_HUB_X1 : CASSETTE_HUB_X0)
#define HUB_Y       CASSETTE_HUB_Y
#define HUB_RING_D  CASSETTE_HUB_RING_D
#define PACK_MIN_D 168
#define PACK_MAX_D 356
#define PACK_RING_GAP 10
#define PACK_RING_W 1

/* The drawn hub: a rim with a divided circle turning inside it. The frame count is the
 * angular resolution of the animation. */
#define HUB_FRAME_COUNT CASSETTE_TAPES_FRAME_COUNT
#define HUB_RIM_W 2
/* The full-screen picture's hub: a thick white rim on the same disc. At the entry pose the hub
 * is 168 px across, so 16 px is the weight the owner asked for after looking at the window
 * there; it leaves 68 px of plastic inside it. */
#define HUB_RIM_PREVIEW_W 16
#define HUB_SPOKE_W 4
#define HUB_SPOKE_R0 66    /* the divider stops short of the centre and of the rim (82); */
#define HUB_SPOKE_R1 72    /* together they cut the box it invalidates ~7x on 0..84 */

/* The full-screen picture's divider: four marks at 90 degrees, twice the body's weight - the
 * teeth a hub has when the window is 556 px across and the hub 168. They stop at 62 so an
 * 8 px stroke's round cap (4 px) lands at 66, just inside the 16 px rim's inner edge at 68. */
#define HUB_SPOKE_PREVIEW_COUNT 4
#define HUB_SPOKE_PREVIEW_W 8
#define HUB_SPOKE_PREVIEW_R0 50
#define HUB_SPOKE_PREVIEW_R1 62
#define HUB_SPOKE_MAX 4

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

static hub_spoke_t s_hub_spoke[2][HUB_SPOKE_MAX];
static lv_obj_t *s_pack[2], *s_strand[2];  /* the packs, and the two side tape paths */
static lv_obj_t *s_roller[2];               /* the two rollers under the tape */
static lv_obj_t *s_run;                    /* the horizontal run between the rollers */
static lv_point_precise_t s_run_pts[2];
static lv_obj_t *s_rim[2];                  /* the 168 px hub circle the rewind dims */
/* The cassette's two border frames and the three chips along the bottom: the body is drawn on
 * them, and they exist where the panel's RGB path needs them - `k_col_frame` (FF00FF) masks its
 * overscan. On a diagnostic framebuffer they would be magenta lines round a cassette, so the
 * build asks screen_caps()->panel_borders and builds none. */
static lv_obj_t *s_frame[2];
static const struct { int x, y, w, h; } k_chip[3] = {
    { 120, 440, 50, 20 },
    { 360, 430, 80, 30 },
    { 800 - 120 - 50, 440, 50, 20 },
};
#define CHIP_R 8
static lv_obj_t *s_chip[3];
static int      s_pack_d[2];      /* px, so a redraw happens only when it changes */
static uint8_t  s_shown_hub[2];   /* the animation frame currently on screen */
/* The ink currently on the rims, at the resolution the border mix takes: tape_ink 1 is
 * LV_OPA_COVER. Like the view's cache it starts full, so a first frame that is not a reel
 * change writes nothing - and the rims are created with the playing red, so the state the
 * cache claims is what is on screen from the first frame. (A black border here would only
 * be visible on the controls view's white page, where it would read as an ink the cassette
 * does not have.) */
static lv_opa_t s_tape_ink = LV_OPA_COVER;

/* The hubs' lines are white while a Track's picture is printed on the Card - the rim's border
 * and the spokes - and the tape's own inks otherwise. The flag is the Cover's, the ink is this
 * module's. */
static bool s_hubs_white;

/* The full-screen picture is up: the hubs are drawn as the owner wants the preview - a thick
 * rim round the disc, divider and all - and each pack as the tape's edge once, not the ring
 * stack. */
static bool s_preview;

/* The tape's paint for the mode in force: the cassette's own for the body, the owner's flat
 * red for the full-screen picture. */
static lv_color_t tape_colour(void)
{
    return s_preview ? k_col_tape_preview : k_col_tape;
}

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
    double cx = SCX(HUB_X(i)), cy = SCY(HUB_Y), R = (double)SC(s_pack_d[i]) / 2.0;
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

/* ------------------------------------------------------- the window's slice */

/* What the slice above the Card has to repaint since the last take. The slice is a second
 * copy of the tape, so LVGL cannot know that a change *under* the Card is a change there:
 * the tapes record the screen rectangles a frame moved, and the view hands them to the cover.
 *
 * Sizing this generously is what cost the stream its audio the first time: invalidating the
 * whole 291x87 capsule rebuilt two 50 KB ARGB corner layers and redrew ten rings twice on
 * every 40 ms tick. A spoke tick is a box a few dozen pixels across. */
#define SLICE_DIRTY_MAX CASSETTE_TAPES_SLICE_DIRTY_MAX
static lv_area_t s_dirty[SLICE_DIRTY_MAX];
static int s_dirty_n;
static lv_area_t s_pack_box[2];         /* each pack's box as last placed */

static void slice_dirty(const lv_area_t *a)
{
    if (!a || a->x2 < a->x1 || a->y2 < a->y1) return;

    for (int i = 0; i < s_dirty_n; i++) {
        if (s_dirty[i].x1 <= a->x1 && s_dirty[i].y1 <= a->y1 &&
            s_dirty[i].x2 >= a->x2 && s_dirty[i].y2 >= a->y2) {
            return;   /* already inside one of them */
        }
    }
    if (s_dirty_n < SLICE_DIRTY_MAX) {
        s_dirty[s_dirty_n++] = *a;
        return;
    }

    /* More changes than the list holds: one window is cheaper than dropping one. */
    if (s_window.w > 0 && s_window.h > 0) {
        lv_area_t w = { s_window.x, s_window.y,
                        s_window.x + s_window.w - 1, s_window.y + s_window.h - 1 };
        s_dirty[0] = w;
        s_dirty_n = 1;
    }
}

int cassette_tapes_take_slice_dirty(lv_area_t *out, int max)
{
    if (!out || max <= 0) {
        s_dirty_n = 0;
        return 0;
    }

    int n = s_dirty_n < max ? s_dirty_n : max;
    for (int i = 0; i < n; i++) out[i] = s_dirty[i];
    s_dirty_n = 0;
    return n;
}

/* The box the two spokes of hub `i` sweep through at `frame`, padded for the stroke and its
 * caps. The old and the new frame are unioned by the caller, because a spoke that moved must
 * repaint where it was as well as where it is. */
static void spokes_box(int i, uint8_t frame, lv_area_t *a)
{
    double base = -(double)frame * k_hub_step;
    int cx = SCX(HUB_X(i)), cy = SCY(HUB_Y);
    int pad = HUB_SPOKE_W / 2 + 2;
    int x1 = cx, x2 = cx, y1 = cy, y2 = cy;

    for (int k = 0; k < 2; k++) {
        double t = base + (k ? HUB_HALF_TURN : 0.0);
        int px = cx + (int)lround(SC(HUB_SPOKE_R1) * cos(t));
        int py = cy + (int)lround(SC(HUB_SPOKE_R1) * sin(t));
        x1 = LV_MIN(x1, px);
        x2 = LV_MAX(x2, px);
        y1 = LV_MIN(y1, py);
        y2 = LV_MAX(y2, py);
    }

    a->x1 = x1 - pad;
    a->y1 = y1 - pad;
    a->x2 = x2 + pad;
    a->y2 = y2 + pad;
}

/* A hub's whole box: the rim's ring and the spokes. What a change of the hub's ink dirties. */
static void hub_box(int i, lv_area_t *a)
{
    int d = SC(HUB_RING_D), cx = SCX(HUB_X(i)), cy = SCY(HUB_Y);
    a->x1 = cx - d / 2 - HUB_RIM_W;
    a->y1 = cy - d / 2 - HUB_RIM_W;
    a->x2 = cx + d / 2 + HUB_RIM_W;
    a->y2 = cy + d / 2 + HUB_RIM_W;
}

/* Whether a ring of radius `r`, `w` px wide, centred at (cx,cy), can meet the layer's clip
 * area at all. Ten rings per pack, drawn once per band: a 12 px box next to one hub meets
 * none of the other hub's, and none of the rings that pass outside it. Without this the slice
 * creates two dozen draw tasks on every spoke tick and rasterises nothing. */
static bool ring_in_clip(const lv_layer_t *layer, int cx, int cy, int r, int w)
{
    const lv_area_t *c = &layer->_clip_area;
    int nx = LV_CLAMP(c->x1, cx, c->x2);
    int ny = LV_CLAMP(c->y1, cy, c->y2);
    long dx = nx - cx, dy = ny - cy;
    long near2 = dx * dx + dy * dy;

    long far2 = 0;
    const int fx[2] = { c->x1, c->x2 };
    const int fy[2] = { c->y1, c->y2 };
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < 2; k++) {
            long ex = fx[i] - cx, ey = fy[k] - cy;
            long d2 = ex * ex + ey * ey;
            if (d2 > far2) far2 = d2;
        }
    }

    long lo = r - w / 2 - 1, hi = r + w / 2 + 1;
    return far2 >= lo * lo && near2 <= hi * hi;
}

/* Whether a box can meet the layer's clip area. */
static bool box_in_clip(const lv_layer_t *layer, const lv_area_t *a)
{
    const lv_area_t *c = &layer->_clip_area;
    return !(a->x2 < c->x1 || a->x1 > c->x2 || a->y2 < c->y1 || a->y1 > c->y2);
}

/* One polyline per side, as a single line draw task. lv_draw_line() copies the points, so
 * the array can live on the stack here. The window's slice draws the same polyline into
 * its own layer, so the body and the slice cannot disagree about where the tape runs. */
static void strand_draw(lv_layer_t *layer, int i)
{
    lv_point_precise_t pts[STRAND_POINTS_MAX];
    int n = strand_points(i, pts);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = tape_colour();
    dsc.width = STRAND_W;
    dsc.opa = LV_OPA_COVER;
    dsc.points = pts;
    dsc.point_cnt = n;
    lv_draw_line(layer, &dsc);
}

static void strand_draw_cb(lv_event_t *e)
{
    strand_draw(lv_event_get_layer(e), (int)(intptr_t)lv_event_get_user_data(e));
}

/* One half of the hub's divider: a 2 px radius from HUB_SPOKE_R0 to HUB_SPOKE_R1 at
 * `a` radians, LVGL's convention (0 = 3 o'clock, positive clockwise). The object's
 * box is the stroke's bounding box, padded so the line and its round caps are not
 * clipped - and that box is the whole invalidation cost, which is why the radius stops
 * short of the centre. Two opposite spokes are two objects on purpose: each is clipped
 * to its own quadrant, and two quadrant boxes are never joined into the hub's full
 * box (lv_refr_join_area() joins only when the union is smaller than the sum). */
static void spoke_set(hub_spoke_t *sp, int cx, int cy, double a, int r0, int r1, int width)
{
    int x0 = cx + (int)lround(SC(r0) * cos(a));
    int y0 = cy + (int)lround(SC(r0) * sin(a));
    int x1 = cx + (int)lround(SC(r1) * cos(a));
    int y1 = cy + (int)lround(SC(r1) * sin(a));

    int pad = width / 2 + 1;
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
    /* Body: two opposite marks. Preview: four, twice the weight, at 90 degrees. */
    const int count = s_preview ? HUB_SPOKE_PREVIEW_COUNT : 2;
    const int r0 = s_preview ? HUB_SPOKE_PREVIEW_R0 : HUB_SPOKE_R0;
    const int r1 = s_preview ? HUB_SPOKE_PREVIEW_R1 : HUB_SPOKE_R1;
    const int w = s_preview ? HUB_SPOKE_PREVIEW_W : HUB_SPOKE_W;
    const double step = 2.0 * 3.14159265358979323846 / (double)count;
    double a = -(double)frame * k_hub_step;   /* the reels turn counter-clockwise */

    for (int k = 0; k < count; k++)
        spoke_set(&s_hub_spoke[i][k], SCX(HUB_X(i)), SCY(HUB_Y), a + (double)k * step,
                  r0, r1, w);
}

/* A hub as the window's slice draws it: the disc, its rim and the two spokes, at the frame
 * the body is showing. The body paints these with three kinds of object - a filled circle
 * with a border, and two lv_line spokes - so that each invalidation stays in its own box.
 * The slice has no objects of its own: it draws the same three things into its layer, whose
 * clip is the window, so nothing outside the window is ever rasterised. */
static void hub_draw(lv_layer_t *layer, int i)
{
    const int cx = SCX(HUB_X(i)), cy = SCY(HUB_Y), d = SC(HUB_RING_D);

    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.bg_color = k_col_hub_ring;
    disc.bg_opa = LV_OPA_COVER;
    disc.radius = LV_RADIUS_CIRCLE;
    lv_area_t box = { cx - d / 2, cy - d / 2, cx - d / 2 + d - 1, cy - d / 2 + d - 1 };
    if (box_in_clip(layer, &box)) lv_draw_rect(layer, &disc, &box);

    lv_draw_arc_dsc_t ring;
    lv_draw_arc_dsc_init(&ring);
    ring.color = s_hubs_white ? k_col_hub_white
                              : lv_color_mix(k_col_tape_dark, k_col_tape, s_tape_ink);
    ring.width = HUB_RIM_W;
    ring.opa = LV_OPA_COVER;
    ring.center.x = cx;
    ring.center.y = cy;
    ring.start_angle = 0;
    ring.end_angle = 360;
    ring.radius = (uint16_t)(d / 2 - HUB_RIM_W / 2);
    if (ring_in_clip(layer, cx, cy, d / 2, HUB_RIM_W)) lv_draw_arc(layer, &ring);

    /* The divider stops short of the centre and of the rim, exactly as spoke_set() places
     * it, so the slice's divider is the body's and not a second reading of the geometry. */
    double a = -(double)s_shown_hub[i] * k_hub_step;
    for (int k = 0; k < 2; k++) {
        double t = a + (k ? HUB_HALF_TURN : 0.0);
        lv_point_precise_t pts[2];
        pts[0].x = (lv_value_precise_t)(cx + lround(SC(HUB_SPOKE_R0) * cos(t)));
        pts[0].y = (lv_value_precise_t)(cy + lround(SC(HUB_SPOKE_R0) * sin(t)));
        pts[1].x = (lv_value_precise_t)(cx + lround(SC(HUB_SPOKE_R1) * cos(t)));
        pts[1].y = (lv_value_precise_t)(cy + lround(SC(HUB_SPOKE_R1) * sin(t)));

        int pad = HUB_SPOKE_W / 2 + 1;
        lv_area_t sb = { (int)LV_MIN(pts[0].x, pts[1].x) - pad,
                         (int)LV_MIN(pts[0].y, pts[1].y) - pad,
                         (int)LV_MAX(pts[0].x, pts[1].x) + pad,
                         (int)LV_MAX(pts[0].y, pts[1].y) + pad };
        if (!box_in_clip(layer, &sb)) continue;

        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = s_hubs_white ? k_col_hub_white : k_col_hub_ink;
        line.width = HUB_SPOKE_W;
        line.opa = LV_OPA_COVER;
        line.points = pts;
        line.point_cnt = 2;
        line.round_start = 1;
        line.round_end = 1;
        lv_draw_line(layer, &line);
    }
}

/* A pack's rings: the outer circle at the pack's radius, then one every PACK_RING_GAP
 * px inwards until the hub rim, which covers anything under it. All of them come from
 * one object's draw callback rather than one object per circle: the count follows the
 * fill, so a circle-per-object would resize and invalidate a dozen boxes on every frame
 * the tape moves, while this invalidates the pack's box once. The rings are the only
 * paint - the object has no background - so the screen shows through between them. */
static void pack_draw_rings(lv_layer_t *layer, int i)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = tape_colour();
    dsc.width = PACK_RING_W;
    dsc.opa = LV_OPA_COVER;
    dsc.center.x = SCX(HUB_X(i));
    dsc.center.y = SCY(HUB_Y);
    dsc.start_angle = 0;
    dsc.end_angle = 360;

    /* The preview paints the tape's edge once: the rings are the pack's winding, and at the
     * entry pose the window shows one edge, not ten. Under that edge the circle is filled -
     * the wound tape, in its darker red - so the pack reads as a body with an outline rather
     * than as a hoop. The hub's disc is drawn over it by the rim object. */
    if (s_preview) {
        int r = SC(s_pack_d[i]) / 2;
        if (r > SC(HUB_RING_D) / 2) {
            lv_draw_rect_dsc_t body;
            lv_draw_rect_dsc_init(&body);
            body.bg_color = k_col_tape_dark_preview;
            body.bg_opa = LV_OPA_COVER;
            body.radius = LV_RADIUS_CIRCLE;
            lv_area_t box = { dsc.center.x - r, dsc.center.y - r,
                              dsc.center.x + r - 1, dsc.center.y + r - 1 };
            if (box_in_clip(layer, &box)) lv_draw_rect(layer, &body, &box);

            if (ring_in_clip(layer, dsc.center.x, dsc.center.y, r, PACK_RING_W)) {
                dsc.radius = (uint16_t)r;
                lv_draw_arc(layer, &dsc);
            }
        }
        return;
    }

    /* The gap shrinks with the cassette, but a ring is a pixel: a gap of 0 would draw the
     * same circle for ever. */
    int gap = SC(PACK_RING_GAP);
    if (gap < 1) gap = 1;

    for (int r = SC(s_pack_d[i]) / 2; r > SC(HUB_RING_D) / 2; r -= gap) {
        if (!ring_in_clip(layer, dsc.center.x, dsc.center.y, r, PACK_RING_W)) continue;
        dsc.radius = (uint16_t)r;
        lv_draw_arc(layer, &dsc);
    }
}

static void pack_draw_cb(lv_event_t *e)
{
    pack_draw_rings(lv_event_get_layer(e), (int)(intptr_t)lv_event_get_user_data(e));
}

/* The window's slice: what the white page's Card used to show by having a hole cut in it.
 * The Card is opaque now and the porthole above the print paints this instead, clipped to
 * the window's capsule by its own object - so the same tape, one layer higher, and no A8
 * mask anywhere. Order is the body's, bottom first: packs, strands, hubs. */
void cassette_tapes_draw_slice(lv_layer_t *layer)
{
    if (!layer || !s_run) return;

    for (int i = 0; i < 2; i++) {
        pack_draw_rings(layer, i);
        strand_draw(layer, i);
        hub_draw(layer, i);
    }
}

/* Where the pack sits and how big it is, for the diameter the fill last gave it. It is
 * split from pack_set() so a scale change can re-place a pack whose fill has not moved.
 *
 * While the white page's Card is up the box is intersected with its window, and that is
 * an invalidation change only, not a geometry one: pack_draw_cb draws the rings from
 * SCX/SCY(HUB_X(i), HUB_Y), the hub's screen centre, not from the object's box, and
 * lv_obj_redraw() truncates the layer to the object's box before LV_EVENT_DRAW_MAIN_END
 * (lv_refr.c:131). So a clamped box clips the rings to the window and moves none of them.
 * Do not "fix" this by drawing the rings relative to the box: the 25 Hz reel tick would
 * then invalidate the whole pack area, and the Card would be re-composited over it every
 * frame - which is the one cost the window exists to remove. */
static void pack_place(int i)
{
    int sd = SC(s_pack_d[i]);
    int x = SCX(HUB_X(i)) - sd / 2, y = SCY(HUB_Y) - sd / 2;
    int w = sd, h = sd;

    if (s_window.w > 0 && s_window.h > 0) {
        int x1 = LV_MAX(x, s_window.x), y1 = LV_MAX(y, s_window.y);
        int x2 = LV_MIN(x + w, s_window.x + s_window.w);
        int y2 = LV_MIN(y + h, s_window.y + s_window.h);

        if (x2 > x1 && y2 > y1) {
            x = x1;
            y = y1;
            w = x2 - x1;
            h = y2 - y1;
        } else {
            /* No overlap: keep a 1x1 box rather than a zero-size one, which LVGL does not
             * place reliably, and park it inside the window so even a pose the Card never
             * draws at cannot invalidate outside it. Nothing shows either way: the box is
             * the disc's bounding box, so a pack whose box misses the window has no ring
             * in the window to draw. */
            x = LV_MIN(LV_MAX(x, s_window.x), s_window.x + s_window.w - 1);
            y = LV_MIN(LV_MAX(y, s_window.y), s_window.y + s_window.h - 1);
            w = h = 1;
        }
    }

    lv_obj_set_size(s_pack[i], w, h);
    lv_obj_set_pos(s_pack[i], x, y);
    lv_obj_get_coords(s_pack[i], &s_pack_box[i]);
}

/* The pack's fill is the outer radius, and the rings are drawn from it in
 * pack_draw_cb. A size change invalidates the pack's old and new boxes, and the
 * callback then draws every ring for the new radius, so nothing is left stale. The slice
 * above the Card gets the union of the two boxes, which is what it must repaint. */
static void pack_set(int i, int d)
{
    if (d == s_pack_d[i]) return;

    lv_area_t before = s_pack_box[i];
    s_pack_d[i] = d;
    pack_place(i);
    strand_set(i);

    lv_area_t after = s_pack_box[i];
    lv_area_t both = { LV_MIN(before.x1, after.x1), LV_MIN(before.y1, after.y1),
                       LV_MAX(before.x2, after.x2), LV_MAX(before.y2, after.y2) };
    slice_dirty(&both);
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

/* Put a widget at a design position and size. Only the border frames and the chips use it, and
 * they are built only where the panel needs them. */
static void place_rect(lv_obj_t *o, int x, int y, int w, int h)
{
    lv_obj_set_pos(o, SCX(x), SCY(y));
    lv_obj_set_size(o, SC(w), SC(h));
}

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

    if (screen_caps()->panel_borders) {
        place_rect(s_frame[0], FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
        lv_obj_set_style_radius(s_frame[0], SC(FRAME_R), 0);
        place_rect(s_frame[1], 122, 122, 556, 168);
        /* The design's 16 px corner is all frame[1] has now: on the white page the Card covers
         * it, so nothing prints the cover's pill on it any more. */
        lv_obj_set_style_radius(s_frame[1], SC(16), 0);

        for (int i = 0; i < 3; i++) {
            place_rect(s_chip[i], k_chip[i].x, k_chip[i].y, k_chip[i].w, k_chip[i].h);
            lv_obj_set_style_radius(s_chip[i], SC(CHIP_R), 0);
        }
    }

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
        place_disc(s_rim[i], HUB_X(i), HUB_Y, HUB_RING_D);
        hub_frame_set(i, s_shown_hub[i]);
    }
}

/* ------------------------------------------------------- the model's frame */

void cassette_tapes_set_window(cassette_rect_t window)
{
    s_window = window;

    /* The clamp is a box change and nothing else, so it lands at once rather than on the
     * next reel tick: the Card comes up with the page. Before create() there is no pack to
     * re-place, and create()'s own place() applies the window that is already held. */
    for (int i = 0; i < 2; i++)
        if (s_pack[i]) pack_place(i);
}

/* The body's reels are covered by something opaque, so stop drawing them. Only the cover's
 * porthole asks for this, and only while it paints the window itself: while the Card cuts the
 * window out with its mask, the tape under it is the picture, seen through the hole. */
void cassette_tapes_set_body_hidden(bool hidden)
{
    for (int i = 0; i < 2; i++) {
        if (s_pack[i]) lv_obj_set_hidden(s_pack[i], hidden);
        if (s_rim[i]) lv_obj_set_hidden(s_rim[i], hidden);
        for (int k = 0; k < HUB_SPOKE_MAX; k++)
            if (s_hub_spoke[i][k].obj) lv_obj_set_hidden(s_hub_spoke[i][k].obj, hidden);
    }
}

/* Push the tape's ink into the hub rims: the playing red at LV_OPA_COVER, the tape's own
 * colour at LV_OPA_TRANSP. Only the border changes, so each rim invalidates its own
 * 168 px circle and nothing else. */
/* The hub's three paints - the disc, the rim and the divider marks - from the mode and the
 * frame in force:
 *
 *   body     the disc is the hub's plastic, the rim carries the playing ink, the marks the
 *            hub's own ink; with a picture printed on the Card the rim and marks go white.
 *   preview  the rim and the four marks are white and the disc is the hub's plastic: the
 *            window is the picture's, so the hub's drawing is what shows, not its colour.
 */
static void hub_ink_apply(void)
{
    const lv_color_t disc = k_col_hub_ring;
    lv_color_t ring = s_preview ? k_col_hub_white
                                : lv_color_mix(k_col_tape_dark, k_col_tape, s_tape_ink);
    if (!s_preview && s_hubs_white) ring = k_col_hub_white;
    const lv_color_t mark = s_preview ? k_col_hub_white
                                      : (s_hubs_white ? k_col_hub_white : k_col_hub_ink);

    for (int i = 0; i < 2; i++) {
        if (s_rim[i]) {
            lv_obj_set_style_bg_color(s_rim[i], disc, 0);
            lv_obj_set_style_border_color(s_rim[i], ring, 0);
        }
        for (int k = 0; k < HUB_SPOKE_MAX; k++) {
            if (s_hub_spoke[i][k].obj)
                lv_obj_set_style_line_color(s_hub_spoke[i][k].obj, mark, 0);
        }
        /* The slice above the Card draws the same hub. */
        lv_area_t hb;
        hub_box(i, &hb);
        slice_dirty(&hb);
    }
}

static void tape_ink_apply(lv_opa_t ink)
{
    s_tape_ink = ink;
    hub_ink_apply();
}

/* White hub lines, or the tape's inks back: the rim's border and both spokes, which are the
 * hub's drawing. Its face keeps the playing red - the white is the turn, not the spool. */
void cassette_tapes_set_hubs_white(bool white)
{
    if (white == s_hubs_white) return;
    s_hubs_white = white;
    hub_ink_apply();
}

/* The full-screen picture's hub: a thicker rim, the divider kept, and each pack as its edge
 * once. Only the two paints change - the body keeps running, so putting the picture away
 * restores the cassette exactly as it was. */
void cassette_tapes_set_preview(bool on)
{
    if (on == s_preview) return;
    s_preview = on;

    const lv_color_t col = on ? k_col_tape_preview : k_col_tape;
    for (int i = 0; i < 2; i++) {
        if (s_rim[i])
            lv_obj_set_style_border_width(s_rim[i], on ? HUB_RIM_PREVIEW_W : HUB_RIM_W, 0);
        if (s_roller[i]) lv_obj_set_style_border_color(s_roller[i], col, 0);
        /* The body's divider is two marks, the preview's four: show what the mode uses, and
         * re-place the hub so the marks come back at the right weight and length. */
        for (int k = 0; k < HUB_SPOKE_MAX; k++) {
            if (!s_hub_spoke[i][k].obj) continue;
            lv_obj_set_style_line_width(s_hub_spoke[i][k].obj,
                                        on ? HUB_SPOKE_PREVIEW_W : HUB_SPOKE_W, 0);
            lv_obj_set_hidden(s_hub_spoke[i][k].obj, k >= (on ? HUB_SPOKE_PREVIEW_COUNT : 2));
        }
        if (s_rim[i]) hub_frame_set(i, s_shown_hub[i]);
        /* The pack's paint changed shape and ink: it is not a size change, so repaint it
         * whole. */
        if (s_pack[i]) lv_obj_invalidate(s_pack[i]);
    }
    hub_ink_apply();
    if (s_run) lv_obj_set_style_line_color(s_run, col, 0);
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
            /* Where the spoke was and where it now is: the slice must repaint both. */
            lv_area_t was, now;
            spokes_box(i, s_shown_hub[i], &was);
            spokes_box(i, frame->hub_frame[i], &now);
            lv_area_t both = { LV_MIN(was.x1, now.x1), LV_MIN(was.y1, now.y1),
                               LV_MAX(was.x2, now.x2), LV_MAX(was.y2, now.y2) };
            slice_dirty(&both);
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
    if (screen_caps()->panel_borders) {
        /* The two rounded rectangles the body is drawn on. */
        s_frame[0] = cassette_border_box(parent, 0, 0, 1, 1, 0, k_col_frame, 0, k_col_back, 1);
        s_frame[1] = cassette_border_box(parent, 0, 0, 1, 1, 0, k_col_frame, 0, k_col_back, 1);

        /* The three small boxes along the bottom are objects too, placed by the pose in
         * cassette_tapes_place(); a fixed position here would not scale with the cassette. */
        for (int i = 0; i < 3; i++)
            s_chip[i] = cassette_border_box(parent, 0, 0, 1, 1, 0, k_col_frame, 255,
                                            k_col_tape_dark, 1);
    }
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
     * move, and each is its own object so its bounding box stays small. The rim's border
     * is the playing red from the first frame - it is the only thing the tape's ink
     * writes - so the module's ink cache starts on the state that is on screen. */
    for (int i = 0; i < 2; i++) {
        s_rim[i] = cassette_box(parent, 0, 0, 1, 1, LV_RADIUS_CIRCLE, k_col_hub_ring);
        lv_obj_set_style_border_color(s_rim[i], k_col_tape_dark, 0);
        lv_obj_set_style_border_opa(s_rim[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_rim[i], HUB_RIM_W, 0);

        for (int k = 0; k < HUB_SPOKE_MAX; k++) {
            lv_obj_t *sp = lv_line_create(parent);
            lv_obj_remove_style_all(sp);
            lv_obj_set_style_line_color(sp, k_col_hub_ink, 0);
            lv_obj_set_style_line_width(sp, HUB_SPOKE_W, 0);
            lv_obj_set_style_line_rounded(sp, true, 0);
            lv_obj_set_clickable(sp, false);
            /* The body's divider is two marks; the preview's is four. */
            if (k >= 2) lv_obj_set_hidden(sp, true);
            s_hub_spoke[i][k].obj = sp;
        }

        s_shown_hub[i] = 0;
    }

    /* The body is built: put it where the pose it holds says it goes. */
    cassette_tapes_place(s_pose);
}
