/*
 * T13: the two wire formats, without a socket.
 *
 * Part A drives the burst ring well past its capacity with uneven record sizes and
 * compares every surviving record against an independent oracle. The interesting
 * cases are the ones a connected client rarely produces on demand: a record that
 * straddles the end of the ring, and eviction that must drop whole records.
 *
 * Part B drives the ICY splitter with a metaint that does not divide the chunk
 * size, and asserts the exact byte sequence: audio spans, the length byte, the
 * 16-byte padding, and the zero-length block that an unchanged title still emits.
 */
#include "test_util.h"

#include "mi_burst.h"
#include "mi_icy.h"

/* ---------------------------------------------------------------- burst */

typedef struct {
    uint8_t data[16];
    size_t  len;
} rec_t;

static void test_burst(void)
{
    /* 40 bytes holds only a few records, so 200 puts force both eviction and
     * wraparound many times over. */
    const size_t cap = 40;
    mi_burst_t *b = mi_burst_new(cap, false);
    CHECK(b != NULL, "a 40-byte ring is accepted");
    if (!b) return;
    CHECK_EQ(mi_burst_capacity(b), cap, "the capacity is what was asked for");
    CHECK_EQ(mi_burst_used(b), 0, "a new ring is empty");

    rec_t expect[64];
    int nexp = 0;
    size_t total = 0;

    for (int step = 0; step < 200; step++) {
        size_t len = 5 + (size_t)(step * 7) % 9; /* 5..13 bytes */
        uint8_t frame[16];
        for (size_t i = 0; i < len; i++) frame[i] = (uint8_t)(step * 31 + (int)i);

        CHECK(mi_burst_put(b, frame, len), "put record %d (%u bytes)", step, (unsigned)len);

        /* The oracle: append, then drop oldest until the new record fits. */
        while (nexp > 0 && total + len + 2 > cap) {
            total -= expect[0].len + 2;
            memmove(expect, expect + 1, (size_t)(nexp - 1) * sizeof expect[0]);
            nexp--;
        }
        total += len + 2;
        expect[nexp].len = len;
        memcpy(expect[nexp].data, frame, len);
        nexp++;

        CHECK_EQ(mi_burst_used(b), total, "used bytes match the oracle after every put");

        mi_burst_cursor_t cur;
        mi_burst_begin(b, &cur);
        uint8_t out[16];
        size_t n;
        int i = 0;
        while ((n = mi_burst_next(b, &cur, out, sizeof out)) > 0) {
            CHECK(i < nexp, "the ring does not hold more records than expected");
            if (i < nexp) {
                CHECK_EQ(n, expect[i].len, "a surviving record has the expected length");
                CHECK(n == expect[i].len && memcmp(out, expect[i].data, n) == 0,
                      "record %d.%d is intact", step, i);
            }
            i++;
        }
        CHECK_EQ(i, nexp, "the walk yields exactly the surviving records");
    }

    /* A record that cannot fit at all is refused, and the ring is untouched. */
    size_t before = mi_burst_used(b);
    uint8_t big[64];
    memset(big, 0xAA, sizeof big);
    CHECK(!mi_burst_put(b, big, cap - 1), "a record larger than capacity - 2 is refused");
    CHECK_EQ(mi_burst_used(b), before, "a refused record does not disturb the ring");

    /* The cursor contract: a caller buffer smaller than the record stops the walk
     * rather than overrunning. */
    mi_burst_t *small = mi_burst_new(16, false);
    CHECK(small != NULL, "a small ring is accepted");
    if (small) {
        uint8_t frame[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        CHECK(mi_burst_put(small, frame, sizeof frame), "an 8-byte record fits 16 bytes");
        mi_burst_cursor_t cur;
        mi_burst_begin(small, &cur);
        uint8_t out[4];
        CHECK_EQ(mi_burst_next(small, &cur, out, sizeof out), 0,
                 "a too-small caller buffer stops the walk");
        mi_burst_free(small);
    }

    CHECK(mi_burst_new(2, false) == NULL, "a capacity that cannot hold a record is refused");
    mi_burst_free(b);
}

/* ------------------------------------------------------------------ ICY */

typedef struct {
    uint8_t bytes[4096];
    size_t  n;
    int     audio_spans;
    int     metadata_blocks;
    size_t  audio_bytes;
    int     calls;
    int     fail_at; /* -1 = never */
} sink_t;

static bool record_sink(void *ctx, mi_icy_kind_t kind, const uint8_t *d, size_t n)
{
    sink_t *s = ctx;
    s->calls++;
    if (s->fail_at >= 0 && s->calls == s->fail_at) return false;
    if (s->n + n > sizeof s->bytes) return false;
    if (kind == MI_ICY_METADATA) {
        s->metadata_blocks++;
    } else {
        s->audio_spans++;
        s->audio_bytes += n;
    }
    memcpy(s->bytes + s->n, d, n);
    s->n += n;
    return true;
}

/* "StreamTitle='<title>';", padded to 16-byte units, prefixed by the length byte. */
static size_t build_block(uint8_t *out, size_t cap, const char *title)
{
    char text[256];
    int len = snprintf(text, sizeof text, "StreamTitle='%s';", title);
    unsigned blocks = ((unsigned)len + 15) / 16;
    size_t total = 1 + (size_t)blocks * 16;
    if (total > cap) return 0;
    memset(out, 0, total);
    out[0] = (uint8_t)blocks;
    memcpy(out + 1, text, (size_t)len);
    return total;
}

static void test_icy(void)
{
    /* 1. Audio split at the interval, a block after each exact metaint. */
    sink_t s = {.fail_at = -1};
    mi_icy_t *icy = mi_icy_new(4);
    CHECK(icy != NULL, "a metaint of 4 is accepted");
    if (!icy) return;
    CHECK_EQ(mi_icy_until_metadata(icy), 4, "the first block is due after metaint bytes");

    CHECK(mi_icy_push(icy, (const uint8_t *)"abcd", 4, "A", 1, record_sink, &s),
          "the first push is accepted");
    uint8_t block[256];
    size_t blen = build_block(block, sizeof block, "A");
    CHECK_EQ(s.audio_bytes, 4, "four audio bytes were sent");
    CHECK_EQ(s.metadata_blocks, 1, "one metadata block followed them");
    CHECK_EQ(s.n, 4 + blen, "the block is exactly one interval after the audio");
    CHECK(memcmp(s.bytes, "abcd", 4) == 0, "the audio bytes are unchanged");
    CHECK(memcmp(s.bytes + 4, block, blen) == 0, "the block is StreamTitle='A'; padded to 16 bytes");

    /* 2. An unchanged title still emits a block, as a single zero byte. */
    s.n = 0;
    CHECK(mi_icy_push(icy, (const uint8_t *)"efgh", 4, "A", 1, record_sink, &s),
          "the second push is accepted");
    CHECK_EQ(s.n, 5, "four audio bytes and a one-byte block");
    CHECK_EQ(s.bytes[4], 0, "an unchanged title is a zero-length block");

    /* 3. A changed version carries the new title, mid-chunk. */
    s.n = 0;
    CHECK(mi_icy_push(icy, (const uint8_t *)"ijkl", 4, "B", 2, record_sink, &s),
          "a changed title is accepted");
    size_t blen_b = build_block(block, sizeof block, "B");
    CHECK(s.n == 4 + blen_b && memcmp(s.bytes + 4, block, blen_b) == 0,
          "the changed title is carried by the next block");

    /* 4. A metaint that does not divide the chunk: the counter carries over. */
    sink_t s2 = {.fail_at = -1};
    mi_icy_t *icy2 = mi_icy_new(3);
    CHECK(icy2 != NULL, "a metaint of 3 is accepted");
    if (icy2) {
        CHECK(mi_icy_push(icy2, (const uint8_t *)"abcde", 5, "T", 9, record_sink, &s2),
              "five bytes with a metaint of 3 is accepted");
        CHECK_EQ(s2.audio_spans, 2, "the audio arrives in two spans");
        CHECK_EQ(s2.audio_bytes, 5, "all five audio bytes arrive");
        CHECK_EQ(s2.metadata_blocks, 1, "one block lands inside the five bytes");
        CHECK_EQ(mi_icy_until_metadata(icy2), 1, "one byte is still to go before the next block");
        CHECK(s2.bytes[3] == 1 && s2.bytes[4 + 16] == 'd', "the block starts after the third byte");
        mi_icy_free(icy2);
    }

    /* 5. A title long enough for several blocks, with zero padding at the end. */
    sink_t s3 = {.fail_at = -1};
    mi_icy_t *icy3 = mi_icy_new(1);
    char long_title[121];
    memset(long_title, 'x', sizeof long_title - 1);
    long_title[sizeof long_title - 1] = 0;
    if (icy3) {
        CHECK(mi_icy_push(icy3, (const uint8_t *)"z", 1, long_title, 5, record_sink, &s3),
              "a long title is accepted");
        size_t want = build_block(block, sizeof block, long_title);
        CHECK(want > 17, "the long title needs more than one block");
        CHECK_EQ(s3.bytes[1], (uint8_t)((want - 1) / 16), "the length byte counts the blocks");
        CHECK(s3.n == 1 + want && memcmp(s3.bytes + 1, block, want) == 0,
              "the long block is byte-identical to the reference");
        CHECK(s3.bytes[s3.n - 1] == 0, "the block is zero-padded");
        mi_icy_free(icy3);
    }

    /* 6. A quote inside the title cannot end the payload early. */
    sink_t s4 = {.fail_at = -1};
    mi_icy_t *icy4 = mi_icy_new(2);
    if (icy4) {
        CHECK(mi_icy_push(icy4, (const uint8_t *)"ab", 2, "don't", 1, record_sink, &s4),
              "a title with an apostrophe is accepted");
        size_t want = build_block(block, sizeof block, "don t");
        CHECK(s4.n == 2 + want && memcmp(s4.bytes + 2, block, want) == 0,
              "the apostrophe is replaced, not passed through");
        mi_icy_free(icy4);
    }

    /* 7. A refusing sink stops the push. */
    sink_t s5 = {.fail_at = 2};
    mi_icy_t *icy5 = mi_icy_new(2);
    if (icy5) {
        CHECK(!mi_icy_push(icy5, (const uint8_t *)"abcdef", 6, "T", 1, record_sink, &s5),
              "a refused sink makes the push fail");
        mi_icy_free(icy5);
    }

    /* 8. Zero is not a metadata interval. */
    CHECK(mi_icy_new(0) == NULL, "a metaint of 0 has no metadata to interleave");

    mi_icy_free(icy);
}

int main(void)
{
    test_burst();
    test_icy();
    TEST_END();
}
