/* What the effect actually does to a sine: gain per frequency, and level change
 * per setting. Validates the DSP, not just the cost. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tape_fx.h"

#define N 44100
static int16_t buf[N * 2];

static double run(tape_fx_t *fx, double hz, double amp)
{
    for (int i = 0; i < N; i++) {
        double v = amp * sin(2 * M_PI * hz * i / 44100.0);
        int16_t q = (int16_t)lrint(v * 32767.0);
        buf[2 * i] = q; buf[2 * i + 1] = q;
    }
    tape_fx_process_s16(fx, buf, N, 2);
    double si = 0, so = 0;
    int start = N / 4; /* skip the delay line filling and the filters settling */
    for (int i = start; i < N; i++) {
        si += (double)buf[2 * i] * buf[2 * i];
    }
    /* Output RMS is measured against the *input* RMS of the same signal. */
    double in_rms = amp * 32767.0 / sqrt(2.0);
    so = sqrt(si / (N - start));
    return 20.0 * log10((so + 1e-12) / in_rms);
}

int main(int argc, char **argv)
{
    static tape_fx_t fx;
    double hq = argc > 1 ? atof(argv[1]) : -1.0;   /* <0: leave the default */
    int ty = argc > 2 ? atoi(argv[2]) : 0;
    const double fs[] = {50, 110, 440, 1000, 2000, 5000, 10000, 15000, 18000};
    printf("type %d, head_q %.2f, -20 dBFS sine, gain vs input (dB):\n", ty, hq);
    for (unsigned i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        tape_fx_init(&fx, 44100.f);
        if (hq >= 0) tape_fx_set_head_quality(&fx, (float)hq);
        tape_fx_set_tape_type(&fx, (tape_type_t)ty);
        double g = run(&fx, fs[i], 0.1);
        printf("  %6.0f Hz  %+6.2f\n", fs[i], g);
    }
    if (argc > 1) return 0;   /* response only when asked for a setting */
    printf("\nlevel change at 440 Hz (-20 dBFS) by setting:\n");
    for (int ty = 0; ty < 3; ty++) {
        for (double q = 0.0; q <= 1.01; q += 0.5) {
            tape_fx_init(&fx, 44100.f);
            tape_fx_set_tape_type(&fx, (tape_type_t)ty);
            tape_fx_set_tape_quality(&fx, (float)q);
            printf("  type %d q %.1f  %+6.2f dB\n", ty, q, run(&fx, 440.0, 0.1));
        }
    }
    printf("\nnoise floor with silence in (RMS dBFS):\n");
    for (int d = 0; d < 4; d++) {
        tape_fx_init(&fx, 44100.f);
        tape_fx_set_dolby(&fx, (tape_dolby_t)d);
        memset(buf, 0, sizeof buf);
        tape_fx_process_s16(&fx, buf, N, 2);
        double s = 0;
        for (int i = N / 4; i < N; i++) s += (double)buf[2 * i] * buf[2 * i];
        printf("  dolby %d  %+7.1f\n", d, 20 * log10((sqrt(s / (3 * N / 4)) + 1e-12) / 32767.0));
    }
    return 0;
}
