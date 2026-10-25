#pragma once
/*
 * tone_eq - the broadcast's three-band DJ EQ and its trim, on the PCM chunk the encoder
 * is about to take.
 *
 * One low shelf, one mid bell and one high shelf, plus a gain applied before them. The
 * ranges are the ones the screen's knobs use: bands -26..+6 dB (a DJ cut is deeper than
 * the boost) and a trim of -12..+12 dB. The default trim is 0 dB, the same neutral as the
 * bands; the owner lowers it when a hot source needs headroom.
 *
 * Targets are smoothed per chunk (2 dB), so a knob moved quickly glides instead of
 * stepping the coefficients. The biquad state is never reset on a change: the filters are
 * minimum phase and their state is continuous, which is what keeps a moved knob from
 * clicking. The output is clamped to int16; a hot trim plus a boosted band can reach the
 * clamp, and that is the caller's setting, not a bug.
 *
 * `tone_eq_process` is called on the broadcast task and owns the slew and the coefficient
 * recompute; the setters only record a target and are safe to call between chunks. No
 * allocation, no locks, no ESP-IDF dependency: the module compiles on the host.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TONE_EQ_LOW = 0,
    TONE_EQ_MID = 1,
    TONE_EQ_HIGH = 2,
    TONE_EQ_BANDS = 3,
};

#define TONE_EQ_BAND_MIN_DB  (-26.0f)
#define TONE_EQ_BAND_MAX_DB  (6.0f)
#define TONE_EQ_GAIN_MIN_DB  (-12.0f)
#define TONE_EQ_GAIN_MAX_DB  (12.0f)
#define TONE_EQ_GAIN_DEFAULT (0.0f)

/* One biquad: transposed direct form II, one state pair per channel. */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1[2], z2[2];
} tone_eq_sec_t;

typedef struct {
    uint32_t rate;
    float    band_db[TONE_EQ_BANDS];      /* smoothed, what the coefficients use */
    float    band_target[TONE_EQ_BANDS];
    float    coeff_db[TONE_EQ_BANDS];     /* the value the coefficients were built at */
    float    gain_db;                     /* smoothed trim, dB */
    float    gain_target;
    float    gain_lin;                    /* 10^(gain_db/20), applied before the bands */
    tone_eq_sec_t sec[TONE_EQ_BANDS];
} tone_eq_t;

/* Set the sample rate and the neutral state: bands and trim at 0 dB. */
void tone_eq_init(tone_eq_t *eq, uint32_t rate);

/* Record a target. Out-of-range values are clamped; the change is applied by the next
 * process() call, at its slew rate. `band` is TONE_EQ_LOW/MID/HIGH. */
void tone_eq_set_band(tone_eq_t *eq, int band, float db);
void tone_eq_set_gain(tone_eq_t *eq, float db);

/* Process interleaved stereo int16 in place. */
void tone_eq_process(tone_eq_t *eq, int16_t *pcm, size_t frames);

#ifdef __cplusplus
}
#endif
