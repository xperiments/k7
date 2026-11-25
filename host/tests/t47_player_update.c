/*
 * T47: the player's update, the bytes.
 *
 * The tables in docs/specs/player-update.md are checked twice: once by writing a value and
 * reading it back out of the packet at the offset the table names, and once by handing the
 * parser a packet damaged in every way the table can be damaged. The digest is the handover's
 * own SHA-256 - already proven against FIPS 180-4 in t41 - because the header's whole job is
 * to carry a hash the other board recomputes.
 */
#include "player_update.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "handover.h"
#include "test_util.h"

/* A buffer that is not all one byte, so a field written in the wrong place shows up. */
static void fill(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(i * 7u + 3u);
}

static pu_version_t ver(uint16_t major, uint16_t minor, uint16_t patch)
{
    pu_version_t v = {major, minor, patch};
    return v;
}

/* The 0.6.2 image: 891,712 bytes is 0x000D9B40, which puts a different byte in each of the
 * four the length occupies. */
#define IMAGE_062 891712u

static void test_header_round_trip(void)
{
    static uint8_t image[4096];
    uint8_t digest[32];
    fill(image, sizeof image);
    ho_sha256(image, sizeof image, digest);

    pu_header_t h = {
        .board = PU_BOARD_ESP32,
        .image_len = IMAGE_062,
        .version = ver(0, 7, 0),
        .sig_len = 70,
    };
    memcpy(h.sha256, digest, 32);
    fill(h.sig, sizeof h.sig);

    uint8_t out[PU_HEADER_SIZE];
    CHECK(pu_header_encode(&h, out), "a header encodes");

    /* U1's table, read from the bytes rather than from the writer. */
    CHECK(memcmp(out, "K7PU", 4) == 0, "the magic is at 0");
    CHECK_EQ(out[4], 1, "the header version is at 4");
    CHECK_EQ(out[5], PU_BOARD_ESP32, "the board is at 5");
    CHECK_EQ(out[6], 70, "the signature length is at 6");
    CHECK_EQ(out[7], 0, "the reserved byte at 7 is zero");
    CHECK_EQ(out[8], 0x00, "the image length at 8, high byte");
    CHECK_EQ(out[9], 0x0D, "the image length at 9");
    CHECK_EQ(out[10], 0x9B, "the image length at 10");
    CHECK_EQ(out[11], 0x40, "the image length at 11, low byte");
    CHECK_EQ(out[12], 0, "the version's major at 12");
    CHECK_EQ(out[13], 0, "the version's major at 13");
    CHECK_EQ(out[14], 0, "the version's minor at 14");
    CHECK_EQ(out[15], 7, "the version's minor at 15");
    CHECK_EQ(out[16], 0, "the version's patch at 16");
    CHECK_EQ(out[17], 0, "the version's patch at 17");
    CHECK_EQ(out[18], 0, "the reserved run at 18 is zero");
    CHECK_EQ(out[19], 0, "the reserved run at 19 is zero");
    CHECK(memcmp(out + 20, digest, 32) == 0, "the SHA-256 is at 20");
    CHECK(memcmp(out + 52, h.sig, 70) == 0, "the signature is at 52");
    CHECK_EQ(out[122], 0, "the bytes after the signature are zero");
    CHECK_EQ(out[123], 0, "up to the reserved tail");
    for (int i = 124; i < PU_HEADER_SIZE; i++) CHECK_EQ(out[i], 0, "the tail is zero");

    pu_header_t back;
    CHECK(pu_header_parse(out, sizeof out, &back), "the header parses");
    CHECK_EQ(back.board, h.board, "the board comes back");
    CHECK_EQ(back.image_len, h.image_len, "the length comes back");
    CHECK_EQ(back.version.major, 0, "the major comes back");
    CHECK_EQ(back.version.minor, 7, "the minor comes back");
    CHECK_EQ(back.sig_len, 70, "the signature length comes back");
    CHECK(memcmp(back.sha256, digest, 32) == 0, "the digest comes back");
    CHECK(memcmp(back.sig, h.sig, 70) == 0, "the signature comes back");

    CHECK(pu_header_matches_digest(&back, digest), "the digest matches the image");
    digest[31] ^= 0x01;
    CHECK(!pu_header_matches_digest(&back, digest), "one changed byte does not");

    /* The largest legal image, and the shortest signature. */
    h.image_len = PU_IMAGE_MAX;
    h.sig_len = 1;
    CHECK(pu_header_encode(&h, out), "the format's ceiling encodes");
    CHECK(pu_header_parse(out, sizeof out, &back), "and parses");
    CHECK_EQ(back.image_len, PU_IMAGE_MAX, "at the ceiling's length");
}

#define REFUSES_HEADER(what, stmt)                                                           \
    do {                                                                                     \
        uint8_t bad[PU_HEADER_SIZE];                                                         \
        memcpy(bad, out, sizeof bad);                                                        \
        stmt;                                                                                \
        pu_header_t x;                                                                       \
        CHECK(!pu_header_parse(bad, sizeof bad, &x), what);                                  \
    } while (0)

static void test_header_refuses(void)
{
    pu_header_t h = {
        .board = PU_BOARD_ESP32,
        .image_len = IMAGE_062,
        .version = ver(1, 0, 0),
        .sig_len = 70,
    };
    fill(h.sha256, 32);
    fill(h.sig, sizeof h.sig);
    uint8_t out[PU_HEADER_SIZE];
    CHECK(pu_header_encode(&h, out), "the reference header encodes");

    REFUSES_HEADER("a wrong magic is not a header", bad[0] = 'X');
    REFUSES_HEADER("a header version of 2 is not one", bad[4] = 2);
    REFUSES_HEADER("board 0 is not a board", bad[5] = 0);
    REFUSES_HEADER("board 2 is not this board", bad[5] = 2);
    REFUSES_HEADER("a signature of length 0", bad[6] = 0);
    REFUSES_HEADER("a signature of 73", bad[6] = 73);
    REFUSES_HEADER("a non-zero byte at 7", bad[7] = 1);
    REFUSES_HEADER("a non-zero byte at 18", bad[18] = 1);
    REFUSES_HEADER("a non-zero byte at 19", bad[19] = 1);
    REFUSES_HEADER("a non-zero byte at 127", bad[127] = 1);
    REFUSES_HEADER("an image of length 0", bad[8] = bad[9] = bad[10] = bad[11] = 0);
    REFUSES_HEADER("an image past the format's ceiling",
                   (bad[8] = 0x00, bad[9] = 0x20, bad[10] = 0x00, bad[11] = 0x00));
    REFUSES_HEADER("an image one byte past the ceiling",
                   (bad[8] = 0x00, bad[9] = 0x1F, bad[10] = 0xFF, bad[11] = 0x81));
    CHECK(!pu_header_parse(out, PU_HEADER_SIZE - 1, &h), "a truncated header");
    CHECK(!pu_header_parse(out, PU_HEADER_SIZE + 1, &h), "an over-long header");

    h.image_len = 0;
    CHECK(!pu_header_encode(&h, out), "an image of length 0 does not encode");
    h.image_len = IMAGE_062;
    h.sig_len = 73;
    CHECK(!pu_header_encode(&h, out), "a signature of 73 does not encode");
    h.sig_len = 70;
    h.board = 2;
    CHECK(!pu_header_encode(&h, out), "another board does not encode");
}

static void test_probe_and_answer(void)
{
    uint8_t pkt[PU_ANSWER_SIZE];
    CHECK_EQ(pu_probe_encode(0xDEADBEEFu, pkt), PU_PROBE_SIZE, "the probe is six bytes");
    CHECK_EQ(pkt[0], 1, "its version");
    CHECK_EQ(pkt[1], PU_TYPE_PROBE, "its type");
    CHECK_EQ(pkt[2], 0xDE, "the nonce's first byte");
    CHECK_EQ(pkt[5], 0xEF, "the nonce's last byte");

    uint32_t nonce = 0;
    CHECK(pu_probe_parse(pkt, PU_PROBE_SIZE, &nonce), "the probe parses");
    CHECK_EQ(nonce, 0xDEADBEEFu, "and carries the nonce");

    uint8_t bad[PU_ANSWER_SIZE];
    memcpy(bad, pkt, PU_PROBE_SIZE);
    bad[1] = PU_TYPE_ANSWER;
    CHECK(!pu_probe_parse(bad, PU_PROBE_SIZE, &nonce), "an answer is not a probe");
    CHECK(!pu_probe_parse(pkt, PU_PROBE_SIZE - 1, &nonce), "a short probe");
    CHECK(!pu_probe_parse(pkt, PU_PROBE_SIZE + 1, &nonce), "a long probe");

    pu_answer_t a = {.nonce = 0x01020304u, .version = ver(1, 2, 3), .board = PU_BOARD_ESP32,
                     .flags = 0};
    CHECK_EQ(pu_answer_encode(&a, pkt), PU_ANSWER_SIZE, "the answer is fourteen bytes");
    CHECK_EQ(pkt[1], PU_TYPE_ANSWER, "its type");
    CHECK_EQ(pkt[6], 0, "the version's major at 6, high byte");
    CHECK_EQ(pkt[7], 1, "the version's major at 7");
    CHECK_EQ(pkt[8], 0, "the version's minor at 8, high byte");
    CHECK_EQ(pkt[9], 2, "the version's minor at 9");
    CHECK_EQ(pkt[10], 0, "the version's patch at 10, high byte");
    CHECK_EQ(pkt[11], 3, "the version's patch at 11");
    CHECK_EQ(pkt[12], PU_BOARD_ESP32, "the board at 12");
    CHECK_EQ(pkt[13], 0, "the flags at 13");

    pu_answer_t back;
    CHECK(pu_answer_parse(pkt, PU_ANSWER_SIZE, &back), "the answer parses");
    CHECK_EQ(back.nonce, 0x01020304u, "its nonce");
    CHECK_EQ(back.version.major, 1, "its major");
    CHECK_EQ(back.version.minor, 2, "its minor");
    CHECK_EQ(back.version.patch, 3, "its patch");
    CHECK_EQ(back.board, PU_BOARD_ESP32, "its board");

    /* An unknown flag bit is an old reader's, not a malformed frame. */
    pkt[13] = 0x80;
    CHECK(pu_answer_parse(pkt, PU_ANSWER_SIZE, &back), "an unknown flag is still an answer");
    CHECK_EQ(back.flags, 0x80, "and it is reported");
}

static void test_begin(void)
{
    pu_begin_t b = {
        .version = ver(0, 7, 0),
        .board = PU_BOARD_ESP32,
        .image_len = IMAGE_062,
        .sig_len = 70,
    };
    fill(b.sha256, 32);
    fill(b.sig, sizeof b.sig);

    uint8_t pkt[PU_BEGIN_SIZE];
    CHECK_EQ(pu_begin_encode(&b, pkt), PU_BEGIN_SIZE, "the begin is 118 bytes");
    CHECK_EQ(pkt[0], 1, "its version");
    CHECK_EQ(pkt[1], PU_TYPE_BEGIN, "its type");
    CHECK_EQ(pkt[2], 0, "the image version's major at 2");
    CHECK_EQ(pkt[4], 0, "the image version's minor at 4");
    CHECK_EQ(pkt[5], 7, "the image version's minor, second byte");
    CHECK_EQ(pkt[8], PU_BOARD_ESP32, "the board at 8");
    CHECK_EQ(pkt[9], 0x00, "the image length at 9, high byte");
    CHECK_EQ(pkt[10], 0x0D, "the image length at 10");
    CHECK_EQ(pkt[11], 0x9B, "the image length at 11");
    CHECK_EQ(pkt[12], 0x40, "the image length at 12, low byte");
    CHECK(memcmp(pkt + 13, b.sha256, 32) == 0, "the SHA-256 at 13");
    CHECK_EQ(pkt[45], 70, "the signature length at 45");
    CHECK(memcmp(pkt + 46, b.sig, 70) == 0, "the signature at 46");

    pu_begin_t back;
    CHECK(pu_begin_parse(pkt, PU_BEGIN_SIZE, &back), "the begin parses");
    CHECK_EQ(back.version.minor, 7, "its version");
    CHECK_EQ(back.image_len, IMAGE_062, "its length");
    CHECK_EQ(back.sig_len, 70, "its signature length");
    CHECK(memcmp(back.sha256, b.sha256, 32) == 0, "its digest");

    /* The board and the room are the receiver's judgement, so a foreign board still parses:
     * it is how the player can answer "wrong board" instead of going silent (U10). */
    pkt[8] = 2;
    CHECK(pu_begin_parse(pkt, PU_BEGIN_SIZE, &back), "a foreign board is still a begin");
    CHECK_EQ(back.board, 2, "and says which");

    uint8_t bad[PU_BEGIN_SIZE];
    memcpy(bad, pkt, sizeof bad);
    bad[45] = 0;
    CHECK(!pu_begin_parse(bad, PU_BEGIN_SIZE, &back), "a signature of length 0");
    memcpy(bad, pkt, sizeof bad);
    bad[45] = 73;
    CHECK(!pu_begin_parse(bad, PU_BEGIN_SIZE, &back), "a signature of 73");
    memcpy(bad, pkt, sizeof bad);
    bad[1] = PU_TYPE_BLOCK;
    CHECK(!pu_begin_parse(bad, PU_BEGIN_SIZE, &back), "a block is not a begin");
    CHECK(!pu_begin_parse(pkt, PU_BEGIN_SIZE - 1, &back), "a short begin");
}

/* One block of `len` bytes fragments, parses back and reassembles byte for byte. */
static void check_block(size_t len, size_t want_frags)
{
    static uint8_t block[PU_BLOCK_SIZE];
    static uint8_t joined[PU_BLOCK_SIZE];
    uint8_t packets[PU_BLOCK_MAX_FRAGS * PU_MTU];
    size_t lens[PU_BLOCK_MAX_FRAGS];
    fill(block, len);

    size_t n = pu_block_fragment(0x1234, block, len, packets, lens);
    CHECK_EQ(n, want_frags, "the fragment count");

    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        pu_frag_t f;
        CHECK(lens[i] <= PU_MTU, "a packet fits the radio's payload");
        CHECK(pu_block_parse(packets + i * PU_MTU, lens[i], &f), "the packet parses");
        CHECK_EQ(f.block, 0x1234, "its block");
        CHECK_EQ(f.frag, i, "its index, in order");
        CHECK_EQ(f.nfrags, want_frags, "its count");
        memcpy(joined + off, f.payload, f.payload_len);
        off += f.payload_len;
    }
    CHECK_EQ(off, len, "the payloads add up to the block");
    CHECK(memcmp(joined, block, len) == 0, "and reassemble byte for byte");
}

static void test_blocks(void)
{
    /* A whole block: sixteen full packets and one of 192. The 0.6.2 image's last block is
     * 2 880 bytes, which is eleven full packets and one of 196. */
    check_block(PU_BLOCK_SIZE, PU_BLOCK_MAX_FRAGS);
    check_block(2880, 12);
    check_block(245, 2);
    check_block(244, 1);

    static uint8_t block[PU_BLOCK_SIZE + 1];
    uint8_t packets[PU_BLOCK_MAX_FRAGS * PU_MTU];
    size_t lens[PU_BLOCK_MAX_FRAGS];
    fill(block, sizeof block);
    CHECK_EQ(pu_block_fragment(0, block, sizeof block, packets, lens), 0,
             "a block over 4 KB is refused");
    CHECK_EQ(pu_block_fragment(0, block, 0, packets, lens), 0, "an empty block is refused");

    /* Damage every part of the header. */
    static uint8_t one[PU_BLOCK_SIZE];
    fill(one, sizeof one);
    size_t n = pu_block_fragment(7, one, sizeof one, packets, lens);
    CHECK_EQ(n, PU_BLOCK_MAX_FRAGS, "the reference block");
    uint8_t bad[PU_MTU];
    pu_frag_t f;
    memcpy(bad, packets + 1 * PU_MTU, lens[1]);
    bad[1] = PU_TYPE_ACK;
    CHECK(!pu_block_parse(bad, lens[1], &f), "an ack is not a block");
    memcpy(bad, packets + 1 * PU_MTU, lens[1]);
    bad[4] = 17;
    CHECK(!pu_block_parse(bad, lens[1], &f), "an index past the count");
    memcpy(bad, packets + 1 * PU_MTU, lens[1]);
    bad[5] = 0;
    CHECK(!pu_block_parse(bad, lens[1], &f), "a count of zero");
    memcpy(bad, packets + 1 * PU_MTU, lens[1]);
    bad[5] = 18;
    CHECK(!pu_block_parse(bad, lens[1], &f), "a count of eighteen");
    CHECK(!pu_block_parse(packets + 1 * PU_MTU, lens[1] - 1, &f),
          "a short non-final fragment");
    CHECK(!pu_block_parse(packets + 1 * PU_MTU, lens[1] + 1, &f),
          "a fragment over the payload");
    CHECK(!pu_block_parse(packets, 5, &f), "a fragment without its block index");
    /* The last fragment may be short, and the packet may end after it. */
    CHECK(pu_block_parse(packets + 16 * PU_MTU, lens[16], &f), "a short last fragment parses");
    CHECK_EQ(f.payload_len, 192, "and is 192 bytes");
}

static void test_ack_and_result(void)
{
    uint8_t pkt[PU_RESULT_SIZE];
    CHECK_EQ(pu_ack_encode(9, PU_ACK_WRITTEN, pkt), PU_ACK_SIZE, "the ack is five bytes");
    CHECK_EQ(pkt[1], PU_TYPE_ACK, "its type");
    CHECK_EQ(pkt[2], 0, "the block at 2, high byte");
    CHECK_EQ(pkt[3], 9, "the block at 3");
    CHECK_EQ(pkt[4], PU_ACK_WRITTEN, "the result at 4");

    uint16_t block = 0;
    uint8_t result = 0xFF;
    CHECK(pu_ack_parse(pkt, PU_ACK_SIZE, &block, &result), "the ack parses");
    CHECK_EQ(block, 9, "its block");
    CHECK_EQ(result, PU_ACK_WRITTEN, "its result");
    CHECK_EQ(pu_ack_encode(1, PU_ACK_REFUSED, pkt), PU_ACK_SIZE, "a refusal encodes");
    CHECK_EQ(pu_ack_encode(1, 3, pkt), 0, "a result of 3 does not encode");

    CHECK_EQ(pu_result_encode(PU_VERIFY_FAILED, ver(0, 7, 0), pkt), PU_RESULT_SIZE,
             "the result is nine bytes");
    CHECK_EQ(pkt[1], PU_TYPE_RESULT, "its type");
    CHECK_EQ(pkt[2], PU_VERIFY_FAILED, "the result at 2");
    CHECK_EQ(pkt[3], 0, "the image version's major at 3");
    CHECK_EQ(pkt[5], 0, "the image version's minor at 5");
    CHECK_EQ(pkt[6], 7, "the image version's minor, second byte");
    CHECK_EQ(pkt[8], 0, "the image version's patch at 8");

    pu_version_t v;
    CHECK(pu_result_parse(pkt, PU_RESULT_SIZE, &result, &v), "the result parses");
    CHECK_EQ(result, PU_VERIFY_FAILED, "its result");
    CHECK_EQ(v.minor, 7, "its version");
    CHECK_EQ(pu_result_encode(PU_READY, ver(0, 7, 0), pkt), PU_RESULT_SIZE, "ready encodes");
    CHECK_EQ(pkt[2], PU_READY, "and is the begin's acceptance");
    CHECK_EQ(pu_result_encode(7, v, pkt), 0, "a result of 7 does not encode");
}

static void test_version(void)
{
    CHECK_EQ(pu_version_cmp(ver(0, 6, 2), ver(0, 7, 0)), -1, "0.6.2 is before 0.7.0");
    CHECK_EQ(pu_version_cmp(ver(0, 7, 0), ver(0, 6, 2)), 1, "and after it, the other way");
    CHECK_EQ(pu_version_cmp(ver(0, 6, 2), ver(0, 6, 2)), 0, "the same version");
    CHECK_EQ(pu_version_cmp(ver(0, 10, 0), ver(0, 9, 9)), 1, "0.10.0 is after 0.9.9");
    CHECK_EQ(pu_version_cmp(ver(1, 0, 0), ver(0, 99, 99)), 1, "a major wins over anything");

    char text[PU_VERSION_TEXT_MAX];
    CHECK_EQ(pu_version_format(ver(0, 6, 2), text), 5, "the text's length");
    CHECK(strcmp(text, "0.6.2") == 0, "the row's word for it");
    CHECK_EQ(pu_version_format(ver(65535, 65535, 65535), text), 17, "the widest text's length");
    CHECK(strcmp(text, "65535.65535.65535") == 0, "which still fits the buffer");

    /* The rule the row reads (U26): equal is a no-op, different is offered either way. */
    CHECK(pu_version_cmp(ver(0, 6, 2), ver(0, 6, 2)) == 0, "equal is a no-op");
    CHECK(pu_version_cmp(ver(0, 7, 0), ver(0, 6, 2)) != 0, "a step back is offered");
    CHECK(pu_version_cmp(ver(0, 6, 2), ver(0, 7, 0)) != 0, "and so is a step forward");
}

static void test_geometry(void)
{
    CHECK_EQ(pu_block_count(IMAGE_062), 218, "the 0.6.2 image is 218 blocks");
    CHECK_EQ(pu_block_count(PU_BLOCK_SIZE), 1, "a block is one block");
    CHECK_EQ(pu_block_count(1), 1, "one byte is one block");
    CHECK_EQ(pu_block_count(PU_BLOCK_SIZE + 1), 2, "one byte over is two");
    CHECK_EQ(pu_block_count(0), 0, "nothing is none");
}

int main(void)
{
    test_header_round_trip();
    test_header_refuses();
    test_probe_and_answer();
    test_begin();
    test_blocks();
    test_ack_and_result();
    test_version();
    test_geometry();
    TEST_END();
}
