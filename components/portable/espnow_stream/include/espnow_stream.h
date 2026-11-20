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
 * The version and the type bytes are the handover's: 0x01 beacon, 0x02 request, 0x03 record,
 * 0x04 audio, 0x05 status and 0x06 tone. One receiver dispatches on them, over one radio.
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
#define ES_TYPE_TONE   0x06

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

/* E3a's optional flags byte, after the sequence list. Bit 0 says the player applies the K7's
 * tone (E12/E13); the other bits are reserved. An absent byte reads as 0. */
#define ES_STATUS_FLAG_TONE 0x01

/* The player's status (E3): free ring bytes, the highest sequence it holds without a gap, the
 * sequences it is missing, oldest first, and the optional flags byte. */
typedef struct {
    uint32_t free_bytes;
    uint16_t highest;
    uint8_t  nack_count;
    uint16_t nack[ES_MAX_NACK];
    uint8_t  flags;
} es_status_t;

/* 9 + 2 * nack_count bytes, or 0 when the packet would not fit `cap`. */
size_t es_status_encode(const es_status_t *s, uint8_t *out, size_t cap);

/* False unless it is a version-1 status packet whose declared list fits its length. A packet
 * without the flags byte parses with flags 0: that is an old player, not a malformed one. */
bool es_status_parse(const uint8_t *pkt, size_t len, es_status_t *out);

/* ------------------------------------------------------------------- the tone (E12)

 * The K7 sends the player the four EQ values when the player says it can apply them: 10 bytes,
 * absolute centi-dB, clamped to the knobs' ranges. The player side's conversion to dB and the
 * tone_eq that consumes it are k7player.md R33's. */

#define ES_TONE_SIZE 10
#define ES_TONE_GAIN_MIN_CDB (-1200) /* -12 dB */
#define ES_TONE_GAIN_MAX_CDB (1200)  /* +12 dB */
#define ES_TONE_BAND_MIN_CDB (-2600) /* -26 dB */
#define ES_TONE_BAND_MAX_CDB (600)   /* +6 dB */

typedef struct {
    int16_t gain_cdb;
    int16_t low_cdb;
    int16_t mid_cdb;
    int16_t high_cdb;
} es_tone_t;

/* 10 bytes, or 0 when `cap` cannot hold the packet. Out-of-range values are clamped, so a
 * caller's bad number becomes the end of the knob rather than a packet the receiver drops. */
size_t es_tone_encode(const es_tone_t *t, uint8_t *out, size_t cap);

/* False unless it is a version-1 tone packet of exactly 10 bytes. The encoder clamps and the
 * parser clamps again, so a hand-built packet cannot exceed the knobs' ranges. */
bool es_tone_parse(const uint8_t *pkt, size_t len, es_tone_t *out);

/* ------------------------------------------------------------- the sender (E4, E6)

 * The K7's side: every frame the encoder emits is kept for two seconds and sent when the
 * receiver's status packet says there is room. A frame the credit does not allow is held, not
 * dropped - the receiver will name it in the next gap and it is resent from here. That is why
 * nothing is lost when the ring is momentarily full: the hole becomes a request.
 *
 * The caller serialises these calls (the broadcast task pushes, the radio's receive callback
 * reports status) and owns the memory the history lives in, so the board can put it in PSRAM. */

#define ES_MAX_FRAME      1441 /* the largest legal MP3 frame, at 320 kbps and 32 kHz */
#define ES_HISTORY_FRAMES 96   /* two and a half seconds of a 38.3-frame-a-second station */

typedef bool (*es_send_fn)(const uint8_t *pkt, size_t len, void *ctx);

typedef struct {
    uint16_t seq;
    uint16_t len;
    bool     used;
} es_hist_ent_t;

typedef struct {
    uint8_t       *arena;      /* ES_HISTORY_FRAMES * ES_MAX_FRAME bytes, the caller's */
    unsigned       slots;
    es_hist_ent_t  ent[ES_HISTORY_FRAMES];
    uint8_t        pktbuf[ES_MAX_FRAGS * ES_MTU];
    uint16_t       seq_next;
    uint32_t       credit;     /* bytes the receiver last said it could take, net of sends */
    bool           have_status;
    bool           tone_capable; /* the player's last status carried ES_STATUS_FLAG_TONE */
    es_send_fn     send;
    void          *ctx;
    uint32_t       packets, sent, resent, dropped, held;
} es_emitter_t;

typedef struct {
    uint32_t packets; /* ESP-NOW packets handed to the radio */
    uint32_t sent;    /* frames sent for the first time */
    uint32_t resent;  /* frames sent again because the receiver named them */
    uint32_t dropped; /* frames the history could not hold, or the radio refused */
    uint32_t held;    /* frames held back for want of credit */
    bool     have_status; /* whether the receiver has ever spoken */
    bool     tone_capable; /* the last status said the player applies the tone (E13) */
} es_emitter_stats_t;

/* The arena is divided into ES_MAX_FRAME-byte slots: at 96 slots that is 138 KB, which is why
 * the board gives it PSRAM. */
void es_emitter_init(es_emitter_t *e, uint8_t *arena, size_t arena_bytes,
                     es_send_fn send, void *ctx);

/* One encoded frame from the broadcast: kept, and sent when the credit allows it. False when the
 * frame is not one this format can carry. */
bool es_emitter_push(es_emitter_t *e, const uint8_t *frame, size_t len);

/* A status packet from the receiver: its credit, and the frames it is missing. Returns how many
 * frames were resent. */
unsigned es_emitter_status(es_emitter_t *e, const uint8_t *pkt, size_t len);

void es_emitter_stats(const es_emitter_t *e, es_emitter_stats_t *out);

#ifdef __cplusplus
}
#endif
