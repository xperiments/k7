#pragma once
/*
 * pad - the Cassette screen's Pad: what one press means. Portable: no LVGL, no Menu, no
 * card, no board, no allocation (docs/specs/menu.md, ADR 0022).
 *
 * The Pad is one plate over the DJ block's place. The plate, its rect, its ink and its
 * callbacks are the view's; this module owns only the classification of a press. The view
 * feeds it the raw pointer samples the surface sees and acts on the one event each call
 * returns:
 *
 *   PAD_EV_NONE  the rest: nothing to do
 *   PAD_EV_STEP  vertical travel, for the Menu's ratchet: menu_drag(dy, dt)
 *   PAD_EV_TAP   a press that never travelled: activate the band
 *   PAD_EV_BACK  a long leftward swipe: up one Menu list
 *
 * One press takes one axis, decided once, so the same touch cannot both ratchet the band
 * and go Back. The travel up to a vertical decision is handed over as a single STEP timed
 * from the press, so the first row is not lost to the undecided start.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A tap is a press that never travelled PAD_TAP_SLOP px in total. The axis is decided by
 * the first PAD_AXIS_SLOP px of travel. PAD_SWIPE_MIN is the screen's own gesture limit
 * (LV_INDEV_DEF_GESTURE_LIMIT), and a leftward swipe that far on a horizontal press is
 * Back. All three are screen pixels of cumulative travel. */
#define PAD_TAP_SLOP  8
#define PAD_AXIS_SLOP 12
#define PAD_SWIPE_MIN 50

/* What the surface saw, named without LVGL's event codes. */
typedef enum {
    PAD_IN_PRESS = 0, /* the touch went down on the Pad */
    PAD_IN_MOVE,      /* PRESSING: the pointer moved (or repeated its point) */
    PAD_IN_RELEASE,   /* the touch came up */
    PAD_IN_LOST,      /* the press stopped belonging to the Pad */
} pad_in_t;

/* What the press means. The Pad's three meanings are the Menu's (docs/specs/CONTEXT.md). */
typedef enum {
    PAD_EV_NONE = 0,
    PAD_EV_STEP, /* vertical travel, for the ratchet */
    PAD_EV_TAP,  /* activate the band */
    PAD_EV_BACK, /* up one Menu list */
} pad_ev_kind_t;

/* One event. Only PAD_EV_STEP carries values; every other kind has dy = 0 and dt = 0.
 *
 * dy  signed travel in pixels: positive is down (the screen's y grows downwards). On the
 *     first STEP of a press it is the whole travel since the press; afterwards it is the
 *     travel since the previous accepted sample, and it may be 0 when that sample moved
 *     only horizontally (the ratchet tolerates a 0 px sample).
 * dt  the milliseconds that travel took: since the press for the first STEP, since the
 *     previous accepted sample afterwards. A repeated now_ms is legal and yields dt = 0;
 *     a clock that goes backwards is clamped to dt = 0, never a wrapped huge value.
 */
typedef struct {
    pad_ev_kind_t kind;
    int           dy;
    uint32_t      dt;
} pad_ev_t;

/* The press in flight: its axis, the last accepted sample, its clock, the signed travel
 * since the press and the cumulative travel the tap is measured on.
 *
 * Treat it as private. Only pad_reset() and pad_feed() write it, and a caller acts on the
 * returned event, never on a field. Zero-initialise it or call pad_reset() before the
 * first press; it is plain value state (28 B here) and carries no pointer and no buffer. */
typedef struct {
    bool     pressed;           /* a press is in flight */
    uint8_t  axis;              /* 0 undecided, 1 vertical, 2 horizontal */
    int16_t  last_x, last_y;    /* the last accepted sample, screen pixels */
    uint32_t press_ms;          /* when the press began */
    uint32_t last_ms;           /* when that sample arrived */
    int32_t  dx, dy;            /* signed travel since the press, right/down positive */
    int32_t  moved;             /* |dx| + |dy| since the press: the tap's measure */
} pad_t;

/* Drop any press in flight. No event. The next pad_feed() starts clean. */
void pad_reset(pad_t *p);

/* Feed one sample: what the surface saw, the pointer in screen pixels, the clock in
 * milliseconds. Returns exactly one event - a press in flight yields at most one STEP per
 * accepted sample - and never fails or allocates. A NULL state yields PAD_EV_NONE.
 *
 * A MOVE, RELEASE or LOST with no press in flight yields NONE. A MOVE that repeats the
 * last accepted point yields NONE and does not move the clock. x and y are ignored on
 * RELEASE and LOST: a release is judged on where the press last was, not where it ended.
 */
pad_ev_t pad_feed(pad_t *p, pad_in_t in, int x, int y, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
