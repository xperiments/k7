#pragma once
/*
 * cassette_layout - the design grid the Cassette screen is drawn on, as a value.
 *
 * The screen's layout is written on the 800x480 grid of the Label cover. A pose
 * scales that grid (256 = LV_SCALE_NONE, pixel for pixel), centres the scaled box
 * in the panel and moves it by (dx, dy) screen pixels. This module owns the
 * conversion and the rounding conventions; it has no LVGL include, so the host
 * tier compiles it and t16 pins the grid.
 *
 * A pose is a value: every part of the screen receives the current one on its
 * place() call instead of reading a file-static. The animator lives in the view;
 * cassette_pose_lerp() is the arithmetic it eases.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CASSETTE_DESIGN_W   = 800,   /* the Label cover's grid, and the panel's own size */
    CASSETTE_DESIGN_H   = 480,
    CASSETTE_SCALE_NONE = 256,   /* at rest: the design grid, pixel for pixel */
    CASSETTE_SCALE_MIN  = 32,    /* below this the tape and the rings stop reading */
    CASSETTE_SCALE_MAX  = 1024,
};

typedef struct {
    int scale;   /* 256 is the design box, pixel for pixel */
    int dx;      /* screen pixels from the centred position: +x right, +y down */
    int dy;
} cassette_pose_t;

/* A pose, with the scale clamped to CASSETTE_SCALE_MIN..CASSETTE_SCALE_MAX. The
 * offset is not clamped: the cassette may be taken off screen. */
cassette_pose_t cassette_pose(int scale, int dx, int dy);

/* A design length in pixels: design * scale / 256, truncated toward zero. */
int cassette_len(cassette_pose_t pose, int design);

/* The same, rounded down to an even number of pixels. A circle drawn on an even
 * diameter has its centre and its rim on the same pixel grid at any scale. */
int cassette_len_even(cassette_pose_t pose, int design);

/* Round a value down to even; for a non-negative pixel count. */
int cassette_even(int px);

/* The design box's origin: the scaled grid centred in the panel, then the pose's
 * offset in screen pixels. */
int cassette_origin_x(cassette_pose_t pose);
int cassette_origin_y(cassette_pose_t pose);

/* A design coordinate in screen pixels. */
int cassette_x(cassette_pose_t pose, int design_x);
int cassette_y(cassette_pose_t pose, int design_y);

/* One step of a 0..1024 LVGL animation: 0 is `from`, 1024 is `to`. */
int cassette_lerp(int from, int to, int32_t step);
cassette_pose_t cassette_pose_lerp(cassette_pose_t from, cassette_pose_t to, int32_t step);

#ifdef __cplusplus
}
#endif
