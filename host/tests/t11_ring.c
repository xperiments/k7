/*
 * T19: the send ring between the broadcast and a client's sender.
 *
 * The audio task must never block on a socket, so the hand-off is a ring: put as
 * much as fits, read at the consumer's pace, and never read past what was
 * written. The wrap in both directions is the part that is easy to get wrong.
 */
#include "test_util.h"

#include "mi_ring.h"

int main(void)
{
    mi_ring_t *r = mi_ring_new(8, false);
    CHECK(r != NULL, "an 8-byte ring is accepted");
    if (!r) return 1;
    CHECK_EQ(mi_ring_capacity(r), 8, "the capacity is what was asked for");
    CHECK_EQ(mi_ring_used(r), 0, "a new ring is empty");

    CHECK_EQ(mi_ring_put(r, "abcdef", 6), 6, "six bytes fit");
    CHECK_EQ(mi_ring_used(r), 6, "used follows the put");
    uint8_t out[16];
    CHECK_EQ(mi_ring_get(r, out, 4), 4, "four bytes come back");
    CHECK(memcmp(out, "abcd", 4) == 0, "in order");

    /* Six more bytes into two free: the write wraps from the end to the start. */
    CHECK_EQ(mi_ring_put(r, "ghijkl", 6), 6, "six more fit in the remaining room");
    CHECK_EQ(mi_ring_used(r), 8, "the ring is now full");
    CHECK_EQ(mi_ring_put(r, "x", 1), 0, "a full ring accepts nothing");

    /* Read everything: the read wraps too, and the order is preserved. */
    CHECK_EQ(mi_ring_get(r, out, sizeof out), 8, "all eight bytes come back");
    CHECK(memcmp(out, "efghijkl", 8) == 0, "in order across the wrap");
    CHECK_EQ(mi_ring_get(r, out, sizeof out), 0, "an empty ring returns nothing");

    /* A short read leaves the rest for next time. */
    CHECK_EQ(mi_ring_put(r, "01234567", 8), 8, "eight bytes fit");
    CHECK_EQ(mi_ring_get(r, out, 3), 3, "a three-byte read");
    CHECK(memcmp(out, "012", 3) == 0, "takes the front");
    CHECK_EQ(mi_ring_used(r), 5, "the rest stays queued");
    CHECK_EQ(mi_ring_get(r, out, sizeof out), 5, "and comes back next time");
    CHECK(memcmp(out, "34567", 5) == 0, "in order");

    mi_ring_reset(r);
    CHECK_EQ(mi_ring_used(r), 0, "reset empties the ring");
    CHECK_EQ(mi_ring_put(r, "ab", 2), 2, "and it accepts after a reset");

    CHECK(mi_ring_new(0, false) == NULL, "a zero-capacity ring is refused");
    mi_ring_free(r);
    mi_ring_free(NULL);
    TEST_END();
}
