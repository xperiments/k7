/*
 * The Screen's design grid, as arithmetic. No LVGL, no globals: the view
 * passes its current pose in, so every part of the screen lays out from one value
 * instead of reading a file-static through macros. See cassette_layout.h.
 */
#include "cassette_layout.h"

#include <math.h>
#include <stddef.h>

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

/* ------------------------------------------------------------- the frame */

/* The Controls' fixed frame, in screen pixels. The parts share their 2 px border:
 * the badge's right edge is the header's left + 2, the window's right edge is the
 * Pad's left, and the bands' inner edges are the window's top and bottom. */
static const cassette_rect_t k_parts[CASSETTE_PART_COUNT] = {
    { 16, 6, 96, 72 },     /* badge */
    { 110, 6, 674, 72 },   /* header */
    { 16, 358, 768, 116 }, /* footer */
    { 110, 76, 486, 284 }, /* window */
    { 596, 76, 186, 284 }, /* pad */
    { 594, 76, 190, 284 }, /* block */
    { 16, 16, 768, 448 },  /* back */
};

cassette_rect_t cassette_part_rect(cassette_part_t part)
{
    cassette_rect_t none = { 0, 0, 0, 0 };
    if ((unsigned)part >= CASSETTE_PART_COUNT) return none;
    return k_parts[part];
}

/* -------------------------------------------------------------- the bars */

/* The left bar's cells start at (16,76); the right bar's at (594,76), two
 * columns. Both run three rows, 94 px apart. The left slide is the drawer's
 * 120 px of travel (the drawer's 96 px column plus its margin); the right puts the
 * block's left edge on the screen's right edge. */
static const cassette_bar_geometry_t k_bars[CASSETTE_BAR_COUNT] = {
    { 16, 76, 1, 3, -120 },
    { 594, 76, 2, 3, 206 },
};

const cassette_bar_geometry_t *cassette_bar_geometry(cassette_bar_t bar)
{
    if ((unsigned)bar >= CASSETTE_BAR_COUNT) return NULL;
    return &k_bars[bar];
}

cassette_rect_t cassette_cell_rect(cassette_bar_t bar, int col, int row)
{
    cassette_rect_t rect = { 0, 0, 0, 0 };
    const cassette_bar_geometry_t *g = cassette_bar_geometry(bar);
    if (!g || col < 0 || col >= g->columns || row < 0 || row >= g->rows) return rect;
    rect.w = CASSETTE_CELL_W;
    rect.h = CASSETTE_CELL_H;
    rect.x = g->cell_x + col * CASSETTE_CELL_PITCH;
    rect.y = g->cell_y + row * CASSETTE_CELL_PITCH;
    return rect;
}

int cassette_cell_at(cassette_bar_t bar, int x, int y)
{
    const cassette_bar_geometry_t *g = cassette_bar_geometry(bar);
    if (!g) return -1;

    int w = (g->columns - 1) * CASSETTE_CELL_PITCH + CASSETTE_CELL_W;
    int h = (g->rows - 1) * CASSETTE_CELL_PITCH + CASSETTE_CELL_H;
    int lx = x - g->cell_x;
    int ly = y - g->cell_y;
    if (lx < 0 || ly < 0 || lx >= w || ly >= h) return -1;

    /* The last column and the last row are 96 px, not 94: the pitch partitions
     * the shared borders and the remaining two pixels belong to the last cell. */
    int col = lx / CASSETTE_CELL_PITCH;
    int row = ly / CASSETTE_CELL_PITCH;
    if (col >= g->columns) col = g->columns - 1;
    if (row >= g->rows) row = g->rows - 1;
    return col * g->rows + row;
}

/* -------------------------------------------------------------- the page */

cassette_rect_t cassette_page_rect(void)
{
    cassette_rect_t window = cassette_part_rect(CASSETTE_PART_WINDOW);
    cassette_rect_t page = {
        window.x + CASSETTE_BORDER_W,
        window.y + CASSETTE_BORDER_W,
        window.w - 2 * CASSETTE_BORDER_W,
        window.h - 2 * CASSETTE_BORDER_W,
    };
    return page;
}

int cassette_page_inset(void)
{
    return CASSETTE_PAGE_INSET;
}

int cassette_page_slot_y(int slot)
{
    cassette_rect_t page = cassette_page_rect();
    int content_h = page.h - 2 * CASSETTE_PAGE_INSET;
    if (slot < 0) slot = 0;
    if (slot > CASSETTE_PAGE_ROWS) slot = CASSETTE_PAGE_ROWS;
    return (slot * content_h + CASSETTE_PAGE_ROWS / 2) / CASSETTE_PAGE_ROWS;
}

int cassette_page_slot_h(int slot)
{
    if (slot < 0 || slot >= CASSETTE_PAGE_ROWS) return 0;
    return cassette_page_slot_y(slot + 1) - cassette_page_slot_y(slot);
}

/* ------------------------------------------------------------- the poses */

cassette_pose_t cassette_pose_entry(void)
{
    return cassette_pose(CASSETTE_SCALE_ENTRY, 0, 0);
}

cassette_pose_t cassette_pose_peek(void)
{
    return cassette_pose(CASSETTE_SCALE_PEEK, 0, CASSETTE_PEEK_DY);
}

cassette_pose_t cassette_pose_controls(void)
{
    return cassette_pose(CASSETTE_SCALE_CONTROLS, CASSETTE_CONTROLS_DX, CASSETTE_CONTROLS_DY);
}

/* -------------------------------------------------------------- the tape */

cassette_rect_t cassette_tape_box(void)
{
    cassette_rect_t box = { 16, 16, 768, 360 };
    return box;
}

cassette_rect_t cassette_tape_ink(void)
{
    /* The frame plus the 84 px the chips and the run reach below it: 16..460. */
    cassette_rect_t ink = { 16, 16, 768, 444 };
    return ink;
}

int cassette_tape_centre_x(void)
{
    cassette_rect_t ink = cassette_tape_ink();
    return ink.x + ink.w / 2;
}

int cassette_tape_centre_y(void)
{
    cassette_rect_t ink = cassette_tape_ink();
    return ink.y + ink.h / 2;
}

cassette_rect_t cassette_screen_rect(cassette_pose_t pose, cassette_rect_t design)
{
    cassette_rect_t rect;
    rect.x = cassette_x(pose, design.x);
    rect.y = cassette_y(pose, design.y);
    rect.w = cassette_x(pose, design.x + design.w) - rect.x;
    rect.h = cassette_y(pose, design.y + design.h) - rect.y;
    return rect;
}

void cassette_hub_anchor(cassette_pose_t pose, int index, bool top, int *x, int *y)
{
    if (!x || !y) return;
    if (index < 0 || index > 1) index = 0;
    int r = cassette_len(pose, CASSETTE_HUB_RING_D) / 2;
    *x = cassette_x(pose, index ? CASSETTE_HUB_X1 : CASSETTE_HUB_X0);
    *y = cassette_y(pose, CASSETTE_HUB_Y) + (top ? -r : r);
}

/* ------------------------------------------------------------------ packs */

int cassette_pack_diameter(uint16_t fill, int min_d, int max_d, int step)
{
    if (fill > 1000) fill = 1000;
    if (max_d < min_d) max_d = min_d;
    double min = min_d, max = max_d;
    double d = sqrt(min * min + (max * max - min * min) * (double)fill / 1000.0);
    int px = (int)lround(d);
    if (step > 0) {
        /* The preview's coarser grid is anchored at the minimum, so the last step ends short
         * of the full pack: keeping the exact maximum would need a hysteresis gate in the
         * setter instead, which is a worse trade than four pixels. */
        return min_d + ((px - min_d) / step) * step;
    }
    return cassette_even(px);
}

/* ---------------------------------------------------------------- plaques */

void cassette_plaque_anchor(int index, int count, const cassette_plaque_geom_t *g,
                            int *x, int *y, cassette_plaque_at_t *at)
{
    if (!g || count <= 0 || index < 0 || index >= count) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (at) *at = CASSETTE_PLAQUE_CENTRE;
        return;
    }
    int odd = count % 2;
    int sides = (count - odd) / 2;

    if (odd && index == sides) { /* the median, above the dial */
        if (x) *x = g->cx;
        if (y) *y = g->cy - g->top;
        if (at) *at = CASSETTE_PLAQUE_CENTRE;
        return;
    }

    bool left = index < sides;
    int row = left ? index : index - sides - odd;
    int dy = 0;
    if (left) {
        /* The left column reads upwards, because the arc does: its first value is at 7:30
         * and the last one it holds is at 10:30. */
        if (sides == 2) dy = (row == 0) ? g->step / 2 : -g->step / 2;
        else if (sides >= 3) dy = (1 - row) * g->step;
    } else {
        if (sides == 2) dy = (row == 0) ? -g->step / 2 : g->step / 2;
        else if (sides >= 3) dy = (row - 1) * g->step;
    }

    if (x) *x = left ? g->pad : g->width - g->pad;
    if (y) *y = g->cy + dy;
    if (at) *at = left ? CASSETTE_PLAQUE_LEFT : CASSETTE_PLAQUE_RIGHT;
}
