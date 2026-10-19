/*
 * t13: the tape effect, as a component.
 *
 * The effect is stateful and the station feeds it one 1024-frame chunk at a time,
 * so the properties worth testing are the ones a chunk boundary could break:
 * that the output does not depend on how the audio is cut into calls, that a
 * quiet signal comes out at the same level it went in (the make-up gain is
 * unity, so switching the effect on is not a volume jump), and that it cannot run
 * away or overflow.
 */
#include <math.h>

#include "tape_fx.h"
#include "test_util.h"

#define SR 44100.f
#define N  4096

static int16_t a[N * 2], b[N * 2];

/* A deterministic, broadband, roughly -12 dBFS stereo signal. */
static void fill(int16_t *buf, size_t frames)
{
    unsigned s = 12345u;
    for (size_t i = 0; i < frames; i++) {
        for (int c = 0; c < 2; c++) {
            s = s * 1664525u + 1013904223u;
            int32_t n = (int32_t)(s >> 8) - 8388608;             /* -2^23..2^23 */
            int32_t tone = (int32_t)(0.35 * 32767.0 * sin(2.0 * M_PI * 441.0 * (double)i / 44100.0));
            int32_t v = tone + n / 64;
            buf[i * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

static double rms(const int16_t *buf, size_t frames, size_t from)
{
    double acc = 0;
    size_t n = 0;
    for (size_t i = from; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            acc += (double)buf[i * 2 + c] * buf[i * 2 + c];
            n++;
        }
    return n ? sqrt(acc / (double)n) : 0.0;
}

int main(void)
{
    /* 1. One call or thirty-two: the same samples come out. If the state were
     *    reset per call - or a pointer were recomputed from scratch - the cut
     *    would be audible at every 23 ms chunk boundary. */
    {
        static tape_fx_t one, many;
        tape_fx_init(&one, SR);
        tape_fx_init(&many, SR);
        fill(a, N);
        memcpy(b, a, sizeof a);
        tape_fx_process_s16(&one, a, N, 2);
        for (size_t i = 0; i < N; i += 128)
            tape_fx_process_s16(&many, b + i * 2, 128, 2);
        int same = memcmp(a, b, sizeof a) == 0;
        CHECK(same, "processing in 128-frame pieces differs from one 4096-frame call");
    }

    /* 2. Pristine settings are unity gain. The saturation stage multiplies by
     *    tanh(x*drive)/drive, so a quiet signal must come out at its own level;
     *    this is the check that keeps a mode switch from being a volume jump. */
    {
        static tape_fx_t t;
        tape_fx_init(&t, SR);
        tape_fx_set_tape_type(&t, TAPE_METAL);
        tape_fx_set_dolby(&t, DOLBY_K);
        tape_fx_set_tape_quality(&t, 1.f);
        tape_fx_set_head_quality(&t, 1.f);
        tape_fx_set_motor_quality(&t, 1.f);

        /* -20 dBFS at 1 kHz, where every filter in the chain is flat. */
        for (size_t i = 0; i < N; i++) {
            int16_t v = (int16_t)(0.1 * 32767.0 * sin(2.0 * M_PI * 1000.0 * (double)i / 44100.0));
            a[i * 2] = a[i * 2 + 1] = v;
        }
        double before = rms(a, N, 0);
        tape_fx_process_s16(&t, a, N, 2);
        double after = rms(a, N, N / 2);     /* skip the delay line filling */
        double db = 20.0 * log10(after / before);
        CHECK(fabs(db) < 1.0, "pristine settings are not unity gain: %+.2f dB", db);
    }

    /* 3. Silence in, hiss out - and not a runaway. The noise floor is deliberate,
     *    so it must be present, but bounded well below anything that would read as
     *    audio. */
    {
        static tape_fx_t t;
        tape_fx_init(&t, SR);
        memset(a, 0, sizeof a);
        tape_fx_process_s16(&t, a, N, 2);
        double floor_db = 20.0 * log10((rms(a, N, N / 2) + 1e-9) / 32768.0);
        CHECK(floor_db < -40.0 && floor_db > -80.0, "hiss with silence in: %.1f dBFS", floor_db);
    }

    /* 4. Full scale in does not wrap. The final saturator bounds the sample to
     *    +-0.984, so the int16 cast can never see 32768 - a cast that overflows
     *    would flip the sign and produce a click at every peak. */
    {
        static tape_fx_t t;
        tape_fx_init(&t, SR);
        tape_fx_set_tape_quality(&t, 0.f);
        tape_fx_set_motor_quality(&t, 0.f);
        for (size_t i = 0; i < N; i++)
            a[i * 2] = a[i * 2 + 1] = (i & 1) ? 32767 : -32768;
        tape_fx_process_s16(&t, a, N, 2);
        int bad = 0;
        for (size_t i = 0; i < N * 2; i++)
            if (a[i] == -32768) bad++;            /* only reachable by an overflow or a real -1.0 */
        CHECK(bad == 0, "%d samples wrapped to -32768 on a full-scale input", bad);
    }

    TEST_END();
}
