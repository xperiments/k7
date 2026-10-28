#pragma once
/*
 * cassette_meter - the VU meter, in the EQ group's `[V]` cell.
 *
 * Two rows, L on top and R below, each a badge and seven touching dim bars with the lit level as
 * one rounded fill that grows from the left, so its right end is a semicircle. The row clips its
 * children to its rounded corners, so the track's right end follows the row's outline. The scale
 * is dB over -48..0, fed by the cassette frames' `vu_l` and `vu_r`.
 *
 * It is built into the cell the bar made for it and stays in cell coordinates: 84x62, two 84x26
 * rows ten px apart, an 18 px badge, a 10 px gap and seven 8 px bars. The cell is the panel's, so
 * the meter is created and deleted with the EQ group's cells; between the two its `set()` is a
 * no-op, and the delete event detaches the module so a frame that arrives while the group is away
 * cannot touch freed objects.
 *
 * Private to screen_display. Called under the LVGL lock.
 */
#include "lvgl.h"

#include "cassette.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the meter inside `cell`, a carcase the bar made. It is visible from this call: the cell
 * is the visibility. A second create() on the same cell is a no-op; a create() after the old
 * cell was deleted rebuilds from zero. */
void cassette_meter_create(lv_obj_t *cell);

/* Show a frame's two channels: L on the top row, R on the bottom. NULL is a no-op, and so is a
 * frame with no meter on screen; a row whose lit count has not changed is not touched. */
void cassette_meter_set(const cassette_frame_t *frame);

#ifdef __cplusplus
}
#endif
