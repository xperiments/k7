#pragma once
/*
 * handover - the bytes a K7 and a K7Player exchange in a share window, and the key that
 * protects them.
 *
 * Two firmwares implement this one protocol: the K7 sends, the player receives, and a
 * field that means one thing on one side and another on the other cannot be found until a
 * board is in front of a person. So the format and the derivation live here, once, and
 * both firmwares compile this file. ADR 0099 is the decision; docs/specs/network-handover.md
 * §5 is the contract, byte by byte.
 *
 * Frames (all integers big-endian, all lengths in bytes):
 *
 *   beacon   36   version, type 0x01, X25519 public key (32), channel, seconds left
 *   request  34   version, type 0x02, X25519 public key (32)
 *   record   10..235, type 0x03, flags, length-prefixed home/ap/host, IPv4 (4), crc8
 *
 * The sender's MAC is never in a frame: ESP-NOW hands the receiver
 * esp_now_recv_info_t.src_addr, and a body field would be the same fact twice.
 *
 * The record's fields present only when their flag bit says so, and the flag bits and the
 * lengths have to agree. A record longer than HO_RECORD_MAX is not a record: ESP-NOW's own
 * ceiling is ESP_NOW_MAX_DATA_LEN (250), and HO_RECORD_MAX is the arithmetic proof that the
 * format fits under it with a field at every maximum.
 *
 * The key. The LMK is HKDF-SHA256 over the X25519 shared secret and a fixed transcript
 * (H27). The curve itself is *not* here, and deliberately: both boards already link
 * mbedTLS, the host build links no crypto library at all, and a second X25519 written into
 * this repository would be a second implementation of a security primitive. ho_lmk() takes
 * the shared secret that the platform's curve produced.
 *
 * The hash is here for the opposite reason: the KDF is ours to get wrong, and it has to be
 * testable on the host, where there is no SHA-256 to borrow. ho_sha256() is FIPS 180-4 and
 * is checked against the published vectors, so the derivation is proven off the board.
 *
 * Validation is the same on both sides, and the same on encode and decode: an SSID of
 * 1..32 bytes with no control characters, a home password of 0..63 printable ASCII (0 means
 * an open network), a setup password of 8..63, a hostname of 1..32. Nothing is accepted
 * that the other side would not have written.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HO_VERSION   1
#define HO_FRAME_MAX 250 /* ESP_NOW_MAX_DATA_LEN, the ceiling every frame belongs under */

#define HO_KEY_LEN 32
#define HO_LMK_LEN 16

#define HO_SSID_MAX 32
#define HO_PASS_MAX 63
#define HO_HOST_MAX 32

#define HO_BEACON_SIZE  36
#define HO_REQUEST_SIZE 34
/* The longest legal record: every field at its maximum, plus version, type, flags, IPv4 and
 * the checksum. Proven in the host test rather than trusted. */
#define HO_RECORD_MAX ((size_t)3 + (1 + HO_SSID_MAX) + (1 + HO_PASS_MAX) + \
                       (1 + HO_SSID_MAX) + (1 + HO_PASS_MAX) + (1 + HO_HOST_MAX) + 4 + 1)

typedef enum {
    HO_TYPE_BEACON = 0x01,
    HO_TYPE_REQUEST = 0x02,
    HO_TYPE_RECORD = 0x03
} ho_type_t;

/* What a K7 announces four or five times a second inside a share window. It carries no
 * secret: a version, a public key, the channel and the seconds left. */
typedef struct {
    uint8_t pub[HO_KEY_LEN];   /* the window's ephemeral X25519 public key */
    uint8_t channel;           /* 1..13 */
    uint8_t seconds_left;      /* 0..60 */
} ho_beacon_t;

/* What a sweeping player answers with, and the only thing it ever sends. */
typedef struct {
    uint8_t pub[HO_KEY_LEN];   /* the request's ephemeral X25519 public key */
} ho_request_t;

/* What the K7 sends back, encrypted. Strings are NUL-terminated here and length-prefixed on
 * the wire; a field that is absent (its flag clear) is empty here. */
typedef struct {
    bool has_home;             /* the K7 has a home network to offer */
    bool has_ap;               /* the K7 has a setup network to offer */
    char home_ssid[HO_SSID_MAX + 1];
    char home_pass[HO_PASS_MAX + 1]; /* empty = the home network is open */
    char ap_ssid[HO_SSID_MAX + 1];
    char ap_pass[HO_PASS_MAX + 1];   /* 8..63 */
    char host[HO_HOST_MAX + 1];      /* the mDNS label, "k7" */
    uint32_t ipv4;                   /* host order; four bytes big-endian on the wire */
} ho_record_t;

/* ---------------------------------------------------------------- frames */

/* Encode into out, at most cap bytes. 0 means the value is not encodable - a field out of
 * range, a channel outside 1..13, or a record that would not fit - and nothing was written
 * that a decoder would accept. */
size_t ho_beacon_encode(const ho_beacon_t *b, uint8_t *out, size_t cap);
size_t ho_request_encode(const ho_request_t *r, uint8_t *out, size_t cap);
size_t ho_record_encode(const ho_record_t *r, uint8_t *out, size_t cap);

/* Decode exactly one frame. False on a wrong length, an unknown version or type, a field out
 * of range, a flag that disagrees with its lengths, or a checksum that does not match. */
bool ho_beacon_decode(const uint8_t *in, size_t len, ho_beacon_t *out);
bool ho_request_decode(const uint8_t *in, size_t len, ho_request_t *out);
bool ho_record_decode(const uint8_t *in, size_t len, ho_record_t *out);

/* ---------------------------------------------------------------- the key */

void ho_sha256(const uint8_t *in, size_t len, uint8_t out[32]);
void ho_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len,
                    uint8_t out[32]);

/* RFC 5869. A zero-length salt is the RFC's 32 zero bytes; out_len is capped at 255 * 32. */
bool ho_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *salt, size_t salt_len,
                    const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len);

/* The handover's LMK: HKDF-SHA256(shared, salt empty, "k7-handover-v1" || k7_pub ||
 * player_pub, 16). False when shared is all zeroes - the low-order-point answer is not a
 * network record - and the output is then untouched. */
bool ho_lmk(const uint8_t shared[HO_KEY_LEN], const uint8_t k7_pub[HO_KEY_LEN],
            const uint8_t player_pub[HO_KEY_LEN], uint8_t out[HO_LMK_LEN]);

#ifdef __cplusplus
}
#endif
