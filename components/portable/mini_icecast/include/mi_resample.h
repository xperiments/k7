#pragma once
#include <stddef.h>
#include <stdint.h>

/* Streaming windowed-sinc resampler, stereo int16 -> stereo int16.
 * Low-pass cutoff follows min(in, out) so downsampling does not alias. */
typedef struct mi_rs mi_rs_t;
typedef void (*mi_rs_emit_t)(const int16_t *stereo, size_t frames, void *ctx);

/* Build a resampler. `in_hz == out_hz` gives a pass-through.
 *
 * Returns NULL on failure. Refusal is a configuration error, not a crash: an
 * extreme downsample needs more kernel taps than the implementation can hold, so
 * it is refused with a diagnostic naming the rates rather than overrunning the
 * kernel row. `err`, when non-NULL, receives that diagnostic. */
mi_rs_t *mi_rs_new(uint32_t in_hz, uint32_t out_hz, char *err, size_t err_size);
void     mi_rs_process(mi_rs_t *r, const int16_t *stereo_in, size_t frames, mi_rs_emit_t emit, void *ctx);
void     mi_rs_free(mi_rs_t *r);
