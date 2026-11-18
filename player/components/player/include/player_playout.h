#pragma once
/*
 * player_playout - the ring's frames, decoded and sent to the output.
 *
 * It owns the last piece of R28's ring: the output task takes the oldest whole frame, decodes
 * it (one MPEG frame at a time, dr_mp3's frame decoder), and writes the PCM. When the ring is
 * empty it writes silence rather than blocking the socket or stopping the output - the DAC
 * keeps clocking and the gap is a gap, not a stall.
 *
 * The rate is the stream's, from the first frame header (R27): the output is opened at 44.1 kHz
 * because the K7 said so in the frame, and reopened if the station's rate ever changes.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ten milliseconds of silence at 44.1 kHz: the unit the output writes when it has nothing. */
#define PLAYER_PLAYOUT_SILENCE_FRAMES 441

/* The buffer the output waits for before it plays: 4.5 s of the station's audio at connect,
 * measured in the ring rather than in frames, and capped by the ring itself so a station whose
 * rate makes that more than the ring can hold primes the ring full instead of waiting forever.
 *
 * This is the number that decides how long an outage can be and go unheard: the ring is only ever
 * as deep as the prime, because at a steady state the K7 feeds at exactly the rate the output
 * plays. Measured the hard way - primed to 1.5 s, the ring sat at 38 KB of its 96 and a
 * three-second outage already ran it dry (ADR 0102). It costs 4.5 s of silence before the first
 * sound, which is the trade and the reason to keep the connect burst (R25) short.
 *
 * After the ring has run dry the wait is much shorter: the listener has just sat through the
 * outage, and what follows is the backlog the K7 held. */
#define PLAYER_PLAYOUT_PRIME_MS  4500
#define PLAYER_PLAYOUT_RESUME_MS 1000

/* How many frames one tick takes off the ring. A tick is already up to 400 ms of blocking
 * write, and the ring is two seconds deep, so an unbounded drain would hold this task - the
 * highest in the player - for as long as the backlog lasts. */
#define PLAYER_PLAYOUT_MAX_FRAMES 16

typedef struct {
    bool     open;
    uint32_t rate;      /* the rate the output is open at */
    uint32_t decoded;   /* MPEG frames decoded */
    uint32_t refused;   /* MPEG frames the decoder would not take */
    uint32_t frames;    /* PCM frames of audio written */
    uint32_t silent;    /* PCM frames of silence written */
    uint32_t underruns; /* times the ring was empty while a stream was running */
    uint32_t primes;    /* times the output waited for the ring to hold the buffer */
    bool     audio_now; /* the last tick wrote decoded audio, not silence */
    bool     failed;    /* the output refused to open; the light calls that a fault (R31) */
} player_playout_status_t;

void player_playout_init(void);

/* The task that drives player_playout_tick(). Called once by main. */
void player_playout_start_task(void);

/* One step: open or reopen the output for the stream's rate, then write what the ring has, or
 * silence. Returns the PCM frames written (0 when the output is not open). */
int player_playout_tick(void);

void player_playout_status(player_playout_status_t *out);

#ifdef __cplusplus
}
#endif
