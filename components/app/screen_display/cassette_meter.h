#pragma once
/*
 * The Cassette screen's VU meter: two rows centred on the right hub, L on top and R
 * below. Each row has an L/R badge and seven touching dim bars; the lit level is one
 * rounded fill that grows from the left, so its right end is a semicircle. The row clips
 * its children to its rounded corners, so the track's right end follows the row's
 * outline.
 *
 * The meter is written on the cassette's design grid, like the rest of the picture, so
 * it is a function of the pose: place() takes the current one and moves the panel, the
 * rows, the badges and the bars. The module owns its own objects and state - the panel,
 * the two rows, the two badges, the fourteen track bars, the two lit fills and each
 * row's lit count - and shares
 * nothing with the view. The view supplies the parent - the cassette's container - and
 * calls create() once, place() on every pose change, then set() for each frame.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "lvgl.h"

#include "cassette.h"
#include "cassette_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the two rows, their L/R badges and the fourteen bars under `parent`, the
 * cassette's container. The panel is transparent and takes no press. The view creates it
 * after the tapes, so the meter paints inside the right hub. The meter starts hidden; a
 * two-finger swipe left shows it and a right hides it. */
void cassette_meter_create(lv_obj_t *parent);

/* Fade the whole meter in (show) or out (hide) over 220 ms. The meter is hidden at rest;
 * show/hide are idempotent, and a show during a hide reverses from the current opacity. */
void cassette_meter_show(void);
void cassette_meter_hide(void);

/* Put the meter and its rows where `pose` says. Called from the view's cassette
 * placement, so it runs on every step of a scale or offset change. */
void cassette_meter_place(cassette_pose_t pose);

/* Show a frame's two channels: L on the top row, R on the bottom. NULL is a no-op; a row
 * whose lit count has not changed is not touched. */
void cassette_meter_set(const cassette_frame_t *frame);

#ifdef __cplusplus
}
#endif
