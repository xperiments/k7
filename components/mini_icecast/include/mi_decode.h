#pragma once
#include <stddef.h>
#include <stdint.h>

/* One decoder API for .mp3 / .flac / .wav. Always yields stereo int16
 * (mono is duplicated, >2 channels keep the first two) at the file's own rate. */
#include <stdbool.h>

#define MI_READ_FRAMES 1024

typedef struct mi_dec mi_dec_t;

/* The source formats this decoder can open. Decided by extension, case
 * insensitively - this function is the single authority on that list, so the
 * broadcast's library scan, the download surface and the decoder itself cannot
 * drift apart. It is only a name test: a leading dot (macOS AppleDouble files)
 * and everything else about a name is the caller's business. */
typedef enum {
    MI_FMT_NONE = -1,
    MI_FMT_MP3 = 0,
    MI_FMT_FLAC,
    MI_FMT_WAV,
} mi_fmt_t;

mi_fmt_t mi_dec_format_of(const char *name);

mi_dec_t *mi_dec_open(const char *path); /* NULL if unsupported extension / unreadable / corrupt */
uint32_t  mi_dec_hz(const mi_dec_t *d);
size_t    mi_dec_read(mi_dec_t *d, int16_t *stereo, size_t max_frames); /* <= MI_READ_FRAMES; 0 = end */
void      mi_dec_close(mi_dec_t *d);
