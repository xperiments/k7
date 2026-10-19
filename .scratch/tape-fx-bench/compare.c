/*
 * Original vs the two fast variants, on the same PCM.
 *
 *   original          tape_fx.c          rational tanh + 2 sinf per frame
 *   cubic             tape_fx_v3.c       sine table + cubic soft clip
 *   poly              tape_fx_v4.c       sine table + polynomial fitted to the rational
 *
 * "diff" is the variant's output minus the original's, in dB relative to the
 * signal. A tone cannot be used for THD here - the wow/flutter moves its phase
 * across any window longer than ~100 ms - so "how much of the sound changed" is
 * measured directly as a null test, and also given in dBFS so it can be compared
 * with the hiss floor the effect itself adds (-66 dBFS at the defaults).
 *
 *   ./compare [level_dbfs] [hz]
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

extern void orig_init(void *t, float sr);
extern void orig_process(void *t, int16_t *buf, size_t frames, int ch);
extern void cubic_init(void *t, float sr);
extern void cubic_process(void *t, int16_t *buf, size_t frames, int ch);
extern void poly_init(void *t, float sr);
extern void poly_process(void *t, int16_t *buf, size_t frames, int ch);
extern void tab_init(void *t, float sr);
extern void tab_process(void *t, int16_t *buf, size_t frames, int ch);

#define N 44100
static int16_t in[N * 2], o[N * 2], c[N * 2], p[N * 2], t2[N * 2];
static union { double d; char b[9000]; } st_o, st_c, st_p, st_t;

static void diff(const char *name, const int16_t *y, double level, double *db, int *maxlsb)
{
    int start = N / 4, m = 0;
    double sd = 0, so = 0;
    for (int i = 2 * start; i < 2 * N; i++) {
        int d = o[i] - y[i];
        if (abs(d) > m) m = abs(d);
        sd += (double)d * d;
        so += (double)o[i] * o[i];
    }
    *db = 20 * log10((sqrt(sd / (2.0 * (N - start))) + 1e-12) / sqrt(so / (2.0 * (N - start))));
    *maxlsb = m;
    printf("  %-8s diff %6.1f dB  (max %4d LSB, absolute %.1f dBFS)\n", name, *db, m, *db + level);
}

int main(int argc, char **argv)
{
    double dbfs = argc > 1 ? atof(argv[1]) : -20.0;
    double amp = pow(10.0, dbfs / 20.0);
    for (int i = 0; i < N; i++) {
        int16_t q = (int16_t)lrint(amp * sin(2 * M_PI * 441.0 * i / 44100.0) * 32767.0);
        in[2 * i] = q; in[2 * i + 1] = q;
    }
    orig_init(&st_o, 44100.f);  cubic_init(&st_c, 44100.f);  poly_init(&st_p, 44100.f);
    tab_init(&st_t, 44100.f);
    memcpy(o, in, sizeof o); memcpy(c, in, sizeof c); memcpy(p, in, sizeof p);
    memcpy(t2, in, sizeof t2);
    orig_process(&st_o, o, N, 2);
    cubic_process(&st_c, c, N, 2);
    poly_process(&st_p, p, N, 2);
    tab_process(&st_t, t2, N, 2);

    printf("input %.1f dBFS @ 441 Hz   (the effect's own hiss sits at about -66 dBFS)\n", dbfs);
    double db; int m;
    diff("table", t2, dbfs, &db, &m);
    diff("cubic", c, dbfs, &db, &m);
    diff("poly", p, dbfs, &db, &m);
    return 0;
}
