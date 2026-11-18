#pragma once
/*
 * player_output - the line output: stereo 16-bit frames to whatever the board drives, at the
 * stream's own rate (k7player.md R4, R10-R11, R27).
 *
 * A seam with two implementations: the ESP32's I2S to an external DAC, and the host's, which
 * does nothing and counts. The policy that decides when to open it and what to do when the
 * ring runs dry is player_playout's, and it is host-tested.
 *
 * There is no volume here. The line is full scale and the amplifier owns the level (R11).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open (or re-open) at `hz`. False when the board cannot: this player then has no output. */
bool player_output_open(uint32_t hz);

/* Write `frames` interleaved stereo frames. Blocks until the driver has taken them, or its
 * timeout: that pacing is what keeps the loop in step with the audio clock. */
void player_output_write(const int16_t *stereo, size_t frames);

void player_output_close(void);

#ifdef __cplusplus
}
#endif
