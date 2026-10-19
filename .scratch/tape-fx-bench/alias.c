/*
 * How much aliasing does the effect actually produce?
 *
 * A single sine at f0; every component that is neither f0 nor a harmonic below
 * Nyquist can only come from a non-linearity folding energy back into the band.
 * For f0 = 5 kHz at 44.1 kHz those are 25->19.1, 30->14.1, 35->9.1, 40->4.1 kHz.
 *
 * Wow/flutter and hiss are switched off (motor_q = 1, dolby = K) so the spectrum
 * is the saturator's, not the transport's.
 *
 *   ./alias [f0] [dbfs]
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tape_fx_comp.c"

#define SR  44100
#define N   44100
#define FRAMES (N * 2)

static int16_t buf[FRAMES];

/* Magnitude of a windowed projection onto `hz`, relative to the fundamental. */
static double mag(const int16_t *x, double hz)
{
    int start = N / 2, n = N - start;
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));      /* Hann */
        double ph = 2 * M_PI * hz * (double)(start + i) / SR;
        double v = x[(start + i) * 2] * w;
        re += v * cos(ph);
        im += v * sin(ph);
        wsum += w;
    }
    return sqrt(re * re + im * im) / wsum * 2.0;
}

static void run(double f0, double amp, float tape_q, float head_q, const char *label)
{
    static tape_fx_t t;
    tape_fx_init(&t, (float)SR);
    tape_fx_set_tape_quality(&t, tape_q);
    tape_fx_set_head_quality(&t, head_q);
    tape_fx_set_motor_quality(&t, 1.f);       /* no wow: keep the spectrum still */
    tape_fx_set_dolby(&t, DOLBY_K);           /* no hiss */

    for (int i = 0; i < N; i++) {
        int16_t v = (int16_t)lrint(amp * 32767.0 * sin(2 * M_PI * f0 * i / SR));
        buf[i * 2] = buf[i * 2 + 1] = v;
    }
    tape_fx_process_s16(&t, buf, N, 2);

    double f = mag(buf, f0);
    double db = 20 * log10(f + 1e-12);
    printf("%-28s f0 %.0f Hz at %.0f dBFS  (fundamental %.1f dB)\n", label, f0,
           20 * log10(amp), db);

    /* Harmonics below Nyquist are the sound; the rest is aliasing. */
    printf("   harmonics: ");
    for (int k = 2; k * f0 < SR / 2.0; k++) {
        double v = 20 * log10(mag(buf, k * f0) + 1e-12) - db;
        if (v > -100) printf("%.0fk %.0f dB  ", k * f0 / 1000, v);
    }
    printf("\n   aliases:   ");
    for (int k = 2; k * f0 < SR; k++) {
        double a = k * f0;
        if (a < SR / 2.0) continue;
        double folded = fabs(SR - a);               /* first image */
        if (folded < 20 || folded > SR / 2.0 - 20) continue;
        double v = 20 * log10(mag(buf, folded) + 1e-12) - db;
        printf("%.1fk %.0f dB  ", folded / 1000, v);
    }
    printf("\n\n");
}

int main(int argc, char **argv)
{
    double f0 = argc > 1 ? atof(argv[1]) : 5000;
    double dbfs = argc > 2 ? atof(argv[2]) : -6;
    double amp = pow(10, dbfs / 20);

    run(f0, amp, 0.0f, 0.6f, "q=0 (max drive), head 0.6");
    run(f0, amp, 0.6f, 0.6f, "q=0.6 (default)");
    run(f0, amp, 1.0f, 0.6f, "q=1 (pristine)");
    return 0;
}
