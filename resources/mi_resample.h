#pragma once
#include <stddef.h>
#include <stdint.h>

/* Streaming windowed-sinc resampler, stereo int16 -> stereo int16.
 * Low-pass cutoff follows min(in, out) so downsampling does not alias. */
typedef struct mi_rs mi_rs_t;
typedef void (*mi_rs_emit_t)(const int16_t *stereo, size_t frames, void *ctx);

mi_rs_t *mi_rs_new(uint32_t in_hz, uint32_t out_hz); /* in_hz == out_hz -> pass-through */
void     mi_rs_process(mi_rs_t *r, const int16_t *stereo_in, size_t frames, mi_rs_emit_t emit, void *ctx);
void     mi_rs_free(mi_rs_t *r);
