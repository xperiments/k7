/*
 * pad - the Pad's press classification. Portable: no LVGL, no Menu, no allocation
 * (docs/specs/menu.md, ADR 0022).
 *
 * One press in, at most one event out. The axis is decided once, after PAD_AXIS_SLOP px of
 * cumulative travel, on the larger of |dx| and |dy| (a tie is vertical), so the same touch
 * cannot both ratchet the band and go Back. The travel it took to decide is handed over as
 * one STEP timed from the press, so the first row is not lost to the undecided start; after
 * that each accepted sample yields its own STEP.
 */
#include "pad.h"

typedef enum {
    AXIS_UNDECIDED = 0, /* the press has not travelled far enough to be one */
    AXIS_VERTICAL,      /* ratchets the band */
    AXIS_HORIZONTAL,    /* never ratchets; a long enough leftward one is Back */
} pad_axis_t;

static int magnitude(int v) { return v < 0 ? -v : v; }

/* A clock that goes backwards reads as no time, never as a wrapped 32-bit span. */
static uint32_t elapsed(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms >= then_ms ? now_ms - then_ms : 0;
}

void pad_reset(pad_t *p)
{
    if (!p) return;
    *p = (pad_t){ 0 }; /* pressed false, axis undecided (0), travel and clocks zero */
}

pad_ev_t pad_feed(pad_t *p, pad_in_t in, int x, int y, uint32_t now_ms)
{
    pad_ev_t ev = { PAD_EV_NONE, 0, 0 };
    if (!p) return ev;

    switch (in) {
    case PAD_IN_PRESS:
        /* A second PRESS while pressed is a new press, not a continuation. */
        p->pressed = true;
        p->axis = AXIS_UNDECIDED;
        p->last_x = (int16_t)x;
        p->last_y = (int16_t)y;
        p->press_ms = now_ms;
        p->last_ms = now_ms;
        p->dx = 0;
        p->dy = 0;
        p->moved = 0;
        return ev;

    case PAD_IN_MOVE: {
        if (!p->pressed) return ev;

        int step_x = x - p->last_x;
        int step_y = y - p->last_y;
        if (step_x == 0 && step_y == 0) return ev; /* the same point: no time passes */

        uint32_t step_ms = elapsed(now_ms, p->last_ms);
        p->last_x = (int16_t)x;
        p->last_y = (int16_t)y;
        p->last_ms = now_ms;
        p->dx += step_x;
        p->dy += step_y;
        p->moved += magnitude(step_x) + magnitude(step_y);

        if (p->axis == AXIS_UNDECIDED) {
            if (p->moved < PAD_AXIS_SLOP) return ev;
            p->axis = magnitude(p->dx) > magnitude(p->dy) ? AXIS_HORIZONTAL : AXIS_VERTICAL;
            if (p->axis != AXIS_VERTICAL) return ev;
            /* The whole travel since the press, timed from the press: the ratchet's first
             * sample, so the undecided start is not swallowed. */
            ev.kind = PAD_EV_STEP;
            ev.dy = (int)p->dy;
            ev.dt = elapsed(now_ms, p->press_ms);
            return ev;
        }

        if (p->axis != AXIS_VERTICAL) return ev; /* a horizontal press never ratchets */
        ev.kind = PAD_EV_STEP;
        ev.dy = step_y;
        ev.dt = step_ms;
        return ev;
    }

    case PAD_IN_RELEASE:
        if (p->pressed) {
            if (p->axis == AXIS_HORIZONTAL && p->dx <= -PAD_SWIPE_MIN) {
                ev.kind = PAD_EV_BACK; /* leftward only, and only on a horizontal press */
            } else if (p->moved < PAD_TAP_SLOP) {
                ev.kind = PAD_EV_TAP; /* cumulative travel, not displacement */
            }
        }
        pad_reset(p);
        return ev;

    case PAD_IN_LOST:
        /* A cancelled touch is neither a tap nor a Back. */
        pad_reset(p);
        return ev;

    default:
        return ev;
    }
}
