#include "mi_resample.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NPH   128 /* sub-sample phases in the kernel table (linear interpolation in between) */
#define ZERO_X 8  /* sinc zero crossings on each side (at the cutoff frequency) */
#define BATCH 256 /* output frames per emit() call */
#define MAXIN 1024

struct mi_rs {
    bool     bypass;
    int      T;            /* taps */
    float   *tab;          /* (NPH+1) rows of T taps */
    uint64_t pos, step;    /* 32.32 fixed point, in input frames, relative to buf[0] */
    float   *buf;          /* stereo interleaved history + new input */
    int      buf_n;
    int16_t  out[2 * BATCH];
    int      out_n;
};

static double kern(double d, double fc, double half)
{
    double x = d / half;
    if (fabs(x) >= 1.0) return 0.0;
    double w = 0.42 + 0.5 * cos(M_PI * x) + 0.08 * cos(2.0 * M_PI * x); /* Blackman */
    double a = M_PI * fc * d;
    return fc * (fabs(a) < 1e-9 ? 1.0 : sin(a) / a) * w;
}

mi_rs_t *mi_rs_new(uint32_t in_hz, uint32_t out_hz)
{
    mi_rs_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    if (in_hz == out_hz) { r->bypass = true; return r; }

    double fc = 0.95 * (out_hz < in_hz ? (double)out_hz / in_hz : 1.0);
    r->T = 2 * (int)ceil(ZERO_X / fc);
    int T = r->T;
    r->tab = malloc(sizeof(float) * (NPH + 1) * T);
    r->buf = malloc(sizeof(float) * 2 * (T + MAXIN + 8));
    if (!r->tab || !r->buf) { mi_rs_free(r); return NULL; }

    for (int p = 0; p <= NPH; p++) {
        double sum = 0, row[256];
        for (int k = 0; k < T; k++) {
            row[k] = kern(k - (T / 2 - 1) - (double)p / NPH, fc, T / 2.0);
            sum += row[k];
        }
        for (int k = 0; k < T; k++) r->tab[p * T + k] = (float)(row[k] / sum); /* unity DC gain */
    }
    r->step = ((uint64_t)in_hz << 32) / out_hz;
    memset(r->buf, 0, sizeof(float) * 2 * (T / 2 - 1));
    r->buf_n = T / 2 - 1;
    r->pos = (uint64_t)(T / 2 - 1) << 32;
    return r;
}

static inline int16_t clip16(float v)
{
    v += (v >= 0 ? 0.5f : -0.5f);
    return v > 32767.f ? 32767 : v < -32768.f ? -32768 : (int16_t)v;
}

void mi_rs_process(mi_rs_t *r, const int16_t *in, size_t n, mi_rs_emit_t emit, void *ctx)
{
    if (r->bypass) { emit(in, n, ctx); return; }
    const int T = r->T, half = T / 2;

    while (n) {
        size_t k = n > MAXIN ? MAXIN : n;
        for (size_t i = 0; i < 2 * k; i++) r->buf[2 * r->buf_n + i] = in[i];
        r->buf_n += (int)k;
        in += 2 * k;
        n -= k;

        while ((int64_t)(r->pos >> 32) + half < r->buf_n) {
            uint32_t f = (uint32_t)(r->pos >> 32), fr = (uint32_t)r->pos;
            const float *c0 = r->tab + (fr >> 25) * T, *c1 = c0 + T;
            const float a = (fr & 0x1FFFFFF) * (1.0f / 33554432.0f);
            const float *s = r->buf + 2 * (f - (half - 1));
            float l = 0, rr = 0;
            for (int t = 0; t < T; t++) {
                float c = c0[t] + (c1[t] - c0[t]) * a;
                l += s[2 * t] * c;
                rr += s[2 * t + 1] * c;
            }
            r->out[2 * r->out_n] = clip16(l);
            r->out[2 * r->out_n + 1] = clip16(rr);
            if (++r->out_n == BATCH) { emit(r->out, BATCH, ctx); r->out_n = 0; }
            r->pos += r->step;
        }
        /* drop frames no future output needs */
        int64_t drop = (int64_t)(r->pos >> 32) - (half - 1);
        if (drop > 0) {
            memmove(r->buf, r->buf + 2 * drop, sizeof(float) * 2 * (r->buf_n - drop));
            r->buf_n -= (int)drop;
            r->pos -= (uint64_t)drop << 32;
        }
    }
    if (r->out_n) { emit(r->out, r->out_n, ctx); r->out_n = 0; }
}

void mi_rs_free(mi_rs_t *r)
{
    if (!r) return;
    free(r->tab);
    free(r->buf);
    free(r);
}
