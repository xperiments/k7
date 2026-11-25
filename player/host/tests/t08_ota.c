/*
 * T08: the player's update, driven by a scripted board.
 *
 * The frames come from the same fragmenter the K7 uses, so the block machine is fed bytes the
 * sender would really build. The flash is the host stub: what is proven here is the accept and
 * refuse rules, the write-once block sequence, the retry's acknowledgement, the verify's
 * refusal and the rollback's clock. What is not: the ECDSA (the stub answers for the curve, as
 * it does for X25519) and the flash's own behaviour, which are the board's (U-B1..U-B7).
 */
#include "player_ota.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "handover.h"
#include "player_board.h"
#include "player_update.h"
#include "test_util.h"

/* The host adapter's script (board_player_host.c). */
void host_player_reset(void);
void host_player_ota_reset(void);
void host_player_ota_record(bool present);
void host_player_ota_marker(const char *version);
void host_player_ota_fail_verify(bool fail);
const uint8_t *host_player_ota_slot(void);
uint32_t host_player_ota_written(void);
int host_player_ota_begins(void);
bool host_player_ota_committed(void);
const char *host_player_ota_committed_version(void);
bool host_player_ota_rebooted(void);
bool host_player_ota_rolled_back(void);
bool host_player_ota_valid(void);
int host_player_sent_to_count(void);
bool host_player_sent_to(int index, uint8_t out[6], uint8_t *data, size_t *len);

static const uint8_t g_k7[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

static void fill(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(i * 31u + 7u);
}

/* The last frame of `type` the player sent, or false. */
static bool last_of_type(uint8_t type, uint8_t *data, size_t *len)
{
    bool found = false;
    for (int i = 0; i < host_player_sent_to_count(); i++) {
        uint8_t dst[6], buf[32];
        size_t n = 0;
        if (!host_player_sent_to(i, dst, buf, &n)) continue;
        if (n >= 2 && buf[0] == PU_VERSION && buf[1] == type) {
            memcpy(data, buf, n);
            *len = n;
            found = true;
        }
    }
    return found;
}

static uint8_t result_of(void)
{
    uint8_t data[32];
    size_t len = 0;
    if (!last_of_type(PU_TYPE_RESULT, data, &len)) return 0xFF;
    uint8_t result = 0xFF;
    pu_version_t version;
    if (!pu_result_parse(data, len, &result, &version)) return 0xFF;
    return result;
}

static bool last_ack(uint16_t *block, uint8_t *result)
{
    uint8_t data[32];
    size_t len = 0;
    if (!last_of_type(PU_TYPE_ACK, data, &len)) return false;
    return pu_ack_parse(data, len, block, result);
}

static pu_version_t ver(uint16_t major, uint16_t minor, uint16_t patch)
{
    pu_version_t v = {major, minor, patch};
    return v;
}

static void feed_begin(pu_version_t version, uint32_t image_len, uint8_t board,
                       const uint8_t sha256[32])
{
    pu_begin_t begin = {
        .version = version,
        .board = board,
        .image_len = image_len,
        .sig_len = 70,
    };
    memcpy(begin.sha256, sha256, 32);
    for (int i = 0; i < 70; i++) begin.sig[i] = (uint8_t)(i + 1);
    uint8_t pkt[PU_BEGIN_SIZE];
    CHECK_EQ(pu_begin_encode(&begin, pkt), PU_BEGIN_SIZE, "the begin encodes");
    player_ota_packet(g_k7, pkt, sizeof pkt);
}

/* One block, fragmented exactly as the K7 would, into the player's radio entry point. */
static void feed_block(uint16_t block, const uint8_t *data, size_t len)
{
    uint8_t packets[PU_BLOCK_MAX_FRAGS * PU_MTU];
    size_t lens[PU_BLOCK_MAX_FRAGS];
    size_t n = pu_block_fragment(block, data, len, packets, lens);
    CHECK(n > 0, "the block fragments");
    for (size_t i = 0; i < n; i++) player_ota_packet(g_k7, packets + i * PU_MTU, lens[i]);
}

/* ---------------------------------------------------------------- the cases */

static void test_boot(void)
{
    host_player_reset();
    player_ota_init();
    CHECK(host_player_ota_valid(), "a boot with no marker marks itself valid (U21)");
    CHECK(!player_ota_updating(), "and is not updating");

    /* A marker naming another version is the old image after a rollback: valid at once. */
    host_player_reset();
    host_player_ota_marker("0.7.0");
    player_ota_init();
    CHECK(host_player_ota_valid(), "a marker for another version marks itself valid");

    /* A marker naming this image makes the boot the one that has to prove itself. */
    host_player_reset();
    host_player_ota_marker("0.6.2");
    player_ota_init();
    player_ota_tick(1000);
    CHECK(!host_player_ota_valid(), "the marked image has not proved itself yet");
    CHECK(!host_player_ota_rolled_back(), "nor expired yet");
    player_ota_tick(1000 + PLAYER_OTA_PROOF_MS + 1);
    CHECK(host_player_ota_rolled_back(), "ninety seconds without sound rolls it back (U22)");

    /* The proof arrives first: valid, and the clock no longer matters. */
    host_player_reset();
    host_player_ota_marker("0.6.2");
    player_ota_init();
    player_ota_tick(0);
    player_ota_streamed();
    CHECK(host_player_ota_valid(), "the first decoded frame marks it valid");
    player_ota_tick(PLAYER_OTA_PROOF_MS + 1);
    CHECK(!host_player_ota_rolled_back(), "and the clock no longer rolls it back");
}

static void test_probe(void)
{
    uint8_t probe[PU_PROBE_SIZE];
    CHECK_EQ(pu_probe_encode(0xA1B2C3D4u, probe), PU_PROBE_SIZE, "the probe encodes");

    host_player_reset();
    player_ota_init();
    host_player_ota_record(false);
    player_ota_packet(g_k7, probe, sizeof probe);
    player_ota_tick(10);
    CHECK_EQ(host_player_sent_to_count(), 0, "a player with no record does not answer (U6)");

    host_player_ota_record(true);
    player_ota_packet(g_k7, probe, sizeof probe);
    player_ota_tick(20);
    CHECK_EQ(host_player_sent_to_count(), 1, "a provisioned player answers");
    uint8_t dst[6], data[32];
    size_t len = 0;
    CHECK(host_player_sent_to(0, dst, data, &len), "the answer was recorded");
    CHECK(memcmp(dst, g_k7, 6) == 0, "to the K7 that probed");
    pu_answer_t answer;
    CHECK(pu_answer_parse(data, len, &answer), "the answer parses");
    CHECK_EQ(answer.nonce, 0xA1B2C3D4u, "with the probe's nonce");
    CHECK_EQ(answer.version.major, 0, "with the player's major");
    CHECK_EQ(answer.version.minor, 6, "its minor");
    CHECK_EQ(answer.version.patch, 2, "its patch");
}

static void test_begin_refusals(void)
{
    uint8_t sha[32];
    memset(sha, 0xA5, sizeof sha);
    host_player_reset();
    player_ota_init();

    feed_begin(ver(0, 6, 2), 4096, PU_BOARD_ESP32, sha);
    player_ota_tick(100);
    CHECK_EQ(result_of(), PU_SAME_VERSION, "the same version is refused before a byte moves");
    CHECK_EQ(host_player_ota_begins(), 0, "and the slot is not even opened");

    feed_begin(ver(0, 7, 0), 4096, 2, sha);
    player_ota_tick(200);
    CHECK_EQ(result_of(), PU_WRONG_BOARD, "another board is refused");

    feed_begin(ver(0, 7, 0), 64 * 1024 + 1, PU_BOARD_ESP32, sha);
    player_ota_tick(300);
    CHECK_EQ(result_of(), PU_NO_ROOM, "an image past the slot is refused");

    CHECK(!player_ota_updating(), "none of those started an act");
    CHECK_EQ(host_player_ota_begins(), 0, "and no slot was opened");
}

static void test_accept_blocks_and_verify(void)
{
    static uint8_t image[PU_BLOCK_SIZE + 100];
    uint8_t sha[32];
    fill(image, sizeof image);
    ho_sha256(image, sizeof image, sha);

    host_player_reset();
    player_ota_init();
    host_player_ota_record(true);

    feed_begin(ver(0, 7, 0), sizeof image, PU_BOARD_ESP32, sha);
    player_ota_tick(100);
    CHECK_EQ(result_of(), PU_READY, "the begin is answered with ready (U10, U13a)");
    CHECK_EQ(host_player_ota_begins(), 1, "the slot was opened once");
    CHECK(player_ota_updating(), "and the light says Updating");

    feed_block(0, image, PU_BLOCK_SIZE);
    player_ota_tick(200);
    uint16_t block = 0xFFFF;
    uint8_t result = 0xFF;
    CHECK(last_ack(&block, &result), "the block is acknowledged");
    CHECK_EQ(block, 0, "for block 0");
    CHECK_EQ(result, PU_ACK_WRITTEN, "as written");
    CHECK_EQ(host_player_ota_written(), PU_BLOCK_SIZE, "and the slot holds the block");

    /* The acknowledgement is lost and the K7 sends block 0 again: it is acknowledged again,
     * and nothing is appended to a slot that only grows (U15). */
    feed_block(0, image, PU_BLOCK_SIZE);
    player_ota_tick(300);
    CHECK(last_ack(&block, &result), "a retried block is acknowledged");
    CHECK_EQ(result, PU_ACK_WRITTEN, "as written");
    CHECK_EQ(host_player_ota_written(), PU_BLOCK_SIZE, "and is not written twice");

    feed_block(1, image + PU_BLOCK_SIZE, 100);
    player_ota_tick(400);
    CHECK(host_player_ota_committed(), "the last block ends the act with a switch");
    CHECK(strcmp(host_player_ota_committed_version(), "0.7.0") == 0, "the marker names 0.7.0");
    CHECK(host_player_ota_rebooted(), "and the player reboots into it");
    CHECK_EQ(result_of(), PU_OK, "after telling the K7");
    CHECK(memcmp(host_player_ota_slot(), image, sizeof image) == 0, "the slot is the image");
    CHECK(!player_ota_updating(), "and the light stops saying Updating");
}

static void test_verify_refusal(void)
{
    static uint8_t image[PU_BLOCK_SIZE];
    uint8_t sha[32];
    fill(image, sizeof image);
    ho_sha256(image, sizeof image, sha);

    host_player_reset();
    player_ota_init();
    host_player_ota_record(true);
    host_player_ota_fail_verify(true);

    feed_begin(ver(0, 7, 0), sizeof image, PU_BOARD_ESP32, sha);
    player_ota_tick(100);
    feed_block(0, image, sizeof image);
    player_ota_tick(200);

    CHECK_EQ(result_of(), PU_VERIFY_FAILED, "an image that does not verify is refused (U18)");
    CHECK(!host_player_ota_committed(), "and is not switched to");
    CHECK(!host_player_ota_rebooted(), "and the player does not reboot");
    CHECK(!player_ota_updating(), "the act is over");
}

static void test_restart_and_gap(void)
{
    uint8_t sha[32];
    memset(sha, 0x11, sizeof sha);
    host_player_reset();
    player_ota_init();
    host_player_ota_record(true);

    /* The begin again is how a lost begin is recovered: the slot is opened again. */
    feed_begin(ver(0, 7, 0), 4096, PU_BOARD_ESP32, sha);
    player_ota_tick(100);
    feed_begin(ver(0, 7, 0), 4096, PU_BOARD_ESP32, sha);
    player_ota_tick(200);
    CHECK_EQ(host_player_ota_begins(), 2, "a second begin restarts the act");
    CHECK_EQ(result_of(), PU_READY, "and is answered with ready");

    /* Two seconds without a block: the act is abandoned and the light lets go. */
    player_ota_tick(200 + PLAYER_OTA_GAP_MS + 1);
    CHECK(!player_ota_updating(), "a silent K7 abandons the act (U17)");
    CHECK(host_player_ota_written() == 0, "with nothing written");
}

int main(void)
{
    test_boot();
    test_probe();
    test_begin_refusals();
    test_accept_blocks_and_verify();
    test_verify_refusal();
    test_restart_and_gap();
    TEST_END();
}
