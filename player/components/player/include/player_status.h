#pragma once
/*
 * player_status - the five states the status light can show, and the rule that decides
 * which one it shows.
 *
 * The light is the K7Player's whole surface (docs/specs/k7player.md R31-R32), so the meaning
 * of each state lives in one place and not in the LED driver:
 *
 *   Updating   an image is being written (player-update.md U25). It outranks everything: while
 *              the player writes its own flash there is nothing else true to say, and a stale
 *              fault must not paint over it.
 *   Fault      something the network is not: a body that is not MPEG audio, a speaker path
 *              that will not initialise. It clears only when the thing that faulted is gone.
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
    PLAYER_LIGHT_UPDATING,
} player_light_t;

/* What the rest of the firmware knows. Each field is set by the module that owns it; none
 * of them is set by the light. */
typedef struct {
    bool network; /* a network it can use */
    bool stream;  /* a stream is established (connected, and bytes are arriving) */
    bool audio;   /* audio is actually going out of an output */
    bool fault;   /* something it cannot do its job through, for a reason the network is not */
    bool updating; /* an image is being received or verified */
} player_status_t;

void player_status_init(player_status_t *s);
void player_status_set_network(player_status_t *s, bool usable);
void player_status_set_stream(player_status_t *s, bool established);
void player_status_set_audio(player_status_t *s, bool going_out);
void player_status_set_fault(player_status_t *s, bool faulted);
void player_status_set_updating(player_status_t *s, bool updating);

/* The state the light shows: the fields folded into one, with Updating first, then Fault, and
 * Playing requiring both of the two conditions (R31, R38). */
player_light_t player_status_light(const player_status_t *s);

/* The state's name, for a log line: "searching", "ready", "playing", "fault". */
const char *player_light_name(player_light_t light);

#ifdef __cplusplus
}
#endif
