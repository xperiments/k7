#pragma once
/*
 * cassette_tapes - the cassette's tape body: the packs, the strands, the rollers, the
 * bottom run, the hubs and, on the panel board, the two panel frames.
 *
 * It is one object per thing that repaints for a different reason. A pack is a single
 * object whose draw callback paints every ring the fill puts there, so a fill change
 * invalidates two boxes and not one per circle. Each side's tape path is a polyline in
 * its own object, and the run between the rollers is a line of its own, so a fill change
 * repaints two narrow sides rather than the whole bottom. A hub is a rim painted once
 * plus two opposite spokes, each its own object, so each invalidation stays in its own
 * quadrant - and the rim's border is the only thing the playing indication writes.
 *
 * The tapes own no truth: the fill, the reel phase and the tape's ink come from the
 * model's frame (components/portable/cassette), the positions from the pose
 * (components/portable/cassette/cassette_layout.h), and cassette_tapes_set() only writes
 * what changed.
 *
 * The objects are children of the parent the caller hands in - the cassette's container
 * in the view - and they are decorative: a press reaches the screen, not a painted box.
 *
 * On the white page all of it is under the Card (cassette_card.h). The frames, the
 * rollers, the run, the strands and the chips are still built and placed; the Card covers
 * them, and only the two rims and a slice of the packs show through its 291x87 window.
 * cassette_tapes_set_window() is what that costs: the packs' boxes clamp to the window so
 * the reel tick never invalidates the Card outside it.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "lvgl.h"

#include "cassette.h"
#include "cassette_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The drawn hubs' angular resolution: one revolution is this many frames, and it is
 * the frame_count the portable model is built with. */
#define CASSETTE_TAPES_FRAME_COUNT 36

/* Build every tape object as a child of `parent`, with its styles and no geometry, then
 * place the whole body at the pose the module holds - the resting design pose unless a
 * place() came first. Every object is created decorative; the draw callbacks and the
 * packs' initial fill diameters are set here. */
void cassette_tapes_create(lv_obj_t *parent);

/* Move the body to where the pose puts it. The layout is a function of the pose alone,
 * so this is the one call a scale or offset change needs: it re-places the frames, the
 * packs, the rollers, the strands, the run, the rims and the spokes. */
void cassette_tapes_place(cassette_pose_t pose);

/* While the white page's Card is up, the packs' boxes clamp to this rect so the reel
 * tick never invalidates outside the Card's window. (0,0,0,0) means no clamp.
 *
 * It is panel pixels, like the Card, and a call re-places both packs at once: the clamp
 * is a box change and not a geometry one, so no ring moves. A page that hides the Card
 * clears it with a zero rect. */
void cassette_tapes_set_window(cassette_rect_t window);

/* Hide the body's packs, rims and spokes: something opaque covers them and their 25 Hz ticks
 * would only re-render it. The cover asks for this while its porthole paints the window; it
 * never asks while the Card's mask cuts the window out and the tape under it is the picture. */
void cassette_tapes_set_body_hidden(bool hidden);

/* White hub lines while a Track's picture is printed on the Card, the tape's own inks
 * otherwise. The rim's border and both spokes go white; the rim's face keeps the playing red,
 * so the hub still reads as a spool and only its drawing changes. The rewind's ink fade leaves
 * the border alone while it is on, and the view asks for it from the Cover's state, once a
 * frame. */
void cassette_tapes_set_hubs_white(bool white);

/* Apply a model frame: the hub phase, the pack fills, and the playing indication in the
 * two rim borders, which is the frame's tape_ink (1 = the playing red, 0 = the tape's own
 * colour). Only what changed is written. A NULL frame is a no-op. */
void cassette_tapes_set(const cassette_frame_t *frame);

/* The full-screen picture is up: draw the hubs with a thick rim - divider and all - and each
 * pack as the tape's edge once instead of the ring stack. The body is not rebuilt, so the
 * cassette comes back exactly as it was. */
void cassette_tapes_set_preview(bool on);

/* The window's slice: the packs' rings, the strands and both hubs, drawn into `layer` in
 * screen coordinates. The white page's porthole calls this from its child's draw callback,
 * with the layer clipped to the window's capsule - the body's own objects are below the
 * Card and cannot be seen through it. Order is the body's, bottom first: packs, strands,
 * hubs. Called under the LVGL lock, like everything else on the screen. */
void cassette_tapes_draw_slice(lv_layer_t *layer);

/* How many rectangles one frame can report at once: two spokes and, on a fill change, two
 * packs. A frame that needs more falls back to the whole window. */
#define CASSETTE_TAPES_SLICE_DIRTY_MAX 4

/* Take the screen rectangles the slice above the Card has to repaint since the last call -
 * what a frame moved that LVGL cannot see for itself, because the change is under the Card.
 * Up to `max` areas are written to `out` and the list is cleared; the view hands each to
 * cassette_cover_window_invalidate_area(). Empty between frames that changed nothing.
 *
 * The list is deliberately narrow: a spoke tick is a box a few dozen pixels across and a
 * fill change is one pack's box. Invalidating the whole window instead rebuilt the capsule's
 * two ARGB corner layers on every tick and starved the stream (2026-11-05). */
int cassette_tapes_take_slice_dirty(lv_area_t *out, int max);

#ifdef __cplusplus
}
#endif
