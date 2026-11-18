#pragma once
/*
 * espnow_stream - the bytes of the player's audio over ESP-NOW: one MP3 frame in fragments, and
 * the status packet that tells the sender what the receiver can take and what it missed.
 *
 * Both firmwares compile this file: the K7's emitter fragments and answers resends, the player's
 * receiver reassembles and asks. The format is docs/specs/espnow-stream.md E1-E3 and the decision
 * is ADR 0103. The assembler, the gap window and the credit arithmetic are the two boards'
 * business and live beside their own state, not here.
 *
 * The version and the type bytes are the handover's: 0x01 beacon, 0x02 request, 0x03 record, and
 * these two (0x04 audio, 0x05 status). One receiver dispatches on them, over one radio.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ES_VERSION 1

#define ES_TYPE_AUDIO  0x04
#define ES_TYPE_STATUS 0x05

/* ESP_NOW_MAX_DATA_LEN: v2 raises it to 1470, but the handover's frames are v1 and share the
 * radio, so every packet this component builds stays under the smaller number. */
#define ES_MTU         250
#define ES_HEADER      6
#define ES_MAX_PAYLOAD (ES_MTU - ES_HEADER) /* 244 */
#define ES_MAX_FRAGS   6                    /* ceil(1441 / 244): the largest legal MP3 frame */

/* E3's ceiling: eight missing frames is 209 ms of audio, and the reorder window is wider. */
#define ES_MAX_NACK 8

/* One audio fragment, decoded. `payload` points into the caller's packet. */
typedef struct {
    uint16_t seq;
    uint8_t  frag;
    uint8_t  nfrags;
    const uint8_t *payload;
    size_t   payload_len;
} es_frag_t;

/* Fragment one whole MP3 frame into packets of at most ES_MTU bytes, written back to back into
 * `out` (ES_MAX_FRAGS * ES_MTU bytes), with each packet's length in `lens`.
 *
 * Returns the packet count, or 0 when the frame is empty or does not fit ES_MAX_FRAGS fragments -
 * the encoder's largest frame is 1 441 bytes and that is six. */
size_t es_fragment(uint16_t seq, const uint8_t *frame, size_t len,
                   uint8_t *out, size_t lens[ES_MAX_FRAGS]);

/* Decode one packet. False unless it is a version-1 audio fragment whose header and length agree:
 * a fragment count of zero, an index past the count, or a payload that overruns the packet is not
 * a fragment. */
bool es_parse_frag(const uint8_t *pkt, size_t len, es_frag_t *out);

/* The player's status (E3): free ring bytes, the highest sequence it holds without a gap, and the
 * sequences it is missing, oldest first. */
typedef struct {
    uint32_t free_bytes;
    uint16_t highest;
    uint8_t  nack_count;
    uint16_t nack[ES_MAX_NACK];
} es_status_t;

/* 8 + 2 * nack_count bytes, or 0 when the packet would not fit `cap`. */
size_t es_status_encode(const es_status_t *s, uint8_t *out, size_t cap);

/* False unless it is a version-1 status packet whose declared list fits its length. */
bool es_status_parse(const uint8_t *pkt, size_t len, es_status_t *out);

#ifdef __cplusplus
}
#endif
