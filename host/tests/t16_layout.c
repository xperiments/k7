/*
 * The Cassette screen's design grid, without LVGL.
 *
 * The view used to convert design coordinates through a file-static pose and three
 * macros. This pins the conversion: the identity grid, the centring at a smaller
 * scale, the offset, the Label peek's pose, the transport drawer's pose, the
 * even-diameter rule and the interpolation the animator eases. No display, no serial
 * dump.
 */
#include "test_util.h"

#include "cassette_layout.h"

static void test_identity(void)
{
    cassette_pose_t p = cassette_pose(CASSETTE_SCALE_NONE, 0, 0);

    CHECK_EQ(cassette_len(p, CASSETTE_DESIGN_W), 800, "the full width");
    CHECK_EQ(cassette_len(p, CASSETTE_DESIGN_H), 480, "the full height");
    CHECK_EQ(cassette_origin_x(p), 0, "no margin on x");
    CHECK_EQ(cassette_origin_y(p), 0, "no margin on y");
    CHECK_EQ(cassette_x(p, 216), 216, "a hub's x");
    CHECK_EQ(cassette_y(p, 206), 206, "a hub's y");
    CHECK_EQ(cassette_len_even(p, 39), 38, "an odd design length rounds down to even");
}

static void test_half_scale_centres(void)
{
    cassette_pose_t p = cassette_pose(128, 0, 0);

    CHECK_EQ(cassette_len(p, 800), 400, "half the width");
    CHECK_EQ(cassette_len(p, 480), 240, "half the height");
    CHECK_EQ(cassette_origin_x(p), 200, "the box is centred on x");
    CHECK_EQ(cassette_origin_y(p), 120, "the box is centred on y");
    CHECK_EQ(cassette_x(p, 0), 200, "the left edge");
    CHECK_EQ(cassette_x(p, 800), 600, "the right edge");
    CHECK_EQ(cassette_y(p, 0), 120, "the top edge");
    CHECK_EQ(cassette_y(p, 480), 360, "the bottom edge");
    CHECK_EQ(cassette_x(p, 216), 308, "a hub's x moves with the box");
    CHECK_EQ(cassette_y(p, 206), 223, "a hub's y moves with the box");
}

static void test_offset_is_screen_pixels(void)
{
    cassette_pose_t p = cassette_pose(CASSETTE_SCALE_NONE, 10, -20);

    CHECK_EQ(cassette_origin_x(p), 10, "the offset is not scaled");
    CHECK_EQ(cassette_origin_y(p), -20, "a negative offset");
    CHECK_EQ(cassette_x(p, 0), 10, "the left edge");
    CHECK_EQ(cassette_y(p, 0), -20, "the top edge");
    CHECK_EQ(cassette_x(p, 800), 810, "the right edge");
}

static void test_label_peek_pose(void)
{
    /* What the Label's peek uses: 0.78x, moved up so the bar clears the tape. */
    cassette_pose_t p = cassette_pose(200, 0, -60);

    CHECK_EQ(cassette_len(p, 800), 625, "the peek width");
    CHECK_EQ(cassette_origin_x(p), 87, "the peek is centred");
    CHECK_EQ(cassette_len(p, 480), 375, "the peek height");
    CHECK_EQ(cassette_origin_y(p), -8, "the peek's origin includes the lift");
    CHECK_EQ(cassette_y(p, 0), -8, "the top edge is lifted with it");
    CHECK_EQ(cassette_len(p, 568), 443, "the Label's width at the peek");
    CHECK_EQ(cassette_len(p, 52), 40, "the Label's height at the peek");
    CHECK_EQ(cassette_len_even(p, 39), 30, "the roller at the peek");
}

static void test_transport_drawer_pose(void)
{
    /* What the transport drawer uses: 0.52x, pulled 51 px left so the tape starts at
     * 148 px instead of the centred 199, and lifted 20 px onto the button column. */
    cassette_pose_t p = cassette_pose(134, -51, -20);

    CHECK_EQ(cassette_len(p, 800), 418, "the design box at the drawer's scale");
    CHECK_EQ(cassette_origin_x(p), 140, "the box is 51 px left of centre");
    CHECK_EQ(cassette_x(p, 16), 148, "the tape's outer frame starts at 148");
    CHECK_EQ(cassette_x(p, 784), 550, "and ends at 550");
    CHECK_EQ(cassette_x(p, 784) - cassette_x(p, 16), 402, "the tape is 402 px wide");
    CHECK_EQ(cassette_len(p, 480), 251, "the scaled height is odd, so it truncates");
    CHECK_EQ(cassette_origin_y(p), 94, "the box's top, centring then the -20 px lift");
    CHECK_EQ(cassette_y(p, 16), 102, "the frame's top edge");
    CHECK_EQ(cassette_y(p, 460), 334, "the bottom chips, the lowest ink");
    CHECK_EQ(p.dx, -51, "the drawer's horizontal offset is kept");
}

static void test_rounding(void)
{
    cassette_pose_t p200 = cassette_pose(200, 0, 0);
    cassette_pose_t p128 = cassette_pose(128, 0, 0);

    CHECK_EQ(cassette_len(p200, 1), 0, "a design pixel can round to nothing");
    CHECK_EQ(cassette_len(p200, 2), 1, "and the next one does not");
    CHECK_EQ(cassette_len(p128, 39), 19, "an odd truncated length");
    CHECK_EQ(cassette_len_even(p128, 39), 18, "rounded down to even");
    CHECK_EQ(cassette_even(0), 0, "zero stays zero");
    CHECK_EQ(cassette_even(7), 6, "seven rounds to six");
    CHECK_EQ(cassette_even(8), 8, "an even length is left alone");
}

static void test_scale_clamp(void)
{
    CHECK_EQ(cassette_pose(1, 0, 0).scale, CASSETTE_SCALE_MIN, "1 clamps to the floor");
    CHECK_EQ(cassette_pose(0, 0, 0).scale, CASSETTE_SCALE_MIN, "0 clamps to the floor");
    CHECK_EQ(cassette_pose(2000, 0, 0).scale, CASSETTE_SCALE_MAX, "2000 clamps to the ceiling");
    CHECK_EQ(cassette_pose(CASSETTE_SCALE_NONE, 0, 0).scale, CASSETTE_SCALE_NONE,
             "the design scale is left alone");
}

static void test_lerp(void)
{
    cassette_pose_t from = cassette_pose(CASSETTE_SCALE_NONE, 0, 0);
    cassette_pose_t to = cassette_pose(200, 0, -60);
    cassette_pose_t mid = cassette_pose_lerp(from, to, 512);

    CHECK_EQ(cassette_lerp(0, 100, 0), 0, "step 0 is the from value");
    CHECK_EQ(cassette_lerp(0, 100, 1024), 100, "step 1024 is the to value");
    CHECK_EQ(cassette_lerp(0, 100, 512), 50, "half way");
    CHECK_EQ(cassette_lerp(-60, 0, 256), -45, "a negative direction");

    CHECK_EQ(mid.scale, 228, "the scale is mixed");
    CHECK_EQ(mid.dx, 0, "the x offset is unchanged");
    CHECK_EQ(mid.dy, -30, "the y offset is mixed");

    mid = cassette_pose_lerp(from, to, 0);
    CHECK_EQ(mid.scale, 256, "the animation starts from the from pose");
    mid = cassette_pose_lerp(from, to, 1024);
    CHECK_EQ(mid.scale, 200, "and lands on the to pose");
    CHECK_EQ(mid.dy, -60, "with no rounding left behind");
}

int main(void)
{
    test_identity();
    test_half_scale_centres();
    test_offset_is_screen_pixels();
    test_label_peek_pose();
    test_transport_drawer_pose();
    test_rounding();
    test_scale_clamp();
    test_lerp();

    TEST_END();
}
