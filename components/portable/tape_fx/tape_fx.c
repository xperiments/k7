/*
 * tape_fx - the cassette character, applied to the station's PCM before the
 * encoder. Own implementation of standard blocks: a saturator, three biquads per
 * channel (rumble cut, head bump, HF loss), a modulated delay for wow/flutter,
 * and high-passed hiss.
 *
 * Three things differ from the original draft, each for a measured reason (see
 * docs/research/tape-fx-output-stream.md, §6.4 and §9):
 *
 *   1. fast_tanh() is a polynomial, not a rational. On this toolchain `x / y` is
 *      a call to __divsf3 - a 23-instruction hardware-FP chain - and the effect
 *      evaluated it four times per frame. The polynomial is fitted to the same
 *      curve (-92 dB) so the sound does not move.
 *   2. the two LFOs read a table instead of calling sinf() twice per frame.
 *   3. the make-up gain is 1/drive rather than 1/sqrt(drive): the stage used to
 *      add up to +4.8 dB depending on the knobs, which would make switching the
 *      effect on a volume jump rather than a change of character.
 *
 * Cost at 44.1 kHz stereo on the ESP32-S3: 408 instructions per frame and no
 * library calls, about 9 % of one 240 MHz core (the encoder next to it is 52.9 %).
 */
#include "tape_fx.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The two LFOs move at 0.9 Hz and 8 Hz, so 1/256 of a cycle with linear
 * interpolation is -83 dBFS of error in the delay modulation (measured with
 * `make compare`), far below the hiss the effect adds. Held in RAM rather than
 * flash because it is read twice per frame and a DRAM read has no cache to miss. */
#define TAPE_SIN_BITS 8
#define TAPE_SIN_SIZE (1 << TAPE_SIN_BITS)
static float tape_sin_tab[TAPE_SIN_SIZE + 1];

static void tape_sin_init(void) {
    for (int i = 0; i <= TAPE_SIN_SIZE; i++)
        tape_sin_tab[i] = sinf(2.f * (float)M_PI * (float)i / (float)TAPE_SIN_SIZE);
}

/* Phase in [0, 2*pi]. The advance wraps with `if (ph > 2*pi)`, which keeps the
 * phase from exceeding 2*pi but not from reaching it: at exactly 2*pi this lands
 * on f = 256. The fraction is then zero, which is the right answer, but the
 * index is one past the end of the table - so the index is masked *after* the
 * fraction is taken. Masking it before makes the fraction 256 and extrapolates
 * the table into nonsense (sin(2*pi) = 6.28). */
static inline float tape_sin(float ph) {
    float f = ph * ((float)TAPE_SIN_SIZE / (2.f * (float)M_PI));
    int i = (int)f;
    float fr = f - (float)i;
    i &= TAPE_SIN_SIZE - 1;
    return tape_sin_tab[i] + fr * (tape_sin_tab[i + 1] - tape_sin_tab[i]);
}

/* x*(27+x^2)/(27+9x^2) - the original saturator - without the division.
 *
 * A degree-13 odd polynomial (degree 6 in t = x^2) fitted to that curve on
 * [-2, 2]: max error 2.7e-5 (-92 dB), monotonic, about 0.9 LSB at full scale.
 * Beyond +-2 the rational is flat (0.984 at 2, 1.000 at 3), so the tails clamp to
 * its value at 2; the difference is only reachable at drive = 3 with a
 * full-scale input. */
static inline float fast_tanh(float x) {
    if (x > 2.f) return 0.984153f;
    if (x < -2.f) return -0.984153f;
    float t = x * x;
    return x * ((((((0.000046004772995f * t - 0.00078208114092f) * t
                    + 0.0058886109434f) * t - 0.027326835159f) * t
                    + 0.095505908738f) * t - 0.29553466172f) * t
                    + 0.99996933015f);
}

static inline uint32_t xorshift(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}
static inline float frand(uint32_t *s) {            // [-1, 1)
    return ((int32_t)xorshift(s)) * (1.0f / 2147483648.0f);
}

static inline float bq_run(const tape_bq_coef_t *c, tape_bq_state_t *s, float x) {
    float y = c->b0 * x + s->z1;
    s->z1 = c->b1 * x - c->a1 * y + s->z2;
    s->z2 = c->b2 * x - c->a2 * y;
    return y;
}

static void bq_norm(tape_bq_coef_t *c, float b0, float b1, float b2, float a0, float a1, float a2) {
    float i = 1.f / a0;
    c->b0 = b0 * i; c->b1 = b1 * i; c->b2 = b2 * i; c->a1 = a1 * i; c->a2 = a2 * i;
}

static void bq_lp(tape_bq_coef_t *c, float sr, float f, float q) {
    float w = 2.f * (float)M_PI * f / sr, cs = cosf(w), al = sinf(w) / (2.f * q);
    bq_norm(c, (1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
}
static void bq_hp(tape_bq_coef_t *c, float sr, float f, float q) {
    float w = 2.f * (float)M_PI * f / sr, cs = cosf(w), al = sinf(w) / (2.f * q);
    bq_norm(c, (1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
}
static void bq_lowshelf(tape_bq_coef_t *c, float sr, float f, float db) {
    float A = powf(10.f, db / 40.f), w = 2.f * (float)M_PI * f / sr;
    float cs = cosf(w), sn = sinf(w), al = sn / 2.f * 1.41421356f, sA = 2.f * sqrtf(A) * al;
    bq_norm(c,
        A * ((A + 1) - (A - 1) * cs + sA),
        2 * A * ((A - 1) - (A + 1) * cs),
        A * ((A + 1) - (A - 1) * cs - sA),
        (A + 1) + (A - 1) * cs + sA,
        -2 * ((A - 1) + (A + 1) * cs),
        (A + 1) + (A - 1) * cs - sA);
}

/* Called only when a setting changes, so the transcendental work here is setup,
 * not per-sample cost. */
static void recompute(tape_fx_t *t) {
    static const float lp_base[3]   = { 9000.f, 12000.f, 15000.f };
    static const float bump_db[3]   = { 2.5f, 1.5f, 1.0f };
    static const float drive_k[3]   = { 2.0f, 1.5f, 1.1f };
    static const float hiss_db[3]   = { 0.f, -3.f, -5.f };
    static const float dolby_db[4]  = { 0.f, -10.f, -20.f, -200.f };

    float sr = t->sr;
    int ty = (int)t->type;

    t->drive = 1.f + drive_k[ty] * (1.f - 0.8f * t->tape_q);
    /* 1/drive, not 1/sqrt(drive): tanh(x*drive) is ~x*drive for small signals, so
     * this is the factor that makes the stage unity for a quiet signal. The
     * square root left the output up to +4.8 dB hot, which a mode switch between
     * "normal" and "FX" cannot afford. */
    t->inv_drive = 1.f / t->drive;

    float hiss = -64.f + 14.f * (1.f - t->tape_q) + hiss_db[ty] + dolby_db[(int)t->dolby];
    t->noise_amp = powf(10.f, hiss / 20.f);

    float fc = lp_base[ty] * (0.3f + 0.7f * t->head_q);
    if (fc > 0.45f * sr) fc = 0.45f * sr;
    bq_hp(&t->hp, sr, 35.f, 0.707f);
    bq_lowshelf(&t->shelf, sr, 110.f, bump_db[ty]);
    bq_lp(&t->lp, sr, fc, 0.8f);

    // pitch-deviation targets -> delay amplitude: A = dev / (2*pi*f) * sr
    float m = 1.f - t->motor_q;
    float k = sr / (2.f * (float)M_PI);
    t->wow_amp   = 0.007f * m / 0.9f * k;
    t->flt_amp   = 0.002f * m / 8.0f * k;
    t->drift_amp = 0.003f * m / 0.4f * k;
    t->center    = 0.004f * sr;
}

void tape_fx_init(tape_fx_t *t, float sr) {
    /* Idempotent, so a second instance re-fills the same table. Instances are
     * created before audio flows, on one task, which is what makes a plain
     * static safe here. */
    tape_sin_init();
    memset(t, 0, sizeof *t);
    t->sr = sr; t->type = TAPE_NORMAL; t->dolby = DOLBY_OFF;
    t->tape_q  = TAPE_FX_DEFAULT_TAPE_Q;
    t->head_q  = TAPE_FX_DEFAULT_HEAD_Q;
    t->motor_q = TAPE_FX_DEFAULT_MOTOR_Q;
    t->rng = 0x1234567u;
    recompute(t);
}
void tape_fx_set_samplerate(tape_fx_t *t, float sr) { t->sr = sr; recompute(t); }
void tape_fx_set_tape_type(tape_fx_t *t, tape_type_t v) { t->type = v; recompute(t); }
void tape_fx_set_dolby(tape_fx_t *t, tape_dolby_t v) { t->dolby = v; recompute(t); }
static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
void tape_fx_set_tape_quality(tape_fx_t *t, float q)  { t->tape_q  = clamp01(q); recompute(t); }
void tape_fx_set_head_quality(tape_fx_t *t, float q)  { t->head_q  = clamp01(q); recompute(t); }
void tape_fx_set_motor_quality(tape_fx_t *t, float q) { t->motor_q = clamp01(q); recompute(t); }

void tape_fx_process_s16(tape_fx_t *t, int16_t *buf, size_t frames, int channels) {
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    const float two_pi = 2.f * (float)M_PI;
    const float dw = two_pi * 0.9f / t->sr, df = two_pi * 8.0f / t->sr;
    const uint32_t drift_period = (uint32_t)(t->sr * 0.5f);
    const float drift_a = 1.f / (0.25f * t->sr);

    for (size_t n = 0; n < frames; n++) {
        // shared modulation (one tape transport for both channels)
        t->ph_wow += dw; if (t->ph_wow > two_pi) t->ph_wow -= two_pi;
        t->ph_flt += df; if (t->ph_flt > two_pi) t->ph_flt -= two_pi;
        if (++t->drift_cnt >= drift_period) { t->drift_cnt = 0; t->drift_tgt = frand(&t->rng); }
        t->drift += drift_a * (t->drift_tgt - t->drift);

        float d = t->center + t->wow_amp * tape_sin(t->ph_wow)
                            + t->flt_amp * tape_sin(t->ph_flt)
                            + t->drift_amp * t->drift;
        float rp = (float)t->w - d;
        if (rp < 0.f) rp += TAPE_FX_DLY_LEN;
        uint32_t i0 = (uint32_t)rp;
        float fr = rp - (float)i0;
        uint32_t i1 = (i0 + 1) & (TAPE_FX_DLY_LEN - 1);
        i0 &= (TAPE_FX_DLY_LEN - 1);

        for (int c = 0; c < channels; c++) {
            float x = buf[n * channels + c] * (1.f / 32768.f);
            float y = fast_tanh(x * t->drive) * t->inv_drive;      // saturation
            y = bq_run(&t->hp,    &t->st[c][0], y);                // rumble cut
            y = bq_run(&t->shelf, &t->st[c][1], y);                // head bump
            y = bq_run(&t->lp,    &t->st[c][2], y);                // HF loss

            t->dly[c][t->w] = y;                                   // wow/flutter
            float o = t->dly[c][i0] + fr * (t->dly[c][i1] - t->dly[c][i0]);

            float nz = frand(&t->rng);                             // hiss (high-passed)
            t->nlp[c] += 0.3f * (nz - t->nlp[c]);
            o += (nz - t->nlp[c]) * t->noise_amp;

            o = fast_tanh(o);                                      // final soft limit
            int32_t s = (int32_t)(o * 32767.f);
            buf[n * channels + c] = (int16_t)s;
        }
        t->w = (t->w + 1) & (TAPE_FX_DLY_LEN - 1);
    }
}
