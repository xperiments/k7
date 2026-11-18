#pragma once
/*
 * player_status - the four states the status light can show, and the rule that decides
 * which one it shows.
 *
 * The light is the K7Player's whole surface (docs/specs/k7player.md R31-R32), so the meaning
 * of each state lives in one place and not in the LED driver:
 *
 *   Fault      something the network is not: a body that is not MPEG audio, a speaker path
 *              that will not initialise. It outranks everything and clears only when the
 *              thing that faulted is gone.
 *   Playing    the stream is established AND audio is going out of an output. A bonded
 *              speaker that is absent is therefore Ready, not Playing (R14).
 *   Ready      on a network, nothing playing yet: connecting, retrying, or nothing to play to.
 *   Searching  no network it can use.
 *
 * This is a value with a pure derivation - no task, no clock, no I/O - which is why it is
 * the first module the player's host harness runs.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_LIGHT_SEARCHING = 0,
    PLAYER_LIGHT_READY,
    PLAYER_LIGHT_PLAYING,
    PLAYER_LIGHT_FAULT,
} player_light_t;

/* What the rest of the firmware knows. Each field is set by the module that owns it; none
 * of them is set by the light. */
typedef struct {
    bool network; /* a network it can use */
    bool stream;  /* a stream is established (connected, and bytes are arriving) */
    bool audio;   /* audio is actually going out of an output */
    bool fault;   /* something it cannot do its job through, for a reason the network is not */
} player_status_t;

void player_status_init(player_status_t *s);
void player_status_set_network(player_status_t *s, bool usable);
void player_status_set_stream(player_status_t *s, bool established);
void player_status_set_audio(player_status_t *s, bool going_out);
void player_status_set_fault(player_status_t *s, bool faulted);

/* The state the light shows: the four fields folded into one, with Fault first and Playing
 * requiring both of the two conditions (R31). */
player_light_t player_status_light(const player_status_t *s);

/* The state's name, for a log line: "searching", "ready", "playing", "fault". */
const char *player_light_name(player_light_t light);

#ifdef __cplusplus
}
#endif
