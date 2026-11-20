#include "espnow_stream.h"
#include "test_util.h"

/* E-T1-E3's first half: the wire. A frame of every legal length fragments and reassembles byte
 * for byte, a damaged packet is refused rather than spliced, and the status packet carries the
 * credit and the list of what the receiver missed. */
static uint8_t g_packets[ES_MAX_FRAGS * ES_MTU];

/* Build a frame that is not all one byte, so a fragment copied in the wrong order shows up. */
static void fill(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(i * 7u + 3u);
}

static void test_round_trip(size_t len, size_t want_frags)
{
    static uint8_t frame[1441];
    static uint8_t joined[1441];
    size_t lens[ES_MAX_FRAGS];
    fill(frame, len);

    size_t n = es_fragment(1234, frame, len, g_packets, lens);
    CHECK_EQ(n, want_frags, "the fragment count");

    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        es_frag_t f;
        CHECK(lens[i] <= ES_MTU, "a packet fits the radio's payload");
        CHECK(es_parse_frag(g_packets + i * ES_MTU, lens[i], &f), "the packet parses");
        CHECK_EQ(f.seq, 1234, "its sequence");
        CHECK_EQ(f.frag, i, "its index, in order");
        CHECK_EQ(f.nfrags, want_frags, "its count");
        memcpy(joined + off, f.payload, f.payload_len);
        off += f.payload_len;
    }
    CHECK_EQ(off, len, "the payloads add up to the frame");
    CHECK(memcmp(joined, frame, len) == 0, "and reassemble byte for byte");
}

static void test_fragments(void)
{
    /* The station's 128 kbps frame, the 256 kbps one, and the format's largest. */
    test_round_trip(417, 2);
    test_round_trip(836, 4);
    test_round_trip(1441, 6);
    test_round_trip(244, 1);

    /* Six fragments carry 6 x 244 = 1 464 bytes; one more needs a seventh, which the format
     * does not have (the encoder's largest frame is 1 441 and fits). */
    static uint8_t big[1465];
    size_t lens[ES_MAX_FRAGS];
    fill(big, sizeof big);
    CHECK_EQ(es_fragment(1, big, sizeof big, g_packets, lens), 0, "a frame over six fragments is refused");
    CHECK_EQ(es_fragment(1, big, 0, g_packets, lens), 0, "an empty frame is refused");
}

static void test_parse_refuses(void)
{
    static uint8_t frame[417];
    size_t lens[ES_MAX_FRAGS];
    fill(frame, sizeof frame);
    size_t n = es_fragment(7, frame, sizeof frame, g_packets, lens);
    CHECK_EQ(n, 2, "the sample frame is two packets");

    es_frag_t f;
    uint8_t bad[ES_MTU];
    memcpy(bad, g_packets, lens[0]);

    bad[0] = 2; /* a version from the future */
    CHECK(!es_parse_frag(bad, lens[0], &f), "a wrong version is not a fragment");
    memcpy(bad, g_packets, lens[0]);
    bad[1] = ES_TYPE_STATUS;
    CHECK(!es_parse_frag(bad, lens[0], &f), "a status packet is not an audio fragment");
    memcpy(bad, g_packets, lens[0]);
    bad[4] = 2; /* index past the count */
    CHECK(!es_parse_frag(bad, lens[0], &f), "an index past the count is refused");
    memcpy(bad, g_packets, lens[0]);
    bad[5] = 0;
    CHECK(!es_parse_frag(bad, lens[0], &f), "a count of zero is refused");
    memcpy(bad, g_packets, lens[0]);
    bad[5] = ES_MAX_FRAGS + 1;
    CHECK(!es_parse_frag(bad, lens[0], &f), "a count over the ceiling is refused");
    /* The first of two fragments must be full: a short one is a damaged packet. */
    CHECK(!es_parse_frag(g_packets, ES_HEADER + 10, &f), "a short non-final fragment is refused");
    CHECK(!es_parse_frag(g_packets, 3, &f), "a packet shorter than a header is refused");
}

static void test_status(void)
{
    es_status_t s;
    memset(&s, 0, sizeof s);
    s.free_bytes = 96000;
    s.highest = 4321;
    CHECK_EQ(es_status_encode(&s, g_packets, ES_MTU), 9,
             "a status with no gaps is nine bytes with the flags byte");

    es_status_t back;
    CHECK(es_status_parse(g_packets, 9, &back), "it parses");
    CHECK_EQ(back.free_bytes, 96000, "the credit survives");
    CHECK_EQ(back.highest, 4321, "and the highest sequence");
    CHECK_EQ(back.nack_count, 0, "with no gaps");
    CHECK_EQ(back.flags, 0, "and no flags");

    /* An old player's packet ends after the list: the flags byte is absent, not zero-length. */
    CHECK(es_status_parse(g_packets, 8, &back), "a packet without the flags byte parses");
    CHECK_EQ(back.flags, 0, "as flags 0, which is an old player");

    s.flags = ES_STATUS_FLAG_TONE;
    CHECK_EQ(es_status_encode(&s, g_packets, ES_MTU), 9, "the flags byte is still one byte");
    CHECK(es_status_parse(g_packets, 9, &back), "it parses");
    CHECK_EQ(back.flags, ES_STATUS_FLAG_TONE, "and the tone flag survives");

    s.flags = 0;
    s.nack_count = 3;
    s.nack[0] = 9;
    s.nack[1] = 11;
    s.nack[2] = 12;
    size_t n = es_status_encode(&s, g_packets, ES_MTU);
    CHECK_EQ(n, 8 + 6 + 1, "three gaps add six bytes and the flags byte");
    CHECK(es_status_parse(g_packets, n, &back), "it parses");
    CHECK_EQ(back.nack_count, 3, "the gap count");
    CHECK_EQ(back.nack[0], 9, "the first gap");
    CHECK_EQ(back.nack[2], 12, "and the last");

    /* Eight is the ceiling; nine is not a status packet. */
    s.nack_count = ES_MAX_NACK + 1;
    CHECK_EQ(es_status_encode(&s, g_packets, ES_MTU), 0, "nine gaps are not encodable");

    s.nack_count = 3;
    n = es_status_encode(&s, g_packets, ES_MTU);
    CHECK(!es_status_parse(g_packets, n - 2, &back), "a truncated list is refused");
    CHECK(!es_status_parse(g_packets, 4, &back), "a truncated header is refused");
    g_packets[0] = 9; /* a version from the future */
    CHECK(!es_status_parse(g_packets, n, &back), "a wrong version is not a status packet");
}

/* E12: the tone packet is ten bytes, absolute centi-dB, and both ends clamp. */
static void test_tone(void)
{
    es_tone_t t = {
        .gain_cdb = 600,
        .low_cdb = -2600,
        .mid_cdb = 0,
        .high_cdb = 600,
    };
    uint8_t pkt[ES_MTU];
    es_tone_t back;
    CHECK_EQ(es_tone_encode(&t, pkt, sizeof pkt), ES_TONE_SIZE, "the packet is ten bytes");
    CHECK(es_tone_parse(pkt, ES_TONE_SIZE, &back), "it parses");
    CHECK_EQ(back.gain_cdb, 600, "the gain");
    CHECK_EQ(back.low_cdb, -2600, "the low band");
    CHECK_EQ(back.mid_cdb, 0, "the mid band");
    CHECK_EQ(back.high_cdb, 600, "the high band");

    /* A value past the knob's end is clamped, not dropped. */
    t.gain_cdb = 5000;
    t.low_cdb = -5000;
    CHECK_EQ(es_tone_encode(&t, pkt, sizeof pkt), ES_TONE_SIZE, "an out-of-range value still encodes");
    CHECK(es_tone_parse(pkt, ES_TONE_SIZE, &back), "and parses");
    CHECK_EQ(back.gain_cdb, ES_TONE_GAIN_MAX_CDB, "the gain is clamped");
    CHECK_EQ(back.low_cdb, ES_TONE_BAND_MIN_CDB, "the band is clamped");

    /* The parser clamps a packet that was built by hand, so the player never has to. */
    pkt[1] = ES_TYPE_TONE;
    pkt[2] = 0x7F;
    pkt[3] = 0xFF;
    CHECK(es_tone_parse(pkt, ES_TONE_SIZE, &back), "a too-large value parses");
    CHECK_EQ(back.gain_cdb, ES_TONE_GAIN_MAX_CDB, "and is clamped by the receiver");

    CHECK_EQ(es_tone_encode(&t, pkt, ES_TONE_SIZE - 1), 0, "a short buffer is refused");
    CHECK(!es_tone_parse(pkt, ES_TONE_SIZE - 1, &back), "a short packet is refused");
    CHECK(!es_tone_parse(pkt, ES_TONE_SIZE + 1, &back), "a long packet is refused");
    pkt[0] = 2;
    CHECK(!es_tone_parse(pkt, ES_TONE_SIZE, &back), "a wrong version is not a tone");
    pkt[0] = ES_VERSION;
    pkt[1] = ES_TYPE_AUDIO;
    CHECK(!es_tone_parse(pkt, ES_TONE_SIZE, &back), "an audio packet is not a tone");
}

int main(void)
{
    test_fragments();
    test_parse_refuses();
    test_status();
    test_tone();
    TEST_END();
}
