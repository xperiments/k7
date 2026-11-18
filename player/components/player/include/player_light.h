#pragma once
/*
 * player_light - the K7Player's whole surface (k7player.md R31-R32).
 *
 * One LED, four states, and the patterns the specification names:
 *
 *   Searching   no network it can use        1 Hz blink
 *   Ready       on a network, nothing playing steady, dim
 *   Playing     a stream is established and audio is going out   steady, bright
 *   Fault       something the network is not  4 Hz blink
 *
 * It reads the three machines' status - the network's, the stream's and the output's - and
 * folds them with player_status's rule, so the light is a reading of what the firmware is
 * doing and never a state of its own. A missing speaker is Ready, not a fault (R14); a body
 * that is not audio, or an output that will not open, is Fault (R31).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The tick's period: fine enough for the 4 Hz pattern's 125 ms halves, and nothing more. */
#define PLAYER_LIGHT_TICK_MS 50

/* What the board is told. 0 is off; the middle value is "dim"; the top is "bright". */
#define PLAYER_LIGHT_DIM   32
#define PLAYER_LIGHT_BRIGHT 255

void player_light_init(void);

/* The task that drives player_light_tick(). Called once by main. */
void player_light_start_task(void);

/* Fold the machines' state into the light and drive the board. Returns the state it showed. */
player_light_t player_light_tick(uint32_t now_ms);

/* What the last tick decided, for the log and the host test. */
player_light_t player_light_state(void);
uint8_t player_light_level(void);

#ifdef __cplusplus
}
#endif
