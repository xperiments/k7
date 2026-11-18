#pragma once
/*
 * player_mp3 - find whole MPEG Layer III frames in a byte stream whose chunk boundaries are
 * arbitrary (k7player.md R24, R26, T1).
 *
 * The stream is close-delimited and arrives in whatever sizes the socket hands over, so a
 * frame can straddle one read, three reads, or a packet boundary. This keeps a carry of up to
 * two frames, finds each header, computes the frame's own length and emits it whole - which
 * is what the ring stores and the decoder will read.
 *
 * The first frame also answers R27's question: the stream's rate and bit rate, read from the
 * frame header rather than assumed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Two frames of carry: enough to hold a frame split across reads and the header of the next. */
#define PLAYER_MP3_CARRY (2 * 1441)

typedef struct {
    uint32_t rate;     /* Hz, from the first frame */
    uint32_t bitrate;  /* kbps */
    uint8_t  channels; /* 1 or 2 */
} player_mp3_info_t;

typedef void (*player_mp3_frame_fn)(void *ctx, const uint8_t *frame, size_t len);

typedef struct {
    uint8_t buf[PLAYER_MP3_CARRY];
    size_t len;        /* bytes held in buf */
    unsigned skipped;  /* bytes dropped before a header was found */
    unsigned frames;   /* frames emitted since init */
    player_mp3_info_t info;
    bool info_set;
} player_mp3_t;

void player_mp3_init(player_mp3_t *p);

/* Feed bytes; emit every whole frame the new bytes complete. Returns the number emitted. */
size_t player_mp3_feed(player_mp3_t *p, const uint8_t *data, size_t len,
                       player_mp3_frame_fn on_frame, void *ctx);

/* True once a frame has been emitted: before that, a stream is not known to be MPEG audio. */
bool player_mp3_streaming(const player_mp3_t *p);

/* The bytes skipped before the first header, for a log line when a body is not what it said. */
unsigned player_mp3_skipped(const player_mp3_t *p);

#ifdef __cplusplus
}
#endif
