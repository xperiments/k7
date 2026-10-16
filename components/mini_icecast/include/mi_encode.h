#pragma once
/*
 * mi_encode - the station's output stage: PCM in, whole MP3 frames out.
 *
 * An encoder frame is 1152 samples per channel at MPEG-I rates and 576 at
 * MPEG-2 / MPEG-2.5 rates, and neither divides the server's 1024-sample chunk.
 * This module owns that mismatch: it accumulates PCM until it holds a whole
 * encoder frame before calling the encoder, and it re-frames the encoder's byte
 * output so that every out() call carries exactly one complete MP3 frame -
 * never a partial one, never a byte shared between two calls.
 *
 * Why re-framing is needed at all: shine's bit writer emits whole 32-bit words
 * and carries the leftover bits of each frame into the next call, so its
 * returned byte counts are word-aligned, not frame-aligned (measured: only 312
 * of 1000 calls began with a frame sync). The bytes themselves are a correct,
 * frame-continuous MP3 stream; only the call boundaries are arbitrary. This
 * module walks the frame headers and hands on whole frames.
 *
 * The end-of-track behaviour is the other trap this module exists to close:
 * shine_flush() does not flush (it reports 0 bytes pending, always), so a
 * partial block is completed with silence here instead of being dropped or
 * handed to the encoder as a short block.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHINE_MAX_SAMPLES: the largest encoder frame, at MPEG-I rates. */
#define MI_ENC_MAX_FRAME_SAMPLES 1152
/* MPEG-1 Layer III at 320 kbps and 32 kHz: the largest frame the format allows. */
#define MI_ENC_MAX_FRAME_BYTES 1441

typedef struct mi_enc mi_enc_t;

/* Emits one complete encoded frame. */
typedef void (*mi_enc_out_t)(const uint8_t *frame, size_t bytes, void *ctx);

/* Validate a station format without allocating anything.
 *
 * Returns the MPEG version, where 0 means MPEG-2.5 and is VALID - the encoder
 * reports its MPEG tier rather than a boolean, so callers must test `>= 0` and
 * never truthiness. Returns -1 on refusal, with a reason in `err`. */
int mi_enc_check_format(uint32_t out_hz, uint32_t bitrate, char *err, size_t err_size);

/* "MPEG-1" / "MPEG-2" / "MPEG-2.5", or "invalid" for a negative version. */
const char *mi_enc_mpeg_name(int mpeg_version);

/* NULL on refusal or out of memory, with the reason in `err`. */
mi_enc_t *mi_enc_new(uint32_t out_hz, uint32_t bitrate, char *err, size_t err_size);
void      mi_enc_free(mi_enc_t *e);

/* Samples per channel in one encoder frame: 1152 or, below 32 kHz, 576. */
size_t mi_enc_samples_per_frame(const mi_enc_t *e);

/* Bytes the encoder's own state occupies, reported at startup so the memory
 * budget is observable rather than arithmetic. Pinned by the host test. */
size_t mi_enc_state_bytes(void);

/* True when this stage's own buffers landed in external memory rather than
 * falling back to internal RAM (requirement P3's diagnostic). */
bool mi_enc_uses_external_memory(const mi_enc_t *e);

/* Feed interleaved stereo frames. Whole frames come out via `out`. */
void mi_enc_feed(mi_enc_t *e, const int16_t *stereo, size_t frames,
                 mi_enc_out_t out, void *ctx);

/* End of track. Encodes the residual block padded with silence (at most one
 * encoder frame) and emits every frame that completes, then calls the encoder's
 * own flush entry point. Nothing is dropped and nothing is encoded twice. */
void mi_enc_flush(mi_enc_t *e, mi_enc_out_t out, void *ctx);

/* Frames held in the accumulator, waiting for a whole encoder frame. Non-zero
 * only mid-block: the encoder is never handed a short block. */
size_t mi_enc_buffered_frames(const mi_enc_t *e);

/* Bytes decoded from the encoder but not yet emitted, because they do not yet
 * complete a frame. shine's bit writer emits whole 32-bit words and carries a
 * frame's last few bits into the next call, so up to MI_ENC_MAX_FRAME_BYTES-1
 * bytes can be waiting at a track boundary. They are emitted as soon as the
 * next block is encoded: deferred, never dropped. */
size_t mi_enc_pending_bytes(const mi_enc_t *e);

/* True when this format's frame headers overstate the frame length by one byte
 * and the output is being corrected (a rounding fault in the vendored encoder,
 * measured at initialisation rather than assumed). */
bool mi_enc_corrects_padding(const mi_enc_t *e);

/* Telemetry (ticket 06). */
uint64_t mi_enc_frames_encoded(const mi_enc_t *e); /* blocks handed to the encoder */
uint64_t mi_enc_encode_us(const mi_enc_t *e);      /* time spent inside the encoder */
size_t   mi_enc_desync_bytes(const mi_enc_t *e);   /* bytes skipped resynchronising; 0 in practice */

#ifdef __cplusplus
}
#endif
