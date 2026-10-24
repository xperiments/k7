#pragma once
/*
 * The Cassette screen's VU meter: a lattice with one column of segments per channel,
 * filling from the bottom, centred in the cassette's outer frame.
 *
 * The meter is written on the cassette's design grid, like the rest of the picture, so
 * it is a function of the pose: place() takes the current one and moves the panel and
 * every segment. The module owns its own objects and state - the panel, the 24
 * segments, the lit count of each column and the pose the lattice is drawn at - and
 * shares nothing with the view. The view supplies the parent - the cassette's container
 * - and calls create() once, place() on every pose change, then set() for each frame.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "lvgl.h"

#include "cassette.h"
#include "cassette_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the meter's panel and segments under `parent`, the cassette's container. The
 * panel is transparent and takes no press: the lattice is its draw callback and the
 * segments are objects, so a level change repaints a few cells and nothing else. The
 * view creates it after the tapes, so the meter paints over the right pack's rings the
 * way it did before the split (the hubs do not reach it). */
void cassette_meter_create(lv_obj_t *parent);

/* Put the meter and its segments where `pose` says. Called from the view's cassette
 * placement, so it runs on every step of a scale or offset change. */
void cassette_meter_place(cassette_pose_t pose);

/* Show a frame's two channels, one column each. NULL is a no-op; a column whose lit
 * count has not changed is not touched. */
void cassette_meter_set(const cassette_frame_t *frame);

#ifdef __cplusplus
}
#endif
