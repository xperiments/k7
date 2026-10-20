#pragma once
#include <stddef.h>
#include <stdint.h>

/* One decoder API for .mp3 / .flac / .wav. Always yields stereo int16
 * (mono is duplicated, >2 channels keep the first two) at the file's own rate. */
#include <stdbool.h>
#include "audio_format.h" /* the source-format authority, shared with the file surface */

#define MI_READ_FRAMES 1024

typedef struct mi_dec mi_dec_t;

mi_dec_t *mi_dec_open(const char *path); /* NULL if unsupported extension / unreadable / corrupt */
uint32_t  mi_dec_hz(const mi_dec_t *d);

/* The track's length, or 0 when the decoder cannot know it (a truncated file, a
 * stream that never announced its length). For MP3 the figure includes the encoder
 * delay and padding the format carries, so it runs a few tens of milliseconds long -
 * the honest answer, since that is how much audio the file holds. */
uint32_t  mi_dec_duration_ms(const mi_dec_t *d);
size_t    mi_dec_read(mi_dec_t *d, int16_t *stereo, size_t max_frames); /* <= MI_READ_FRAMES; 0 = end */
void      mi_dec_close(mi_dec_t *d);
