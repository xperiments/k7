#include "mi_resample.h"

#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "resample"

/* 32 sub-sample phases. The kernel row is linearly interpolated between two
 * phases, which is exact enough for the station's 256 kbps MP3 output: measured
 * against an exact per-output kernel, 32 phases are -88 dB at 19 kHz and -100 dB
 * at 10 kHz. 128 phases were -113 dB but their 38 KB table streamed through the
 * 32 KB data cache (which the screen's framebuffer traffic shares) and cost 71 %
 * of a core on a 192 kHz source; the table now fits in the cache and in internal
 * RAM. See docs/research/audio-decode-cost.md. */
#define PHASE_BITS 5
#define NPH       (1 << PHASE_BITS) /* sub-sample phases in the kernel table */
#define ZERO_X 8  /* sinc zero crossings on each side (at the cutoff frequency) */
#define BATCH 256 /* output frames per emit() call */
#define MAXIN 512

/* The kernel is built one row at a time into a stack array of this many taps, so
 * the tap count must never exceed it. Extremely low output rates make fc tiny
 * and T large: 48000 -> 3000 Hz needs 270 taps and 44100 -> 1000 Hz needs 744,
 * both of which would write past a 256-tap row. Refuse instead, with a
 * diagnostic naming the rates (defect D4). */
#define MI_RS_MAX_TAPS 256
_Static_assert(MI_RS_MAX_TAPS <= 256, "the on-stack kernel row is 256 taps");

struct mi_rs {
    bool     bypass;
    int      T;            /* taps */
    float   *tab;          /* (NPH+1) rows of T taps */
    uint64_t pos, step;    /* 32.32 fixed point, in input frames, relative to buf[0] */
    float   *buf;          /* stereo interleaved history + new input */
    int      buf_n;
    int16_t  out[2 * BATCH];
    int      out_n;
    uint64_t cpu_us; /* own arithmetic only; the emit callback is excluded */
};

static void rs_err(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

static double kern(double d, double fc, double half)
{
    double x = d / half;
    if (fabs(x) >= 1.0) return 0.0;
    double w = 0.42 + 0.5 * cos(M_PI * x) + 0.08 * cos(2.0 * M_PI * x); /* Blackman */
    double a = M_PI * fc * d;
    return fc * (fabs(a) < 1e-9 ? 1.0 : sin(a) / a) * w;
}

mi_rs_t *mi_rs_new(uint32_t in_hz, uint32_t out_hz, char *err, size_t err_size)
{
    if (err && err_size) err[0] = 0;
    if (!in_hz || !out_hz) {
        rs_err(err, err_size, "cannot resample %u Hz to %u Hz: both rates must be non-zero",
               (unsigned)in_hz, (unsigned)out_hz);
        return NULL;
    }
    mi_rs_t *r = calloc(1, sizeof *r);
    if (!r) {
        rs_err(err, err_size, "out of memory for the resampler");
        return NULL;
    }
    if (in_hz == out_hz) { r->bypass = true; return r; }

    double fc = 0.95 * (out_hz < in_hz ? (double)out_hz / in_hz : 1.0);
    r->T = 2 * (int)ceil(ZERO_X / fc);
    int T = r->T;
    if (T > MI_RS_MAX_TAPS) {
        rs_err(err, err_size,
               "cannot resample %u Hz to %u Hz: %d taps exceeds the %d-tap kernel limit",
               (unsigned)in_hz, (unsigned)out_hz, T, MI_RS_MAX_TAPS);
        heap_caps_free(r);
        return NULL;
    }
    /* The kernel table is the hottest data in the firmware after the encoder's
     * state, and it is small enough to keep in internal RAM: at 32 phases and 74
     * taps it is 9.8 KB. External memory would put it behind the same 32 KB data
     * cache the screen's framebuffer streams through, which measured as most of
     * the resampler's cost on a 192 kHz source. Internal falls back to external,
     * and that fallback is the case worth logging. */
    size_t tab_bytes = sizeof(float) * (NPH + 1) * (size_t)T;
    size_t buf_bytes = sizeof(float) * 2 * (size_t)(T + MAXIN + 8);
    r->tab = heap_caps_malloc(tab_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bool tab_internal = r->tab != NULL;
    if (!r->tab) r->tab = heap_caps_malloc(tab_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r->tab) r->tab = malloc(tab_bytes);
    r->buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bool buf_internal = r->buf != NULL;
    if (!r->buf) r->buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r->buf) r->buf = malloc(buf_bytes);
    if (!r->tab || !r->buf) {
        rs_err(err, err_size, "out of memory for the %d-tap resampler kernel", T);
        mi_rs_free(r);
        return NULL;
    }
    if (tab_internal && buf_internal) {
        ESP_LOGD(TAG, "%d-tap kernel: %u B of tables in internal RAM", T,
                 (unsigned)(tab_bytes + buf_bytes));
    } else {
        ESP_LOGW(TAG, "%d-tap kernel: tables in %s (internal RAM unavailable)", T,
                 (tab_internal || buf_internal) ? "mixed memory" : "external memory");
    }

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
    int64_t t0 = esp_timer_get_time(), emit_us = 0;

    while (n) {
        size_t k = n > MAXIN ? MAXIN : n;
        for (size_t i = 0; i < 2 * k; i++) r->buf[2 * r->buf_n + i] = in[i];
        r->buf_n += (int)k;
        in += 2 * k;
        n -= k;

        while ((int64_t)(r->pos >> 32) + half < r->buf_n) {
            uint32_t f = (uint32_t)(r->pos >> 32), fr = (uint32_t)r->pos;
            const float *c0 = r->tab + (fr >> (32 - PHASE_BITS)) * T, *c1 = c0 + T;
            const float a = (fr & ((1u << (32 - PHASE_BITS)) - 1)) *
                            (1.0f / (float)(1u << (32 - PHASE_BITS)));
            const float *s = r->buf + 2 * (f - (half - 1));
            float l = 0, rr = 0;
            for (int t = 0; t < T; t++) {
                float c = c0[t] + (c1[t] - c0[t]) * a;
                l += s[2 * t] * c;
                rr += s[2 * t + 1] * c;
            }
            r->out[2 * r->out_n] = clip16(l);
            r->out[2 * r->out_n + 1] = clip16(rr);
            if (++r->out_n == BATCH) {
                int64_t e0 = esp_timer_get_time();
                emit(r->out, BATCH, ctx);
                emit_us += esp_timer_get_time() - e0;
                r->out_n = 0;
            }
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
    if (r->out_n) {
        int64_t e0 = esp_timer_get_time();
        emit(r->out, r->out_n, ctx);
        emit_us += esp_timer_get_time() - e0;
        r->out_n = 0;
    }
    r->cpu_us += (uint64_t)((esp_timer_get_time() - t0) - emit_us);
}

uint64_t mi_rs_cpu_us(const mi_rs_t *r) { return r->cpu_us; }

void mi_rs_free(mi_rs_t *r)
{
    if (!r) return;
    heap_caps_free(r->tab);
    heap_caps_free(r->buf);
    heap_caps_free(r);
}
