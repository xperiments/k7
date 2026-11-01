/*
 * T19: the shuffle order's arithmetic.
 *
 * One permutation, walked both ways. The broadcast's lap reset, `prev`, an explicit
 * pick and a generation rebuild all lean on these calls, and a wrong cursor does not
 * look like a bug on a progress bar: it looks like a Track the mode repeated or one
 * `prev` skipped. This is the pure unit - an injected seed, no broadcast task and no
 * audio - so the sequence is exactly what t19 pins.
 */
#include "test_util.h"

#include <stdbool.h>
#include <stdint.h>

#include "mi_shuffle.h"

/* The library ceiling is 20,000 Tracks; a thousand is enough to make a repeated or
 * missing index stand out without making the failure output unreadable. */
#define COUNT 1000

static uint32_t order[COUNT];
static uint32_t other[COUNT];

static bool is_permutation(const uint32_t *o, uint32_t count)
{
    unsigned char seen[COUNT] = {0};
    if (count > COUNT) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (o[i] >= count || seen[o[i]]) return false;
        seen[o[i]] = 1;
    }
    return true;
}

static void test_coverage(void)
{
    mi_shuffle_t s = {0};
    mi_shuffle_seed(&s, 0x12345678u);
    CHECK(mi_shuffle_build(&s, order, COUNT, 0), "the order builds");
    CHECK(mi_shuffle_ready(&s), "the order is ready to walk");
    CHECK_EQ(s.count, COUNT, "the order holds every Track");
    CHECK(is_permutation(order, COUNT), "every track index appears exactly once");
    CHECK_EQ(mi_shuffle_current(&s), 0, "the playing Track is at the cursor");
}

static void test_next(void)
{
    mi_shuffle_t s = {0};
    const uint32_t playing = COUNT / 2;
    mi_shuffle_seed(&s, 7u);
    CHECK(mi_shuffle_build(&s, order, COUNT, playing), "the order builds around a Track");
    uint32_t pos = s.cursor;
    CHECK_EQ(mi_shuffle_current(&s), playing, "the cursor names the Track playing now");

    bool follows = true, reports = true;
    uint32_t wraps = 0;
    for (uint32_t i = 0; i < COUNT; i++) {
        bool wrapped = false;
        uint32_t got = mi_shuffle_next(&s, &wrapped);
        pos = (pos + 1) % COUNT;
        if (got != order[pos]) follows = false;
        if (wrapped != (pos == 0)) reports = false;
        if (wrapped) wraps++;
    }
    CHECK(follows, "next walks the order element by element");
    CHECK(reports, "the wrap is reported exactly at the lap's first element");
    CHECK_EQ(wraps, 1, "one lap of advances wraps once");
    CHECK_EQ(mi_shuffle_current(&s), playing, "a full lap comes back to the Track playing now");

    /* The first element of a lap is the wrap the broadcast uses to reset its
     * history; a step inside the lap must not report it. */
    CHECK(mi_shuffle_seek(&s, order[0]), "the lap's first element can be sought");
    bool wrapped = true;
    CHECK_EQ(mi_shuffle_next(&s, &wrapped), order[1], "the step after the first element is the second");
    CHECK(!wrapped, "a step inside a lap does not wrap");

    CHECK(mi_shuffle_seek(&s, order[COUNT - 1]), "the lap's last element can be sought");
    wrapped = false;
    CHECK_EQ(mi_shuffle_next(&s, &wrapped), order[0], "the step after the last element is the first");
    CHECK(wrapped, "the step into the first element reports the wrap");
}

static void test_prev(void)
{
    mi_shuffle_t s = {0};
    mi_shuffle_seed(&s, 42u);
    CHECK(mi_shuffle_build(&s, order, COUNT, 123), "the order builds around a Track");
    uint32_t pos = s.cursor;
    bool follows = true;
    for (uint32_t i = 0; i < COUNT; i++) {
        uint32_t got = mi_shuffle_prev(&s);
        pos = pos ? pos - 1 : COUNT - 1;
        if (got != order[pos]) follows = false;
    }
    CHECK(follows, "prev walks the order backwards and wraps");
    CHECK_EQ(mi_shuffle_current(&s), 123, "a full lap back returns to the Track playing now");

    /* From the first position, prev must wrap to the last element. */
    CHECK(mi_shuffle_seek(&s, order[0]), "the lap's first element can be sought");
    CHECK_EQ(mi_shuffle_prev(&s), order[COUNT - 1], "prev at the first element wraps to the last");
    CHECK_EQ(mi_shuffle_current(&s), order[COUNT - 1], "the cursor follows prev");
}

static void test_seek(void)
{
    mi_shuffle_t s = {0};
    mi_shuffle_seed(&s, 99u);
    CHECK(mi_shuffle_build(&s, order, COUNT, 0), "the order builds");
    uint32_t pos = COUNT / 3;
    uint32_t picked = order[pos];
    CHECK(mi_shuffle_seek(&s, picked), "seek finds a Track in the order");
    CHECK_EQ(mi_shuffle_current(&s), picked, "seek puts that Track at the cursor");
    bool wrapped = true;
    uint32_t got = mi_shuffle_next(&s, &wrapped);
    CHECK_EQ(got, order[(pos + 1) % COUNT], "next continues after the seek");
    CHECK(!wrapped, "a seek inside the lap does not wrap the next step");

    CHECK(!mi_shuffle_seek(&s, COUNT), "seek refuses an index outside the broadcast order");
    CHECK_EQ(mi_shuffle_current(&s), order[(pos + 1) % COUNT],
             "a refused seek leaves the cursor where next left it");
}

static void test_seeds(void)
{
    mi_shuffle_t a = {0}, b = {0}, c = {0};
    mi_shuffle_seed(&a, 0xDEADBEEFu);
    mi_shuffle_seed(&b, 0xDEADBEEFu);
    mi_shuffle_seed(&c, 0xDEADBEF0u);
    CHECK(mi_shuffle_build(&a, order, COUNT, 0), "the first order builds");
    CHECK(mi_shuffle_build(&b, other, COUNT, 0), "the second order builds with the same seed");
    CHECK(memcmp(order, other, sizeof order) == 0, "the same seed builds the same order");

    CHECK(mi_shuffle_build(&c, order, COUNT, 0), "a third order builds with a new seed");
    CHECK(memcmp(order, other, sizeof order) != 0, "a different seed builds a different order");

    /* Rebuilding without a new seed must draw a new order, or turning the mode off
     * and on again would repeat the permutation it just finished. */
    CHECK(mi_shuffle_build(&c, order, COUNT, 0), "a rebuild draws again");
    CHECK(memcmp(order, other, sizeof order) != 0, "the rebuild is not the previous order");
}

static void test_edges(void)
{
    mi_shuffle_t s = {0};
    mi_shuffle_seed(&s, 5u);

    CHECK(!mi_shuffle_build(&s, order, 0, 0), "an empty order cannot be built");
    CHECK(!mi_shuffle_ready(&s), "an empty order is not ready");
    CHECK_EQ(mi_shuffle_current(&s), 0, "an empty order has no current Track");
    bool wrapped = true;
    CHECK_EQ(mi_shuffle_next(&s, &wrapped), 0, "next on an empty order returns 0");
    CHECK(!wrapped, "next on an empty order reports no wrap");
    CHECK_EQ(mi_shuffle_prev(&s), 0, "prev on an empty order returns 0");
    CHECK(!mi_shuffle_seek(&s, 0), "seek on an empty order fails");

    CHECK(mi_shuffle_build(&s, order, 1, 0), "a one-Track order builds");
    CHECK(mi_shuffle_ready(&s), "a one-Track order is ready");
    CHECK_EQ(order[0], 0, "the only Track is track index 0");
    CHECK_EQ(mi_shuffle_current(&s), 0, "the only Track is at the cursor");
    wrapped = false;
    CHECK_EQ(mi_shuffle_next(&s, &wrapped), 0, "next on one Track returns it");
    CHECK(wrapped, "next on one Track wraps every time, like the linear lap");
    CHECK_EQ(mi_shuffle_prev(&s), 0, "prev on one Track returns it");
    CHECK(mi_shuffle_seek(&s, 0), "seek to the only Track succeeds");
    CHECK(!mi_shuffle_seek(&s, 1), "seek past a one-Track order fails");

    CHECK(!mi_shuffle_build(&s, NULL, COUNT, 0), "a NULL array is refused, not written to");
    CHECK(!mi_shuffle_ready(&s), "a refused build leaves no order");
    CHECK_EQ(mi_shuffle_next(&s, &wrapped), 0, "next after a refused build is a no-op");
}

static void test_rebuild(void)
{
    mi_shuffle_t s = {0};
    mi_shuffle_seed(&s, 2024u);
    CHECK(mi_shuffle_build(&s, order, 40, 0), "the first order builds");
    CHECK_EQ(s.count, 40, "the first order holds 40 Tracks");
    CHECK(is_permutation(order, 40), "the first order covers 0..39");

    /* A rescan's library can be smaller or larger; the caller reuses the same struct
     * and array and the build starts over from the new count. */
    CHECK(mi_shuffle_build(&s, order, 37, 12), "a smaller order builds on the same struct");
    CHECK_EQ(s.count, 37, "the rebuilt order holds 37 Tracks");
    CHECK(is_permutation(order, 37), "the rebuilt order covers 0..36 and nothing else");
    CHECK_EQ(mi_shuffle_current(&s), 12, "the anchor Track is at the cursor after the rebuild");

    CHECK(mi_shuffle_build(&s, order, COUNT, COUNT - 1), "a larger order builds on the same struct");
    CHECK_EQ(s.count, COUNT, "the larger order holds every Track");
    CHECK(is_permutation(order, COUNT), "the larger order covers the new range");
    CHECK_EQ(mi_shuffle_current(&s), COUNT - 1, "the anchor Track is at the cursor again");
}

int main(void)
{
    test_coverage();
    test_next();
    test_prev();
    test_seek();
    test_seeds();
    test_edges();
    test_rebuild();
    TEST_END();
}
