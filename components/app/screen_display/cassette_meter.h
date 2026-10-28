#pragma once
/*
 * cassette_meter - the VU meter, in the EQ group's `[V]` plate.
 *
 * Two tall columns side by side, L on the left and R on the right, each a 5x24 matrix of 4 px dots
 * lit from the bottom, with a small rounded L/R pill under it. The reference the owner gave is
 * exactly that shape, and it is why the plate is 96x190 rather than one 96 px cell: a column tall
 * enough to read a level off. The scale is dB over -48..0, fed by the cassette frames' `vu_l` and
 * `vu_r`; the lit rows are the lit dots, so a change of level repaints one column's box.
 *
 * It is built into the plate the bar made for it and stays in cell coordinates: two 30x148
 * columns 12 px apart, each a 28x142 dot matrix, and a 30x14 pill 6 px under them. The plate is
 * the panel's, so the meter is created and deleted with the EQ group's cells; between the two its
 * `set()` is a no-op, and the delete event detaches the module so a frame that arrives while the
 * group is away cannot touch freed objects.
 *
 * Private to screen_display. Called under the LVGL lock.
 */
#include "lvgl.h"

#include "cassette.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the meter inside `cell`, the 96x190 plate the bar made. It is visible from this call: the
 * plate is the visibility. A second create() on the same plate is a no-op; a create() after the
 * old one was deleted rebuilds from zero. */
void cassette_meter_create(lv_obj_t *cell);

/* Show a frame's two channels: L on the left column, R on the right. NULL is a no-op, and so is a
 * frame with no meter on screen; a column whose lit count has not changed is not touched. */
void cassette_meter_set(const cassette_frame_t *frame);

#ifdef __cplusplus
}
#endif
