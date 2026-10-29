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
 *
 * The module also owns the Controls' fixed frame and its cells. Those are screen
 * pixels and never scale with a pose: the badge, the two bands, the window and the
 * Pad are laid out once, at the entry pose, and the cassette moves under them. A
 * rect is half-open - the right edge is x + w - and neighbours share their 2 px
 * border: the badge's right edge is the header's left edge plus 2, the window's
 * right edge is the Pad's left edge, the bands' inner edges are the window's.
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

/* A screen-space rectangle, half-open: the right edge is x + w, the bottom y + h. */
typedef struct {
    int x, y, w, h;
} cassette_rect_t;

/* The corner and the stroke every carcase, band and window shares. */
enum {
    CASSETTE_CORNER_R = 8,
    CASSETTE_BORDER_W = 2,
    CASSETTE_CELL_W     = 96,
    CASSETTE_CELL_H     = 96,
    CASSETTE_CELL_PITCH = 94,
    CASSETTE_PAGE_INSET = 6,
    CASSETTE_PAGE_ROWS  = 5,
};

/* The Controls' frame, in screen pixels. Together with cassette_part_rect it is
 * the one place the chrome's numbers live. */
typedef enum {
    CASSETTE_PART_BADGE = 0,  /* the K7 plate at the header's left: 16, 6, 96, 72 */
    CASSETTE_PART_HEADER,     /* 110, 6, 674, 72 */
    CASSETTE_PART_FOOTER,     /* 16, 358, 768, 116 */
    CASSETTE_PART_WINDOW,     /* 110, 76, 486, 284 */
    CASSETTE_PART_PAD,        /* 596, 76, 186, 284: the window's right edge, the block inset 2 */
    CASSETTE_PART_BLOCK,      /* 594, 76, 190, 284: the right bar's six cells */
    CASSETTE_PART_BACK,       /* 16, 16, 768, 448: the dark ground under the white Page */
    CASSETTE_PART_COUNT
} cassette_part_t;

/* A part's rect in screen pixels. Fixed: the frame never scales with a pose. */
cassette_rect_t cassette_part_rect(cassette_part_t part);

/* The timeline: the elapsed time, the track's length and the 6 px track between them. It is one
 * instrument drawn in two places - the Peek's floating bar and the Menu's footer - so its numbers
 * live here and both read them. That is what makes the second copy a copy: the footer's is the
 * Peek's bar, at the Peek's rect, with the Peek's insets. */
enum {
    CASSETTE_TIMELINE_X        = 116,   /* the bar, centred in the panel */
    CASSETTE_TIMELINE_Y        = 416,
    CASSETTE_TIMELINE_HIDDEN_Y = 488,   /* where its slide in begins, below the panel */
    CASSETTE_TIMELINE_W        = 568,
    CASSETTE_TIMELINE_H        = 52,
    CASSETTE_TIMELINE_R        = 12,
    CASSETTE_TIMELINE_PAD      = 20,    /* the time boxes' inset, inside the bar's border */
    CASSETTE_TIMELINE_TIME_W   = 110,
    CASSETTE_TIMELINE_GAP      = 10,    /* a time box to the track */
    CASSETTE_TIMELINE_TRACK_X  = CASSETTE_TIMELINE_PAD + CASSETTE_TIMELINE_TIME_W +
                                 CASSETTE_TIMELINE_GAP,
    CASSETTE_TIMELINE_TRACK_W  = CASSETTE_TIMELINE_W - 2 * CASSETTE_TIMELINE_TRACK_X,
    CASSETTE_TIMELINE_TRACK_H  = 6,
};

/* The two edge bars. The left bar is the Transport drawer's column; the right one
 * is the DJ block's two columns. */
typedef enum {
    CASSETTE_BAR_LEFT = 0,
    CASSETTE_BAR_RIGHT,
    CASSETTE_BAR_COUNT
} cassette_bar_t;

/* A bar's cells: 96 px carcases on a 94 px pitch, so neighbours share their 2 px
 * border. `hidden_dx` is the x offset that takes the bar off screen. */
typedef struct {
    int cell_x;      /* the first cell's screen x at rest */
    int cell_y;
    int columns;
    int rows;
    int hidden_dx;
} cassette_bar_geometry_t;

const cassette_bar_geometry_t *cassette_bar_geometry(cassette_bar_t bar);

/* A cell's screen rect. The drawn carcase is 96x96; neighbours overlap by 2 px. */
cassette_rect_t cassette_cell_rect(cassette_bar_t bar, int col, int row);

/* The cell at a screen point, in the bar's column-major order (slot = col * rows + row),
 * or -1. The test stops at the pitch, so a point on a shared border belongs to the cell
 * below or to the right, never to both. A panel with its own slot order - the EQ's
 * k_slot_col/row swaps the first column's M and R with the second column - maps (col,row)
 * or its own table; this index is the bar's, not the panel's. It is a pure geometry helper:
 * the screen's hit rule is LVGL's clickable cell, not this function. */
int cassette_cell_at(cassette_bar_t bar, int x, int y);

/* The Menu's page: the window's inner rect, its 6 px inset and its five slots. The
 * slots round to whole pixels (54, 53, 54, 53, 54); the first slot's height is the
 * model's ratchet pitch. `cassette_page_slot_y` is relative to the content box. */
cassette_rect_t cassette_page_rect(void);
int cassette_page_inset(void);
int cassette_page_slot_y(int slot);
int cassette_page_slot_h(int slot);

/* The three poses the screen uses: the entry view, the Peek's 200/256 with the
 * -60 px lift, and the Controls' 134/256 pulled to x=148 and lifted 20 px. */
enum {
    CASSETTE_SCALE_ENTRY    = CASSETTE_SCALE_NONE,
    CASSETTE_SCALE_PEEK     = 200,
    CASSETTE_SCALE_CONTROLS = 134,
    CASSETTE_PEEK_DY        = -60,
    CASSETTE_CONTROLS_DX    = -51,
    CASSETTE_CONTROLS_DY    = -20,
};

cassette_pose_t cassette_pose_entry(void);
cassette_pose_t cassette_pose_peek(void);
cassette_pose_t cassette_pose_controls(void);

/* The tape's design box and its ink, and the centre the edge mark follows. */
cassette_rect_t cassette_tape_box(void);
cassette_rect_t cassette_tape_ink(void);
int cassette_tape_centre_x(void);
int cassette_tape_centre_y(void);

/* A design-space rect at a pose, in screen pixels. The edges are converted first
 * and the size derived from them, so a scaled rect tiles without a 1 px seam. */
cassette_rect_t cassette_screen_rect(cassette_pose_t pose, cassette_rect_t design);

#ifdef __cplusplus
}
#endif
