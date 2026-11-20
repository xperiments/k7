#pragma once
/*
 * player_tone - the K7's four EQ values, applied to the decoded PCM before the output.
 *
 * The K7's tone packets arrive on the radio (espnow-stream.md E12) and are targets, not
 * audio: this module keeps the latest values, builds the filter for the stream's rate, and
 * runs the same tone_eq the broadcast uses. It starts neutral, so a K7 that has not sent a
 * tone - an old K7, or the HTTP transport - is heard as it was encoded, not tinted twice.
 *
 * The packet callback runs on the radio's task and only parses and copies the four values.
 * The filter state belongs to the playout task: `process()` is called after the decode and
 * before the output, and a rate change rebuilds the filters without losing the targets
 * (k7player.md R33). No allocation after init and no locks around the audio.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the targets' lock and the neutral filter. Call once, before the tasks. */
void player_tone_init(void);

/* Register the radio's 0x06 dispatch. Call before player_stream_start_task() starts the
 * radio; the callback itself is player_tone_packet(). */
void player_tone_start(void);

/* One tone packet from the K7. A packet that is not version 1, type 0x06 and 10 bytes is
 * ignored; a legal one replaces the four targets. Runs on the radio's task: short. */
void player_tone_packet(const uint8_t *pkt, size_t len);

/* The output's rate, which is the stream's (R27). Rebuilds the biquads for it and keeps the
 * targets. Called from the playout task when the line is opened or reopened. */
void player_tone_set_rate(uint32_t rate);

/* Apply the latest targets to interleaved stereo int16, in place, with the tone's own slew.
 * Neutral (no packet, or all zero) is a bypass; the caller writes the PCM to the output. */
void player_tone_process(int16_t *pcm, size_t frames);

#ifdef __cplusplus
}
#endif
