#pragma once
/*
 * player_ring - the audio hand-off between the socket and the output, fixed and never
 * growing (docs/specs/k7player.md R28).
 *
 * It holds whole MP3 frames back to back, each behind a two-byte length, so a frame comes out
 * as it went in and a half frame is never decoded. The depth is the jitter budget, and it is
 * the board's number: the link this player runs on (2.4 GHz, classic ESP32) is measured losing
 * the stream for one to nine seconds at a time (T3, ADR 0102), so the ring holds six seconds of
 * the station's audio - the outages up to that are absorbed and the listener hears nothing at
 * all, which is why the station's bit rate is a link decision and not only a quality one.
 *
 * An outage longer than that costs one discontinuity, not a stream of them: a push that does
 * not fit clears the ring whole and keeps the newest audio. The old policy dropped the oldest
 * frame per push, which on a recovering link turned a nine-second backlog into a hundred small
 * jumps in the music instead of one.
 *
 * The socket never blocks on the ring - a push to a full ring succeeds - so a slow output
 * cannot stall the network read.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest Layer III frame at any MPEG-1 rate: 144 * 320 000 / 8 000 = 5 760, but the
 * station format is 44.1 kHz or 48 kHz, where the largest is 1 440 (320 kbps at 32 kHz is
 * 1 440 too). 1 441 covers every legal MPEG-1/2/2.5 frame with padding. */
#define PLAYER_RING_SLOT 1441

/* 96 KB, and the seconds depend on the station: six at its 128 kbps (16 000 B/s), three at the
 * 256 kbps it ran before that. It is a byte budget because the format is the appliance's to
 * change and the ring's job is not to care. The next power of two in this chip's DRAM is 128 KB,
 * which leaves the Wi-Fi too little heap; 96 KB leaves it 85 KB on the board's census (B5). */
#define PLAYER_RING_BYTES (96u * 1024u)

typedef struct {
    uint8_t  data[PLAYER_RING_BYTES];
    unsigned head;          /* where a push writes next */
    unsigned tail;          /* where a pop reads next */
    unsigned used;          /* bytes of frames and lengths in the ring */
    unsigned flushes;       /* times an overrun cleared the ring */
    unsigned dropped_bytes; /* audio discarded by those flushes */
} player_ring_t;

void player_ring_init(player_ring_t *r);

/* Append a whole frame. False only when the frame is bigger than PLAYER_RING_SLOT (a caller
 * bug): a full ring clears itself once, and the push succeeds. */
bool player_ring_push(player_ring_t *r, const uint8_t *frame, size_t len);

/* Copy the oldest frame into `out`, or false when the ring is empty. A copy, not a pointer
 * into the ring: the ring may wrap around it on the next push. */
bool player_ring_pop(player_ring_t *r, uint8_t *out, size_t cap, size_t *len);

unsigned player_ring_used(const player_ring_t *r);          /* bytes */
unsigned player_ring_capacity(void);                        /* bytes, never grows */
unsigned player_ring_flushes(const player_ring_t *r);
unsigned player_ring_dropped_bytes(const player_ring_t *r);

#ifdef __cplusplus
}
#endif
