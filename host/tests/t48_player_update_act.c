/*
 * T48: the K7's side of the update, driven by a scripted player.
 *
 * The act is policy: a probe broadcast three times, the players that answer, and each stale one
 * served in turn with the station stopped and restarted around it. The "player" here is the
 * other end of the same frames the player's own test builds (t08_ota), so the two halves are
 * proven against one format. What is not here: the radio and the partition, which are the K7's.
 */
#include "player_update_act.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "player_update.h"
#include "test_util.h"

#define SENT_MAX 256

typedef struct {
    uint8_t dst[6];
    bool    broadcast;
    uint8_t data[PU_MTU];
    size_t  len;
} sent_t;

static struct {
    uint8_t  image[10000];
    uint32_t len;
    bool     read_ok;
    sent_t   sent[SENT_MAX];
    int      sent_n;
    int      stops, starts;
} S;

static pua_t g_act;
static uint32_t g_now;

/* ---------------------------------------------------------------- the seams */

static bool seam_send(void *ctx, const uint8_t *dst, const uint8_t *pkt, size_t len)
{
    (void)ctx;
    if (S.sent_n >= SENT_MAX || len > PU_MTU) return false;
    sent_t *s = &S.sent[S.sent_n++];
    memset(s, 0, sizeof *s);
    s->broadcast = dst == NULL;
    if (dst) memcpy(s->dst, dst, 6);
    memcpy(s->data, pkt, len);
    s->len = len;
    return true;
}

static bool seam_read(void *ctx, uint32_t offset, uint8_t *out, size_t len)
{
    (void)ctx;
    if (!S.read_ok || offset + len > S.len) return false;
    memcpy(out, S.image + offset, len);
    return true;
}

static void seam_stop(void *ctx) { (void)ctx; S.stops++; }
static void seam_start(void *ctx) { (void)ctx; S.starts++; }

static const pua_seam_t SEAM = {
    .send = seam_send,
    .image_read = seam_read,
    .station_stop = seam_stop,
    .station_start = seam_start,
};

/* ------------------------------------------------------------ the script */

static void reset(uint32_t image_len, bool read_ok)
{
    memset(&S, 0, sizeof S);
    S.len = image_len;
    S.read_ok = read_ok;
    for (uint32_t i = 0; i < image_len; i++) S.image[i] = (uint8_t)(i * 13u + 5u);
    pua_init(&g_act, &SEAM, NULL);
    g_now = 1000;
}

static pu_version_t ver(uint16_t major, uint16_t minor, uint16_t patch)
{
    pu_version_t v = {major, minor, patch};
    return v;
}

static void set_image(pu_version_t version, uint32_t len)
{
    uint8_t sha[32], sig[70];
    for (int i = 0; i < 32; i++) sha[i] = (uint8_t)(i + 1);
    for (int i = 0; i < 70; i++) sig[i] = (uint8_t)(i + 1);
    pua_set_image(&g_act, version, len, sha, sig, sizeof sig);
}

static void start(bool transfer)
{
    pua_start(&g_act, g_now, transfer);
}

static void tick(uint32_t by)
{
    g_now += by;
    pua_tick(&g_act, g_now);
}

/* The last frame of `type`, or false. */
static bool last_of_type(uint8_t type, sent_t *out)
{
    bool found = false;
    for (int i = 0; i < S.sent_n; i++) {
        if (S.sent[i].len >= 2 && S.sent[i].data[0] == PU_VERSION &&
            S.sent[i].data[1] == type) {
            *out = S.sent[i];
            found = true;
        }
    }
    return found;
}

static int count_of_type(uint8_t type)
{
    int n = 0;
    for (int i = 0; i < S.sent_n; i++)
        if (S.sent[i].len >= 2 && S.sent[i].data[1] == type) n++;
    return n;
}

static uint32_t probe_nonce(void)
{
    sent_t probe;
    uint32_t nonce = 0;
    if (!last_of_type(PU_TYPE_PROBE, &probe)) return 0;
    if (!pu_probe_parse(probe.data, probe.len, &nonce)) return 0;
    return nonce;
}

static void answer(const uint8_t mac[6], pu_version_t version)
{
    pu_answer_t a = {.nonce = probe_nonce(), .version = version, .board = PU_BOARD_ESP32, .flags = 0};
    uint8_t pkt[PU_ANSWER_SIZE];
    size_t n = pu_answer_encode(&a, pkt);
    CHECK(n == PU_ANSWER_SIZE, "the answer encodes");
    pua_frame(&g_act, mac, pkt, n, g_now);
}

static void result(const uint8_t mac[6], uint8_t code, pu_version_t version)
{
    uint8_t pkt[PU_RESULT_SIZE];
    size_t n = pu_result_encode(code, version, pkt);
    CHECK(n == PU_RESULT_SIZE, "the result encodes");
    pua_frame(&g_act, mac, pkt, n, g_now);
}

static void ack(const uint8_t mac[6], uint16_t block, uint8_t code)
{
    uint8_t pkt[PU_ACK_SIZE];
    size_t n = pu_ack_encode(block, code, pkt);
    CHECK(n == PU_ACK_SIZE, "the ack encodes");
    pua_frame(&g_act, mac, pkt, n, g_now);
}

static const uint8_t A[6] = {0x02, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
static const uint8_t B[6] = {0x02, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E};

/* The probe's three frames, then the collect window's end. */
static void run_probe(pu_version_t player_version, const uint8_t *mac)
{
    start(false);
    tick(0);
    tick(100);
    tick(100);
    tick(299); /* inside the window: the deadline is 500 ms after the start */
    if (mac) answer(mac, player_version);
    tick(1);
}

/* ---------------------------------------------------------------- the cases */

static void test_no_image(void)
{
    reset(5000, true);
    start(true);
    tick(0);
    CHECK_EQ(pua_state(&g_act), PUA_DONE, "with no image the act is over at once");
    CHECK(strcmp(pua_outcome(&g_act), "no image") == 0, "and says why");
    CHECK_EQ(S.stops, 0, "and never stopped the station");
}

static void test_no_player(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    start(false);
    tick(0);
    tick(100);
    tick(100);
    CHECK_EQ(count_of_type(PU_TYPE_PROBE), 3, "the probe goes out three times");
    sent_t probe;
    CHECK(last_of_type(PU_TYPE_PROBE, &probe), "the probe was recorded");
    CHECK(probe.broadcast, "broadcast, because the K7 knows no player yet");
    CHECK_EQ(S.stops, 0, "a probe neither stops nor starts the station");
    tick(400);
    CHECK_EQ(pua_state(&g_act), PUA_DONE, "no answers: the act is over");
    CHECK(strcmp(pua_outcome(&g_act), "no player") == 0, "and says so");
}

static void test_probe_only(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);

    run_probe(ver(0, 7, 0), A);
    CHECK_EQ(pua_found(&g_act), 1, "one player answered");
    CHECK_EQ(pua_stale(&g_act), 0, "and it is on the image");
    CHECK(strcmp(pua_outcome(&g_act), "up to date") == 0, "so the row says up to date");
    CHECK_EQ(S.stops, 0, "and nothing was stopped");

    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    run_probe(ver(0, 6, 2), A);
    CHECK_EQ(pua_stale(&g_act), 1, "an older player is stale");
    CHECK(strcmp(pua_outcome(&g_act), "found") == 0, "the probe only reports what it found");
    CHECK_EQ(S.stops, 0, "a probe never starts the transfer");
}

static void test_one_player(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    start(true);
    tick(0);
    tick(100);
    tick(100);
    tick(299);
    answer(A, ver(0, 6, 2));
    tick(1);

    CHECK_EQ(pua_state(&g_act), PUA_OPENING, "the stale player is being opened");
    CHECK_EQ(S.stops, 1, "the station stopped for the begin");
    CHECK_EQ(pua_serving(&g_act), 1, "serving the first stale player");
    CHECK_EQ(pua_stale_total(&g_act), 1, "of one");

    sent_t begin;
    CHECK(last_of_type(PU_TYPE_BEGIN, &begin), "the begin went out");
    CHECK(memcmp(begin.dst, A, 6) == 0, "to the player that answered");
    pu_begin_t claim;
    CHECK(pu_begin_parse(begin.data, begin.len, &claim), "the begin parses");
    CHECK_EQ(claim.image_len, 5000, "and claims the image");
    CHECK_EQ(claim.version.minor, 7, "and its version");

    result(A, PU_READY, ver(0, 7, 0));
    CHECK_EQ(pua_state(&g_act), PUA_BLOCKS, "ready opens the blocks");
    CHECK_EQ(pua_blocks_total(&g_act), 2, "5 000 B is two blocks");

    /* Block 0 travels as seventeen fragments whose payloads are the image's first 4 KB. */
    static uint8_t joined[PU_BLOCK_SIZE];
    size_t off = 0;
    for (int i = 0; i < S.sent_n; i++) {
        if (S.sent[i].data[1] != PU_TYPE_BLOCK) continue;
        pu_frag_t f;
        CHECK(pu_block_parse(S.sent[i].data, S.sent[i].len, &f), "the fragment parses");
        if (f.block != 0) continue;
        memcpy(joined + off, f.payload, f.payload_len);
        off += f.payload_len;
    }
    CHECK_EQ(off, PU_BLOCK_SIZE, "block 0 is a whole 4 KB");
    CHECK(memcmp(joined, S.image, PU_BLOCK_SIZE) == 0, "and is the image's first block");

    ack(A, 0, PU_ACK_WRITTEN);
    CHECK_EQ(pua_blocks_done(&g_act), 1, "one block written");
    int blocks_after_0 = 0;
    for (int i = 0; i < S.sent_n; i++)
        if (S.sent[i].data[1] == PU_TYPE_BLOCK) {
            pu_frag_t f;
            if (pu_block_parse(S.sent[i].data, S.sent[i].len, &f) && f.block == 1) blocks_after_0++;
        }
    CHECK_EQ(blocks_after_0, 4, "block 1 is 904 bytes, four fragments");

    ack(A, 1, PU_ACK_WRITTEN);
    CHECK_EQ(pua_state(&g_act), PUA_VERIFYING, "the last block leaves the player verifying");

    result(A, PU_OK, ver(0, 7, 0));
    CHECK_EQ(pua_state(&g_act), PUA_DONE, "and the result ends the act");
    CHECK(strcmp(pua_outcome(&g_act), "updated 1") == 0, "saying what happened");
    CHECK_EQ(S.starts, 1, "the station came back");
    CHECK_EQ(S.stops, 1, "having been stopped once");
}

static void test_dropped_blocks(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    start(true);
    tick(0);
    tick(100);
    tick(100);
    tick(299);
    answer(A, ver(0, 6, 2));
    tick(1);
    result(A, PU_READY, ver(0, 7, 0));

    tick(PUA_ACK_MS + 1);
    tick(PUA_ACK_MS + 1);
    tick(PUA_ACK_MS + 1);
    CHECK_EQ(pua_state(&g_act), PUA_DONE, "three silent sends drop the player");
    CHECK(strcmp(pua_outcome(&g_act), "failed") == 0, "and the act says so");
    CHECK_EQ(S.starts, 1, "the station came back anyway");
}

static void test_a_retry(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    start(true);
    tick(0);
    tick(100);
    tick(100);
    tick(299);
    answer(A, ver(0, 6, 2));
    tick(1);
    result(A, PU_READY, ver(0, 7, 0));

    int before = count_of_type(PU_TYPE_BLOCK);
    ack(A, 0, PU_ACK_RETRY);
    CHECK(count_of_type(PU_TYPE_BLOCK) > before, "a retry re-sends the block at once");
    ack(A, 0, PU_ACK_WRITTEN);
    CHECK_EQ(pua_blocks_done(&g_act), 1, "and the one after it moves on");
}

static void test_two_players(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    start(true);
    tick(0);
    tick(100);
    tick(100);
    tick(299);
    answer(A, ver(0, 6, 2));
    answer(B, ver(0, 5, 0));
    tick(1);

    CHECK_EQ(pua_found(&g_act), 2, "two players answered");
    CHECK_EQ(pua_stale(&g_act), 2, "and both are stale");
    CHECK_EQ(pua_stale_total(&g_act), 2, "so the act serves two");

    /* The first: ready, two blocks, ok. */
    result(A, PU_READY, ver(0, 7, 0));
    ack(A, 0, PU_ACK_WRITTEN);
    ack(A, 1, PU_ACK_WRITTEN);
    result(A, PU_OK, ver(0, 7, 0));
    CHECK_EQ(pua_serving(&g_act), 2, "the second is being served");
    CHECK_EQ(S.stops, 2, "the station stopped again for it");
    CHECK_EQ(S.starts, 1, "and came back after the first");

    result(B, PU_READY, ver(0, 7, 0));
    ack(B, 0, PU_ACK_WRITTEN);
    ack(B, 1, PU_ACK_WRITTEN);
    result(B, PU_OK, ver(0, 7, 0));
    CHECK(strcmp(pua_outcome(&g_act), "updated 2") == 0, "both took the image");
    CHECK_EQ(S.stops, 2, "two stops");
    CHECK_EQ(S.starts, 2, "and two starts");
}

static void test_refused_begin(void)
{
    reset(5000, true);
    set_image(ver(0, 7, 0), 5000);
    start(true);
    tick(0);
    tick(100);
    tick(100);
    tick(299);
    answer(A, ver(0, 6, 2));
    tick(1);
    result(A, PU_NO_ROOM, ver(0, 7, 0));
    CHECK_EQ(pua_state(&g_act), PUA_DONE, "a refusal ends that player's transfer");
    CHECK(strcmp(pua_outcome(&g_act), "failed") == 0, "with the act reporting it");
    CHECK_EQ(S.starts, 1, "and the station back");

    /* A station that cannot be stopped is not a crash: the act still finishes. */
    reset(5000, false);
    set_image(ver(0, 7, 0), 5000);
    start(true);
    tick(0);
    tick(100);
    tick(100);
    tick(299);
    answer(A, ver(0, 6, 2));
    tick(1);
    result(A, PU_READY, ver(0, 7, 0));
    CHECK_EQ(pua_state(&g_act), PUA_DONE, "an unreadable image fails the player, not the K7");
    CHECK_EQ(S.starts, 1, "and the station is back");
}

int main(void)
{
    test_no_image();
    test_no_player();
    test_probe_only();
    test_one_player();
    test_dropped_blocks();
    test_a_retry();
    test_two_players();
    test_refused_begin();
    TEST_END();
}
