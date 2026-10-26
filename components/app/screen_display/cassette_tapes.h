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

/* Apply a model frame: the hub phase, the pack fills, and the playing indication in the
 * two rim borders, which is the frame's tape_ink (1 = the playing red, 0 = the tape's own
 * colour). Only what changed is written. A NULL frame is a no-op. */
void cassette_tapes_set(const cassette_frame_t *frame);

/* The point where a tether meets a hub, in screen pixels at the pose the module holds:
 * the top of the rim when `top` is true, its bottom when false. A line stops at the rim
 * rather than drawing across the hub's ink and the VU meter. Index 0 is the left
 * (supply) hub. */
void cassette_tapes_hub_anchor(int index, bool top, int *x, int *y);

/* The current pixels of frame `index` - 0 the outer frame, 1 the label window - and its
 * corner radius, in the cassette container's coordinates. The white page's cover uses
 * them as its mask: frame 0 clips the picture and frame 1 is the hole in it. A board
 * without the frames leaves every output at zero. */
void cassette_tapes_frame_rect(int index, int *x, int *y, int *w, int *h, int *r);

/* The label is the top layer, so its window's border follows the mask's largest radius - a
 * pill whose ends are the hubs' circles - while it is up, and returns to the design's 16 px
 * corner when it goes. The body itself is not re-ordered by this call. */
void cassette_tapes_set_cover_print(bool on);

/* The label is the top layer, so its two frame borders have to be above it while it is
 * shown. These move only the frames: the body, the hubs and the meter do not move, so the
 * cinta stays below the glow and the label. */
void cassette_tapes_raise_frames(void);
void cassette_tapes_lower_frames(void);

#ifdef __cplusplus
}
#endif
