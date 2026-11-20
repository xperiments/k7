#include "espnow_stream.h"

#include <stdlib.h>

#include "test_util.h"

/* E-T4 and the loop behind E1-E6: the sender keeps what it produced, sends only what the
 * receiver's credit allows, and puts back exactly what the receiver names. The "radio" here is
 * an array of packets and the receiver is the code below, so the trade is proven without either
 * board. Each case re-inits the emitter, so sequences start at zero. */

#define CAP_PACKETS 64

static struct {
    uint8_t  data[CAP_PACKETS][ES_MTU];
    size_t   len[CAP_PACKETS];
    unsigned n;
    bool     refuse;
} radio;

static bool send_cb(const uint8_t *pkt, size_t len, void *ctx)
{
    (void)ctx;
    if (radio.refuse || radio.n >= CAP_PACKETS) return false;
    memcpy(radio.data[radio.n], pkt, len);
    radio.len[radio.n] = len;
    radio.n++;
    return true;
}

static void radio_reset(void)
{
    radio.n = 0;
    radio.refuse = false;
}

static void fill(uint8_t *buf, size_t len, uint8_t seed)
{
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(seed + i * 3u);
}

static es_emitter_t *g_emit;
static uint8_t *g_arena;

/* A clean emitter per case: sequence 0, no credit, empty history. */
static void fresh(void)
{
    es_emitter_init(g_emit, g_arena, ES_HISTORY_FRAMES * ES_MAX_FRAME, send_cb, NULL);
    radio_reset();
}

static void give_flags(uint32_t free_bytes, const uint16_t *nack, unsigned n_nack, uint8_t flags)
{
    es_status_t st;
    memset(&st, 0, sizeof st);
    st.free_bytes = free_bytes;
    st.nack_count = (uint8_t)n_nack;
    st.flags = flags;
    for (unsigned i = 0; i < n_nack && i < ES_MAX_NACK; i++) st.nack[i] = nack[i];

    uint8_t pkt[ES_MTU];
    size_t n = es_status_encode(&st, pkt, sizeof pkt);
    CHECK(n > 0, "the status packet builds");
    es_emitter_status(g_emit, pkt, n);
}

static void give(uint32_t free_bytes, const uint16_t *nack, unsigned n_nack)
{
    give_flags(free_bytes, nack, n_nack, 0);
}

/* Reassemble the packets in the radio into frames, keyed by sequence. */
static unsigned reassemble(uint16_t seqs[], uint8_t frames[][ES_MAX_FRAME], size_t lens[])
{
    unsigned n = 0;
    for (unsigned i = 0; i < radio.n; i++) {
        es_frag_t f;
        if (!es_parse_frag(radio.data[i], radio.len[i], &f)) continue;
        unsigned k;
        for (k = 0; k < n; k++)
            if (seqs[k] == f.seq) break;
        if (k == n) {
            n++;
            seqs[k] = f.seq;
            lens[k] = 0;
        }
        memcpy(frames[k] + lens[k], f.payload, f.payload_len);
        lens[k] += f.payload_len;
    }
    return n;
}

/* Nothing goes out before the receiver speaks, and everything is kept meanwhile. */
static void test_gate(void)
{
    fresh();
    uint8_t frame[417];
    fill(frame, sizeof frame, 1);
    for (int i = 0; i < 3; i++)
        CHECK(es_emitter_push(g_emit, frame, sizeof frame), "a frame is kept");
    CHECK_EQ(radio.n, 0, "no status, no packets");

    es_emitter_stats_t st;
    es_emitter_stats(g_emit, &st);
    CHECK_EQ(st.sent, 0, "nothing sent");
    CHECK_EQ(st.held, 3, "and three frames held");
    CHECK(!st.have_status, "the receiver has not spoken");
}

/* The credit is the budget: room for two frames sends two frames and holds the rest. */
static void test_credit(void)
{
    fresh();
    give(417 * 2, NULL, 0);

    uint8_t frame[417];
    fill(frame, sizeof frame, 2);
    for (int i = 0; i < 4; i++) es_emitter_push(g_emit, frame, sizeof frame);

    es_emitter_stats_t st;
    es_emitter_stats(g_emit, &st);
    CHECK_EQ(radio.n, 4, "two frames went out, two packets each");
    CHECK_EQ(st.sent, 2, "two frames sent");
    CHECK_EQ(st.held, 2, "the other two waited for credit");
}

/* A frame the receiver names comes back, byte for byte, and only that frame. */
static void test_resend(void)
{
    fresh();
    give(417 * 6, NULL, 0);

    static uint8_t sent[6][417];
    for (int i = 0; i < 6; i++) {
        fill(sent[i], sizeof sent[i], (uint8_t)(10 * i));
        es_emitter_push(g_emit, sent[i], sizeof sent[i]);
    }
    es_emitter_stats_t before;
    es_emitter_stats(g_emit, &before);
    CHECK_EQ(before.sent, 6, "six frames went out");

    /* Sequence 3 is the fourth frame pushed: name it and it comes back alone. */
    uint16_t nack[] = {3};
    radio_reset();
    give(417 * 2, nack, 1);
    CHECK_EQ(radio.n, 2, "the resend is two packets");

    static uint8_t frames[CAP_PACKETS][ES_MAX_FRAME];
    uint16_t seqs[CAP_PACKETS];
    size_t lens[CAP_PACKETS];
    unsigned n = reassemble(seqs, frames, lens);
    CHECK_EQ(n, 1, "one frame was resent");
    CHECK_EQ(seqs[0], 3, "the one that was named");
    CHECK_EQ(lens[0], sizeof sent[3], "at its length");
    CHECK(memcmp(frames[0], sent[3], sizeof sent[3]) == 0, "with its own bytes");

    es_emitter_stats_t st;
    es_emitter_stats(g_emit, &st);
    CHECK_EQ(st.resent, 1, "and it is counted as a resend");

    /* A sequence outside the history is not invented. */
    uint16_t absent[] = {60000};
    radio_reset();
    give(417 * 2, absent, 1);
    CHECK_EQ(radio.n, 0, "a frame outside the history is not resent");
}

/* E13's fallback: the tone capability is whatever the current peer's last status said. */
static void test_tone_capability(void)
{
    fresh();
    es_emitter_stats_t st;
    es_emitter_stats(g_emit, &st);
    CHECK(!st.tone_capable, "no status, no tone capability");

    give_flags(50000, NULL, 0, ES_STATUS_FLAG_TONE);
    es_emitter_stats(g_emit, &st);
    CHECK(st.tone_capable, "the player's flag is read");
    CHECK(g_emit->tone_capable, "and it is the emitter's state");

    /* An old player's next status has no flags byte: the K7 goes back to the S3 tone. */
    give(50000, NULL, 0);
    es_emitter_stats(g_emit, &st);
    CHECK(!st.tone_capable, "a status without the flag clears it");

    give_flags(50000, NULL, 0, ES_STATUS_FLAG_TONE);
    es_emitter_stats(g_emit, &st);
    CHECK(st.tone_capable, "and it can come back");
}

/* Six frames through the radio and back, bytes for bytes, then a radio that refuses one. */
static void test_loop(void)
{
    fresh();
    give(417 * 6, NULL, 0);

    static uint8_t sent[6][417];
    for (int i = 0; i < 6; i++) {
        fill(sent[i], sizeof sent[i], (uint8_t)(10 * i));
        es_emitter_push(g_emit, sent[i], sizeof sent[i]);
    }

    static uint8_t frames[CAP_PACKETS][ES_MAX_FRAME];
    uint16_t seqs[CAP_PACKETS];
    size_t lens[CAP_PACKETS];
    unsigned n = reassemble(seqs, frames, lens);
    CHECK_EQ(n, 6, "six frames came out of the packets");
    for (int i = 0; i < 6 && i < (int)n; i++) {
        /* The packets arrived in order, so the first six sequences are the frames. */
        CHECK_EQ(seqs[i], (uint16_t)i, "a frame's sequence, in order");
        CHECK_EQ(lens[i], sizeof sent[i], "a frame's length");
        CHECK(memcmp(frames[i], sent[i], sizeof sent[i]) == 0, "and its own bytes");
    }

    /* A radio that refuses its packet costs the frame, and the census says so. */
    give(417 * 2, NULL, 0);
    radio.refuse = true;
    es_emitter_stats_t before, after;
    es_emitter_stats(g_emit, &before);
    es_emitter_push(g_emit, sent[0], sizeof sent[0]);
    es_emitter_stats(g_emit, &after);
    CHECK_EQ(after.dropped - before.dropped, 1, "a refused frame is counted as dropped");
    radio.refuse = false;
}

int main(void)
{
    g_emit = calloc(1, sizeof *g_emit);
    g_arena = malloc(ES_HISTORY_FRAMES * ES_MAX_FRAME);
    CHECK(g_emit != NULL && g_arena != NULL, "the emitter and its arena are allocated");
    if (!g_emit || !g_arena) return 1;

    test_gate();
    test_credit();
    test_resend();
    test_tone_capability();
    test_loop();

    free(g_arena);
    free(g_emit);
    TEST_END();
}
