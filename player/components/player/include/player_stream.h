#pragma once
/*
 * player_stream - the live stream, from the socket to whole frames in a ring.
 *
 * It is a Client of the K7's broadcast and nothing else (k7player.md R23-R30):
 *
 *   - `GET /stream` against the K7's own answer: HTTP/1.0, `Connection: close`, no length;
 *   - whole MP3 frames out of a body whose chunk boundaries are arbitrary (player_mp3), into
 *     a ring that holds two seconds and clears itself rather than growing (player_ring);
 *   - ten seconds without a byte is a dead connection, and the reconnect is 1 s then 5 s;
 *   - a body that is not MPEG audio is a **Fault**, not a retry loop: something is wrong that
 *     retrying will not fix.
 *
 * Tick-driven with the board's socket behind a non-blocking seam, so the host test owns both
 * the clock and the bytes.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player_mp3.h"
#include "player_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYER_STREAM_TICK_MS 50

/* No bytes for this long and the connection is dropped (R29). */
#define PLAYER_STREAM_STALL_MS 10000

/* And no *frame* for this long, which is the rule that catches a connection still open and
 * still dribbling a few hundred bytes a second: a connection that produces no frame for ten
 * seconds is not slow, it is wedged - and a wedged client can stall the origin's broadcast for
 * everyone (found on the boards, 2026-11-18).
 *
 * It was three seconds, and that was the wrong number: a Wi-Fi stall of four or five seconds
 * made the player hang up on a stream that was about to resume, so a brief network hiccup became
 * a reconnect and a jump in the music. Ten matches the byte rule, and the K7's client ring holds
 * ten seconds of audio, so a stall of that length now arrives as silence and then carries on in
 * sync instead of restarting the connection. */
#define PLAYER_STREAM_FRAME_STALL_MS 10000

/* The reconnect schedule (R30). */
#define PLAYER_STREAM_BACKOFF_FIRST_MS 1000
#define PLAYER_STREAM_BACKOFF_MS       5000

/* Bytes of body to look at before deciding it is not MPEG audio (R26). */
#define PLAYER_STREAM_NOT_MP3_BYTES 4096

typedef enum {
    PLAYER_STREAM_IDLE = 0, /* no K7's address yet */
    PLAYER_STREAM_CONNECTING,
    PLAYER_STREAM_HEAD,
    PLAYER_STREAM_BODY,
    PLAYER_STREAM_BACKOFF,
    PLAYER_STREAM_FAULT, /* a body that is not MPEG audio; retrying will not fix it */
} player_stream_state_t;

typedef struct {
    player_stream_state_t state;
    bool     fault;
    char     host[64];
    uint32_t rate;    /* Hz, from the first frame */
    uint32_t bitrate; /* kbps */
    uint32_t bytes;   /* body bytes taken from the socket */
    uint32_t frames;  /* frames put in the ring */
    unsigned ring_used;          /* bytes held */
    unsigned ring_capacity;      /* bytes it can hold, fixed */
    unsigned ring_flushes;       /* overruns that cleared it */
    unsigned ring_dropped_bytes; /* audio discarded by those overruns */
    uint32_t retries;
} player_stream_status_t;

void player_stream_init(void);

/* The task that drives player_stream_tick(). Called once by main. */
void player_stream_start_task(void);

/* The machine's clock: one connect, one read, or one retry per tick. */
void player_stream_tick(uint32_t now_ms);

void player_stream_status(player_stream_status_t *out);

/* Take the oldest whole frame, copied into the caller's buffer (PL-06's output reads this).
 * False when the ring is empty: the output repeats silence rather than blocking. A copy
 * rather than a pointer into the ring, so a frame cannot be overwritten while it is read. */
bool player_stream_pop(uint8_t *out, size_t cap, size_t *len);

const char *player_stream_state_name(player_stream_state_t state);

#ifdef __cplusplus
}
#endif
