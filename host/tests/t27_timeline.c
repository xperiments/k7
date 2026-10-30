/*
 * The Timeline bar's arithmetic, without LVGL.
 *
 * One bar is drawn twice - the Peek's floating strip and the footer band the Controls close the
 * screen with - so the text, the fraction and the width live in one module and both read it.
 * The tests pin the edges, because the edges are what two copies drift on: a length the decoder
 * did not report, an elapsed time past the duration, a fraction that truncates to no pixel at
 * all, and a long Track whose multiplication overflows 32 bits.
 *
 * The one input the hosts deliberately do not share is the position itself. The band has only
 * the status's elapsed and duration, so it derives one with timeline_permille(); the Peek reads
 * the cassette model's frame, which owns the position and is what the reels and the bar both
 * follow. This module does not close that difference - it makes the width they both draw from
 * one rule, and the tests pin that rule.
 */
#include "test_util.h"

#include <string.h>

#include "timeline.h"

static void check_clock(uint32_t ms, bool known, const char *want, const char *what)
{
    char buf[16];
    timeline_clock(buf, sizeof buf, ms, known);
    CHECK(strcmp(buf, want) == 0, "%s: got \"%s\", want \"%s\"", what, buf, want);
}

static void test_clock(void)
{
    check_clock(0, true, "0:00", "the start of a Track");
    check_clock(36000, true, "0:36", "a short Track: minutes unpadded, seconds to two places");
    check_clock(59999, true, "0:59", "a millisecond short of the minute is still that minute");
    check_clock(60000, true, "1:00", "and the next millisecond rolls it over");
    check_clock(216000, true, "3:36", "three minutes thirty-six");
    check_clock(3599000, true, "59:59", "the last second of the hour");
    check_clock(3600000, true, "60:00", "the format counts minutes, not hours");
    check_clock(216000, false, "--:--", "an unknown length prints the dashes, not a number");
    check_clock(0, false, "--:--", "even when the number happens to be zero");

    /* The buffer is the caller's: a zero length must write nothing, and a NULL must not crash. */
    char guard[8] = "xxxxxxx";
    timeline_clock(guard, 0, 216000, true);
    CHECK(strcmp(guard, "xxxxxxx") == 0, "a zero-length buffer is left alone");
    timeline_clock(NULL, sizeof guard, 216000, true);
}

static void test_permille(void)
{
    CHECK_EQ(timeline_permille(0, 180000), 0u, "the start of a three-minute Track");
    CHECK_EQ(timeline_permille(90000, 180000), 500u, "half way");
    CHECK_EQ(timeline_permille(180000, 180000), 1000u, "the end fills it");
    CHECK_EQ(timeline_permille(181000, 180000), 1000u, "an elapsed past the length is clamped");
    CHECK_EQ(timeline_permille(999, 1000000), 0u, "a thousandth of a thousandth is no position");
    CHECK_EQ(timeline_permille(60000, 0), 0u, "a length nobody knows is no position");
    CHECK_EQ(timeline_permille(0, 0), 0u, "not even at the start of it");

    /* 83 minutes in: the product is past 32 bits, so the multiplication has to be widened or a
     * long Track's fill jumps backwards. */
    CHECK_EQ(timeline_permille(5000000, 6000000), 833u, "an 83-minute Track still has a position");
}

static void test_fill(void)
{
    CHECK_EQ(timeline_fill_w(288, 0), 0, "an empty Track lights nothing");
    CHECK_EQ(timeline_fill_w(288, 500), 144, "half of the 288 px track");
    CHECK_EQ(timeline_fill_w(288, 1000), 288, "a full one fills it exactly");
    CHECK_EQ(timeline_fill_w(288, 1001), 288, "a fraction past 1000 is clamped to the track");
    CHECK_EQ(timeline_fill_w(288, 3), 0, "0.864 px is no pixel");
    CHECK_EQ(timeline_fill_w(288, 4), 1, "1.152 px is one, and it truncates rather than rounds");
    CHECK_EQ(timeline_fill_w(0, 500), 0, "no track, no fill");
    CHECK_EQ(timeline_fill_w(-10, 500), 0, "a negative width is no track either");
    CHECK_EQ(timeline_fill_w(1, 999), 0, "the last thousandth of one pixel lights nothing");
}

int main(void)
{
    test_clock();
    test_permille();
    test_fill();

    TEST_END();
}
