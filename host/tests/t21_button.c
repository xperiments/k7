/*
 * The button's pure rule, without LVGL: the tap guard, the ring table and the click
 * pulse's triangle.
 *
 * These are the parts of the drawer's and the DJ block's buttons that had drifted and
 * neither copy could be reached from the host. Every combination of the guard's inputs is
 * pinned, both known ring tables (the drawer's {2,3} and M/R's {2,2}), and the pulse at its
 * anchors, at its peak, and at every step of its symmetry. No display, no serial dump.
 */
#include "test_util.h"

#include "cassette_button_rule.h"

/* One conjunction, four combinations: a clean press is a tap, and either of the two wrong
 * things alone or in company is not. The named line per combination is what makes a dropped
 * term obvious - a test that only tried the two alone would still pass if the guard were
 * `!(moved && gesturing)`. */
static void test_tap_guard(void)
{
    CHECK(cassette_button_is_tap(false, false), "a clean press is a tap");
    CHECK(!cassette_button_is_tap(true, false), "a press that moved is not a tap");
    CHECK(!cassette_button_is_tap(false, true),
          "a press with a gesture direction is not a tap");
    CHECK(!cassette_button_is_tap(true, true), "moved and gesturing is not a tap");
}

static void test_ring_widths(void)
{
    /* The drawer's carcase, and the pair a button starts from. */
    const cassette_button_ring_t drawer = {
        CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED
    };
    /* M and R: the DJ block's carcase is shared, so it does not thicken. */
    const cassette_button_ring_t block = {
        CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W
    };
    /* Any other table is the spec's: the rule selects, it does not impose a value. */
    const cassette_button_ring_t thick = { 4, 1 };
    const cassette_button_ring_t thin = { 3, 2 };

    CHECK_EQ(drawer.released, 2, "the released ring is 2 px");
    CHECK_EQ(drawer.pressed, 3, "the pressed ring is 3 px");
    CHECK_EQ(cassette_button_ring_w(drawer, false), 2, "the drawer, released");
    CHECK_EQ(cassette_button_ring_w(drawer, true), 3, "the drawer, pressed");
    CHECK_EQ(cassette_button_ring_w(block, false), 2, "M and R, released");
    CHECK_EQ(cassette_button_ring_w(block, true), 2, "M and R do not thicken on a press");
    CHECK_EQ(cassette_button_ring_w(thick, false), 4, "a table is not clamped to a maximum");
    CHECK_EQ(cassette_button_ring_w(thick, true), 1, "or to a minimum");
    CHECK_EQ(cassette_button_ring_w(thin, false), 3, "a released width above the pressed one");
    CHECK_EQ(cassette_button_ring_w(thin, true), 2, "is still selected by state, not by size");
}

/* The pulse's own numbers and its anchors: the two ends, the quarter and three-quarter
 * points and the half. 24 px over 512 steps is 12 px at a quarter, so the triangle is not
 * mistaken for a straight line in either direction. */
static void test_pulse_anchors(void)
{
    const int32_t half = CASSETTE_BUTTON_PULSE_STEPS / 2;

    CHECK_EQ(CASSETTE_BUTTON_PULSE_SCALE, 256, "the rest scale is LV_SCALE_NONE");
    CHECK_EQ(CASSETTE_BUTTON_PULSE_PEAK, 232, "the peak is 232");
    CHECK_EQ(cassette_button_pulse_scale(0), 256, "step 0 is full size");
    CHECK_EQ(cassette_button_pulse_scale(256), 244, "a quarter of the run is 12 px down");
    CHECK_EQ(cassette_button_pulse_scale(half), 232, "the half is the peak");
    CHECK_EQ(cassette_button_pulse_scale(768), 244, "three quarters is 12 px down again");
    CHECK_EQ(cassette_button_pulse_scale(CASSETTE_BUTTON_PULSE_STEPS), 256,
             "the last step is full size, so the button lands where it started");
    CHECK_EQ(cassette_button_pulse_scale(-500), cassette_button_pulse_scale(0),
             "a step below the range is clamped to the start");
    CHECK_EQ(cassette_button_pulse_scale(2000), cassette_button_pulse_scale(1024),
             "a step above it is clamped to the end");
}

/* The whole curve, not just its anchors: the way down never rises, the way back never
 * falls, nothing leaves 232..256, the half lands exactly on 232, and every step is the
 * mirror of its partner - the property that lets a pulse be reversed or restarted without a
 * jump and needs no restoring call at the end. */
static void test_pulse_triangle(void)
{
    const int32_t half = CASSETTE_BUTTON_PULSE_STEPS / 2;
    int prev = cassette_button_pulse_scale(0);
    int32_t step;

    for (step = 1; step <= half; step++) {
        int scale = cassette_button_pulse_scale(step);
        CHECK(scale <= prev, "the way down rises again at step %d", (int)step);
        CHECK(scale <= CASSETTE_BUTTON_PULSE_SCALE, "above rest at step %d", (int)step);
        CHECK(scale >= CASSETTE_BUTTON_PULSE_PEAK, "below the peak at step %d", (int)step);
        prev = scale;
    }
    CHECK_EQ(prev, CASSETTE_BUTTON_PULSE_PEAK, "the half lands exactly on the peak");

    for (step = half + 1; step <= CASSETTE_BUTTON_PULSE_STEPS; step++) {
        int scale = cassette_button_pulse_scale(step);
        CHECK(scale >= prev, "the way back falls again at step %d", (int)step);
        CHECK(scale <= CASSETTE_BUTTON_PULSE_SCALE, "above rest at step %d", (int)step);
        CHECK(scale >= CASSETTE_BUTTON_PULSE_PEAK, "below the peak at step %d", (int)step);
        prev = scale;
    }
    CHECK_EQ(prev, CASSETTE_BUTTON_PULSE_SCALE, "the last step lands on rest");

    for (step = 0; step <= CASSETTE_BUTTON_PULSE_STEPS; step++) {
        CHECK(cassette_button_pulse_scale(step) ==
                  cassette_button_pulse_scale(CASSETTE_BUTTON_PULSE_STEPS - step),
              "the triangle is not symmetric at step %d", (int)step);
    }
}

int main(void)
{
    test_tap_guard();
    test_ring_widths();
    test_pulse_anchors();
    test_pulse_triangle();

    TEST_END();
}
