/*
 * The Cassette screen's design grid, as arithmetic. No LVGL, no globals: the view
 * passes its current pose in, so every part of the screen lays out from one value
 * instead of reading a file-static through macros. See cassette_layout.h.
 */
#include "cassette_layout.h"

static int clamp_scale(int scale)
{
    if (scale < CASSETTE_SCALE_MIN) return CASSETTE_SCALE_MIN;
    if (scale > CASSETTE_SCALE_MAX) return CASSETTE_SCALE_MAX;
    return scale;
}

cassette_pose_t cassette_pose(int scale, int dx, int dy)
{
    cassette_pose_t pose = { clamp_scale(scale), dx, dy };
    return pose;
}

int cassette_len(cassette_pose_t pose, int design)
{
    return design * pose.scale / CASSETTE_SCALE_NONE;
}

int cassette_even(int px)
{
    return px & ~1;
}

int cassette_len_even(cassette_pose_t pose, int design)
{
    return cassette_even(cassette_len(pose, design));
}

int cassette_origin_x(cassette_pose_t pose)
{
    return (CASSETTE_DESIGN_W - cassette_len(pose, CASSETTE_DESIGN_W)) / 2 + pose.dx;
}

int cassette_origin_y(cassette_pose_t pose)
{
    return (CASSETTE_DESIGN_H - cassette_len(pose, CASSETTE_DESIGN_H)) / 2 + pose.dy;
}

int cassette_x(cassette_pose_t pose, int design_x)
{
    return cassette_origin_x(pose) + cassette_len(pose, design_x);
}

int cassette_y(cassette_pose_t pose, int design_y)
{
    return cassette_origin_y(pose) + cassette_len(pose, design_y);
}

int cassette_lerp(int from, int to, int32_t step)
{
    return from + (int)(((int64_t)(to - from) * step) / 1024);
}

cassette_pose_t cassette_pose_lerp(cassette_pose_t from, cassette_pose_t to, int32_t step)
{
    cassette_pose_t pose = {
        cassette_lerp(from.scale, to.scale, step),
        cassette_lerp(from.dx, to.dx, step),
        cassette_lerp(from.dy, to.dy, step),
    };
    return pose;
}
