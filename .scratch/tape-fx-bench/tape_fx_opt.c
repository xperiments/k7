#include "tape_fx.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 256-entry sine table: the two LFOs move at 0.9 Hz and 8 Hz, so linear
 * interpolation on 1/256 of a cycle is -66 dB of error - inaudible in a delay
 * modulation. Replaces two libm sinf() calls per frame. */
#define TAPE_SIN_BITS 8
#define TAPE_SIN_SIZE (1 << TAPE_SIN_BITS)
static float tape_sin_tab[TAPE_SIN_SIZE + 1];

static void tape_sin_init(void) {
    for (int i = 0; i <= TAPE_SIN_SIZE; i++)
        tape_sin_tab[i] = sinf(2.f * (float)M_PI * (float)i / (float)TAPE_SIN_SIZE);
}

/* phase in [0, 2*pi) */
static inline float tape_sin(float ph) {
    float f = ph * ((float)TAPE_SIN_SIZE / (2.f * (float)M_PI));
    int i = (int)f;
    float fr = f - (float)i;
    return tape_sin_tab[i] + fr * (tape_sin_tab[i + 1] - tape_sin_tab[i]);
}

static inline float fast_tanh(float x) {            // rational approx, exact at ±3
    if (x > 3.f) x = 3.f; else if (x < -3.f) x = -3.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
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

static void recompute(tape_fx_t *t) {
    static const float lp_base[3]   = { 9000.f, 12000.f, 15000.f };
    static const float bump_db[3]   = { 2.5f, 1.5f, 1.0f };
    static const float drive_k[3]   = { 2.0f, 1.5f, 1.1f };
    static const float hiss_db[3]   = { 0.f, -3.f, -5.f };
    static const float dolby_db[4]  = { 0.f, -10.f, -20.f, -200.f };

    float sr = t->sr;
    int ty = (int)t->type;

    t->drive = 1.f + drive_k[ty] * (1.f - 0.8f * t->tape_q);
    t->inv_drive = 1.f / sqrtf(t->drive);

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
    static int tab_ready = 0;
    if (!tab_ready) { tape_sin_init(); tab_ready = 1; }
    memset(t, 0, sizeof *t);
    t->sr = sr; t->type = TAPE_NORMAL; t->dolby = DOLBY_OFF;
    t->tape_q = t->head_q = t->motor_q = 0.6f;
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
