#include "player_recv.h"

#include "espnow_stream.h"
#include "test_util.h"

/* E-T2 and E-T3's player half: fragments reassemble in sequence order, a frame that does not
 * arrive is named in the status packet while the frames behind it wait, a resend inside the
 * second restores the order, and a gap that outlives the second is abandoned and counted.
 * No radio: the packets are the same bytes the emitter's host test builds. */

static uint8_t g_packets[ES_MAX_FRAGS * ES_MTU];
static size_t  g_lens[ES_MAX_FRAGS];

/* What the receiver handed out, in order. */
static struct {
    uint16_t seq[16];
    uint16_t len[16];
    uint8_t  data[16][ES_MAX_FRAME];
    unsigned n;
} out;

static void on_frame(void *ctx, const uint8_t *frame, size_t len)
{
    (void)ctx;
    if (out.n >= 16) return;
    /* The test's frames carry their sequence in their third and fourth bytes. */
    out.seq[out.n] = (uint16_t)((frame[2] << 8) | frame[3]);
    out.len[out.n] = (uint16_t)len;
    memcpy(out.data[out.n], frame, len);
    out.n++;
}

static void out_reset(void) { out.n = 0; }

/* A 417-byte frame whose bytes say which frame it is, sent as its two fragments. */
static void feed_frame(player_recv_t *r, uint16_t seq, uint32_t now)
{
    static uint8_t frame[417];
    memset(frame, 0, sizeof frame);
    frame[0] = 0xFF;
    frame[1] = 0xFB;
    frame[2] = (uint8_t)(seq >> 8);
    frame[3] = (uint8_t)seq;
    size_t n = es_fragment(seq, frame, sizeof frame, g_packets, g_lens);
    CHECK_EQ(n, 2, "a 417-byte frame is two packets");
    for (size_t i = 0; i < n; i++)
        player_recv_packet(r, g_packets + i * ES_MTU, g_lens[i], now, on_frame, NULL);
}

/* Frames in order come out in order, whole. */
static void test_in_order(void)
{
    static player_recv_t r;
    static uint8_t storage[(PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                           (PLAYER_RECV_PARTS * sizeof(player_recv_part_t))];
    CHECK(player_recv_init(&r, storage, sizeof storage), "the receiver's buffers are given");
    out_reset();
    for (uint16_t seq = 0; seq < 5; seq++) feed_frame(&r, seq, 100);

    CHECK_EQ(out.n, 5, "five frames delivered");
    for (unsigned i = 0; i < out.n; i++) {
        CHECK_EQ(out.seq[i], (uint16_t)i, "in sequence order");
        CHECK_EQ(out.len[i], 417, "whole");
    }
    player_recv_stats_t st;
    player_recv_stats(&r, &st);
    CHECK_EQ(st.lost, 0, "nothing lost");
}

/* A missing frame is named, the frames behind it wait, and the resend restores the order. */
static void test_gap_and_resend(void)
{
    static player_recv_t r;
    static uint8_t storage[(PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                           (PLAYER_RECV_PARTS * sizeof(player_recv_part_t))];
    CHECK(player_recv_init(&r, storage, sizeof storage), "the receiver's buffers are given");
    out_reset();

    feed_frame(&r, 0, 1000); /* delivered */
    feed_frame(&r, 2, 1000); /* held: 1 is missing */
    feed_frame(&r, 3, 1000);

    CHECK_EQ(out.n, 1, "only the frame before the gap came out");
    player_recv_stats_t st;
    player_recv_stats(&r, &st);
    CHECK_EQ(st.frames, 1, "one delivered");

    es_status_t s;
    player_recv_status(&r, 50000, &s);
    CHECK_EQ(s.free_bytes, 50000, "the credit is the ring's");
    CHECK_EQ(s.highest, 0, "the highest held without a gap");
    CHECK_EQ(s.nack_count, 1, "one frame is missing");
    CHECK_EQ(s.nack[0], 1, "and it is the one that did not arrive");

    /* The resend, inside the second. */
    feed_frame(&r, 1, 1200);
    CHECK_EQ(out.n, 4, "the gap filled and everything behind it followed");
    for (unsigned i = 0; i < out.n; i++) CHECK_EQ(out.seq[i], (uint16_t)i, "in order");
}

/* A gap that outlives the second is abandoned: what was held comes out, and the loss is counted. */
static void test_abandon(void)
{
    static player_recv_t r;
    static uint8_t storage[(PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                           (PLAYER_RECV_PARTS * sizeof(player_recv_part_t))];
    CHECK(player_recv_init(&r, storage, sizeof storage), "the receiver's buffers are given");
    out_reset();

    feed_frame(&r, 0, 1000);
    feed_frame(&r, 2, 1000);
    feed_frame(&r, 3, 1000);
    CHECK_EQ(out.n, 1, "the gap holds what is behind it");

    player_recv_tick(&r, 1500, on_frame, NULL); /* not yet */
    CHECK_EQ(out.n, 1, "a tick inside the second changes nothing");

    player_recv_tick(&r, 2100, on_frame, NULL); /* a second gone */
    CHECK_EQ(out.n, 3, "the held frames are delivered");
    CHECK_EQ(out.seq[1], 2, "starting with the oldest that arrived");
    CHECK_EQ(out.seq[2], 3, "and the next");

    player_recv_stats_t st;
    player_recv_stats(&r, &st);
    CHECK_EQ(st.lost, 1, "one frame was lost");
    CHECK_EQ(st.abandoned, 1, "in one abandoned gap");

    /* A resend that arrives after the abandonment is a duplicate, not a rewind. */
    feed_frame(&r, 1, 2200);
    CHECK_EQ(out.n, 3, "a late frame does not rewind the stream");
}

/* The sequence wraps, and the order survives it. */
static void test_wrap(void)
{
    static player_recv_t r;
    static uint8_t storage[(PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                           (PLAYER_RECV_PARTS * sizeof(player_recv_part_t))];
    CHECK(player_recv_init(&r, storage, sizeof storage), "the receiver's buffers are given");
    out_reset();

    feed_frame(&r, 65534, 100);
    feed_frame(&r, 65535, 100);
    feed_frame(&r, 0, 100);
    feed_frame(&r, 1, 100);

    CHECK_EQ(out.n, 4, "four frames across the wrap");
    CHECK_EQ(out.seq[1], 65535, "the one before the wrap");
    CHECK_EQ(out.seq[2], 0, "the first after it");
    CHECK_EQ(out.seq[3], 1, "and the next");

    player_recv_stats_t st;
    player_recv_stats(&r, &st);
    CHECK_EQ(st.lost, 0, "nothing lost across the wrap");
}

/* A fragment that arrives twice does not double a frame or move the stream. */
static void test_duplicate_fragment(void)
{
    static player_recv_t r;
    static uint8_t storage[(PLAYER_RECV_HELD * sizeof(player_recv_held_t)) +
                           (PLAYER_RECV_PARTS * sizeof(player_recv_part_t))];
    CHECK(player_recv_init(&r, storage, sizeof storage), "the receiver's buffers are given");
    out_reset();

    static uint8_t frame[417];
    memset(frame, 0x22, sizeof frame);
    size_t n = es_fragment(7, frame, sizeof frame, g_packets, g_lens);
    player_recv_packet(&r, g_packets, g_lens[0], 100, on_frame, NULL);
    player_recv_packet(&r, g_packets, g_lens[0], 100, on_frame, NULL); /* again */
    player_recv_packet(&r, g_packets + ES_MTU, g_lens[1], 100, on_frame, NULL);
    CHECK_EQ(out.n, 1, "one frame, not two");

    player_recv_packet(&r, g_packets, g_lens[0], 100, on_frame, NULL); /* after delivery */
    CHECK_EQ(out.n, 1, "and a fragment behind the stream is not redelivered");
    (void)n;
}

int main(void)
{
    test_in_order();
    test_gap_and_resend();
    test_abandon();
    test_wrap();
    test_duplicate_fragment();
    TEST_END();
}
