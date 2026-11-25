#pragma once
/*
 * player_update - the bytes of a K7Player's firmware update: the header a signed image is
 * wrapped in, and the six ESP-NOW frames that find a player, claim an image and move it in
 * 4 KB blocks. The format is docs/specs/player-update.md U1-U16; the decisions are ADR 0116
 * and ADR 0117.
 *
 * Portable on purpose: no radio, no flash, no RTOS. Both firmwares compile this file - the K7
 * builds the header and the frames and reads the image out of its own flash; the player parses
 * them, writes its spare slot and checks the claim - and the host proves the tables byte by
 * byte, because a field that means one thing on one side and another on the other is not found
 * until a board is in front of a person.
 *
 * The signature itself is not here, for the same reason the handover's curve is not: both
 * boards call mbedTLS for ECDSA, and a second signature implementation in this repository
 * would be a second thing to be wrong. A caller computes the image's digest with the SHA-256
 * it already links and stops a frame; this file moves the bytes and compares the digest.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The update's own version, and the type bytes it owns on a radio that already carries the
 * handover's 0x01-0x03 and the stream's 0x04-0x06 (espnow-stream.md). */
#define PU_VERSION 1

#define PU_TYPE_PROBE  0x07
#define PU_TYPE_ANSWER 0x08
#define PU_TYPE_BEGIN  0x09
#define PU_TYPE_BLOCK  0x0A
#define PU_TYPE_ACK    0x0B
#define PU_TYPE_RESULT 0x0C

/* The image's header (U1). 128 bytes, so the image starts on a line and the K7 can read the
 * header alone to know what version it would hand over.
 *
 *   0  4  magic, "K7PU"
 *   4  1  header version, 1
 *   5  1  board, 1 = the classic ESP32 K7Player
 *   6  1  signature length, 1..72
 *   7  1  reserved, 0
 *   8  4  image length, big-endian
 *  12  6  version: major, minor, patch, each a u16 big-endian
 *  18  2  reserved, 0
 *  20 32  SHA-256 of the image
 *  52 72  the signature, DER, the first `signature length` bytes
 * 124  4  reserved, 0
 */
#define PU_HEADER_SIZE 128
#define PU_MAGIC       "K7PU"
#define PU_SIG_MAX     72
#define PU_BOARD_ESP32 1

/* The partition, not the format, bounds this; the K7's is 2 MB and the header lives in it. */
#define PU_IMAGE_MAX (2u * 1024u * 1024u - PU_HEADER_SIZE)

/* A block is 4 KB in at most seventeen packets, and a packet is ESP-NOW v1's 250 bytes. */
#define PU_BLOCK_SIZE      4096
#define PU_BLOCK_MAX_FRAGS 17
#define PU_MTU             250
#define PU_FRAG_PAYLOAD    (PU_MTU - 6) /* 244 */

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} pu_version_t;

typedef struct {
    uint8_t      board;
    uint32_t     image_len;
    pu_version_t version;
    uint8_t      sha256[32];
    uint8_t      sig[PU_SIG_MAX];
    uint8_t      sig_len;
} pu_header_t;

/* 128 bytes, zeroing both reserved runs. */
bool pu_header_encode(const pu_header_t *h, uint8_t out[PU_HEADER_SIZE]);

/* False unless `len` is exactly 128 and every field is one the writer could have made: the
 * magic, header version 1, a known board, a signature length of 1..72, an image length of
 * 1..PU_IMAGE_MAX, and both reserved runs zero. */
bool pu_header_parse(const uint8_t *in, size_t len, pu_header_t *out);

/* True when the header's SHA-256 is the digest the caller computed over the image that follows
 * it. The image's bytes are the caller's: the K7 maps its partition, the player reads its slot.
 * A header whose digest disagrees is not an image (U1), whatever the signature says. */
bool pu_header_matches_digest(const pu_header_t *h, const uint8_t sha256[32]);

/* 6 bytes: version, type, a nonce that changes with every act. */
#define PU_PROBE_SIZE 6
size_t pu_probe_encode(uint32_t nonce, uint8_t out[PU_PROBE_SIZE]);
bool   pu_probe_parse(const uint8_t *pkt, size_t len, uint32_t *nonce);

/* 14 bytes: version, type, the probe's nonce, the player's version, its board, flags. */
#define PU_ANSWER_SIZE 14
typedef struct {
    uint32_t     nonce;
    pu_version_t version;
    uint8_t      board;
    uint8_t      flags;
} pu_answer_t;
size_t pu_answer_encode(const pu_answer_t *a, uint8_t out[PU_ANSWER_SIZE]);
bool   pu_answer_parse(const uint8_t *pkt, size_t len, pu_answer_t *out);

/* 118 bytes: the whole claim. Its fields are the header's without the magic, because the
 * player has to verify what it is given, not what it could read out of a partition. */
#define PU_BEGIN_SIZE 118
typedef struct {
    pu_version_t version;
    uint8_t      board;
    uint32_t     image_len;
    uint8_t      sha256[32];
    uint8_t      sig[PU_SIG_MAX];
    uint8_t      sig_len;
} pu_begin_t;
size_t pu_begin_encode(const pu_begin_t *b, uint8_t out[PU_BEGIN_SIZE]);
bool   pu_begin_parse(const uint8_t *pkt, size_t len, pu_begin_t *out);

/* One block fragment: 6 bytes of header and up to 244 of payload. */
typedef struct {
    uint16_t       block;
    uint8_t        frag;
    uint8_t        nfrags;
    const uint8_t *payload;
    size_t         payload_len;
} pu_frag_t;

/* Fragment one 4 KB block into packets of at most PU_MTU bytes, written back to back into
 * `out` (PU_BLOCK_MAX_FRAGS * PU_MTU bytes), with each packet's length in `lens`.
 *
 * Returns the packet count, or 0 when the block is empty or over 4 KB - the last block of an
 * image is short, and a block longer than 4 KB is not one. */
size_t pu_block_fragment(uint16_t block, const uint8_t *data, size_t len, uint8_t *out,
                         size_t lens[PU_BLOCK_MAX_FRAGS]);

/* False unless it is a version-1 block fragment whose header agrees with its length: a count
 * of 0 or over 17, an index past the count, an empty payload, or a short non-final fragment
 * (which would splice a hole into a block that claims to be whole). */
bool pu_block_parse(const uint8_t *pkt, size_t len, pu_frag_t *out);

/* 5 bytes: version, type, block index, result. */
#define PU_ACK_SIZE 5
#define PU_ACK_WRITTEN 0
#define PU_ACK_RETRY   1
#define PU_ACK_REFUSED 2
size_t pu_ack_encode(uint16_t block, uint8_t result, uint8_t out[PU_ACK_SIZE]);
bool   pu_ack_parse(const uint8_t *pkt, size_t len, uint16_t *block, uint8_t *result);

/* 9 bytes: version, type, result, the image's version. `ready` is the begin's acceptance,
 * sent once the spare slot is erased and open; the K7 sends no block before it (U10, U13a). */
#define PU_RESULT_SIZE 9
#define PU_OK             0
#define PU_SAME_VERSION   1
#define PU_WRONG_BOARD    2
#define PU_NO_ROOM        3
#define PU_VERIFY_FAILED  4
#define PU_ABORTED        5
#define PU_READY          6
size_t pu_result_encode(uint8_t result, pu_version_t version, uint8_t out[PU_RESULT_SIZE]);
bool   pu_result_parse(const uint8_t *pkt, size_t len, uint8_t *result, pu_version_t *version);

/* The version rule: the same version is a no-op and any different one is offered (U26). The
 * comparison is numeric, so 0.10.0 is after 0.9.9 and never before it. */
int pu_version_cmp(pu_version_t a, pu_version_t b);

/* "0.6.2" in `out`, which holds PU_VERSION_TEXT_MAX bytes including the terminator; returns
 * the number of characters written. */
#define PU_VERSION_TEXT_MAX 18
size_t pu_version_format(pu_version_t v, char out[PU_VERSION_TEXT_MAX]);

/* How many 4 KB blocks an image of `image_len` is: 218 for the 0.6.2 image. */
unsigned pu_block_count(uint32_t image_len);

#ifdef __cplusplus
}
#endif
