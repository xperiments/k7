#pragma once
/*
 * player_stream - the ESP-NOW receiver, its ring and the status packet it sends back: what
 * arrives from the K7's emitter becomes whole MP3 frames in a ring the playout reads
 * (docs/specs/espnow-stream.md E3-E8, k7player.md R26-R31, ADR 0103).
 *
 * There is no connection here and nothing to reconnect to. A packet arrives in the board's
 * radio callback and is handed to player_stream_packet; a frame completes, goes through the
 * MP3 framer the player already had (which is what reads the rate and the bit rate out of the
 * header), and lands in the ring. A frame that does not arrive is named in the status packet
 * the tick sends every 100 ms, and the receiver holds the frames behind it for a second.
 *
 * Three seconds without a packet is not a fault: the stream is not arriving, the ring drains,
 * and the light says Ready. Two seconds of frames that never decode is a fault: the K7 is
 * sending something this firmware cannot read, and no amount of waiting fixes a mismatch.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player_mp3.h"
#include "player_recv.h"
#include "player_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYER_STREAM_TICK_MS   50
#define PLAYER_STREAM_STATUS_MS 100  /* the status packet's cadence (E3) */
#define PLAYER_STREAM_SILENT_MS 3000 /* no packet for this long: nothing is arriving (E8) */
#define PLAYER_STREAM_FAULT_MS  2000 /* frames that never decode for this long: a mismatch (R26) */

typedef enum {
    PLAYER_STREAM_IDLE = 0, /* no address yet, or nothing arriving */
    PLAYER_STREAM_BODY,     /* packets are arriving: the stream is on the air */
} player_stream_state_t;

typedef struct {
    player_stream_state_t state;
    bool     fault;
    char     host[64];
    uint32_t rate;
    uint32_t bitrate;
    uint32_t bytes;
    uint32_t frames;
    unsigned ring_used;
    unsigned ring_capacity;
    unsigned ring_flushes;
    unsigned ring_dropped_bytes;
    uint32_t lost;
    uint32_t dups;
    uint32_t abandoned;
} player_stream_status_t;

void player_stream_init(void);
void player_stream_start_task(void);
void player_stream_tick(uint32_t now_ms);
void player_stream_status(player_stream_status_t *out);
void player_stream_packet(const uint8_t *pkt, size_t len);
bool player_stream_pop(uint8_t *out, size_t cap, size_t *len);
const char *player_stream_state_name(player_stream_state_t state);

#ifdef __cplusplus
}
#endif
