/*
 * T23: the Pad's press classification, host tier.
 *
 * The Pad's rules used to live in cassette_menu.c's pad_event_cb, where the host cannot
 * reach them. This drives the state machine through pad.h only: raw (x, y, t) samples in,
 * one pad_ev_t out. No LVGL, no Menu, no private access, and every assertion is on the
 * returned event, never on a pad_t field.
 *
 * docs/specs/menu.md and ADR 0022 are the source of truth; the comments below pin the
 * details the spec leaves open.
 */
#include "test_util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pad.h"

/* ------------------------------------------------------------- helpers */

/* One event, three checks: the kind and, for PAD_EV_STEP, the values handed to the
 * ratchet. Every other kind is required to carry zeroes, so no caller can read stale
 * travel out of a tap or a Back. */
static void check_event(pad_ev_t ev, pad_ev_kind_t kind, int dy, uint32_t dt, const char *what)
{
    CHECK(ev.kind == kind, "%s: kind got %d, want %d", what, (int)ev.kind, (int)kind);
    CHECK_EQ(ev.dy, dy, what);
    CHECK_EQ(ev.dt, dt, what);
}

static pad_ev_t press(pad_t *p, int x, int y, uint32_t t) { return pad_feed(p, PAD_IN_PRESS, x, y, t); }
static pad_ev_t move(pad_t *p, int x, int y, uint32_t t) { return pad_feed(p, PAD_IN_MOVE, x, y, t); }
static pad_ev_t release(pad_t *p, uint32_t t) { return pad_feed(p, PAD_IN_RELEASE, 0, 0, t); }
static pad_ev_t lost(pad_t *p, uint32_t t) { return pad_feed(p, PAD_IN_LOST, 0, 0, t); }

/* ------------------------------------------------------------- totality */

static void test_totality(void)
{
    pad_t p;
    pad_reset(&p);
    pad_reset(NULL); /* a NULL reset is a no-op, not a crash */
    check_event(pad_feed(NULL, PAD_IN_PRESS, 10, 10, 0), PAD_EV_NONE, 0, 0,
                "a NULL state yields nothing");

    /* A zeroed state must behave like a reset one: the module has no constructor. */
    pad_t zero = { 0 };
    check_event(move(&zero, 10, 10, 5), PAD_EV_NONE, 0, 0, "a MOVE with no press in flight");
    check_event(release(&zero, 6), PAD_EV_NONE, 0, 0, "a RELEASE with no press in flight");
    check_event(lost(&zero, 7), PAD_EV_NONE, 0, 0, "a LOST with no press in flight");

    /* A RELEASE with no press does not leak its point into the next press. */
    check_event(press(&zero, 400, 200, 10), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&zero, 400, 212, 20), PAD_EV_STEP, 12, 10, "the press started clean");
}

/* --------------------------------------------- the decision and the hand-off */

static void test_press_initialises_and_hands_off(void)
{
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 1000), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 211, 1100), PAD_EV_NONE, 0, 0,
                "11 px of travel leaves the axis undecided");
    /* The 12th pixel decides: the whole travel since the press is one STEP, timed from the
     * press (200 ms), not from the previous sample (100 ms). */
    check_event(move(&p, 400, 212, 1200), PAD_EV_STEP, 12, 200,
                "12 px decides vertical and hands off the pre-decision travel");
    check_event(move(&p, 400, 217, 1250), PAD_EV_STEP, 5, 50,
                "then each sample is its own step");
    check_event(release(&p, 1300), PAD_EV_NONE, 0, 0,
                "a travelled press is neither a tap nor a Back");
}

static void test_pre_decision_travel_is_cumulative(void)
{
    /* 7 px out and 6 px back reaches the 12 px decision with the finger 1 px from where it
     * started: it is the cumulative travel, not the displacement, that decides. */
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 207, 50), PAD_EV_NONE, 0, 0, "7 px down is still undecided");
    check_event(move(&p, 400, 201, 100), PAD_EV_STEP, 1, 100,
                "6 px back decides on the cumulative 13 px, on a 1 px displacement");

    /* The axis is taken: the same press cannot later go Back. */
    check_event(move(&p, 350, 201, 200), PAD_EV_STEP, 0, 100,
                "a horizontal sample inside a vertical press still hands the ratchet dt");
    check_event(release(&p, 250), PAD_EV_NONE, 0, 0,
                "a 50 px left swipe from a vertical press does not go Back");
}

static void test_tie_goes_vertical(void)
{
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 406, 206, 100), PAD_EV_STEP, 6, 100,
                "6 px right and 6 px down tie, and a tie is vertical");
}

/* ------------------------------------------------------------- horizontal */

static void test_horizontal_never_steps(void)
{
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 410, 206, 100), PAD_EV_NONE, 0, 0,
                "10 px right over 6 px down decides horizontal");
    check_event(move(&p, 400, 206, 200), PAD_EV_NONE, 0, 0,
                "a horizontal press yields no STEP");
    check_event(move(&p, 400, 100, 300), PAD_EV_NONE, 0, 0,
                "even far travel in y is still a horizontal press");
    check_event(release(&p, 400), PAD_EV_NONE, 0, 0,
                "dx is back to 0, and the travel is far too long for a tap");
}

static void test_back_is_leftward_only(void)
{
    /* A 60 px leftward press is Back, and only the horizontal axis can be Back. */
    pad_t p;
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 388, 200, 100), PAD_EV_NONE, 0, 0, "12 px left decides horizontal");
    check_event(move(&p, 340, 200, 200), PAD_EV_NONE, 0, 0,
                "a 60 px leftward move never ratchets");
    check_event(release(&p, 300), PAD_EV_BACK, 0, 0, "a 60 px left swipe is Back");
}

static void test_back_threshold(void)
{
    pad_t p;

    /* Exactly the threshold is Back. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 350, 200, 100), PAD_EV_NONE, 0, 0,
                "50 px left decides horizontal");
    check_event(release(&p, 110), PAD_EV_BACK, 0, 0, "50 px left is Back");

    /* One pixel short is not. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 351, 200, 100), PAD_EV_NONE, 0, 0,
                "49 px left decides horizontal");
    check_event(release(&p, 110), PAD_EV_NONE, 0, 0, "49 px left is not Back");

    /* Back is leftward only: 60 px to the right is nothing, and it is no tap either. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 460, 200, 100), PAD_EV_NONE, 0, 0,
                "60 px right decides horizontal");
    check_event(release(&p, 110), PAD_EV_NONE, 0, 0, "a rightward 60 px swipe is not Back");
}

/* ------------------------------------------------------------- taps */

static void test_tap_edges(void)
{
    pad_t p;

    /* 6 px of travel is a tap. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 206, 50), PAD_EV_NONE, 0, 0, "6 px keeps the axis undecided");
    check_event(release(&p, 60), PAD_EV_TAP, 0, 0, "6 px of travel is a tap");

    /* 7 px still is. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 407, 200, 50), PAD_EV_NONE, 0, 0, "7 px keeps the axis undecided");
    check_event(release(&p, 60), PAD_EV_TAP, 0, 0, "7 px of travel is a tap");

    /* 8 px is not: it is the tap slop's edge. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 208, 50), PAD_EV_NONE, 0, 0, "8 px is not a tap");
    check_event(release(&p, 60), PAD_EV_NONE, 0, 0, "releasing after 8 px is nothing");

    /* The tap slop is cumulative: 5 px right then 5 px back is 10 px of travel, though the
     * finger ends where it started. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 405, 200, 50), PAD_EV_NONE, 0, 0, "5 px right is undecided");
    check_event(move(&p, 400, 200, 100), PAD_EV_NONE, 0, 0, "5 px back makes 10 px of travel");
    check_event(release(&p, 110), PAD_EV_NONE, 0, 0,
                "10 px of cumulative travel is not a tap");
}

/* ------------------------------------------------------------- lost, reset */

static void test_lost_and_reset(void)
{
    pad_t p;
    pad_reset(&p);

    /* A lost press is neither a tap nor a Back, and it ends the press. */
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 212, 50), PAD_EV_STEP, 12, 50, "a vertical press ratchets");
    check_event(lost(&p, 60), PAD_EV_NONE, 0, 0, "PRESS_LOST yields nothing");
    check_event(release(&p, 70), PAD_EV_NONE, 0, 0, "a release after a lost press is nothing");

    /* A reset mid-press behaves the same as a lost one. */
    pad_reset(&p);
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 350, 200, 50), PAD_EV_NONE, 0, 0, "12 px left decides horizontal");
    pad_reset(&p);
    check_event(release(&p, 60), PAD_EV_NONE, 0, 0, "a reset mid-press makes the release nothing");

    /* A lost press does not block the next one. */
    check_event(press(&p, 400, 200, 100), PAD_EV_NONE, 0, 0, "a new press after a loss");
    check_event(move(&p, 400, 212, 200), PAD_EV_STEP, 12, 100,
                "the new press is timed from itself");
}

static void test_second_press_is_new(void)
{
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 212, 100), PAD_EV_STEP, 12, 100, "the first press ratchets");
    /* A second PRESS while pressed clears the travel and both clocks. */
    check_event(press(&p, 500, 300, 200), PAD_EV_NONE, 0, 0, "a second press yields nothing");
    check_event(move(&p, 500, 312, 300), PAD_EV_STEP, 12, 100,
                "the second press is timed from its own press, not the first");
}

/* ------------------------------------------------------------- the clock */

static void test_zero_time_sample(void)
{
    /* A repeated now_ms is legal. The ratchet divides only when dt != 0; the Pad's job is
     * to hand over a 0, never a wrapped span. */
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 5000), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 212, 5000), PAD_EV_STEP, 12, 0,
                "the decision sample may take zero time");
    check_event(move(&p, 400, 220, 5000), PAD_EV_STEP, 8, 0,
                "a later sample at the same millisecond is dt 0");
}

static void test_zero_travel_sample_keeps_the_clock(void)
{
    /* A MOVE that repeats the last accepted point is dropped before the clock moves, so a
     * stationary pointer does not swallow the time the next real sample is timed on. */
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 1000), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 211, 1050), PAD_EV_NONE, 0, 0, "11 px is undecided");
    check_event(move(&p, 400, 211, 1100), PAD_EV_NONE, 0, 0,
                "the same point is dropped");
    check_event(move(&p, 400, 212, 1150), PAD_EV_STEP, 12, 150,
                "the decision is still timed from the press");

    check_event(move(&p, 400, 212, 1200), PAD_EV_NONE, 0, 0,
                "a repeated point after the decision is dropped");
    check_event(move(&p, 400, 220, 1300), PAD_EV_STEP, 8, 150,
                "the next step is timed from the last real sample, not the repeat");
}

static void test_clock_going_back_is_clamped(void)
{
    pad_t p;
    pad_reset(&p);

    check_event(press(&p, 400, 200, 1000), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 212, 900), PAD_EV_STEP, 12, 0,
                "a sample before the press reads as zero time");
    check_event(move(&p, 400, 220, 800), PAD_EV_STEP, 8, 0,
                "a sample before the previous one reads as zero time");
}

/* ------------------------------------------------------------- conservation */

static void test_conservation(void)
{
    /* For any vertical press the STEPs must sum to the total signed vertical travel fed in:
     * nothing lost at the decision, nothing double-counted. */
    pad_t p;
    pad_reset(&p);

    int total_dy = 0;
    int summed_dy = 0;

    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");

    const struct {
        int  y;
        int  dy;
    } samples[] = {
        { 205, 5 },   /* undecided */
        { 212, 7 },   /* the 12th px: the decision hands over 12 */
        { 209, -3 },  /* a reversal after the decision */
        { 219, 10 },
        { 199, -20 },
    };
    uint32_t t = 0;
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++) {
        t += 10;
        pad_ev_t ev = move(&p, 400, samples[i].y, t);
        total_dy += samples[i].dy;
        if (ev.kind == PAD_EV_STEP) summed_dy += ev.dy;
        else check_event(ev, PAD_EV_NONE, 0, 0, "only the first 11 px are undecided");
    }
    CHECK_EQ(summed_dy, total_dy, "the steps sum to the travel fed in");
    CHECK_EQ(summed_dy, -1, "and that travel is 12 - 3 + 10 - 20");

    /* The same over an undecided start that changes direction: -3, +6, +6 reaches 12 px of
     * cumulative travel on a +9 px displacement. */
    pad_reset(&p);
    summed_dy = 0;
    check_event(press(&p, 400, 200, 0), PAD_EV_NONE, 0, 0, "a press yields no event");
    check_event(move(&p, 400, 197, 10), PAD_EV_NONE, 0, 0, "3 px up is undecided");
    check_event(move(&p, 400, 203, 20), PAD_EV_NONE, 0, 0, "9 px of travel is undecided");
    pad_ev_t ev = move(&p, 400, 209, 30);
    check_event(ev, PAD_EV_STEP, 9, 30,
                "the decision hands over the whole +9 px displacement");
    summed_dy += ev.dy;
    CHECK_EQ(summed_dy, 9, "the undecided start is not lost");
}

/* ------------------------------------------------------------- main */

int main(void)
{
    test_totality();
    test_press_initialises_and_hands_off();
    test_pre_decision_travel_is_cumulative();
    test_tie_goes_vertical();
    test_horizontal_never_steps();
    test_back_is_leftward_only();
    test_back_threshold();
    test_tap_edges();
    test_lost_and_reset();
    test_second_press_is_new();
    test_zero_time_sample();
    test_zero_travel_sample_keeps_the_clock();
    test_clock_going_back_is_clamped();
    test_conservation();

    TEST_END();
}
