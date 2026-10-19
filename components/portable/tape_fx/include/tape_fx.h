// tape_fx — cassette-style tape effect for ESP32-S3 (works on any C99 target).
// Own implementation of standard DSP blocks (saturation, EQ, wow/flutter, hiss).
// Control surface mirrors a classic cassette plugin: tape type, dolby, and
// tape / head / motor quality. All quality values: 1.0 = pristine, 0.0 = worn.
//
// Runs on the station's PCM before the encoder (see mini_icecast.c's emit_chunk),
// in place, on interleaved int16. Measured cost at 44.1 kHz stereo on this board:
// 9.0 % of one 240 MHz core, against 52.9 % for the encoder. The measurements,
// and every change made to the original draft, are in
// docs/research/tape-fx-output-stream.md.
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Power of two. The read pointer reaches center + wow + flutter + drift samples
 * behind the write head: 285 at 44.1 kHz and 311 at 48 kHz in the worst case
 * (motor_q = 0), so 512 covers every rate the station accepts and the delay line
 * is 4 KB per instance instead of 8. */
#define TAPE_FX_DLY_LEN 512

/* Defaults. head_q moves the high-frequency corner: fc = lp_base * (0.3 + 0.7 *
 * head_q), with lp_base 9/12/15 kHz for NORMAL/CHROME/METAL. The draft's 0.6
 * put NORMAL's corner at 6.5 kHz, which measured -10.9 dB at 10 kHz and -25 dB
 * at 15 kHz - a worn head, not a cassette in good condition. At 0.85 the same
 * type measures -6.8 dB at 10 kHz and -20.8 dB at 15 kHz, still tape. Measured response per type, and
 * why the attack on the saturator's aliasing is not worth its cost, are in
 * docs/research/tape-fx-output-stream.md §8. */
#define TAPE_FX_DEFAULT_TAPE_Q  0.6f
#define TAPE_FX_DEFAULT_HEAD_Q  0.85f
#define TAPE_FX_DEFAULT_MOTOR_Q 0.6f

typedef enum { TAPE_NORMAL = 0, TAPE_CHROME = 1, TAPE_METAL = 2 } tape_type_t;
typedef enum { DOLBY_OFF = 0, DOLBY_B = 1, DOLBY_C = 2, DOLBY_K = 3 } tape_dolby_t; // K = noise-free

typedef struct { float b0, b1, b2, a1, a2; } tape_bq_coef_t;
typedef struct { float z1, z2; } tape_bq_state_t;

typedef struct {
    float sr;
    tape_type_t type;
    tape_dolby_t dolby;
    float tape_q, head_q, motor_q;

    // derived
    float drive, inv_drive, noise_amp;
    float wow_amp, flt_amp, drift_amp;   // delay-modulation amplitudes in samples
    float center;                        // base delay in samples
    tape_bq_coef_t hp, shelf, lp;

    // state
    tape_bq_state_t st[2][3];
    float dly[2][TAPE_FX_DLY_LEN];
    uint32_t w;
    float ph_wow, ph_flt, drift, drift_tgt;
    uint32_t drift_cnt;
    float nlp[2];
    uint32_t rng;
} tape_fx_t;

void tape_fx_init(tape_fx_t *t, float sample_rate);
void tape_fx_set_samplerate(tape_fx_t *t, float sample_rate);
void tape_fx_set_tape_type(tape_fx_t *t, tape_type_t type);
void tape_fx_set_dolby(tape_fx_t *t, tape_dolby_t dolby);
void tape_fx_set_tape_quality(tape_fx_t *t, float q);   // saturation + hiss
void tape_fx_set_head_quality(tape_fx_t *t, float q);   // high-frequency loss
void tape_fx_set_motor_quality(tape_fx_t *t, float q);  // wow / flutter / drift

// In-place, interleaved int16. channels = 1 or 2. Keeps its state across calls,
// so a chunk boundary is inaudible and a track change keeps the tape rolling.
void tape_fx_process_s16(tape_fx_t *t, int16_t *buf, size_t frames, int channels);

#ifdef __cplusplus
}
#endif
