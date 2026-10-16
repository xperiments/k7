#pragma once
#include <stddef.h>
#include <stdint.h>

/* One decoder API for .mp3 / .flac / .wav. Always yields stereo int16
 * (mono is duplicated, >2 channels keep the first two) at the file's own rate. */
#define MI_READ_FRAMES 1024

typedef struct mi_dec mi_dec_t;

mi_dec_t *mi_dec_open(const char *path); /* NULL if unsupported extension / unreadable / corrupt */
uint32_t  mi_dec_hz(const mi_dec_t *d);
size_t    mi_dec_read(mi_dec_t *d, int16_t *stereo, size_t max_frames); /* <= MI_READ_FRAMES; 0 = end */
void      mi_dec_close(mi_dec_t *d);
