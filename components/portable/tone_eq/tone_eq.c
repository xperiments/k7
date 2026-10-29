/*
 * The broadcast's three-band DJ EQ and its trim; see tone_eq.h.
 *
 * The coefficients are the RBJ cookbook ones evaluated at the DJ frequencies: a low
 * shelf at 200 Hz, a bell at 1 kHz with Q 0.7 and a high shelf at 4 kHz. The trim is a
 * plain linear gain before them, so a hot trim and a boosted band can reach the clamp.
 */
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "tone_eq.h"

#define PI_F      3.14159265358979323846f
#define LN10_20   0.11512925464970229f   /* ln(10) / 20, the dB -> amplitude factor */
#define LN10_40   0.057564627324851145f  /* ln(10) / 40, the shelf/peak A factor */
#define SLEW_DB   2.0f                   /* per chunk; 43 chunks/s on the station rate */
#define LOW_HZ    200.0f
#define MID_HZ    1000.0f
#define HIGH_HZ   4000.0f
#define MID_Q     0.7f

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Normalise one section. Dividing rather than multiplying by a reciprocal keeps the
 * 0 dB case exactly unity, which the host test pins. */
static void sec_set(tone_eq_sec_t *s, float b0, float b1, float b2,
                    float a0, float a1, float a2)
{
    s->b0 = b0 / a0;
    s->b1 = b1 / a0;
    s->b2 = b2 / a0;
    s->a1 = a1 / a0;
    s->a2 = a2 / a0;
}

/* The shelf alpha for S = 1; the general term's first half vanishes. */
static float shelf_alpha(float sw)
{
    return sw * 0.70710678118654752440f; /* sin/2 * sqrt(2) */
}

static void coeff_low_shelf(tone_eq_t *eq, tone_eq_sec_t *s, float db)
{
    float f = LOW_HZ;
    if (f > (float)eq->rate * 0.45f) f = (float)eq->rate * 0.45f;
    float A = expf(db * LN10_40);
    float w0 = 2.0f * PI_F * f / (float)eq->rate;
    float cw = cosf(w0), sw = sinf(w0);
    float k = 2.0f * sqrtf(A) * shelf_alpha(sw);

    float b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + k);
    float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
    float b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - k);
    float a0 = (A + 1.0f) + (A - 1.0f) * cw + k;
    float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
    float a2 = (A + 1.0f) + (A - 1.0f) * cw - k;
    sec_set(s, b0, b1, b2, a0, a1, a2);
}

static void coeff_high_shelf(tone_eq_t *eq, tone_eq_sec_t *s, float db)
{
    float f = HIGH_HZ;
    if (f > (float)eq->rate * 0.45f) f = (float)eq->rate * 0.45f;
    float A = expf(db * LN10_40);
    float w0 = 2.0f * PI_F * f / (float)eq->rate;
    float cw = cosf(w0), sw = sinf(w0);
    float k = 2.0f * sqrtf(A) * shelf_alpha(sw);

    float b0 = A * ((A + 1.0f) + (A - 1.0f) * cw + k);
    float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
    float b2 = A * ((A + 1.0f) + (A - 1.0f) * cw - k);
    float a0 = (A + 1.0f) - (A - 1.0f) * cw + k;
    float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
    float a2 = (A + 1.0f) - (A - 1.0f) * cw - k;
    sec_set(s, b0, b1, b2, a0, a1, a2);
}

static void coeff_mid_bell(tone_eq_t *eq, tone_eq_sec_t *s, float db)
{
    float f = MID_HZ;
    if (f > (float)eq->rate * 0.45f) f = (float)eq->rate * 0.45f;
    float A = expf(db * LN10_40);
    float w0 = 2.0f * PI_F * f / (float)eq->rate;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.0f * MID_Q);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cw;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cw;
    float a2 = 1.0f - alpha / A;
    sec_set(s, b0, b1, b2, a0, a1, a2);
}

static void coeff_update(tone_eq_t *eq, int band)
{
    float db = eq->band_db[band];
    switch (band) {
        case TONE_EQ_LOW:  coeff_low_shelf(eq, &eq->sec[band], db); break;
        case TONE_EQ_MID:  coeff_mid_bell(eq, &eq->sec[band], db); break;
        case TONE_EQ_HIGH: coeff_high_shelf(eq, &eq->sec[band], db); break;
        default: break;
    }
    eq->coeff_db[band] = db;
}

void tone_eq_init(tone_eq_t *eq, uint32_t rate)
{
    if (!eq) return;
    memset(eq, 0, sizeof *eq);
    eq->rate = rate ? rate : 44100u;
    eq->gain_db = TONE_EQ_GAIN_DEFAULT;
    eq->gain_target = TONE_EQ_GAIN_DEFAULT;
    eq->gain_lin = expf(TONE_EQ_GAIN_DEFAULT * LN10_20);
    for (int b = 0; b < TONE_EQ_BANDS; b++) {
        eq->band_db[b] = 0.0f;
        eq->band_target[b] = 0.0f;
        eq->coeff_db[b] = 0.0f;
        coeff_update(eq, b);
    }
}

void tone_eq_set_band(tone_eq_t *eq, int band, float db)
{
    if (!eq || band < 0 || band >= TONE_EQ_BANDS) return;
    eq->band_target[band] = clampf(db, TONE_EQ_BAND_MIN_DB, TONE_EQ_BAND_MAX_DB);
}

void tone_eq_set_gain(tone_eq_t *eq, float db)
{
    if (!eq) return;
    eq->gain_target = clampf(db, TONE_EQ_GAIN_MIN_DB, TONE_EQ_GAIN_MAX_DB);
}

/* One chunk's worth of the targets: move each value at most SLEW_DB toward where the
 * knob is, then rebuild the coefficients that changed. At 43 chunks/s this is a glide
 * of about 0.14 s for a full trim move and 0.30 s for a full band cut. */
static void slew(tone_eq_t *eq)
{
    for (int b = 0; b < TONE_EQ_BANDS; b++) {
        float d = eq->band_target[b] - eq->band_db[b];
        if (d == 0.0f) continue;
        if (d > SLEW_DB) d = SLEW_DB;
        else if (d < -SLEW_DB) d = -SLEW_DB;
        eq->band_db[b] += d;
        if (fabsf(eq->band_target[b] - eq->band_db[b]) < 0.0005f)
            eq->band_db[b] = eq->band_target[b];
        if (eq->band_db[b] != eq->coeff_db[b]) coeff_update(eq, b);
    }

    float d = eq->gain_target - eq->gain_db;
    if (d != 0.0f) {
        if (d > SLEW_DB) d = SLEW_DB;
        else if (d < -SLEW_DB) d = -SLEW_DB;
        eq->gain_db += d;
        if (fabsf(eq->gain_target - eq->gain_db) < 0.0005f)
            eq->gain_db = eq->gain_target;
        eq->gain_lin = expf(eq->gain_db * LN10_20);
    }
}

static int16_t clamp16(float x)
{
    if (x >= 32767.0f) return 32767;
    if (x <= -32768.0f) return -32768;
    int32_t v = (int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

void tone_eq_process(tone_eq_t *eq, int16_t *pcm, size_t frames)
{
    if (!eq || !pcm || frames == 0) return;

    /* Neutral is the default and the common case: four knobs at 0 dB. Running
     * three biquads per channel for that costs about 1.4 % of a core, so the
     * whole stage is bypassed while both the targets and the slewed values are
     * neutral. The filter state is cleared so that turning a band up starts from
     * rest rather than from a frozen history. */
    bool neutral = eq->gain_target == 0.0f && eq->gain_db == 0.0f;
    for (int b = 0; b < TONE_EQ_BANDS && neutral; b++)
        if (eq->band_target[b] != 0.0f || eq->band_db[b] != 0.0f) neutral = false;
    if (neutral) {
        for (int b = 0; b < TONE_EQ_BANDS; b++) {
            eq->sec[b].z1[0] = eq->sec[b].z2[0] = 0.0f;
            eq->sec[b].z1[1] = eq->sec[b].z2[1] = 0.0f;
        }
        return;
    }
    slew(eq);

    const float g = eq->gain_lin;
    for (size_t i = 0; i < frames; i++) {
        float l = (float)pcm[2 * i] * g;
        float r = (float)pcm[2 * i + 1] * g;

        for (int b = 0; b < TONE_EQ_BANDS; b++) {
            tone_eq_sec_t *s = &eq->sec[b];
            float y = s->b0 * l + s->z1[0];
            s->z1[0] = s->b1 * l - s->a1 * y + s->z2[0];
            s->z2[0] = s->b2 * l - s->a2 * y;
            l = y;

            y = s->b0 * r + s->z1[1];
            s->z1[1] = s->b1 * r - s->a1 * y + s->z2[1];
            s->z2[1] = s->b2 * r - s->a2 * y;
            r = y;
        }

        pcm[2 * i] = clamp16(l);
        pcm[2 * i + 1] = clamp16(r);
    }
}
