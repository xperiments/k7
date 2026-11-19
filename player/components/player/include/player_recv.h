#pragma once
/*
 * player_recv - the ESP-NOW receiver: fragments in, whole frames out in order, and the status
 * packet that tells the K7 what to send and what to send again
 * (docs/specs/espnow-stream.md E3-E5, ADR 0103).
 *
 * Two jobs, and the second is why the first is not simply a byte stream:
 *
 *   - fragments of one MP3 frame are joined by sequence and delivered whole, in sequence order;
 *   - a frame that does not arrive is *named* in the status packet and given a second to come
 *     back, while the frames behind it wait here. After that second the gap is abandoned and
 *     what was held is delivered, so the music jumps over one frame (26 ms) instead of stopping
 *     for good.
 *
 * The ring, the prime and the playout are not this file's business: a delivered frame goes
 * straight to them, and this module never allocates.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "espnow_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Frames being assembled at once (a resent frame can arrive while the next is in flight) and
 * complete frames waiting behind a gap. Four parts and six held frames is 15 KB of DRAM, which
 * is what the player can spare beside a 96 KB ring. */
#define PLAYER_RECV_PARTS 2
#define PLAYER_RECV_HELD  6

/* How long a gap is given to be filled by a resend before it is abandoned. */
#define PLAYER_RECV_GAP_MS 1000

typedef struct {
    bool     used;
    uint16_t seq;
    uint8_t  nfrags;
    uint8_t  mask;   /* bit per fragment received */
    uint16_t len;    /* of the completed frame, once the last fragment is in */
    uint8_t  data[ES_MAX_FRAME];
} player_recv_part_t;

typedef struct {
    uint16_t seq;
    uint16_t len;
    uint8_t  data[ES_MAX_FRAME];
} player_recv_held_t;

typedef struct {
    player_recv_part_t *part; /* the caller's memory, carved by player_recv_init */
    unsigned part_n;
    player_recv_held_t *held;
    unsigned held_max;
    unsigned held_n;
    bool     have_next;
    uint16_t next;   /* the sequence the stream is waiting for */
    bool     gap;    /* a frame before a held one is missing */
    uint32_t gap_at;
    uint32_t frames, lost, abandoned, dups, parts_dropped;
} player_recv_t;

/* A delivered frame, in order. The pointer is only valid during the call. */
typedef void (*player_recv_frame_fn)(void *ctx, const uint8_t *frame, size_t len);

/* The frame buffers are the caller's: twelve kilobytes is a third of what is left of this
 * chip's DRAM after the ring, so they live on the heap rather than in .bss. False when the
 * storage is too small for one part and one held frame. */
bool player_recv_init(player_recv_t *r, void *storage, size_t bytes);

/* One packet from the radio. Anything that is not an audio fragment is ignored here: the status
 * packets are the player's to send, not to receive. */
void player_recv_packet(player_recv_t *r, const uint8_t *pkt, size_t len, uint32_t now_ms,
                        player_recv_frame_fn on_frame, void *ctx);

/* Abandon a gap that has waited PLAYER_RECV_GAP_MS, delivering what was behind it. Call once a
 * tick: a gap that is never abandoned is a stream that never resumes. */
void player_recv_tick(player_recv_t *r, uint32_t now_ms,
                      player_recv_frame_fn on_frame, void *ctx);

/* The status to send: `free_bytes` is the ring's room, and the rest is what the stream knows
 * about itself - the highest sequence held without a gap, and the sequences it is missing. */
void player_recv_status(player_recv_t *r, uint32_t free_bytes, es_status_t *out);

typedef struct {
    uint32_t frames;       /* delivered in order */
    uint32_t lost;         /* abandoned without arriving */
    uint32_t abandoned;    /* gaps given up on */
    uint32_t dups;         /* fragments or frames behind the stream */
    uint32_t parts_dropped; /* frames that could not be assembled for want of a slot */
} player_recv_stats_t;

void player_recv_stats(const player_recv_t *r, player_recv_stats_t *out);

#ifdef __cplusplus
}
#endif
